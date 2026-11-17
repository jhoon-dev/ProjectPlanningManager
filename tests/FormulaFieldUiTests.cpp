#include "ui/ClassificationDialogs.h"
#include "ui/TaskTableModel.h"
#include "ui/TaskViewProjection.h"
#include "ui/ResourceViewDialog.h"
#include "domain/FormulaFields.h"
#include "persistence/JsonProjectStore.h"
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTimer>
#include <QTreeWidget>
#include <iostream>
namespace {
using namespace jobs;
void check(bool v, const char *s) {
    if (!v)
        throw std::runtime_error(s);
}
template <class F> void soon(F f) {
    QTimer::singleShot(0, [f] {
        try {
            f();
        } catch (const std::exception &e) {
            std::cerr << "callback: " << e.what();
            std::exit(2);
        }
    });
}
template <class F> void until(F f, const char *s) {
    QElapsedTimer t;
    t.start();
    while (!f() && t.elapsed() < 30000)
        QTest::qWait(10);
    check(f(), s);
}
QDialog &modal() {
    auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    check(d, "modal");
    return *d;
}
QPushButton *button(QDialog &d, const char *s) {
    auto *b = d.findChild<QPushButton *>(s);
    check(b, "button");
    return b;
}
void choose(QComboBox *box, const QString &data) {
    check(box, "combo");
    const int n = box->findData(data);
    check(n >= 0, "choice");
    box->setCurrentIndex(n);
}
void run() {
    QTemporaryDir temp;
    JsonProjectStore store;
    Document doc(store);
    doc.create("계산식 필드 화면", "2026-11-02", {"설계", "검토"});
    doc.setDuration(0, 480);
    doc.setDuration(1, 120);
    auto factor = doc.putCustomField({{}, "배수", CustomFieldType::Number, "", {}}, FieldScope::Project);
    doc.setProjectAttribute("field:" + factor, "2");
    Resource r;
    r.name = "설계 인력";
    const auto rid = doc.putResource(r);
    const auto output = QDir::currentPath() + "/professional-fields-qa";
    QDir().mkpath(output);
    const auto old = doc.project()->revision;
    soon([&] {
        auto &manager = modal();
        soon([&] {
            auto &editor = modal();
            check(editor.objectName() == "customFieldEditor", "field form");
            editor.findChild<QLineEdit *>("fieldName")->setText("환산 기간");
            editor.findChild<QComboBox *>("fieldType")->setCurrentIndex(1);
            editor.findChild<QLineEdit *>("fieldUnit")->setText("h");
            auto *references = editor.findChild<QComboBox *>("formulaReferences");
            choose(references, "[durationHours]");
            button(editor, "insertFormulaReference")->click();
            auto *expr = editor.findChild<QPlainTextEdit *>("fieldExpression");
            expr->insertPlainText(" * ");
            choose(references, QString::fromStdString("[project:" + factor + "]"));
            button(editor, "insertFormulaReference")->click();
            button(editor, "checkFormula")->click();
            check(editor.findChild<QLabel *>("classificationError")->text().contains("16"), "static preview result");
            check(doc.project()->revision == old, "static check committed");
            editor.resize(800, 600);
            QTest::qWait(20);
            check(editor.grab().save(output + "/formula-editor.png"), "formula screenshot");
            editor.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        });
        button(manager, "addCustomField")->click();
        manager.reject();
    });
    manageCustomFields(doc, nullptr, FieldScope::Task);
    check(doc.project()->customFields.size() == 1, "formula create");
    const auto id = doc.project()->customFields[0].id;
    const auto tid = doc.project()->tasks[0].id;
    TaskTableModel model(doc);
    model.synchronize({0, 1});
    const auto column = model.columnForKey("field:" + id);
    check(column >= 14 && model.data(model.index(0, column)).toString() == "16 h" &&
              !(model.flags(model.index(0, column)) & Qt::ItemIsEditable),
          "computed task cell");
    QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
    doc.setProjectAttribute("field:" + factor, "3");
    model.synchronize({0, 1});
    check(model.data(model.index(0, column)).toString() == "24 h" && !changes.isEmpty(),
          "project dependency render refresh");
    doc.undo();
    model.synchronize({0, 1});
    TaskViewSpec spec;
    spec.classification.sortKey = "field:" + id;
    spec.classification.groupBy = {"field:" + id};
    spec.classification.attributeFilters = {{"field:" + id, "equals", "4"}};
    auto projected = projectTaskView(*doc.project(), nullptr, spec);
    check(projected.rows == std::vector<std::size_t>{1}, "computed task group/filter");
    soon([&] {
        auto &d = modal();
        choose(d.findChild<QComboBox *>("taskAttributeKey"), QString::fromStdString("field:" + id));
        check(!button(d, "applyTaskAttribute")->isEnabled() && !button(d, "clearTaskAttribute")->isEnabled(),
              "task formula manual edit");
        d.reject();
    });
    editTaskAttributes(doc, {tid}, nullptr);
    auto failureId = doc.putCustomField({{}, "오류 확인", CustomFieldType::Number, "", {}, "1/0"});
    model.synchronize({0, 1});
    check(model.data(model.index(0, model.columnForKey("field:" + failureId))).toString().contains("계산 오류"),
          "formula failure hidden");
    const auto stable = *doc.project();
    soon([&] {
        auto &manager = modal();
        auto *list = manager.findChild<QListWidget *>("customFields");
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->data(Qt::UserRole).toString() == QString::fromStdString(id))
                list->setCurrentRow(i);
        soon([&] {
            auto &editor = modal();
            editor.findChild<QPlainTextEdit *>("fieldExpression")
                ->setPlainText(QString::fromStdString("[field:" + id + "]"));
            button(editor, "checkFormula")->click();
            check(editor.findChild<QLabel *>("classificationError")->text().contains("순환"), "cycle preview");
            editor.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
            check(*doc.project() == stable && editor.isVisible(), "cycle saved");
            editor.reject();
        });
        button(manager, "editCustomField")->click();
        manager.reject();
    });
    manageCustomFields(doc, nullptr, FieldScope::Task);
    const auto resourceFormula = doc.putCustomField(
        {{}, "가용 단위", CustomFieldType::Number, "", {}, "[capacity]/100{%}"}, FieldScope::Resource);
    doc.putCustomField({{}, "자원 오류", CustomFieldType::Number, "", {}, "1/0"}, FieldScope::Resource);
    soon([&] {
        auto &d = modal();
        choose(d.findChild<QComboBox *>("objectFieldKey"), QString::fromStdString("field:" + resourceFormula));
        auto *table = d.findChild<QTableView *>("objectFieldValues");
        table->selectRow(0);
        check(table->model()->data(table->model()->index(0, 2)).toString() == "1" &&
                  !button(d, "applyObjectField")->isEnabled() && !button(d, "clearObjectField")->isEnabled(),
              "resource formula editable");
        d.reject();
    });
    editObjectAttributes(doc, FieldScope::Resource, nullptr);
    {
        ResourceViewDialog d(doc);
        d.show();
        until([&] { return button(d, "exportResourceViewXlsx")->isEnabled(); }, "formula resource view");
        auto *tree = d.findChild<QTreeWidget *>("resourceViewTree");
        check(tree->topLevelItem(0)->text(6) == "1" && tree->topLevelItem(0)->text(7).contains("계산 오류"),
              "resource computed/error rendering");
        check(d.grab().save(output + "/formula-resources.png"), "resource formula screen");
        d.reject();
    }
    const auto count =
        doc.putCustomField({{}, "가중 작업 수", CustomFieldType::Number, "", {}, "[taskCount]*[field:" + factor + "]"},
                           FieldScope::Project);
    soon([&] {
        auto &d = modal();
        choose(d.findChild<QComboBox *>("objectFieldKey"), QString::fromStdString("field:" + count));
        auto *table = d.findChild<QTableView *>("objectFieldValues");
        check(table->model()->data(table->model()->index(0, 2)).toString() == "4" &&
                  !button(d, "applyObjectField")->isEnabled(),
              "project formula result");
        d.reject();
    });
    editObjectAttributes(doc, FieldScope::Project, nullptr);
    soon([&] {
        auto &manager = modal();
        soon([&] {
            auto &editor = modal();
            editor.findChild<QLineEdit *>("fieldName")->setText("권한 변경 검사");
            editor.findChild<QComboBox *>("fieldType")->setCurrentIndex(1);
            editor.findChild<QPlainTextEdit *>("fieldExpression")->setPlainText("1");
            const auto before = doc.project()->customFields;
            doc.setWriteAuthorization([] { return false; });
            editor.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
            check(doc.project()->customFields == before && editor.isVisible(), "revoked editor committed");
            editor.reject();
        });
        button(manager, "addCustomField")->click();
        manager.reject();
    });
    manageCustomFields(doc, nullptr, FieldScope::Task);
    doc.setWriteAuthorization({});
    auto large = *doc.project();
    std::vector<std::size_t> indexes;
    for (int i = 0; i < 1200; ++i) {
        large.tasks.push_back({"L" + std::to_string(i), "작업 " + std::to_string(i), 60 * (i + 1), {}});
        indexes.push_back(std::size_t(i + 2));
    }
    const auto path = temp.filePath("cache.ppm").toStdString();
    store.save(path, large, {});
    doc.open(path);
    model.synchronize(indexes);
    const int h = model.columnForKey("field:" + id);
    for (int i = 0; i < 1200; ++i)
        check(model.data(model.index(i, h)).toString() == QString::number(2 * (i + 1)) + " h",
              "bounded render cache mismatch");
    check(model.data(model.index(0, h)).toString() == "2 h", "evicted render result");
    doc.open(path, true);
    soon([&] {
        auto &d = modal();
        check(!button(d, "addCustomField")->isEnabled() && !button(d, "editCustomField")->isEnabled(),
              "readonly definition editor");
        d.reject();
    });
    manageCustomFields(doc, nullptr, FieldScope::Task);
    QThreadPool::globalInstance()->waitForDone();
    std::cout << "PASS formula UI: named reference insertion, unit/check preview, cycle rejection, computed readonly "
                 "cells, project dependencies, task/resource views, role/readonly, bounded rendering\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    auto font = QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        run();
    } catch (const jobs::AppError &e) {
        std::cerr << e.code();
        for (const auto &a : e.diagnostic().arguments)
            std::cerr << " " << a;
        return 1;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
