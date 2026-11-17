#include "ui/GovernanceDialog.h"
#include "ui/DesktopWorkspace.h"
#include "ui/MainWindow.h"
#include "persistence/Automation.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
using namespace jobs;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void rejects(const std::function<void()> &f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("expected rejection");
}
template <class T> T *child(QWidget &w, const char *name) {
    auto *v = w.findChild<T *>(name);
    check(v, name);
    return v;
}
void waitFor(const std::function<bool()> &condition) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 30000) {
        QApplication::processEvents();
        QTest::qWait(10);
    }
    check(condition(), "timed out");
}
void ui() {
    QTemporaryDir temp;
    auto files = std::make_shared<JsonProjectStore>();
    Project p;
    p.documentId = "P";
    p.name = "조직 운영 검증";
    p.startDate = "2026-11-02";
    p.tasks = {{"A", "설계", 480, {}}};
    const auto path = temp.filePath("project.ppm");
    files->save(path.toStdString(), p, {});
    DesktopWorkspace workspace(files, nullptr);
    workspace.openProject(path.toStdString());
    auto *doc = workspace.document("P");
    check(doc, "document missing");
    auto w = workspace.configuration();
    w.governance.members = {
        {"owner", "관리자", ""}, {"editor", "편집자", ""}, {"reviewer", "검토자", ""}, {"viewer", "조회자", ""}};
    w.governance.grants = {
        {"owner", "", "owner"}, {"editor", "P", "editor"}, {"reviewer", "P", "reviewer"}, {"viewer", "P", "viewer"}};
    w.governance.portfolio = {{"P", "", "owner", "개발 포트폴리오", "KRW", 5000000, 60}};
    w.governance.risks = {{"R", "P", "A", "설계 변동", "owner", "open", "", 480, 960, 1440, 10000}};
    workspace.applyConfiguration(w);
    workspace.selectActor("owner");
    check(workspace.configuration().governance.audit.size() == 1, "setup not audited");
    GovernanceDialog dialog(workspace);
    dialog.show();
    QTest::qWait(30);
    auto *actor = child<QComboBox>(dialog, "governanceActor");
    actor->setCurrentIndex(actor->findData("viewer"));
    check(doc->readOnly(), "viewer document still writable");
    const auto before = *doc->project();
    rejects([&] { doc->renameTask(0, "denied"); });
    child<QPushButton>(dialog, "governanceApply")->click();
    check(*doc->project() == before && !child<QLabel>(dialog, "governanceStatus")->text().isEmpty(),
          "viewer configuration applied");
    actor->setCurrentIndex(actor->findData("editor"));
    check(!doc->readOnly(), "editor cannot edit");
    child<QLineEdit>(dialog, "governanceEventText")->setText("설계 검토 요청");
    child<QComboBox>(dialog, "governanceEventKind")->setCurrentText("request");
    child<QPushButton>(dialog, "governanceAppendEvent")->click();
    check(workspace.configuration().governance.events.size() == 1, "request UI failed");
    const auto requestId = workspace.configuration().governance.events[0].id;
    actor->setCurrentIndex(actor->findData("reviewer"));
    child<QComboBox>(dialog, "governanceEventKind")->setCurrentText("approve");
    child<QLineEdit>(dialog, "governanceEventTarget")->setText(QString::fromStdString(requestId));
    child<QPushButton>(dialog, "governanceAppendEvent")->click();
    check(workspace.configuration().governance.events.size() == 2, "approval UI failed");
    actor->setCurrentIndex(actor->findData("owner"));
    child<QSpinBox>(dialog, "riskIterations")->setValue(100);
    child<QPushButton>(dialog, "riskAnalyze")->click();
    waitFor([&] { return child<QPushButton>(dialog, "riskAnalyze")->isEnabled(); });
    auto *result = child<QPlainTextEdit>(dialog, "governanceAnalysisResult");
    check(result->toPlainText().contains("P90:"), "risk result missing");
    auto *tabs = dialog.findChild<QTabWidget *>();
    for (int i = 0; i < tabs->count(); ++i)
        if (tabs->tabText(i).contains("위험·포트폴리오"))
            tabs->setCurrentIndex(i);
    QTest::qWait(30);
    QDir().mkpath("governance-qa");
    dialog.grab().save("governance-qa/governance-risk-ui.png");
    child<QPushButton>(dialog, "portfolioAnalyze")->click();
    waitFor([&] { return child<QPushButton>(dialog, "portfolioAnalyze")->isEnabled(); });
    check(result->toPlainText().contains("5000000"), "portfolio result missing");
    child<QPushButton>(dialog, "automationExample")->click();
    auto *input = child<QPlainTextEdit>(dialog, "automationRequest");
    auto json = QJsonDocument::fromJson(input->toPlainText().toUtf8()).object();
    auto operations = json["operations"].toArray();
    auto op = operations[0].toObject();
    op["name"] = "자동화 변경";
    operations[0] = op;
    json["operations"] = operations;
    input->setPlainText(QJsonDocument(json).toJson());
    child<QPushButton>(dialog, "automationPreview")->click();
    check(child<QPushButton>(dialog, "automationApply")->isEnabled() && doc->project()->tasks[0].name == "설계",
          "automation UI preview mutated");
    child<QPushButton>(dialog, "automationApply")->click();
    check(doc->project()->tasks[0].name == "자동화 변경", "automation UI apply failed");
    check(workspace.configuration().governance.audit.back().action == "automation", "automation not audited");
    check(child<QPlainTextEdit>(dialog, "governanceAudit")->toPlainText().contains("automation"),
          "audit UI missing execution");
    doc->undo();
    check(doc->project()->tasks[0].name == "설계", "UI automation undo");
    child<QPushButton>(dialog, "automationExample")->click();
    child<QPushButton>(dialog, "automationPreview")->click();
    const auto audits = workspace.configuration().governance.audit.size();
    actor->setCurrentIndex(actor->findData("viewer"));
    child<QPushButton>(dialog, "automationApply")->click();
    check(workspace.configuration().governance.audit.size() == audits && doc->project()->tasks[0].name == "설계",
          "revoked preview changed data/audit");
    workspace.selectActor("owner");
    GovernanceDialog stale(workspace);
    auto changed = workspace.configuration();
    changed.governance.portfolio[0].budgetMinor = 7000000;
    workspace.applyConfiguration(changed);
    child<QPushButton>(stale, "governanceApply")->click();
    check(workspace.configuration().governance.portfolio[0].budgetMinor == 7000000,
          "stale governance overwrote configuration");
    auto rewrite = workspace.configuration();
    rewrite.governance.events.clear();
    rejects([&] { workspace.applyConfiguration(rewrite); });
    rewrite = workspace.configuration();
    rewrite.governance.audit.clear();
    rejects([&] { workspace.applyConfiguration(rewrite); });
    auto *working = new GovernanceDialog(workspace);
    child<QSpinBox>(*working, "riskIterations")->setValue(10000);
    child<QPushButton>(*working, "riskAnalyze")->click();
    delete working;
    QApplication::processEvents();
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const auto font = QFontDatabase::addApplicationFont("C:/Windows/Fonts/malgun.ttf");
    if (font >= 0)
        app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 10));
    try {
        ui();
        std::cout << "governance UI passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
