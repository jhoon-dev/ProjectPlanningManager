#include "ExternalEvmFixture.h"
#include "ui/ExternalEvmDialog.h"
#include "ui/ReportDialog.h"
#include "ui/MainWindow.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDir>
#include <QFileDialog>
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
void check(bool good, const char *message) {
    if (!good)
        throw std::runtime_error(message);
}
bool ready(ExternalEvmDialog &d) {
    for (int i = 0; i < 1500; ++i) {
        QTest::qWait(10);
        if (d.findChild<QPushButton *>("externalEvmSubmit")->isEnabled())
            return true;
    }
    std::cerr << d.findChild<QLabel *>("externalEvmMessage")->text().toStdString() << '\n';
    return false;
}
void fill(ExternalEvmDialog &d) {
    auto *t = d.findChild<QTableView *>("externalEvmPeriods");
    auto *m = t->model();
    check(m->rowCount() == 2, "source row count");
    for (int r = 0; r < 2; ++r) {
        check(m->setData(m->index(r, 0), Qt::Checked, Qt::CheckStateRole), "choose period");
        check(m->setData(m->index(r, 6), r == 0 ? "0.00" : "12.00", Qt::EditRole), "opening EV");
        check(m->setData(m->index(r, 7), r == 0 ? "0.00" : "10.00", Qt::EditRole), "opening PV");
    }
    d.findChild<QLineEdit *>("externalEvmReporter")->setText("현장 EV 담당");
    d.findChild<QLineEdit *>("externalEvmReason")->setText("원본 기간·개시 누계 및 대체 범위 확인");
    d.findChild<QCheckBox *>("externalEvmConfirmed")->setChecked(true);
}
QString cell(QTableView *t, int r, int c) {
    return t->model()->index(r, c).data().toString();
}
bool reportReady(ReportDialog &d, const QString &needle = {}) {
    for (int i = 0; i < 1000; ++i) {
        QTest::qWait(10);
        if (d.findChild<QPushButton *>("exportXlsx")->isEnabled() &&
            d.findChild<QLabel *>("reportMessage")->text().contains(needle))
            return true;
    }
    return false;
}
void chooseFile(const QString &path) {
    QTimer::singleShot(40, [path] {
        auto *f = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        if (!f) {
            std::cerr << "chooser missing\n";
            std::exit(2);
        }
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
        std::cerr << "external EVM UI timeout\n";
        std::exit(2);
    });
    watchdog.start(120000);
    try {
        QTemporaryDir tmp;
        JsonProjectStore store;
        const auto path = (tmp.path() + "/source.ppm").toStdString();
        const auto p = external_evm_fixture::make(store, path);
        Document d(store);
        d.open(path);
        QDir().mkpath("external-xer-evm-ui-qa");
        MainWindow main(d);
        main.show();
        auto *action = main.findChild<QAction *>("adoptExternalXerEvm");
        check(action && action->isEnabled(), "adoption menu");
        bool menu = false;
        QTimer::singleShot(70, [&] {
            auto *v = dynamic_cast<ExternalEvmDialog *>(QApplication::activeModalWidget());
            if (!v) {
                std::cerr << "adoption dialog missing\n";
                std::exit(2);
            }
            menu = true;
            v->reject();
        });
        action->trigger();
        check(menu, "menu not launched");
        const auto original = *d.project();
        ExternalEvmDialog dialog(d, p.baselines.back().id, &main);
        dialog.show();
        QTest::qWait(40);
        check(!dialog.findChild<QPushButton *>("externalEvmAnalyze")->isEnabled(), "unconfirmed proposal enabled");
        auto *source = dialog.findChild<QTableView *>("externalEvmPeriods");
        check(cell(source, 0, 6) == "미정" && cell(source, 0, 7) == "미정", "opening zero inferred");
        fill(dialog);
        dialog.resize(1100, 760);
        dialog.grab().save("external-xer-evm-ui-qa/source.png");
        dialog.resize(800, 600);
        QTest::qWait(30);
        dialog.grab().save("external-xer-evm-ui-qa/source-small.png");
        check(dialog.findChild<QPushButton *>("externalEvmAnalyze")->visibleRegion().boundingRect().height() > 0,
              "footer not visible");
        dialog.findChild<QPushButton *>("externalEvmAnalyze")->click();
        check(ready(dialog), "adoption preview");
        auto *preview = dialog.findChild<QTableView *>("externalEvmPreview");
        check(cell(preview, 0, 3) == "25.00" && cell(preview, 0, 5) == "20.00" && cell(preview, 0, 6) == "122.80",
              "preview values");
        check(*d.project() == original && !d.dirty(), "preview mutated native facts");
        check(!source->model()->setData(source->model()->index(0, 6), "0.001", Qt::EditRole) &&
                  !dialog.findChild<QPushButton *>("externalEvmSubmit")->isEnabled() &&
                  !dialog.findChild<QCheckBox *>("externalEvmConfirmed")->isChecked(),
              "invalid draft kept verification");
        fill(dialog);
        dialog.findChild<QPushButton *>("externalEvmAnalyze")->click();
        check(ready(dialog), "preview after invalid draft");
        dialog.resize(1100, 760);
        dialog.grab().save("external-xer-evm-ui-qa/preview.png");
        source->model()->setData(source->model()->index(0, 6), "0.01", Qt::EditRole);
        check(!dialog.findChild<QCheckBox *>("externalEvmConfirmed")->isChecked() &&
                  !dialog.findChild<QPushButton *>("externalEvmSubmit")->isEnabled(),
              "edited draft kept verification");
        fill(dialog);
        dialog.findChild<QPushButton *>("externalEvmAnalyze")->click();
        dialog.findChild<QPushButton *>("externalEvmCancel")->click();
        QTest::qWait(150);
        check(!dialog.findChild<QPushButton *>("externalEvmSubmit")->isEnabled() && *d.project() == original,
              "cancelled preparation published");
        dialog.findChild<QPushButton *>("externalEvmAnalyze")->click();
        check(ready(dialog), "retry");
        dialog.findChild<QPushButton *>("externalEvmSubmit")->click();
        check(dialog.result() == QDialog::Accepted && d.project()->externalEvmAdoptions.size() == 1 &&
                  analyzeCosts(*d.project(), p.baselines.back().id).evm.ev == 2000,
              "UI submit");
        auto same = *d.project();
        same.externalEvmAdoptions.clear();
        same.revision = original.revision;
        check(same == original, "submit changed original/native journals");
        const auto out = QDir::currentPath() + "/external-xer-evm-ui-qa/";
        QFile::remove(out + "adopted.ppm");
        d.saveAs((out + "adopted.ppm").toStdString());
        {
            ReportDialog report(d, {}, p.baselines.back().id, &main, true);
            report.show();
            report.findChild<QDateEdit *>("reportFrom")->setDate(QDate(2026, 10, 19));
            report.findChild<QDateEdit *>("reportTo")->setDate(QDate(2026, 10, 28));
            check(reportReady(report), "adopted native period report");
            auto *values = report.findChild<QTableView *>("periodEvm");
            check(cell(values, 0, 11) == "20.00" && cell(values, 0, 14) == "122.80", "report values");
            check(report.findChild<QTableView *>("externalEvmHistory") &&
                      report.findChild<QTableView *>("externalEvmPeriods"),
                  "report adoption provenance");
            report.resize(1100, 760);
            report.grab().save(out + "report.png");
            QFile::remove(out + "ui-report.xlsx");
            chooseFile(out + "ui-report.xlsx");
            report.findChild<QPushButton *>("exportXlsx")->click();
            check(reportReady(report, "내보내기 완료") && QFile::exists(out + "ui-report.xlsx"), "UI XLSX");
            QFile::remove(out + "ui-report.pdf");
            chooseFile(out + "ui-report.pdf");
            report.findChild<QPushButton *>("exportPdf")->click();
            check(reportReady(report, "내보내기 완료") && QFile::exists(out + "ui-report.pdf"), "UI PDF");
            report.reject();
        }
        Document view(store);
        view.open(d.path(), true);
        ExternalEvmDialog readonly(view, p.baselines.back().id);
        readonly.show();
        QTest::qWait(30);
        check(!readonly.findChild<QPushButton *>("externalEvmAnalyze")->isEnabled() &&
                  !readonly.findChild<QPushButton *>("externalEvmSubmit")->isEnabled(),
              "readonly mutation available");
        readonly.reject();
        ExternalEvmDialog stale(d, p.baselines.back().id);
        stale.show();
        fill(stale);
        stale.findChild<QPushButton *>("externalEvmAnalyze")->click();
        d.setWriteAuthorization([] { return true; });
        QTest::qWait(250);
        check(!stale.findChild<QPushButton *>("externalEvmSubmit")->isEnabled(), "session-stale result submitted");
        stale.reject();
        ExternalEvmDialog withdrawal(d, p.baselines.back().id);
        withdrawal.show();
        withdrawal.findChild<QComboBox *>("externalEvmMode")->setCurrentIndex(1);
        withdrawal.findChild<QLineEdit *>("externalEvmReporter")->setText("PM");
        withdrawal.findChild<QLineEdit *>("externalEvmReason")->setText("native 측정 복원");
        withdrawal.findChild<QCheckBox *>("externalEvmConfirmed")->setChecked(true);
        withdrawal.findChild<QPushButton *>("externalEvmAnalyze")->click();
        check(ready(withdrawal), "withdrawal preview");
        withdrawal.findChild<QPushButton *>("externalEvmSubmit")->click();
        check(d.project()->externalEvmAdoptions.size() == 2 && d.project()->externalEvmAdoptions.back().withdrawn &&
                  analyzeCosts(*d.project(), p.baselines.back().id).evm.ev == 3725,
              "withdrawal submit");
        std::cout << "PASS: external XER EV/PV menu, virtual explicit anchors, preview/native AC equality, "
                     "confirmation/draft/cancel/session/read-only, submit/withdrawal and actual XLSX/PDF with "
                     "responsive Windows UI\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
