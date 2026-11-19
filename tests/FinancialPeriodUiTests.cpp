#include "PeriodEvmFixture.h"
#include "ui/FinancialPeriodDialog.h"
#include "ui/ReportDialog.h"
#include "ui/MainWindow.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
using namespace ppm;
namespace {
void check(bool c, const char *m) {
    if (!c)
        throw std::runtime_error(m);
}
bool prepared(FinancialPeriodDialog &d) {
    for (int i = 0; i < 1500; ++i) {
        QTest::qWait(10);
        if (d.findChild<QCheckBox *>("confirmFinancialPeriod")->isEnabled())
            return true;
    }
    std::cerr << d.findChild<QLabel *>("financialPeriodMessage")->text().toStdString() << '\n';
    return false;
}
bool reportReady(ReportDialog &d, const QString &text = {}) {
    for (int i = 0; i < 1500; ++i) {
        QTest::qWait(10);
        if (d.findChild<QPushButton *>("exportXlsx")->isEnabled() &&
            d.findChild<QLabel *>("reportMessage")->text().contains(text))
            return true;
    }
    std::cerr << d.findChild<QLabel *>("reportMessage")->text().toStdString() << '\n';
    return false;
}
void metadata(FinancialPeriodDialog &d) {
    d.findChild<QLineEdit *>("financialPeriodReporter")->setText("기간 담당자");
    d.findChild<QLineEdit *>("financialPeriodReason")->setText("기간별 예산·성과 및 당시 지식 확인");
}
void submit(FinancialPeriodDialog &d) {
    d.findChild<QPushButton *>("prepareFinancialPeriod")->click();
    check(prepared(d), "period preview");
    d.findChild<QCheckBox *>("confirmFinancialPeriod")->setChecked(true);
    auto *apply = d.findChild<QPushButton *>("applyFinancialPeriod");
    check(apply->isEnabled(), "submit disabled");
    apply->click();
    QTest::qWait(20);
}
void chooseFile(const QString &path) {
    QTimer::singleShot(40, [path] {
        auto *f = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        if (!f)
            std::exit(2);
        f->findChild<QLineEdit *>("fileNameEdit")->setText(path);
        QMetaObject::invokeMethod(f, "accept");
    });
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#ifdef Q_OS_WIN
    const int font = QFontDatabase::addApplicationFont("C:/Windows/Fonts/malgun.ttf");
    if (font >= 0)
        app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [] {
        std::cerr << "custom period UI timeout\n";
        std::exit(2);
    });
    watchdog.start(120000);
    try {
        QTemporaryDir tmp;
        JsonProjectStore store;
        const auto path = (tmp.path() + "/source.ppm").toStdString();
        const auto p = period_evm_fixture::make(store, path);
        Document d(store);
        d.open(path);
        MainWindow main(d);
        main.show();
        auto *menu = main.findChild<QAction *>("customFinancialPeriods");
        check(menu && menu->isEnabled(), "custom period menu");
        bool menuOpened = false;
        QTimer::singleShot(60, [&] {
            auto *dlg = dynamic_cast<FinancialPeriodDialog *>(QApplication::activeModalWidget());
            if (!dlg)
                std::exit(2);
            menuOpened = true;
            dlg->reject();
        });
        menu->trigger();
        check(menuOpened, "menu dialog not opened");
        QDir().mkpath("custom-financial-periods-ui-qa");
        FinancialPeriodDialog dialog(d, p.baselines[0].id, &main);
        dialog.show();
        QTest::qWait(40);
        dialog.findChild<QLineEdit *>("financialPeriodName")->setText("현장 1차 기간");
        dialog.findChild<QDateEdit *>("financialPeriodFrom")->setDate(QDate(2026, 10, 26));
        dialog.findChild<QDateEdit *>("financialPeriodTo")->setDate(QDate(2026, 10, 27));
        metadata(dialog);
        auto *grid = dialog.findChild<QTableView *>("financialPeriodValues");
        auto *model = grid->model();
        check(model->rowCount() == 2 && model->setData(model->index(0, 2), "5.00", Qt::EditRole) &&
                  model->setData(model->index(0, 3), "4.50", Qt::EditRole),
              "manual period amounts");
        dialog.findChild<QDateEdit *>("financialPeriodTo")->setDate(QDate(2026, 10, 25));
        dialog.findChild<QPushButton *>("prepareFinancialPeriod")->click();
        bool explained = false;
        for (int i = 0; i < 1000; ++i) {
            QTest::qWait(10);
            if (dialog.findChild<QLabel *>("financialPeriodMessage")->text().contains("시작일/종료일")) {
                explained = true;
                break;
            }
        }
        check(explained && !dialog.findChild<QPushButton *>("applyFinancialPeriod")->isEnabled(),
              "worker error did not explain invalid dates");
        dialog.findChild<QDateEdit *>("financialPeriodTo")->setDate(QDate(2026, 10, 27));
        dialog.grab().save("custom-financial-periods-ui-qa/definition.png");
        dialog.resize(800, 600);
        QTest::qWait(40);
        check(dialog.findChild<QPushButton *>("prepareFinancialPeriod")->visibleRegion().boundingRect().height() > 0,
              "small footer hidden");
        dialog.grab().save("custom-financial-periods-ui-qa/definition-small.png");
        dialog.resize(1100, 760);
        submit(dialog);
        check(d.project()->financialPeriodEvents.size() == 1, "define not committed");
        const auto id = d.project()->financialPeriodEvents[0].period.id;
        auto *action = dialog.findChild<QComboBox *>("financialPeriodAction");
        action->setCurrentIndex(1);
        auto *prepare = dialog.findChild<QPushButton *>("prepareFinancialPeriod");
        prepare->click();
        check(prepared(dialog), "close preview");
        auto *preview = dialog.findChild<QTableView *>("financialPeriodPreview");
        check(preview->model()->index(0, 2).data().toString() == "35.00" &&
                  preview->model()->index(0, 3).data().toString() == "25.50" &&
                  preview->model()->index(0, 4).data().toString() == "25.00",
              "preview amounts");
        dialog.grab().save("custom-financial-periods-ui-qa/close-preview.png");
        // Any draft change invalidates a verified receipt, including malformed money.
        dialog.findChild<QCheckBox *>("confirmFinancialPeriod")->setChecked(true);
        dialog.findChild<QLineEdit *>("financialPeriodReason")->setText("수정 사유");
        check(!dialog.findChild<QPushButton *>("applyFinancialPeriod")->isEnabled() && !preview->model(),
              "draft kept confirmation");
        submit(dialog);
        check(d.project()->financialPeriodEvents.size() == 2 &&
                  d.project()->financialPeriodEvents.back().action == "close",
              "close not committed");
        dialog.grab().save("custom-financial-periods-ui-qa/closed-history.png");
        check(!grid->isEnabled(), "closed values editable");
        ReportDialog report(d, {}, p.baselines[0].id, &main, true, id);
        report.show();
        check(reportReady(report), "custom report initial");
        auto *summary = report.findChild<QTableView *>("financialPeriod");
        check(summary && summary->model()->index(0, 4).data().toString() == "25.00", "closed report amount");
        report.grab().save("custom-financial-periods-ui-qa/report.png");
        report.resize(800, 600);
        QTest::qWait(40);
        report.grab().save("custom-financial-periods-ui-qa/report-small.png");
        const auto xlsx = QDir::currentPath() + "/custom-financial-periods-ui-qa/ui-report.xlsx";
        QFile::remove(xlsx);
        chooseFile(xlsx);
        report.findChild<QPushButton *>("exportXlsx")->click();
        check(reportReady(report, "내보내기 완료") && QFile::exists(xlsx), "UI XLSX");
        const auto pdf = QDir::currentPath() + "/custom-financial-periods-ui-qa/ui-report.pdf";
        QFile::remove(pdf);
        chooseFile(pdf);
        report.findChild<QPushButton *>("exportPdf")->click();
        check(reportReady(report, "내보내기 완료") && QFile::exists(pdf), "UI PDF");
        report.reject();
        action->setCurrentIndex(2);
        submit(dialog);
        check(d.project()->financialPeriodEvents.back().action == "reopen", "reopen not committed");
        dialog.findChild<QTabWidget *>("financialPeriodTabs")->setCurrentIndex(0);
        metadata(dialog);
        model = grid->model();
        model->setData(model->index(0, 2), "5.001", Qt::EditRole);
        prepare->click();
        QTest::qWait(30);
        check(!dialog.findChild<QPushButton *>("applyFinancialPeriod")->isEnabled(), "invalid currency accepted");
        model->setData(model->index(0, 2), "5.00", Qt::EditRole);
        prepare->click();
        dialog.findChild<QPushButton *>("cancelFinancialPeriod")->click();
        QTest::qWait(80);
        check(!dialog.findChild<QPushButton *>("applyFinancialPeriod")->isEnabled(), "cancel accepted");
        d.saveAs((tmp.path() + "/periods.ppm").toStdString());
        check(!prepare->isEnabled(), "stale dialog still editable");
        dialog.reject();
        QFile::remove("custom-financial-periods-ui-qa/ui-periods.ppm");
        store.save("custom-financial-periods-ui-qa/ui-periods.ppm", *d.project(), {});
        Document ro(store);
        ro.open((tmp.path() + "/periods.ppm").toStdString(), true);
        FinancialPeriodDialog readonly(ro, p.baselines[0].id, &main);
        readonly.show();
        readonly.findChild<QComboBox *>("financialPeriodChoice")->setCurrentIndex(1);
        check(readonly.findChild<QPushButton *>("financialPeriodReport")->isEnabled(),
              "read-only history report unavailable");
        check(!readonly.findChild<QPushButton *>("prepareFinancialPeriod")->isEnabled(), "read-only period write");
        readonly.reject();
        ReportDialog historical(d, {}, p.baselines[0].id, &main, true, id);
        historical.show();
        check(reportReady(historical), "reopened current report");
        auto *choices = historical.findChild<QComboBox *>("periodEvmBucket");
        int index = -1;
        for (int i = 0; i < choices->count(); ++i)
            if (choices->itemData(i).toString().startsWith("closed:"))
                index = i;
        check(index >= 0, "historical close choice absent");
        choices->setCurrentIndex(index);
        check(reportReady(historical), "historic close report");
        historical.reject();
        Project large;
        large.documentId = store.newId();
        large.name = "1만 작업 기간 표";
        large.startDate = "2026-10-26";
        for (int i = 0; i < 10000; ++i)
            large.tasks.push_back({"large-" + std::to_string(i), "Task " + std::to_string(i), 60, {}});
        const auto largePath = (tmp.path() + "/large.ppm").toStdString();
        store.save(largePath, large, {});
        Document ld(store);
        ld.open(largePath);
        const auto lb = ld.captureBaseline("대규모", "PM", "가상 표", "2026-11-08T00:00:00Z");
        FinancialPeriodDialog largeDialog(ld, lb, &main);
        largeDialog.show();
        QTest::qWait(30);
        auto *largeGrid = largeDialog.findChild<QTableView *>("financialPeriodValues");
        check(largeGrid->model()->rowCount() == 10000 && largeDialog.findChildren<QLineEdit *>().size() < 50,
              "large grid created per-row editors");
        check(largeGrid->model()->setData(largeGrid->model()->index(9999, 2), "0", Qt::EditRole),
              "large final task edit");
        largeDialog.reject();
        ReportDialog obsolete(d, {}, p.baselines[0].id, &main, true, id);
        obsolete.show();
        check(reportReady(obsolete), "stale source report");
        d.create("다른 문서", "2026-10-26", {"Task"});
        obsolete.findChild<QPushButton *>("refreshReport")->click();
        QTest::qWait(200);
        check(!obsolete.findChild<QPushButton *>("exportXlsx")->isEnabled(),
              "replaced source exported old custom period");
        obsolete.reject();

        std::cout << "PASS custom period menu, manual values, preview/confirmation, define/close/reopen, "
                     "read-only/stale/cancel/invalid input, 800x600, historical reports and XLSX/PDF\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
