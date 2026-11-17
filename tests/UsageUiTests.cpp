#include "UsageFixture.h"
#include "ui/UsageDialog.h"
#include "ui/MainWindow.h"
#include "persistence/JsonProjectStore.h"
#include "reporting/ReportExport.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDateEdit>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <iostream>
namespace {
using namespace jobs;
void check(bool good, const char *message) {
    if (!good)
        throw std::runtime_error(message);
}
template <class F> void until(F f, const char *message, int timeout = 20000) {
    QElapsedTimer timer;
    timer.start();
    while (!f() && timer.elapsed() < timeout)
        QTest::qWait(10);
    check(f(), message);
}
QTreeView *tree(UsageDialog &dialog) {
    return dialog.findChild<QTreeView *>("usageTree");
}
void ready(UsageDialog &dialog) {
    until([&] { return tree(dialog)->model()->rowCount() > 0; }, "Usage worker did not produce model");
}
std::int64_t raw(const QModelIndex &index, int metric) {
    return index.data(Qt::UserRole + 2).toList().at(metric).toLongLong();
}
void workflow() {
    JsonProjectStore store;
    QTemporaryDir temp;
    auto p = usage_fixture::project();
    usage_fixture::progress(p);
    const auto path = (temp.path() + "/usage.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path, true);
    const auto before = *doc.project();
    UsageDialog dialog(doc, {}, p.baselines.front().id);
    dialog.show();
    ready(dialog);
    auto *model = tree(dialog)->model();
    auto root = model->index(0, 0);
    check(raw(model->index(0, 1), 0) == 96000 && raw(model->index(0, 1), 1) == 24000, "UI work root wrong");
    check(model->index(0, 2).data().toString() == "80", "UI unallocated actual not shown");
    check(!(model->flags(root) & Qt::ItemIsEditable), "Usage unexpectedly editable");
    dialog.findChild<QComboBox *>("usageMetric")->setCurrentIndex(3);
    check(raw(model->index(0, 1), 2) == 212, "UI cost root wrong");
    dialog.findChild<QComboBox *>("usageHierarchy")->setCurrentIndex(1);
    root = model->index(0, 0);
    check(raw(model->index(0, 1), 2) == 212 && model->rowCount(root) == 3,
          "resource hierarchy total/fixed group wrong");
    check(model->index(0, 2, root).data().toString() == "미정", "resource actual was invented");
    dialog.findChild<QComboBox *>("usagePeriod")->setCurrentIndex(1);
    check(!dialog.findChild<QPushButton *>("exportUsageXlsx")->isEnabled() && model->rowCount() == 0,
          "old condition remained exportable");
    dialog.findChild<QPushButton *>("refreshUsage")->click();
    ready(dialog);
    check(model->columnCount() == 5 && raw(model->index(0, 1), 2) == 212, "weekly UI total changed");
    dialog.findChild<QComboBox *>("usageMetric")->setCurrentIndex(5);
    check(raw(model->index(0, 1), 2) == 74, "remaining resource cost UI wrong");
    QDir().mkpath("usage-qa");
    tree(dialog)->expandAll();
    QTest::qWait(20);
    check(dialog.grab().save("usage-qa/usage.png"), "screenshot failed");
    const auto buttonPath = QDir::current().absoluteFilePath("usage-qa/button-usage.xlsx");
    QFile::remove(buttonPath);
    bool choseFile = false;
    QTimer::singleShot(30, [&] {
        if (auto *file = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
            file->selectFile(buttonPath);
            choseFile = true;
            static_cast<QDialog *>(file)->accept();
        }
    });
    dialog.findChild<QPushButton *>("exportUsageXlsx")->click();
    check(choseFile, "Usage export action did not open file selector");
    until([&] { return QFile::exists(buttonPath) && dialog.findChild<QPushButton *>("exportUsageXlsx")->isEnabled(); },
          "Usage export worker did not publish XLSX");
    auto result = buildUsage(p, {"2026-11-02", "2026-11-15", p.baselines.front().id, {}, UsagePeriod::Day});
    const auto report =
        usageReport(result, UsageHierarchy::Resources, "usage-independent", "2026-11-03T00:00:00Z", false);
    QFile::remove("usage-qa/usage.xlsx");
    QFile::remove("usage-qa/usage.pdf");
    exportXlsx(report, "usage-qa/usage.xlsx");
    PdfOptions pdf;
    pdf.font = QApplication::font();
    exportPdf(report, "usage-qa/usage.pdf", ReportFamily::All, pdf);
    check(*doc.project() == before && doc.revisionSaved(), "read-only Usage mutated input/history");
    doc.open(path, true); // Same document ID/revision, new session must invalidate.
    check(model->rowCount() == 0 && !dialog.findChild<QPushButton *>("exportUsageXlsx")->isEnabled(),
          "session replacement kept stale Usage");
    dialog.reject();
    MainWindow window(doc);
    window.show();
    auto *action = window.findChild<QAction *>("projectUsage");
    check(action, "Usage menu missing");
    bool opened = false;
    QTimer::singleShot(30, [&] {
        auto *active = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (active && active->objectName() == "usageDialog") {
            opened = true;
            active->reject();
        }
    });
    action->trigger();
    check(opened, "Usage action did not open dialog");
    window.hide();
}
void performance() {
    JsonProjectStore store;
    QTemporaryDir temp;
    QJsonArray samples;
    for (const int count : {1000, 10000}) {
        Project p;
        p.documentId = "usage-performance";
        p.name = "Usage performance";
        p.startDate = "2026-11-02";
        p.resources = {{"R", "Crew", "labor", 100, {}}};
        p.resources[0].rates = {{"2026-11-02", 1}};
        for (int i = 0; i < count; ++i) {
            const auto id = "T" + std::to_string(i);
            p.tasks.push_back({id, id, 1, {}});
            p.assignments.push_back({id, "R", 33});
            p.taskCosts.push_back({id, 0});
        }
        const auto path = (temp.path() + "/perf-" + QString::number(count) + ".ppm").toStdString();
        store.save(path, p, {});
        Document doc(store);
        doc.open(path);
        UsageDialog dialog(doc, {}, {});
        dialog.findChild<QDateEdit *>("usageTo")->setDate(QDate(2026, 11, 2));
        QElapsedTimer timer;
        timer.start();
        dialog.show();
        ready(dialog);
        const auto milliseconds = timer.elapsed();
        const auto root = tree(dialog)->model()->index(0, 0);
        check(tree(dialog)->model()->rowCount(root) == count, "large Usage omitted tasks");
        check(raw(tree(dialog)->model()->index(0, 1), 0) == std::int64_t(count) * 33, "large Usage sum wrong");
        dialog.findChild<QPushButton *>("refreshUsage")->click();
        doc.open(path, true);
        until([&] { return dialog.findChildren<QFutureWatcherBase *>().empty(); },
              "replaced Usage worker did not finish cancellation");
        check(tree(dialog)->model()->rowCount() == 0 &&
                  !dialog.findChild<QPushButton *>("exportUsageXlsx")->isEnabled(),
              "in-flight same-ID/revision replacement published stale Usage");
        dialog.findChild<QPushButton *>("refreshUsage")->click();
        dialog.findChild<QPushButton *>("cancelUsage")->click();
        until([&] { return dialog.findChild<QLabel *>("usageMessage")->text().contains("취소"); },
              "Usage cancel was not acknowledged");
        check(tree(dialog)->model()->rowCount() == 0, "canceled Usage showed partial result");
        samples.append(QJsonObject{{"tasks", count},
                                   {"periods", 1},
                                   {"initial_worker_and_model_ms", double(milliseconds)},
                                   {"repeats", 1},
                                   {"scope", "Qt offscreen initial plan + Usage costing + tree model"}});
        dialog.reject();
    }
    QFile file("usage-qa/performance.json");
    check(file.open(QIODevice::WriteOnly), "performance JSON failed");
    file.write(QJsonDocument(samples).toJson());
}
} // namespace
int main(int argc, char **argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    const auto id =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (id >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(id).first(), 9));
#endif
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [] {
        std::cerr << "Usage UI timeout\n";
        std::exit(2);
    });
    watchdog.start(180000);
    try {
        workflow();
        performance();
        std::cout << "Usage UI: hierarchy/period/readonly/session/export/cancel/performance passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
