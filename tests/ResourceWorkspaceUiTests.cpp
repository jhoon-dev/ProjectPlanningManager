#include "ui/ResourcePlanDialog.h"
#include "ui/IntegratedWorkspaceDialog.h"
#include "ui/DesktopWorkspace.h"
#include "ui/MainWindow.h"
#include "persistence/JsonProjectStore.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
using namespace jobs;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QPushButton *button(QWidget &w, const char *id) {
    auto *b = w.findChild<QPushButton *>(id);
    check(b, "button missing");
    return b;
}
QTableWidget *table(QWidget &w, const char *id) {
    auto *t = w.findChild<QTableWidget *>(id);
    check(t, "table missing");
    return t;
}
void resources() {
    QTemporaryDir tmp;
    JsonProjectStore files;
    Document d(files);
    d.create("자원 편집", "2026-11-02", {"시공"});
    d.setDuration(0, 480);
    auto resource = d.putResource({"", "시공조", "labor", 100, {}});
    ResourcePlanDialog dialog(d);
    dialog.show();
    QTest::qWait(30);
    auto *tabs = dialog.findChild<QTabWidget *>();
    button(dialog, "resourcePlanAdd0")->click();
    auto *materials = table(dialog, "resourcePlanTable0");
    materials->item(0, 0)->setText("볼트");
    materials->item(0, 2)->setText("199");
    materials->item(0, 3)->setText("2.000");
    tabs->setCurrentIndex(1);
    button(dialog, "resourcePlanAdd1")->click();
    table(dialog, "resourcePlanTable1")->item(0, 2)->setText("1.500");
    tabs->setCurrentIndex(2);
    button(dialog, "resourcePlanAdd2")->click();
    table(dialog, "resourcePlanTable2")->item(0, 3)->setText("101");
    tabs->setCurrentIndex(3);
    button(dialog, "resourcePlanAdd3")->click();
    tabs->setCurrentIndex(4);
    button(dialog, "resourcePlanAdd4")->click();
    auto *demands = table(dialog, "resourcePlanTable4");
    auto *binding = qobject_cast<QComboBox *>(demands->cellWidget(0, 2));
    binding->setCurrentIndex(binding->findData(QString::fromStdString(resource)));
    button(dialog, "saveResourcePlan")->click();
    check(dialog.result() == QDialog::Accepted, "resource plan form save failed");
    check(d.project()->resourcePlan.uses[0].quantityMilli == 1500 && d.project()->assignments.size() == 1,
          "form lost quantity/binding");
    d.undo();
    check(d.project()->resourcePlan.materials.empty() && d.project()->assignments.empty(), "form not atomic undo");
    d.redo();
    const auto before = *d.project();
    {
        ResourcePlanDialog cancel(d);
        button(cancel, "resourcePlanAdd0")->click();
        cancel.reject();
        check(*d.project() == before, "cancel changed input");
    }
    {
        ResourcePlanDialog stale(d);
        d.renameTask(0, "변경됨");
        button(stale, "saveResourcePlan")->click();
        check(stale.result() != QDialog::Accepted && !stale.findChild<QLabel *>("resourcePlanError")->text().isEmpty(),
              "stale form committed");
    }
    d.saveAs(tmp.filePath("ro.ppm").toStdString());
    Document ro(files);
    ro.open(d.path(), true);
    ResourcePlanDialog readOnly(ro);
    check(!button(readOnly, "saveResourcePlan")->isEnabled(), "read only save enabled");
    ResourcePlanDialog capture(d);
    capture.show();
    QTest::qWait(30);
    capture.grab().save("resource-plan-ui.png");
    capture.reject();
}
void integrated() {
    QTemporaryDir tmp;
    auto files = std::make_shared<JsonProjectStore>();
    std::vector<std::string> paths;
    for (const auto *id : {"P", "Q"}) {
        Project p;
        p.documentId = id;
        p.name = std::string("Project ") + id;
        p.startDate = "2026-11-02";
        p.calculationProfile = effortProfile;
        p.tasks = {{"A", "같은 ID", 480, {}}};
        p.resources = {{"R", "crew", "labor", 100, {}}};
        p.assignments = {{"A", "R", 100}};
        auto path = tmp.filePath(QString(id) + ".ppm").toStdString();
        files->save(path, p, {});
        paths.push_back(path);
    }
    DesktopWorkspace workspace(files, {});
    workspace.openProject(paths[0]);
    workspace.openProject(paths[1]);
    auto config = workspace.configuration();
    config.resources = {{"S", "시공조", "labor", 100}};
    config.bindings = {{"P", "R", "S"}, {"Q", "R", "S"}};
    config.integrated.forecast = false;
    workspace.applyConfiguration(config);
    const auto p = *workspace.document("P")->project(), q = *workspace.document("Q")->project();
    IntegratedWorkspaceDialog dialog(workspace);
    dialog.show();
    dialog.findChild<QComboBox *>("integratedTo")->setCurrentIndex(1);
    button(dialog, "integratedAdd")->click();
    check(table(dialog, "integratedLinks")->rowCount() == 1, "add qualified link");
    button(dialog, "integratedSave")->click();
    check(workspace.configuration().links.size() == 1, "link not applied");
    button(dialog, "integratedCalculate")->click();
    check(table(dialog, "integratedTasks")->rowCount() == 2, "integrated UI result missing");
    check(button(dialog, "integratedJson")->isEnabled(), "valid result cannot export");
    check(*workspace.document("P")->project() == p && *workspace.document("Q")->project() == q,
          "preview mutated source");
    dialog.grab().save("integrated-workspace-ui.png");
    workspace.document("Q")->renameTask(0, "외부 변경");
    QTest::qWait(300);
    check(table(dialog, "integratedTasks")->rowCount() == 0 && !button(dialog, "integratedJson")->isEnabled(),
          "stale integrated result remained exportable");
    button(dialog, "integratedCalculate")->click();
    check(table(dialog, "integratedTasks")->rowCount() == 2, "recalculation failed");
    dialog.findChild<QCheckBox *>("integratedLevel")->setChecked(true);
    check(table(dialog, "integratedTasks")->rowCount() == 0, "option edit kept old result");
    button(dialog, "integratedCalculate")->click();
    check(table(dialog, "integratedTasks")->rowCount() == 2, "integrated leveling UI");
    dialog.reject();
    for (const auto *id : {"P", "Q"}) {
        auto *d = workspace.document(id);
        if (d->dirty())
            d->save();
    }
    config = workspace.configuration();
    config.links.clear();
    workspace.applyConfiguration(config);
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    const auto font = QFontDatabase::addApplicationFont("C:/Windows/Fonts/malgun.ttf");
    if (font >= 0)
        app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 10));
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [] {
        std::cerr << "UI watchdog\n";
        std::exit(2);
    });
    watchdog.start(90000);
    try {
        QDir().mkpath("resource-workspace-ui-qa");
        check(QDir::setCurrent("resource-workspace-ui-qa"), "output directory");
        resources();
        integrated();
        std::cout << "Resource/workspace UI: catalogs, exact input, binding, atomic save/cancel/undo/read-only/stale, "
                     "qualified links, calculation and stale exports passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
