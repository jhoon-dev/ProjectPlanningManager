#include "ProgressPolicyFixture.h"
#include "persistence/JsonProjectStore.h"
#include "ui/MainWindow.h"
#include "ui/ProgressPolicyDialog.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
using namespace ppm;
namespace {
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
QPushButton *button(QWidget &w, const char *name) {
    auto *b = w.findChild<QPushButton *>(name);
    check(b, "button missing");
    return b;
}
void ready(QWidget &w) {
    QElapsedTimer t;
    t.start();
    while (!w.property("progressPolicyReady").toBool() && t.elapsed() < 20000)
        QTest::qWait(10);
    check(w.property("progressPolicyReady").toBool(), "comparison timeout");
}
QByteArray read(const QString &path) {
    QFile f(path);
    check(f.open(QIODevice::ReadOnly), "read");
    return f.readAll();
}
void write(const QString &path, const QByteArray &b) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly), "write");
    f.write(b);
}
void chooseFile(QWidget &w, const QString &path, const std::function<void()> &after = {}) {
    QTimer driver;
    driver.setInterval(10);
    bool chosen = false;
    std::string lastModal;
    QObject::connect(&driver, &QTimer::timeout, [&] {
        auto *modal = QApplication::activeModalWidget();
        const auto modalName =
            modal ? modal->metaObject()->className() + std::string("/") + modal->objectName().toStdString()
                  : std::string("none");
        if (lastModal != modalName) {
            std::cout << "file stage: " << modalName << std::endl;
            lastModal = modalName;
        }
        if (auto *file = qobject_cast<QFileDialog *>(modal)) {
            if (chosen)
                return;
            chosen = true;
            auto *edit = file->findChild<QLineEdit *>("fileNameEdit");
            if (!edit)
                std::_Exit(2);
            edit->setText(path);
            if (after)
                after();
            QMetaObject::invokeMethod(file, "accept", Qt::QueuedConnection);
        } else if (auto *box = qobject_cast<QMessageBox *>(modal)) {
            if (box->standardButtons().testFlag(QMessageBox::Yes))
                box->button(QMessageBox::Yes)->click();
            else {
                std::cerr << box->text().toStdString();
                std::_Exit(2);
            }
        }
    });
    driver.start();
    button(w, "exportProgressPolicy")->click();
    check(chosen, "file picker missing");
}
void workflow() {
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto p = progress_fixture::small();
    const auto path = temp.filePath("source.ppm");
    store.save(path.toStdString(), p, {});
    Document doc(store);
    doc.open(path.toStdString());
    doc.applyCalculation(calculate(p));
    const auto calculation = doc.calculation()->tasks;
    MainWindow main(doc);
    main.show();
    check(main.selectTaskById("B"), "source selection failed");
    auto *action = main.findChild<QAction *>("compareProgressPolicies");
    check(action && action->isEnabled(), "menu missing");
    action->trigger();
    auto *dialog = dynamic_cast<ProgressPolicyDialog *>(main.findChild<QDialog *>("progressPolicyDialog"));
    check(dialog && !dialog->isModal(), "comparison dialog missing");
    ready(*dialog);
    auto *table = dialog->findChild<QTableView *>("progressPolicyTasks");
    auto *relations = dialog->findChild<QTableView *>("progressPolicyIgnored");
    check(table->model()->rowCount() == 5 && relations->model()->rowCount() == 1, "virtual models wrong");
    check(table->model()->index(1, 4).data().toString() == "2026-11-03 14:00" &&
              table->model()->index(1, 10).data().toString() == "2026-11-02 14:00",
          "UI forecast/actual mismatch");
    dialog->findChild<QLineEdit *>("progressPolicySearch")->setText("B");
    check(table->model()->rowCount() == 1 && table->model()->index(0, 0).data(Qt::UserRole).toString() == "B",
          "stable search failed");
    dialog->findChild<QLineEdit *>("progressPolicySearch")->clear();
    dialog->findChild<QCheckBox *>("progressPolicyChangedOnly")->setChecked(true);
    check(table->model()->rowCount() == 4, "changed-only filter wrong");
    dialog->findChild<QCheckBox *>("progressPolicyChangedOnly")->setChecked(false);
    const auto exported = temp.filePath("comparison.json");
    chooseFile(*dialog, exported);
    auto json = QJsonDocument::fromJson(read(exported)).object();
    check(json["adopted"] == false && json["tasks"].toArray().size() == 5, "export invalid");
    QDir().mkpath("progress-policy-qa");
    write("progress-policy-qa/ui-comparison.json", read(exported));
    dialog->resize(1380, 850);
    QTest::qWait(30);
    check(dialog->grab().save("progress-policy-qa/comparison.png"), "screenshot failed");
    button(*dialog, "jumpProgressPolicyTask")->click();
    check(*doc.project() == p && !doc.dirty() && doc.calculation()->tasks == calculation,
          "preview/jump adopted or mutated source");
    const auto existing = temp.filePath("preserve.json");
    write(existing, "sentinel");
    chooseFile(*dialog, existing, [&] { doc.renameTask(0, "changed during picker"); });
    check(read(existing) == "sentinel" && !button(*dialog, "exportProgressPolicy")->isEnabled(),
          "stale file chooser overwrote existing output");
    doc.undo();
    button(*dialog, "refreshProgressPolicy")->click();
    ready(*dialog);
    button(*dialog, "refreshProgressPolicy")->click();
    button(*dialog, "cancelProgressPolicy")->click();
    QTest::qWait(60);
    check(dialog->property("progressPolicyRows").toInt() == 0 && !button(*dialog, "exportProgressPolicy")->isEnabled(),
          "cancelled result applied");
    doc.save();
    button(*dialog, "refreshProgressPolicy")->click();
    const auto reopen = doc.path();
    doc.open(reopen);
    QTest::qWait(80);
    check(dialog->property("progressPolicyRows").toInt() == 0, "same ID session stale applied");
    button(*dialog, "refreshProgressPolicy")->click();
    ready(*dialog);
    const auto oldId = doc.project()->documentId;
    doc.saveAs(temp.filePath("independent-copy.ppm").toStdString());
    check(doc.project()->documentId != oldId && dialog->property("progressPolicyRows").toInt() == 0 &&
              !button(*dialog, "exportProgressPolicy")->isEnabled(),
          "Save As kept old identity/session visible");
    button(*dialog, "refreshProgressPolicy")->click();
    ready(*dialog);
    dialog->reject();
    QTest::qWait(10);
    main.hide();
    Document ro(store);
    ro.open(path.toStdString(), true);
    ProgressPolicyDialog readonly(ro);
    readonly.show();
    ready(readonly);
    check(button(readonly, "exportProgressPolicy")->isEnabled() && !ro.dirty(), "readonly inspection disabled");
    readonly.reject();
}
void benchmark() {
    QTemporaryDir temp;
    JsonProjectStore store;
    QJsonArray metrics;
    for (int n : {1000, 10000}) {
        const auto p = progress_fixture::large(n);
        const auto path = temp.filePath(QString::number(n) + ".ppm");
        store.save(path.toStdString(), p, {});
        Document doc(store);
        doc.open(path.toStdString());
        QElapsedTimer timer;
        timer.start();
        ProgressPolicyDialog dialog(doc);
        dialog.show();
        ready(dialog);
        const auto readyMs = timer.elapsed();
        auto *table = dialog.findChild<QTableView *>("progressPolicyTasks");
        check(table->model()->rowCount() == n && *doc.project() == p && !doc.dirty(), "large rows/mutation failed");
        metrics.append(QJsonObject{{"tasks", n},
                                   {"ready_ms", qint64(readyMs)},
                                   {"build_ms", dialog.property("progressPolicyBuildMs").toDouble()},
                                   {"render_ms", dialog.property("progressPolicyRenderMs").toDouble()}});
        if (n == 10000) {
            QTest::qWait(20);
            check(dialog.grab().save("progress-policy-qa/large-10000.png"), "large screenshot");
        }
        dialog.reject();
    }
    write("progress-policy-qa/ui-metrics.json", QJsonDocument(metrics).toJson());
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#ifdef Q_OS_WIN
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [] {
        std::cerr << "policy UI timeout\n";
        std::_Exit(2);
    });
    watchdog.start(90000);
    try {
        workflow();
        benchmark();
        std::cout << "progress policy UI menu/virtual table/filter/export/stale/cancel/session/readonly/scale passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
