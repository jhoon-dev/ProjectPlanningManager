#include "TaskTableTest.h"
#include "ui/MainWindow.h"
#include "ui/TaskTableModel.h"
#include "ui/GanttView.h"
#include "persistence/JsonProjectStore.h"
#include "reporting/ReportExport.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <algorithm>
#include <iostream>

namespace {
using namespace ppm;
void check(bool good, const char *message) {
    if (!good)
        throw std::runtime_error(message);
}
void callbackCheck(bool good, const char *message) {
    if (!good) {
        std::cerr << "FAIL callback: " << message << '\n';
        std::exit(2);
    }
}
QAction *action(QWidget &window, const char *name) {
    auto *a = window.findChild<QAction *>(name);
    check(a && a->isEnabled(), "action missing/disabled");
    return a;
}
template <class F> void modal(QAction *a, F f) {
    QTimer::singleShot(0, [f] {
        try {
            f(QApplication::activeModalWidget());
        } catch (const std::exception &e) {
            std::cerr << e.what() << '\n';
            std::exit(2);
        }
    });
    a->trigger();
}
void chooseDefault(QWidget *dialog) {
    auto *input = qobject_cast<QInputDialog *>(dialog);
    callbackCheck(input, "choice dialog missing");
    input->accept();
}
void calculate(Document &doc, MainWindow &window) {
    for (auto *a : window.findChildren<QAction *>())
        if (a->shortcut() == QKeySequence(Qt::Key_F9)) {
            a->trigger();
            break;
        }
    for (int i = 0; i < 1000; ++i) {
        QTest::qWait(10);
        if (doc.calculation() && doc.calculation()->status == CalculationStatus::Valid &&
            doc.calculation()->inputRevision == doc.project()->revision)
            return;
    }
    throw std::runtime_error("calculation did not finish");
}
void workflow() {
    QTemporaryDir temp;
    JsonProjectStore store;
    Document doc(store);
    doc.create("현장 분류 검증", "2026-11-02", {"기초 A", "구조 B", "전기 C"});
    for (std::size_t i = 0; i < 3; ++i)
        doc.setDuration(i, std::int64_t(i + 1) * 60);
    const auto a = doc.project()->tasks[0].id, b = doc.project()->tasks[1].id, c = doc.project()->tasks[2].id;
    doc.setPredecessors(1, {a});
    doc.putCustomField({"quantity", "수량", CustomFieldType::Number, "m²", {}});
    doc.putCustomField({"phase", "단계", CustomFieldType::Choice, {}, {{"planned", "계획"}, {"started", "진행"}}});
    doc.putActivityCodeType(
        {"zone", "구역", {{"north", "북측", {}}, {"block", "1구역", "north"}, {"south", "남측", {}}}});
    doc.setTaskAttribute({a}, "field:quantity", "10");
    doc.setTaskAttribute({b}, "field:quantity", "2");
    doc.setTaskAttribute({c}, "field:quantity", "-0.125001");
    doc.setTaskAttribute({a, b}, "field:phase", "planned");
    doc.setTaskAttribute({c}, "field:phase", "started");
    doc.setTaskAttribute({a, b}, "code:zone", "block");
    doc.setTaskAttribute({c}, "code:zone", "south");
    doc.captureBaseline("원본 승인", "PM", "검증", "2026-11-02T00:00:00Z");
    const auto frozen = doc.project()->baselines[0];
    doc.applyCalculation(ppm::calculate(*doc.project()));
    MainWindow window(doc);
    window.resize(1500, 900);
    window.show();
    QTest::qWait(30);
    auto *table = taskTable(window);
    auto *model = dynamic_cast<TaskTableModel *>(table->model());
    check(model && model->columnCount() == 17 && cellText(table, 0, 14) == "10 m²",
          "dynamic attribute columns missing");
    const auto beforeView = *doc.project();
    modal(action(window, "configureTaskView"), [](QWidget *dialog) {
        callbackCheck(dialog && dialog->objectName() == "taskViewSettingsDialog", "view settings absent");
        auto *g0 = dialog->findChild<QComboBox *>("viewGroup0"), *g1 = dialog->findChild<QComboBox *>("viewGroup1"),
             *sort = dialog->findChild<QComboBox *>("viewAttributeSort");
        g0->setCurrentIndex(g0->findData("code:zone"));
        g1->setCurrentIndex(g1->findData("field:phase"));
        sort->setCurrentIndex(sort->findData("field:quantity"));
        auto *columns = dialog->findChild<QListWidget *>("viewColumns");
        for (int i = 0; i < columns->count(); ++i)
            if (columns->item(i)->data(Qt::UserRole) == "field:quantity") {
                auto *item = columns->takeItem(i);
                item->setData(Qt::UserRole + 1, 180);
                columns->insertItem(0, item);
                break;
            }
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    check(*doc.project() == beforeView && doc.calculation()->status == CalculationStatus::Valid,
          "transient grouping edited project or schedule");
    auto *tree = window.findChild<QTreeWidget *>("wbsTree");
    QTreeWidgetItem *north = nullptr;
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        if ((*it)->data(0, Qt::UserRole + 2).toStringList() == QStringList{"vblock"})
            north = *it;
    check(north && north->text(0).contains("2개"), "code group count missing");
    tree->setCurrentItem(north);
    QTest::qWait(20);
    check(model->rowCount() == 2 && cellText(table, 0, 8).toStdString() == b &&
              cellText(table, 1, 8).toStdString() == a && cellText(table, 0, 2) == "1",
          "group filtering/numeric sort changed source identity or predecessors");
    auto *gantt = dynamic_cast<GanttView *>(window.findChild<QGraphicsView *>("ganttView"));
    int bars = 0;
    for (auto *item : gantt->scene()->items())
        if (item->data(1) == "task")
            ++bars;
    check(bars == 2 && window.findChild<QLabel *>("wbsSummary")->text().contains("3h"),
          "grouped Gantt/count/duration mismatch");
    modal(action(window, "manageCustomFields"), [&](QWidget *manager) {
        QTimer::singleShot(0, [] {
            auto *editor = QApplication::activeModalWidget();
            callbackCheck(editor && editor->objectName() == "customFieldEditor", "field editor missing");
            editor->findChild<QLineEdit *>("fieldName")->setText("담당 부서");
            editor->findChild<QComboBox *>("fieldType")->setCurrentIndex(0);
            editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        });
        manager->findChild<QPushButton *>("addCustomField")->click();
        callbackCheck(doc.project()->customFields.size() == 3, "field UI did not save");
        qobject_cast<QDialog *>(manager)->reject();
    });
    check(model->columnCount() == 18, "column schema did not reset after field creation");
    table->setCurrentIndex(model->index(0, 14));
    table->selectRow(0);
    modal(action(window, "editTaskAttributes"), [&](QWidget *dialog) {
        auto *key = dialog->findChild<QComboBox *>("taskAttributeKey"),
             *value = dialog->findChild<QComboBox *>("taskAttributeValue");
        key->setCurrentIndex(key->findData("field:quantity"));
        const auto original = *doc.project();
        value->setEditText("bad");
        dialog->findChild<QPushButton *>("applyTaskAttribute")->click();
        callbackCheck(*doc.project() == original &&
                          !dialog->findChild<QLabel *>("classificationError")->text().isEmpty(),
                      "invalid attribute input mutated project");
        value->setEditText("0");
        dialog->findChild<QPushButton *>("applyTaskAttribute")->click();
        qobject_cast<QDialog *>(dialog)->reject();
    });
    check(attributeValue(doc.project()->tasks[1], "field:quantity") == "0" && doc.project()->baselines[0] == frozen,
          "batch UI did not preserve zero/baseline");
    modal(action(window, "manageActivityCodes"), [&](QWidget *dialog) {
        auto *codes = dialog->findChild<QTreeWidget *>("activityCodeTree");
        for (QTreeWidgetItemIterator it(codes); *it; ++it)
            if ((*it)->data(0, Qt::UserRole) == "block")
                codes->setCurrentItem(*it);
        const auto original = *doc.project();
        dialog->findChild<QPushButton *>("removeActivityCode")->click();
        callbackCheck(*doc.project() == original &&
                          !dialog->findChild<QLabel *>("classificationError")->text().isEmpty(),
                      "used code was deleted");
        qobject_cast<QDialog *>(dialog)->reject();
    });
    calculate(doc, window);
    modal(action(window, "saveTaskView"), [](QWidget *dialog) {
        auto *input = qobject_cast<QInputDialog *>(dialog);
        callbackCheck(input, "view name input missing");
        input->setTextValue("현장 보기");
        input->accept();
    });
    check(doc.project()->savedTaskViews.size() == 1 && doc.calculation()->status == CalculationStatus::Valid,
          "view save lost valid calculation");
    doc.saveAs((temp.path() + "/classified.ppm").toStdString());
    const auto saved = *doc.project();
    Document readonly(store);
    readonly.open(doc.path(), true);
    readonly.applyCalculation(ppm::calculate(*readonly.project()));
    MainWindow viewer(readonly);
    viewer.resize(1500, 900);
    viewer.show();
    modal(action(viewer, "applySavedTaskView"), chooseDefault);
    auto *viewTable = taskTable(viewer);
    auto *viewModel = dynamic_cast<TaskTableModel *>(viewTable->model());
    check(viewModel->rowCount() == 2 &&
              viewTable->horizontalHeader()->visualIndex(viewModel->columnForKey("field:quantity")) == 0 &&
              viewTable->columnWidth(viewModel->columnForKey("field:quantity")) == 180 && viewTable->isColumnHidden(8),
          "saved group/column order/width/hidden not restored");
    check(!(viewModel->index(0, 14).flags() & Qt::ItemIsEditable) && *readonly.project() == saved,
          "readonly view altered input or allowed editing");
    modal(action(viewer, "manageCustomFields"), [](QWidget *dialog) {
        callbackCheck(!dialog->findChild<QPushButton *>("addCustomField")->isEnabled(),
                      "readonly definitions editable");
        qobject_cast<QDialog *>(dialog)->reject();
    });
    QDir qa("classification-qa");
    check(QDir().mkpath(qa.path()), "QA directory failed");
    check(viewer.grab().save(qa.filePath("groups.png")), "screenshot failed");
    ReportOptions options;
    options.reportId = "classification-ui";
    options.generatedAt = "2026-11-02T10:00:00Z";
    const auto report = buildReport(saved, options);
    const auto xlsx = qa.filePath("classification.xlsx"), pdf = qa.filePath("classification.pdf");
    QFile::remove(xlsx);
    QFile::remove(pdf);
    exportXlsx(report, xlsx, ReportFamily::Schedule);
    PdfOptions pdfOptions;
    pdfOptions.font = QApplication::font();
    check(exportPdf(report, pdf, ReportFamily::Schedule, pdfOptions) > 0, "PDF export failed");
    check(viewer.selectTaskById(c), "cross-project task navigation failed");
    check(cellText(viewTable, viewTable->currentRow(), 8).toStdString() == c, "group filters hid navigation target");
    viewer.hide();
    window.hide();
}
void performance() {
    QTemporaryDir temp;
    JsonProjectStore store;
    QJsonArray samples;
    for (const int count : {1000, 10000}) {
        Project p;
        p.documentId = "classification-performance";
        p.name = "Classification performance";
        p.startDate = "2026-11-02";
        p.customFields = {{"quantity", "수량", CustomFieldType::Number, {}, {}},
                          {"category", "구분", CustomFieldType::Text, {}, {}}};
        p.codeTypes = {{"zone", "구역", {}}};
        for (int i = 0; i < 20; ++i)
            p.codeTypes[0].values.push_back({"zone" + std::to_string(i), "구역 " + std::to_string(i), {}});
        for (int i = 0; i < count; ++i) {
            Task t{"T" + std::to_string(i), "Task " + std::to_string(i), 60, {}};
            if (i >= 100)
                t.predecessors.push_back("T" + std::to_string(i - 100));
            t.attributes = {{"field:quantity", std::to_string(i % 10)},
                            {"field:category", "C" + std::to_string(i % 5)},
                            {"code:zone", "zone" + std::to_string(i % 20)}};
            p.tasks.push_back(std::move(t));
        }
        SavedTaskView view{"grouped", "분류 보기", {}};
        view.settings.groupBy = {"code:zone", "field:category"};
        view.settings.sortKey = "field:quantity";
        p.savedTaskViews.push_back(view);
        const auto path = (temp.path() + "/performance-" + QString::number(count) + ".ppm").toStdString();
        store.save(path, p, {});
        Document doc(store);
        doc.open(path);
        doc.applyCalculation(ppm::calculate(*doc.project()));
        MainWindow window(doc);
        window.show();
        QJsonArray times;
        for (int repeat = 0; repeat < 3; ++repeat) {
            window.findChild<QPushButton *>("resetTaskView")->click();
            QElapsedTimer timer;
            timer.start();
            modal(action(window, "applySavedTaskView"), chooseDefault);
            QApplication::processEvents();
            times.append(double(timer.nsecsElapsed()) / 1000000);
            check(taskTable(window)->rowCount() == count, "performance view omitted tasks");
        }
        samples.append(QJsonObject{{"tasks", count},
                                   {"group_apply_ms", times},
                                   {"groups", 40},
                                   {"scope", "Qt offscreen saved view apply including tree/model/Gantt"}});
        window.hide();
    }
    QFile output("classification-qa/performance.json");
    check(output.open(QIODevice::WriteOnly), "performance output failed");
    output.write(QJsonDocument(samples).toJson());
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    const auto fontId =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (fontId >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).first(), 9));
#endif
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [] {
        std::cerr << "classification UI timeout\n";
        std::exit(2);
    });
    watchdog.start(180000);
    try {
        workflow();
        performance();
        std::cout << "classification UI/grouping/saved views/readonly/export/performance passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
