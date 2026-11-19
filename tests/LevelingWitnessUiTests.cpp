#include "RepartitionFloatFixture.h"
#include "ProgressPolicyFixture.h"
#include "calculation/LevelingFloat.h"
#include "calculation/Resources.h"
#include "persistence/JsonProjectStore.h"
#include "ui/LevelingWitnessDialog.h"
#include "ui/ResourceComparisonDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <iostream>
using namespace ppm;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void until(F f) {
    QElapsedTimer timer;
    timer.start();
    while (!f() && timer.elapsed() < 20000)
        QTest::qWait(10);
    check(f(), "UI timeout");
}
QTableView *view(LevelingWitnessDialog &dialog) {
    return dialog.findChild<QTableView *>("witnessDates");
}
QString cell(LevelingWitnessDialog &dialog, int row, int col) {
    auto *model = view(dialog)->model();
    return model->data(model->index(row, col)).toString();
}
QJsonObject snapshot(LevelingWitnessDialog &dialog) {
    auto *model = view(dialog)->model();
    QJsonArray rows;
    for (int row = 0; row < model->rowCount(); ++row) {
        QJsonArray cells;
        for (int col = 0; col < model->columnCount(); ++col)
            cells.append(cell(dialog, row, col));
        rows.append(QJsonObject{{"id", model->data(model->index(row, 0), Qt::UserRole).toString()}, {"cells", cells}});
    }
    return {{"source", dialog.findChild<QLabel *>("witnessSource")->text()},
            {"summary", dialog.findChild<QLabel *>("witnessSummary")->text()},
            {"focus", dialog.findChild<QComboBox *>("witnessTask")->currentData().toString()},
            {"rows", rows}};
}
LevelingWitnessDialog *opened(ResourceComparisonDialog &parent, const QString &prefix) {
    for (auto *candidate : parent.findChildren<QDialog *>())
        if (auto *dialog = dynamic_cast<LevelingWitnessDialog *>(candidate);
            dialog && dialog->isVisible() && dialog->findChild<QLabel *>("witnessSource")->text().startsWith(prefix))
            return dialog;
    return nullptr;
}
void metadataContracts() {
    const auto p = repartition_fixture::project();
    auto result = levelResources(p, "task-order", 260).leveled;
    check(result.levelingWitnesses && result.levelingWitnesses->tasks.size() == 2, "structured witnesses missing");
    for (const auto &proof : result.levelingWitnesses->tasks) {
        auto witness = result;
        for (const auto &dates : proof.dates) {
            auto it = std::find_if(witness.tasks.begin(), witness.tasks.end(),
                                   [&](const auto &r) { return r.taskId == dates.taskId; });
            check(it != witness.tasks.end(), "witness task identity");
            it->start = dates.start;
            it->finish = dates.finish;
            it->remainingStart = dates.remainingStart;
        }
        check(resourceLoad(p, witness).status == CalculationStatus::Valid, "structured witness not feasible");
    }
    auto unavailable = p;
    unavailable.resources[0].exceptions = {{"2026-11-04", {}}};
    const auto unknown = levelResources(unavailable, "task-order", 260).leveled;
    check(!unknown.levelingFloatCalculated && !unknown.levelingWitnesses, "partial structured witness leaked");
    LevelingFloatScope legacy(3);
    const auto old = levelResources(p, "task-order", 260).leveled;
    check(old.levelingFloatCalculated && !old.levelingWitnesses && old.tasks == result.tasks,
          "legacy evidence changed numbers");
    LevelingWitnessDialog missing(p, old, "B", "증인 없음");
    check(!missing.findChild<QComboBox *>("witnessTask")->isEnabled() && view(missing)->model()->rowCount() == 0,
          "missing witness invented dates");
    auto stale = p;
    ++stale.revision;
    LevelingWitnessDialog wrong(stale, result, "B", "불일치");
    check(view(wrong)->model()->rowCount() == 0, "mismatched source exposed dates");
}
QJsonObject workflow() {
    QTemporaryDir temp;
    JsonProjectStore store;
    auto p = repartition_fixture::project();
    const auto path = temp.filePath("source.ppm").toStdString();
    store.save(path, p, {});
    Document document(store);
    document.open(path);
    const auto sourceId = document.project()->documentId;
    const auto sourceRevision = document.project()->revision;
    document.saveScenario("저장된 원본", "task-order", 260, "2026-11-04T00:00:00Z", sourceId, sourceRevision);
    document.renameTask(0, "현재 A <원문>");
    ResourceComparisonDialog parent(document);
    parent.show();
    auto *buttonA = parent.findChild<QPushButton *>("showLevelingWitnessA");
    auto *buttonB = parent.findChild<QPushButton *>("showLevelingWitnessB");
    auto *other = parent.findChild<QComboBox *>("levelingOther");
    until([&] { return buttonA->isEnabled(); });
    check(!buttonB->isEnabled(), "B evidence enabled without B");
    other->setCurrentIndex(1);
    until([&] { return buttonA->isEnabled() && buttonB->isEnabled(); });
    parent.findChild<QTableWidget *>("levelingTasks")->setCurrentCell(1, 0);
    const auto before = *document.project();
    const auto dirty = document.dirty();
    buttonA->click();
    buttonB->click();
    QPointer<LevelingWitnessDialog> a = opened(parent, "A ·"), b = opened(parent, "B ·");
    check(a && b, "A/B witness windows");
    check(view(*a)->model()->rowCount() == 2 && cell(*a, 0, 0).contains("현재 A <원문>") &&
              cell(*b, 0, 0).contains("분할 A") && !cell(*b, 0, 0).contains("현재 A"),
          "A/B input identity mixing");
    check(a->findChild<QComboBox *>("witnessTask")->currentData().toString() == "B" &&
              a->findChild<QLabel *>("witnessSummary")->text().contains("총여유 9h / 자유여유 1h"),
          "focus or total/free display");
    check(cell(*a, 0, 3) == "2026-11-04 09:00" && cell(*a, 1, 3) == "2026-11-03 14:00", "co-moving witness dates");
    QDir().mkpath("leveling-witness-qa");
    QTest::qWait(20);
    check(a->grab().save("leveling-witness-qa/A-total.png"), "A screenshot");
    check(b->grab().save("leveling-witness-qa/B-total.png"), "B screenshot");
    a->findChild<QCheckBox *>("witnessChangedOnly")->setChecked(false);
    b->findChild<QCheckBox *>("witnessChangedOnly")->setChecked(false);
    check(view(*a)->model()->rowCount() == 3 && cell(*a, 2, 1) == cell(*a, 2, 3) && cell(*a, 2, 7) == "현재 유지",
          "omitted dates reconstruction");
    auto result = QJsonObject{{"A", snapshot(*a)}, {"B", snapshot(*b)}};
    auto *selector = a->findChild<QComboBox *>("witnessTask");
    selector->setCurrentIndex(selector->findData("A"));
    check(a->findChild<QLabel *>("witnessSummary")->text().contains("총여유 11h") &&
              view(*a)->currentIndex().data(Qt::UserRole).toString() == "A",
          "task choice did not navigate to focal row");
    selector->setCurrentIndex(selector->findData("B"));
    a->resize(900, 540);
    QTest::qWait(20);
    check(view(*a)->horizontalScrollBar()->maximum() > 0 && a->findChild<QPushButton *>("closeWitness")->isVisible(),
          "narrow window navigation");
    check(a->grab().save("leveling-witness-qa/narrow.png"), "narrow screenshot");
    check(*document.project() == before && document.dirty() == dirty, "read-only view mutated document");
    document.save();
    check(a->isVisible() && b->isVisible(), "save invalidated unchanged revision");
    document.renameTask(0, "수정된 A");
    check((!a || !a->isVisible()) && (!b || !b->isVisible()) && !buttonA->isEnabled() && !buttonB->isEnabled(),
          "edited document retained stale views");
    parent.findChild<QPushButton *>("runLeveling")->click();
    until([&] { return buttonA->isEnabled(); });
    buttonA->click();
    a = opened(parent, "A ·");
    check(a, "recalculated view missing");
    parent.findChild<QSpinBox *>("levelingHorizon")->setValue(261);
    check((!a || !a->isVisible()) && !buttonA->isEnabled(), "condition change retained stale view");
    parent.findChild<QPushButton *>("runLeveling")->click();
    until([&] { return buttonA->isEnabled(); });
    buttonA->click();
    a = opened(parent, "A ·");
    check(a, "second view missing");
    parent.reject();
    check(!a || !a->isVisible(), "parent close retained witness view");
    QFile::remove("leveling-witness-qa/selected.ppm");
    document.saveAs("leveling-witness-qa/selected.ppm");
    parent.show();
    parent.findChild<QPushButton *>("runLeveling")->click();
    until([&] { return buttonA->isEnabled(); });
    buttonA->click();
    a = opened(parent, "A ·");
    check(a, "post-save comparison missing");
    parent.findChild<QLineEdit *>("levelingReason")->setText("근거 검토");
    auto *adopt = parent.findChild<QPushButton *>("adoptLeveling");
    adopt->click();
    check(!a || !a->isVisible(), "adoption submission retained stale witness");
    until([&] { return document.project()->planLeveling && adopt->isEnabled(); });
    buttonA->click();
    a = opened(parent, "A ·");
    check(a, "adopted comparison missing");
    document.create("다른 문서", "2026-11-02", {"새 작업"});
    check((!a || !a->isVisible()) && !buttonA->isEnabled() && !buttonB->isEnabled(),
          "document replacement retained stale witness");
    parent.reject();
    return result;
}
QJsonObject forecastAndScale() {
    auto p = repartition_fixture::project();
    p.statusAt = "2026-11-02 10:00";
    p.tasks.push_back({"D", "완료 실적", 60, {}});
    p.progress = {progress_fixture::entry(p, "A", 480, 60, 420, "2026-11-02 09:00"),
                  progress_fixture::entry(p, "D", 60, 60, 0, "2026-11-02 09:00", "2026-11-02 10:00")};
    const auto result = levelResources(p, "task-order", 260).leveled;
    check(result.levelingWitnesses && result.isForecast, "forecast evidence missing");
    LevelingWitnessDialog forecast(p, result, "A", "잔여 예측");
    forecast.show();
    forecast.findChild<QCheckBox *>("witnessChangedOnly")->setChecked(false);
    check(cell(forecast, 0, 5) == "2026-11-02 09:00" && cell(forecast, 3, 6) == "2026-11-02 10:00" &&
              cell(forecast, 3, 3) == "—" && cell(forecast, 3, 7) == "완료 실적",
          "actual dates not fixed separately");
    QTest::qWait(20);
    check(forecast.grab().save("leveling-witness-qa/forecast.png"), "forecast screenshot");
    auto output = QJsonObject{{"forecast", snapshot(forecast)}};
    forecast.close();
    auto zero = repartition_fixture::project();
    zero.tasks[2].durationMinutes = 480;
    auto fixed = levelResources(zero, "task-order", 260).leveled;
    LevelingWitnessDialog critical(zero, fixed, "A", "0여유");
    check(view(critical)->model()->rowCount() == 1 &&
              critical.findChild<QLabel *>("witnessSummary")->text().contains("총여유 0h"),
          "zero float treated as missing");
    auto huge = repartition_fixture::project();
    auto prepared = levelResources(huge, "task-order", 260).leveled;
    for (int i = 3; i < 10000; ++i) {
        const auto id = "I" + std::to_string(i);
        huge.tasks.push_back({id, "독립 작업 " + std::to_string(i), 1, {}});
        TaskSchedule row;
        row.taskId = id;
        row.start = "2026-11-02 09:00";
        row.finish = "2026-11-02 09:01";
        prepared.tasks.push_back(row);
    }
    QElapsedTimer timer;
    timer.start();
    LevelingWitnessDialog large(huge, prepared, "B", "준비된 1만 작업 화면");
    large.findChild<QCheckBox *>("witnessChangedOnly")->setChecked(false);
    check(view(large)->model()->rowCount() == 10000 && cell(large, 9999, 0).contains("9999"),
          "virtual large table identity");
    output["prepared_10000_model_ms"] = double(timer.nsecsElapsed()) / 1000000;
    return output;
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        metadataContracts();
        auto result = workflow();
        const auto extra = forecastAndScale();
        for (auto it = extra.begin(); it != extra.end(); ++it)
            result[it.key()] = it.value();
        QFile file("leveling-witness-qa/view.json");
        check(file.open(QIODevice::WriteOnly), "view artifact write");
        file.write(QJsonDocument(result).toJson());
        std::cout << "Witness metadata, A/B read-only views, actuals, stale invalidation and virtual 10k rows passed\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
