#include "TaskTableTest.h"
#include "ui/MainWindow.h"
#include "ui/SchedulePrintDialog.h"
#include "persistence/JsonProjectStore.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFile>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <iostream>
namespace {
using namespace ppm;
void check(bool good, const char *message) {
    if (!good)
        throw std::runtime_error(message);
}
template <class F> void until(F f, const char *message) {
    QElapsedTimer t;
    t.start();
    while (!f() && t.elapsed() < 15000)
        QTest::qWait(10);
    check(f(), message);
}
QPushButton *button(SchedulePrintDialog &d, const char *name) {
    auto *b = d.findChild<QPushButton *>(name);
    check(b, "print button missing");
    return b;
}
void ready(SchedulePrintDialog &d) {
    until([&] { return button(d, "exportSchedulePdf")->isEnabled(); }, "print preparation did not finish");
}
void workflow() {
    JsonProjectStore store;
    Document doc(store);
    doc.create("화면 인쇄 검증", "2026-11-02", {"Alpha", "Beta", "KeepA", "KeepB"});
    for (int i = 0; i < 4; ++i)
        doc.setDuration(i, 480);
    const auto baseline = doc.captureBaseline("최초", "PM", "인쇄", "2026-11-03T00:00:00Z");
    QTemporaryDir temp;
    const auto path = (temp.path() + "/print.ppm").toStdString();
    doc.saveAs(path);
    MainWindow window(doc);
    window.show();
    auto *table = taskTable(window);
    check(table, "table missing");
    auto *search = window.findChild<QLineEdit *>("taskSearch");
    search->setText("Keep");
    until([&] { return table->model()->rowCount() == 2; }, "search filter not applied");
    window.findChild<QComboBox *>("taskSort")->setCurrentIndex(2);
    until([&] { return cellText(table, 0, 0) == "KeepB"; }, "sorted print source not ready");
    table->setColumnHidden(2, true);
    table->horizontalHeader()->moveSection(table->horizontalHeader()->visualIndex(4), 0);
    table->selectRow(0);
    auto source = captureSchedulePrint(doc, *table, "Keep · 이름 내림차순", QString::fromStdString(baseline));
    check(source.rows.size() == 2 && source.rows[0].taskId == doc.project()->tasks[3].id &&
              source.columns[2].key == "finish" &&
              std::none_of(source.columns.begin(), source.columns.end(),
                           [](const auto &c) { return c.key == "predecessors"; }),
          "capture lost filter/order/hidden columns");
    const auto before = *doc.project();
    SchedulePrintDialog dialog(doc, source);
    dialog.show();
    ready(dialog);
    dialog.findChild<QComboBox *>("printScope")->setCurrentIndex(1);
    check(!button(dialog, "exportSchedulePdf")->isEnabled(), "changed settings left old output enabled");
    button(dialog, "prepareSchedulePrint")->click();
    ready(dialog);
    check(dialog.findChild<QLabel *>("schedulePrintMessage")->text().startsWith("1 작업"),
          "selected visible scope wrong");
    auto *fit = dialog.findChild<QCheckBox *>("printFitTime");
    fit->setChecked(false);
    dialog.findChild<QCheckBox *>("printAutoDates")->setChecked(false);
    dialog.findChild<QDateEdit *>("printFrom")->setDate(QDate(2026, 11, 1));
    dialog.findChild<QDateEdit *>("printTo")->setDate(QDate(2026, 11, 15));
    dialog.findChild<QSpinBox *>("printDays")->setValue(7);
    button(dialog, "prepareSchedulePrint")->click();
    ready(dialog);
    check(dialog.findChild<QSpinBox *>("printPage")->maximum() >= 4, "time pages missing in preview");
    QDir().mkpath("schedule-print-qa");
    QTest::qWait(30);
    check(dialog.grab().save("schedule-print-qa/preview.png"), "preview screenshot failed");
    bool fileDialog = false;
    QTimer::singleShot(20, [&] {
        if (auto *file = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
            fileDialog = true;
            file->reject();
        }
    });
    button(dialog, "exportSchedulePdf")->click();
    check(fileDialog && *doc.project() == before, "PDF choice cancel changed source");
    dialog.findChild<QSpinBox *>("printFont")->setValue(10);
    button(dialog, "prepareSchedulePrint")->click();
    button(dialog, "cancelSchedulePrint")->click();
    until([&] { return !button(dialog, "cancelSchedulePrint")->isEnabled(); }, "print cancellation stalled");
    check(!button(dialog, "exportSchedulePdf")->isEnabled(), "cancelled preview applied");
    button(dialog, "prepareSchedulePrint")->click();
    doc.open(path);
    until([&] { return !button(dialog, "cancelSchedulePrint")->isEnabled(); }, "stale preparation stalled");
    check(!button(dialog, "exportSchedulePdf")->isEnabled(), "same-id reopened source accepted old preparation");
    dialog.reject();
    SchedulePrintDialog staleFile(
        doc, captureSchedulePrint(doc, *table, "파일 선택 중 변경", QString::fromStdString(baseline)));
    staleFile.show();
    ready(staleFile);
    const auto stalePath = temp.path() + "/stale.pdf";
    bool selectedFile = false;
    QTimer::singleShot(20, [&] {
        if (auto *file = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
            selectedFile = true;
            file->setDirectory(temp.path());
            file->selectFile("stale.pdf");
            doc.open(path);
            QMetaObject::invokeMethod(file, "accept", Qt::QueuedConnection);
        }
    });
    button(staleFile, "exportSchedulePdf")->click();
    check(selectedFile && !QFile::exists(stalePath) && !button(staleFile, "exportSchedulePdf")->isEnabled(),
          "file-choice source change published old PDF");
    staleFile.reject();
    Document ro(store);
    ro.open(path, true);
    MainWindow viewer(ro);
    viewer.show();
    auto readSource = captureSchedulePrint(ro, *taskTable(viewer), "읽기 전용", {});
    SchedulePrintDialog readOnly(ro, readSource);
    readOnly.show();
    ready(readOnly);
    check(!ro.dirty(), "read-only print mutated document");
    readOnly.reject();
    viewer.hide();
    bool opened = false;
    QTimer::singleShot(30, [&] {
        if (auto *active = QApplication::activeModalWidget(); active && active->objectName() == "schedulePrintDialog") {
            opened = true;
            static_cast<QDialog *>(active)->reject();
        }
    });
    auto *action = window.findChild<QAction *>("schedulePrintPreview");
    check(action, "print menu missing");
    action->trigger();
    check(opened, "print menu did not open preview");
    window.hide();
}
} // namespace
int main(int argc, char **argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).first(), 9));
#endif
    try {
        workflow();
        std::cout << "print UI: filter/order/columns/selection, prepare/options/preview, cancel, same-id session, "
                     "readonly, file cancellation, menu passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
