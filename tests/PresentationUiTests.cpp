#include "ui/NetworkDialog.h"
#include "ui/SchedulePrintDialog.h"
#include "persistence/JsonProjectStore.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTimer>
#include <iostream>
namespace {
using namespace jobs;
void check(bool ok, const char *s) {
    if (!ok)
        throw std::runtime_error(s);
}
template <class F> void until(F f, const char *s) {
    QElapsedTimer t;
    t.start();
    while (!f() && t.elapsed() < 20000)
        QTest::qWait(10);
    check(f(), s);
}
QPushButton *button(QWidget &w, const char *s) {
    auto *b = w.findChild<QPushButton *>(s);
    check(b, "missing button");
    return b;
}
template <class F> void modal(F f) {
    QTimer::singleShot(0, [f] {
        try {
            auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            check(d, "missing modal");
            f(*d);
        } catch (const std::exception &e) {
            std::cerr << "modal FAIL: " << e.what() << '\n';
            std::exit(2);
        }
    });
}
SchedulePrintSource source(Document &d) {
    SchedulePrintSource s;
    s.project = d.snapshot();
    s.session = d.sessionGeneration();
    s.unsaved = !d.revisionSaved();
    s.scope = "UI 표시";
    s.generatedAt = "2026-11-05T03:00:00Z";
    s.printId = "presentation-ui";
    s.columns = {{"number", "번호", 36}, {"name", "작업", 140}};
    for (std::size_t i = 0; i < d.project()->tasks.size(); ++i) {
        const auto &t = d.project()->tasks[i];
        s.rows.push_back({t.id, int(i + 1), {QString::number(i + 1), QString::fromStdString(t.name)}});
    }
    return s;
}
void run() {
    QTemporaryDir temp;
    JsonProjectStore store;
    Document d(store);
    d.create("인쇄·네트워크 화면", "2026-11-02", {"기초", "상세"});
    d.setDuration(0, 480);
    d.setDuration(1, 480);
    const auto a = d.project()->tasks[0].id, b = d.project()->tasks[1].id;
    d.setPredecessors(1, {a});
    const auto out = QDir::currentPath() + "/print-network-qa";
    QDir().mkpath(out);
    {
        SchedulePrintDialog dialog(d, source(d));
        dialog.show();
        until([&] { return dialog.findChild<QWidget *>("schedulePrintCanvas") != nullptr; }, "schedule preview");
        dialog.findChild<QComboBox *>("printPaper")->setCurrentIndex(1);
        dialog.findChild<QSpinBox *>("printMargin")->setValue(13);
        dialog.findChild<QCheckBox *>("printRelations")->setChecked(true);
        dialog.findChild<QCheckBox *>("printWbsSummary")->setChecked(true);
        button(dialog, "savePrintPreferences")->click();
        until([&] { return d.project()->printPreferences && dialog.findChild<QWidget *>("schedulePrintCanvas"); },
              "persist print preferences");
        check(d.project()->printPreferences->margin == 13 && d.project()->printPreferences->relations,
              "UI saved fields");
        dialog.reject();
    }
    {
        SchedulePrintDialog dialog(d, source(d));
        dialog.show();
        check(dialog.findChild<QComboBox *>("printPaper")->currentIndex() == 1 &&
                  dialog.findChild<QCheckBox *>("printWbsSummary")->isChecked(),
              "reopen prefs");
        dialog.reject();
    }
    {
        NetworkDialog dialog(d, b, [=] { return std::set<std::string>{a, b}; });
        dialog.show();
        until([&] { return dialog.property("networkNodeCount").toInt() == 2; }, "network prepare");
        auto *scene = dialog.findChild<QGraphicsScene *>();
        check(scene, "scene");
        for (auto *item : scene->items())
            if (item->data(1) == "networkNode" && item->data(0).toString().toStdString() == a)
                item->setPos(90, 110);
        button(dialog, "saveNetworkLayout")->click();
        until(
            [&] {
                return d.project()->networkPositions.size() == 2 && dialog.property("networkNodeCount").toInt() == 2;
            },
            "save layout");
        bool found = false;
        for (const auto &v : d.project()->networkPositions)
            if (v.taskId == a)
                found = v.x == 90 && v.y == 110;
        check(found, "manual coordinates");
        modal([&](QDialog &editor) {
            check(editor.objectName() == "networkRelationEditor", "relation editor");
            auto *table = editor.findChild<QTableWidget *>("networkDependencyTable");
            check(table && table->rowCount() == 1, "incoming logical rows");
            qobject_cast<QComboBox *>(table->cellWidget(0, 1))->setCurrentIndex(1);
            qobject_cast<QSpinBox *>(table->cellWidget(0, 2))->setValue(60);
            editor.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        button(dialog, "editNetworkRelations")->click();
        until([&] { return dialog.property("networkNodeCount").toInt() == 2; }, "edited network refresh");
        check(dependencies(d.project()->tasks[1])[0].type == DependencyType::SS &&
                  dependencies(d.project()->tasks[1])[0].lagMinutes == 60,
              "relation applied");
        const auto before = *d.project();
        modal([&](QDialog &editor) {
            editor.findChild<QTableWidget *>("networkDependencyTable")->item(0, 0)->setText(QString::fromStdString(b));
            editor.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            until([&] { return editor.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled(); },
                  "invalid relation worker");
            check(*d.project() == before, "self relation applied");
            editor.reject();
        });
        button(dialog, "editNetworkRelations")->click();
        modal([&](QDialog &printer) {
            check(printer.objectName() == "networkPrintDialog" && printer.findChild<QWidget *>("networkPrintCanvas"),
                  "network print preview");
            printer.findChild<QCheckBox *>("networkPrintFit")->setChecked(false);
            printer.findChild<QSpinBox *>("networkPrintScale")->setValue(125);
            button(printer, "saveNetworkPrintPreferences")->click();
            check(d.project()->printPreferences->networkScale == 125, "network prefs");
            check(printer.grab().save(out + "/network-print.png"), "network print screenshot");
            printer.reject();
        });
        button(dialog, "printNetwork")->click();
        button(dialog, "refreshNetwork")->click();
        until([&] { return dialog.property("networkNodeCount").toInt() == 2; }, "post prefs refresh");
        check(dialog.grab().save(out + "/network-edit.png"), "network screenshot");
        const auto revision = d.project()->revision;
        button(dialog, "autoNetworkLayout")->click();
        check(d.project()->revision == revision, "draft auto layout edited document");
        button(dialog, "resetNetworkLayout")->click();
        until([&] { return d.project()->networkPositions.empty() && dialog.property("networkNodeCount").toInt() == 2; },
              "reset saved layout");
        dialog.reject();
    }
    const auto path = temp.filePath("ui.ppm").toStdString();
    d.saveAs(path);
    d.open(path, true);
    {
        NetworkDialog dialog(d, b, [=] { return std::set<std::string>{a, b}; });
        dialog.show();
        until([&] { return dialog.property("networkNodeCount").toInt() == 2; }, "readonly network");
        check(!button(dialog, "saveNetworkLayout")->isEnabled() && !button(dialog, "editNetworkRelations")->isEnabled(),
              "readonly layout edit");
        dialog.reject();
    }
    {
        SchedulePrintDialog dialog(d, source(d));
        check(!button(dialog, "savePrintPreferences")->isEnabled(), "readonly preferences");
        dialog.reject();
    }
    QThreadPool::globalInstance()->waitForDone();
    std::cout << "Presentation UI: reopen prefs/layout/relations atomic failure/print preview/readonly passed\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    const auto f = QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (f >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(f).front(), 9));
#endif
    try {
        run();
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
