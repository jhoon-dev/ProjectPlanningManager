#include "ui/ResourcePlanningDialog.h"
#include "ui/MainWindow.h"
#include "application/Document.h"
#include "persistence/JsonProjectStore.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent>
#include <future>
#include <iostream>
using namespace jobs;
namespace {
void check(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
template <class F> void wait(F done, const char *s) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < 30000)
        QTest::qWait(10);
    check(done(), s);
}
QPushButton *button(QDialog &d, const char *name) {
    auto *b = d.findChild<QPushButton *>(name);
    check(b, "button");
    return b;
}
QLineEdit *line(QDialog &d, const char *name) {
    auto *b = d.findChild<QLineEdit *>(name);
    check(b, "line");
    return b;
}
void preview(QDialog &d) {
    button(d, "previewResourcePlanning")->click();
    wait([&] { return d.findChild<QCheckBox *>("resourcePlanningConfirm")->isEnabled(); }, "preview prepared");
}
void apply(QDialog &d) {
    auto *c = d.findChild<QCheckBox *>("resourcePlanningConfirm");
    c->setChecked(true);
    check(button(d, "applyResourcePlanning")->isEnabled(), "explicit confirmation");
    button(d, "applyResourcePlanning")->click();
}
void run() {
    QDir().mkpath("resource-planning-qa");
    QTemporaryDir temp;
    JsonProjectStore store;
    Document doc(store);
    doc.create("生産性·고유 곡선 화면", "2026-11-02", {"시공"});
    doc.setDuration(0, 960);
    Resource r;
    r.name = "시공 인력";
    r.maxUnits = 300;
    const auto rid = doc.putResource(r), tid = doc.project()->tasks[0].id;
    doc.setAssignments(tid, {{tid, rid, 100}});
    const auto original = *doc.project();
    {
        ResourcePlanningDialog dialog(doc);
        dialog.show();
        line(dialog, "productivityUnit")->setText("m3");
        line(dialog, "productivityRate")->setText("30.000");
        line(dialog, "productivityDate")->setText("2026-11-09");
        line(dialog, "productivityReference")->setText("현장 측정 30m3/인시");
        line(dialog, "resourcePlanningReporter")->setText("검토자");
        line(dialog, "resourcePlanningReason")->setText("명시 계획 산정");
        button(dialog, "saveProductivityRate")->click();
        wait([&] { return doc.project()->resources[0].productivity.has_value(); }, "rate saved");
        check(doc.project()->tasks == original.tasks && doc.project()->assignments == original.assignments,
              "catalog changed plan");
        line(dialog, "productivityQuantity")->setText("120.000");
        preview(dialog);
        check(!button(dialog, "applyResourcePlanning")->isEnabled(), "unconfirmed commit enabled");
        check(dialog.findChild<QLabel *>("resourcePlanningMessage")->text().contains("4.0000"),
              "quantity-to-work preview");
        dialog.grab().save("resource-planning-qa/productivity.png");
        dialog.resize(800, 600);
        QTest::qWait(30);
        auto *submit = button(dialog, "applyResourcePlanning");
        check(dialog.rect().contains(QRect(submit->mapTo(&dialog, QPoint{}), submit->size())), "small footer");
        dialog.grab().save("resource-planning-qa/productivity-small.png");
        line(dialog, "productivityQuantity")->setText("90.000");
        check(!submit->isEnabled() && !dialog.findChild<QCheckBox *>("resourcePlanningConfirm")->isEnabled(),
              "changed quantity kept certificate");
        preview(dialog);
        apply(dialog);
        wait([&] { return doc.project()->assignments[0].productivityEstimate.has_value(); }, "estimate applied");
        check(doc.project()->assignments[0].productivityEstimate->workUnitMinutes == 18000, "UI estimate differs");
        auto *tabs = dialog.findChild<QTabWidget *>("resourcePlanningTabs");
        tabs->setCurrentIndex(1);
        auto *preset = dialog.findChild<QComboBox *>("nativeCurvePreset");
        preset->setCurrentIndex(6);
        preview(dialog);
        dialog.resize(1100, 900);
        QTest::qWait(20);
        dialog.grab().save("resource-planning-qa/curve.png");
        dialog.resize(800, 600);
        QTest::qWait(20);
        dialog.grab().save("resource-planning-qa/curve-small.png");
        apply(dialog);
        wait([&] { return doc.project()->assignments[0].nativeCurve.has_value(); }, "curve apply");
        check(doc.project()->assignments[0].nativeCurve->weights.size() == 21, "curve definition missing");
        // Invalid point sum stays a reversible draft.
        auto *points = dialog.findChild<QTableWidget *>("nativeCurvePoints");
        points->item(1, 1)->setText("99");
        const auto before = *doc.project();
        button(dialog, "previewResourcePlanning")->click();
        wait([&] { return button(dialog, "previewResourcePlanning")->isEnabled(); }, "bad curve result");
        check(*doc.project() == before && !submit->isEnabled(), "bad curve committed");
        button(dialog, "reloadResourcePlanning")->click();
        preset->setCurrentIndex(2);
        preview(dialog);
        doc.renameTask(0, "외부 편집");
        check(!submit->isEnabled(), "stale document permitted submit");
        button(dialog, "reloadResourcePlanning")->click();
        const int threads = QThreadPool::globalInstance()->maxThreadCount();
        QThreadPool::globalInstance()->setMaxThreadCount(1);
        std::promise<void> release;
        auto pending = release.get_future().share();
        auto blocker = QtConcurrent::run([pending] { pending.wait(); });
        preset->setCurrentIndex(1);
        button(dialog, "previewResourcePlanning")->click();
        button(dialog, "cancelResourcePlanning")->click();
        release.set_value();
        wait([&] { return button(dialog, "previewResourcePlanning")->isEnabled(); }, "cancel completion");
        check(!submit->isEnabled(), "cancelled certificate enabled");
        QThreadPool::globalInstance()->setMaxThreadCount(threads);
        preset->setCurrentIndex(3);
        preview(dialog);
        doc.setWriteAuthorization([] { return false; });
        check(!submit->isEnabled() && !button(dialog, "previewResourcePlanning")->isEnabled(),
              "revoked role still editable");
        dialog.reject();
        doc.setWriteAuthorization({});
    }
    doc.undo();
    doc.undo();
    const auto path = temp.filePath("resource.ppm").toStdString();
    doc.saveAs(path);
    doc.open(path, true);
    {
        ResourcePlanningDialog dialog(doc);
        dialog.show();
        check(!button(dialog, "saveProductivityRate")->isEnabled() &&
                  !button(dialog, "previewResourcePlanning")->isEnabled(),
              "readonly controls");
        dialog.reject();
    }
    doc.open(path);
    {
        MainWindow window(doc);
        window.show();
        auto *action = window.findChild<QAction *>("resourcePlanning");
        check(action, "menu action");
        QTimer::singleShot(0, [] {
            auto *dialog = dynamic_cast<ResourcePlanningDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                std::exit(2);
            dialog->reject();
        });
        action->trigger();
    }
    QThreadPool::globalInstance()->waitForDone();
    std::cout << "PASS resource planning UI: resource productivity definition, quantity/work preview, "
                 "confirmation/invalidation, named/custom curves, cancel/stale/role/readonly, menu and default/800x600 "
                 "screens\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setFont(QFont("Malgun Gothic", 9));
    try {
        run();
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
