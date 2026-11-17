#include "persistence/ProjectMpp.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/ProjectJson.h"
#include "domain/CalculationControl.h"
#include "formatting/DiagnosticText.h"
#include "ui/MainWindow.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFont>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QTest>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <thread>
#include <chrono>
using namespace jobs;
namespace {
void check(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &e) {
        rejected = true;
        std::cout << "REJECT: " << errorText(e) << '\n';
    }
    check(rejected, "MPP input unexpectedly accepted");
}
void write(const QString &p, const QByteArray &b) {
    QFile f(p);
    check(f.open(QIODevice::WriteOnly) && f.write(b) == b.size(), "write MPP evidence");
}
void callback(bool ok, const char *message) {
    if (!ok) {
        std::cerr << message << '\n';
        std::_Exit(2);
    }
}
struct Drive {
    QTimer timer;
    QString source, destination;
    bool accept = true, errorExpected = false, seenPreview = false, seenProgress = false, seenError = false,
         cancelProgress = false, handledProgress = false;
    int beats = 0;
    std::function<void()> beforeAccept, whileProgress;
    Drive(QString in, QString out) : source(std::move(in)), destination(std::move(out)) {
        timer.setInterval(20);
        QObject::connect(&timer, &QTimer::timeout, [this] {
            ++beats;
            auto *modal = QApplication::activeModalWidget();
            if (!modal)
                return;
            if (auto *file = qobject_cast<QFileDialog *>(modal)) {
                auto *line = file->findChild<QLineEdit *>("fileNameEdit");
                callback(line, "MPP filename input missing");
                line->setText(source);
                QMetaObject::invokeMethod(file, "accept");
                return;
            }
            if (modal->objectName() == "mppImportOptions") {
                modal->findChild<QComboBox *>("mppPlanCurrency")->setCurrentText("USD");
                modal->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
                return;
            }
            if (modal->objectName() == "exchangeOperation") {
                seenProgress = true;
                if (!handledProgress) {
                    handledProgress = true;
                    if (whileProgress)
                        whileProgress();
                    if (cancelProgress)
                        modal->findChild<QPushButton *>("exchangeCancel")->click();
                }
                return;
            }
            if (modal->objectName() == "projectMppPreview") {
                if (seenPreview)
                    return;
                seenPreview = true;
                auto *detail = modal->findChild<QPlainTextEdit *>("mppDiagnostics");
                callback(detail && detail->isReadOnly() && detail->toPlainText().contains("MPP 원본") &&
                             detail->toPlainText().contains("원가·단가 변환/환산 없음") &&
                             detail->toPlainText().contains("미검증") &&
                             (QFileInfo(source).fileName() != "timephased-multi-rate.mpp" ||
                              detail->toPlainText().contains("계획 곡선: 9배정/13구간")),
                         "MPP source/loss review absent");
                auto *buttons = modal->findChild<QDialogButtonBox *>();
                callback(!buttons->button(QDialogButtonBox::Ok)->isEnabled(), "MPP review confirmation bypassed");
                modal->findChild<QLineEdit *>("mppDestination")->setText(destination);
                modal->findChild<QCheckBox *>("mppConfirm")->setChecked(true);
                modal->grab().save("mpp-qa/preview.png");
                modal->resize(800, 600);
                QApplication::processEvents();
                modal->grab().save("mpp-qa/preview-small.png");
                if (beforeAccept)
                    beforeAccept();
                buttons->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
                return;
            }
            if (auto *box = qobject_cast<QMessageBox *>(modal)) {
                if (box->standardButtons().testFlag(QMessageBox::Discard)) {
                    box->button(QMessageBox::Discard)->click();
                    return;
                }
                if (!errorExpected)
                    std::cerr << box->text().toStdString() << '\n';
                callback(errorExpected, "unexpected MPP UI error");
                seenError = true;
                box->accept();
            }
        });
        timer.start();
    }
};
void ui(const QString &root, const QString &temporary) {
    JsonProjectStore store;
    Document doc(store);
    doc.create("보존할 문서", "2026-11-02", {"원래 작업"});
    doc.setDuration(0, 480);
    doc.saveAs((temporary + "/original.ppm").toStdString());
    MainWindow window(doc);
    window.show();
    QTest::qWait(30);
    auto trigger = [&] {
        auto *action = window.findChild<QAction *>("importProjectMpp");
        check(action && action->isEnabled(), "MPP menu absent");
        action->trigger();
    };
    const auto original = *doc.project();
    const auto session = doc.sessionGeneration();
    const bool curved = QCoreApplication::arguments().contains("--curve-ui");
    const auto source = QDir(root).filePath(curved ? "timephased-multi-rate.mpp" : "mpp14relations.mpp");
    {
        Drive d(source, temporary + "/cancel");
        d.accept = false;
        trigger();
        check(d.seenPreview && d.seenProgress && d.beats > 5 && *doc.project() == original &&
                  doc.sessionGeneration() == session && !QFileInfo::exists(d.destination),
              "MPP review cancel mutated source");
    }
    {
        Drive d(source, temporary + "/cancel-running");
        d.cancelProgress = true;
        trigger();
        check(d.seenProgress && !d.seenPreview && *doc.project() == original && !QFileInfo::exists(d.destination),
              "MPP process cancel published/mutated source");
    }
    {
        Drive d(source, temporary + "/stale");
        d.errorExpected = true;
        d.beforeAccept = [&] { doc.renameTask(0, "의도한 새 편집"); };
        trigger();
        check(d.seenPreview && d.seenError && doc.project()->tasks[0].name == "의도한 새 편집" &&
                  !QFileInfo::exists(d.destination),
              "stale MPP review replaced current document");
        doc.undo();
    }
    {
        Drive d(source, temporary + "/session");
        d.errorExpected = true;
        d.whileProgress = [&] { doc.open((temporary + "/original.ppm").toStdString(), true); };
        trigger();
        check(d.seenError && !d.seenPreview && doc.readOnly() && !QFileInfo::exists(d.destination),
              "MPP worker ignored session/read-only change");
    }
    {
        Drive d(source, temporary + "/published");
        trigger();
        check(d.seenPreview && !d.seenError && !doc.readOnly() &&
                  doc.path() == (d.destination + "/project.ppm").toStdString() &&
                  doc.project()->tasks.size() == (curved ? 12u : 5u),
              "MPP readonly source/new copy import failed");
        if (curved)
            check(std::any_of(doc.project()->assignments.begin(), doc.project()->assignments.end(),
                              [](const auto &a) { return a.nativeCurve && a.nativeCurve->mspCode == 8; }),
                  "UI flattened manual MPP curve");
        write("mpp-qa/ui-project.json", QJsonDocument(json::encodeProject(*doc.project())).toJson());
    }
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setFont(QFont("Malgun Gothic", 9));
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, [] { std::_Exit(3); });
    guard.start(170000);
    try {
        QDir().mkpath("mpp-qa");
        QTemporaryDir temporary;
        JsonProjectStore store;
        const QString root = QString::fromUtf8(JOBS_MPP_FIXTURES);
        if (!app.arguments().contains("--ui-only")) {
            for (const auto &name : QStringList{"mpp9relations.mpp", "mpp12relations.mpp", "mpp14relations.mpp",
                                                "assigned-resources-mpp8-project2003.mpp"}) {
                std::cout << "READ: " << name.toStdString() << std::endl;
                const auto bytes = readProjectMpp(QDir(root).filePath(name));
                auto e = importProjectMpp(bytes, name);
                check(e.mpp == bytes && !e.project.tasks.empty(), "original bytes/task projection");
                check(e.project.costEntries.empty() && e.project.progress.empty() && e.project.baselines.empty() &&
                          e.project.periodActuals.empty() && e.project.stepReports.empty() && !e.project.statusAt,
                      "MPP generated native observed values");
                const auto path = QDir(temporary.path()).filePath(name + "-import");
                auto folder = publishProjectMpp(e, path);
                check(store.load(QDir(folder).filePath("project.ppm").toStdString()).project == e.project,
                      "MPP native roundtrip");
                QFile source(QDir(folder).filePath("original.mpp"));
                check(source.open(QIODevice::ReadOnly) && source.readAll() == bytes, "MPP original preservation");
                rejects([&] { publishProjectMpp(e, path); });
                write("mpp-qa/" + name + ".json", QJsonDocument(json::encodeProject(e.project)).toJson());
                write("mpp-qa/" + name + ".report.json", QJsonDocument(e.report).toJson());
                write("mpp-qa/" + name + ".reader.xml", e.readerXml);
            }
            const auto valid = readProjectMpp(QDir(root).filePath("mpp9relations.mpp"));
            rejects([&] { importProjectMpp({}, "empty.mpp"); });
            rejects([&] { importProjectMpp("<Project/>", "renamed.mpp"); });
            rejects([&] { importProjectMpp(valid.left(valid.size() / 2), "truncated.mpp"); });
            rejects([&] {
                importProjectMpp(readProjectMpp(QDir(root).filePath("password-protected-mpp9.mpp")), "protected.mpp");
            });
            MppReaderOptions missing;
            missing.runtimeDirectory = temporary.path() + "/missing";
            rejects([&] { importProjectMpp(valid, "missing.mpp", missing); });
            MppReaderOptions timeout;
            timeout.timeoutMilliseconds = 1;
            rejects([&] { importProjectMpp(valid, "timeout.mpp", timeout); });
            auto c = std::make_shared<CalculationControl>();
            c->cancel();
            bool cancelled = false;
            try {
                EvaluationScope scope(c);
                importProjectMpp(valid, "cancel.mpp");
            } catch (const CalculationCancelled &) {
                cancelled = true;
            }
            check(cancelled, "MPP cancellation swallowed");
            rejects([&] { importProjectMpp(QByteArray(32 * 1024 * 1024 + 1, 'x'), "oversized.mpp"); });
            MppReaderOptions wrongCurrency;
            wrongCurrency.planCurrency = "GBP";
            rejects([&] { importProjectMpp(valid, "invalid-currency.mpp", wrongCurrency); });
            MppReaderOptions badTime;
            badTime.timeoutMilliseconds = 120001;
            rejects([&] { importProjectMpp(valid, "invalid-timeout.mpp", badTime); });
            auto stopped = std::make_shared<CalculationControl>();
            std::thread cancelThread([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                stopped->cancel();
            });
            bool cancelledRunning = false;
            QElapsedTimer elapsed;
            elapsed.start();
            try {
                EvaluationScope scope(stopped);
                importProjectMpp(valid, "running-cancel.mpp");
            } catch (const CalculationCancelled &) {
                cancelledRunning = true;
            }
            cancelThread.join();
            check(cancelledRunning && elapsed.elapsed() < 5000, "MPP running cancellation/cleanup took too long");
        }
        if (!app.arguments().contains("--contract-only"))
            ui(root, temporary.path());
        std::cout << "PASS: real MPP8/9/12/14 reading, exact original/native publication, no inferred observations, "
                     "malformed/password/missing runtime/timeout/cancel boundaries\n";
    } catch (const std::exception &e) {
        std::cerr << errorText(e) << '\n';
        return 1;
    }
    return 0;
}
