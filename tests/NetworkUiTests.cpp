#include "TaskTableTest.h"
#include "ui/MainWindow.h"
#include "ui/NetworkDialog.h"
#include "ui/GanttView.h"
#include "persistence/JsonProjectStore.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <iostream>
namespace {
using namespace jobs;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void until(F f, const char *message) {
    QElapsedTimer t;
    t.start();
    while (!f() && t.elapsed() < 20000)
        QTest::qWait(10);
    check(f(), message);
}
QPushButton *button(NetworkDialog &d, const char *name) {
    auto *b = d.findChild<QPushButton *>(name);
    check(b, "network button missing");
    return b;
}
void ready(NetworkDialog &d) {
    until([&] { return !button(d, "cancelNetwork")->isEnabled() && d.property("networkNodeCount").toInt() > 0; },
          "network worker did not finish");
}
QGraphicsView *canvas(NetworkDialog &d) {
    return d.findChild<QGraphicsView *>("networkView");
}
std::set<std::string> shown(NetworkDialog &d) {
    std::set<std::string> result;
    for (auto *item : canvas(d)->scene()->items())
        if (item->data(1) == "networkNode")
            result.insert(item->data(0).toString().toStdString());
    return result;
}
std::set<std::string> taskIds(const Project &p) {
    std::set<std::string> result;
    for (const auto &t : p.tasks)
        result.insert(t.id);
    return result;
}
void workflow() {
    QTemporaryDir temp;
    JsonProjectStore store;
    Document doc(store);
    doc.create("네트워크 선택 연동", "2026-11-02", {"기초 A <b>원문</b>", "후속 B", "후속 C", "독립 D"});
    for (int i = 0; i < 4; ++i)
        doc.setDuration(i, 480);
    doc.setPredecessors(1, {doc.project()->tasks[0].id});
    doc.setPredecessors(2, {doc.project()->tasks[1].id});
    const auto a = doc.project()->tasks[0].id, b = doc.project()->tasks[1].id, c = doc.project()->tasks[2].id;
    const auto path = (temp.path() + "/network.ppm").toStdString();
    doc.saveAs(path);
    const auto before = *doc.project();
    MainWindow main(doc);
    main.resize(1450, 850);
    main.show();
    check(main.selectTaskById(c), "initial table selection failed");
    auto *action = main.findChild<QAction *>("networkDiagram");
    check(action, "network menu missing");
    action->trigger();
    auto *network = dynamic_cast<NetworkDialog *>(main.findChild<QDialog *>("networkDialog"));
    check(network && !network->isModal(), "network dialog missing or modal");
    ready(*network);
    check(shown(*network) == std::set<std::string>{b, c}, "initial selected neighbor window wrong");
    main.selectTaskById(a);
    ready(*network);
    until([&] { return shown(*network) == std::set<std::string>{a, b}; }, "table selection did not move graph focus");
    network->findChild<QComboBox *>("networkMode")->setCurrentIndex(int(NetworkMode::Ancestors));
    ready(*network);
    network->findChild<QLineEdit *>("networkFind")->setText("3");
    button(*network, "findNetworkTask")->click();
    check(network->findChild<QComboBox *>("networkFound")->count() == 1, "stable row search missing");
    button(*network, "focusNetworkFound")->click();
    ready(*network);
    check(shown(*network) == std::set<std::string>{a, b, c}, "ancestor traversal lost chain");
    bool escaped = false;
    for (auto *item : canvas(*network)->scene()->items())
        if (item->data(1) == "networkNode" && item->data(0).toString() == QString::fromStdString(a))
            escaped = item->toolTip().contains("&lt;b&gt;");
    check(escaped, "task tooltip interpreted user markup");
    auto *relations = network->findChild<QTableView *>("networkRelations");
    check(relations->model()->rowCount() == 1 && relations->model()->index(0, 3).data().toString() == "FS",
          "full adjacency/relationship model wrong");
    QTest::qWait(20);
    const auto index = relations->model()->index(0, 2);
    QTest::mouseClick(relations->viewport(), Qt::LeftButton, {}, relations->visualRect(index).center());
    QTest::mouseDClick(relations->viewport(), Qt::LeftButton, {}, relations->visualRect(index).center());
    until(
        [&] {
            return !button(*network, "cancelNetwork")->isEnabled() && shown(*network) == std::set<std::string>{a, b};
        },
        "relation list did not refocus graph");
    button(*network, "jumpNetworkTask")->click();
    until(
        [&] {
            auto *table = taskTable(main);
            return table->currentIndex().isValid() &&
                   table->currentIndex().data(Qt::UserRole).toString() == QString::fromStdString(b);
        },
        "graph jump did not select table");
    auto *gantt = dynamic_cast<GanttView *>(main.findChild<QGraphicsView *>("ganttView"));
    check(gantt && gantt->hasSchedule() && doc.calculation() && doc.calculation()->status == CalculationStatus::Valid,
          "graph calculation/selection not linked to Gantt");
    check(*doc.project() == before && !doc.dirty(), "network/jump changed project/dirty/history");
    QDir().mkpath("network-qa");
    network->resize(1320, 900);
    QTest::qWait(30);
    check(network->grab().save("network-qa/diagram.png"), "network screenshot failed");
    doc.renameTask(0, "기초 A 변경");
    check(network->property("networkNodeCount").toInt() == 0 && !button(*network, "jumpNetworkTask")->isEnabled(),
          "edited document left old diagram active");
    button(*network, "refreshNetwork")->click();
    ready(*network);
    check(network->findChild<QLabel *>("networkSummary")->text().contains(QString::number(doc.project()->revision)),
          "new revision not shown");
    button(*network, "refreshNetwork")->click();
    button(*network, "cancelNetwork")->click();
    until([&] { return !button(*network, "cancelNetwork")->isEnabled(); }, "canceled graph build stalled");
    check(network->property("networkNodeCount").toInt() == 0, "canceled graph result applied");
    doc.save();
    button(*network, "refreshNetwork")->click();
    doc.open(path);
    QTest::qWait(80);
    check(network->property("networkNodeCount").toInt() == 0 && !button(*network, "jumpNetworkTask")->isEnabled(),
          "same-id session applied stale graph");
    button(*network, "refreshNetwork")->click();
    ready(*network);
    auto *scopeOnly = network->findChild<QCheckBox *>("networkVisibleOnly");
    scopeOnly->setChecked(true);
    ready(*network);
    scopeOnly->setChecked(false);
    ready(*network);
    network->reject();
    QTest::qWait(20);
    main.hide();
    Document ro(store);
    ro.open(path, true);
    NetworkDialog readOnly(ro, b, [&] { return taskIds(*ro.project()); });
    readOnly.show();
    ready(readOnly);
    check(!ro.dirty() && button(readOnly, "jumpNetworkTask")->isEnabled(),
          "read-only inspection unavailable or mutated source");
    readOnly.reject();
    Document cycle(store);
    cycle.create("구조 순환", "2026-11-02", {"A", "B"});
    const auto ca = cycle.project()->tasks[0].id, cb = cycle.project()->tasks[1].id;
    cycle.setDuration(0, 60);
    cycle.setDuration(1, 60);
    cycle.setPredecessors(0, {cb});
    cycle.setPredecessors(1, {ca});
    NetworkDialog cyclic(cycle, ca, [&] { return taskIds(*cycle.project()); });
    cyclic.show();
    ready(cyclic);
    check(cyclic.findChild<QLabel *>("networkSummary")->text().contains("구조 순환 1묶음") &&
              cyclic.findChild<QLabel *>("networkSummary")->text().contains("충돌"),
          "structural cycle/invalid schedule not shown");
    QTest::qWait(20);
    check(cyclic.grab().save("network-qa/cycle.png"), "cycle screenshot failed");
    cyclic.reject();
}
void benchmark() {
    QTemporaryDir temp;
    JsonProjectStore store;
    QJsonArray metrics;
    for (int count : {1000, 10000}) {
        Project p;
        p.documentId = "network-ui-" + std::to_string(count);
        p.name = "대용량 네트워크 " + std::to_string(count);
        p.startDate = "2026-11-02";
        for (int i = 0; i < count; ++i)
            p.tasks.push_back({"T" + std::to_string(i), "작업 " + std::to_string(i), 1,
                               i ? std::vector<std::string>{"T" + std::to_string(i - 1)} : std::vector<std::string>{}});
        const auto path = (temp.path() + "/large" + QString::number(count) + ".ppm").toStdString();
        store.save(path, p, {});
        Document doc(store);
        doc.open(path);
        auto all = taskIds(p);
        QElapsedTimer timer;
        timer.start();
        NetworkDialog dialog(doc, "T" + std::to_string(count / 2), [all] { return all; });
        dialog.show();
        ready(dialog);
        const auto readyMs = timer.elapsed();
        check(dialog.property("networkNodeCount").toInt() == 3, "large neighbor graph expanded all nodes");
        dialog.findChild<QComboBox *>("networkMode")->setCurrentIndex(int(NetworkMode::All));
        ready(dialog);
        check(dialog.property("networkNodeCount").toInt() == 80 && canvas(dialog)->scene()->items().size() < 2100,
              "large scene was not bounded");
        auto *relations = dialog.findChild<QTableView *>("networkRelations");
        check(relations->model()->rowCount() == 2, "hidden neighbors missing from lazy relation list");
        button(dialog, "nextNetwork")->click();
        ready(dialog);
        check(button(dialog, "previousNetwork")->isEnabled(), "large graph paging unavailable");
        QTest::qWait(20);
        bool focusVisible = false;
        for (auto *item : canvas(dialog)->scene()->items())
            if (item->data(1) == "networkNode" && item->data(0).toString() == QString("T%1").arg(count / 2))
                focusVisible = canvas(dialog)->viewport()->rect().contains(
                    canvas(dialog)->mapFromScene(item->sceneBoundingRect().center()));
        check(focusVisible, "paged graph focus is outside viewport");
        if (count == 10000) {
            QTest::qWait(20);
            check(dialog.grab().save("network-qa/large-10000.png"), "large screenshot failed");
        }
        metrics.append(QJsonObject{{"tasks", count},
                                   {"ready_ms", qint64(readyMs)},
                                   {"build_ms", dialog.property("networkBuildMs").toDouble()},
                                   {"query_ms", dialog.property("networkQueryMs").toDouble()},
                                   {"render_ms", dialog.property("networkRenderMs").toDouble()},
                                   {"shown_nodes", dialog.property("networkNodeCount").toInt()},
                                   {"shown_edges", dialog.property("networkEdgeCount").toInt()},
                                   {"scene_items", int(canvas(dialog)->scene()->items().size())},
                                   {"relation_rows", relations->model()->rowCount()},
                                   {"source_unchanged", *doc.project() == p}});
        check(!doc.dirty() && *doc.project() == p, "large network modified source");
        dialog.reject();
    }
    QFile file("network-qa/ui-metrics.json");
    check(file.open(QIODevice::WriteOnly), "UI metrics write failed");
    file.write(QJsonDocument(metrics).toJson());
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    const auto id =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (id >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(id).first(), 9));
#endif
    try {
        workflow();
        benchmark();
        std::cout << "network UI: source selection, graph/relations, table/Gantt jump, escaped names, "
                     "revision/cancel/session, readonly/cycle, 1k/10k bounded scene passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
