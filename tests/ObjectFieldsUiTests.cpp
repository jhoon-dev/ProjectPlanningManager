#include "ui/MainWindow.h"
#include "ui/ClassificationDialogs.h"
#include "ui/TaskTableModel.h"
#include "domain/Classification.h"
#include "persistence/JsonProjectStore.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <QTest>
#include <iostream>
namespace {
using namespace ppm;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void soon(F f) {
    QTimer::singleShot(0, [f] {
        try {
            f();
        } catch (const std::exception &e) {
            std::cerr << "FAIL callback: " << e.what() << '\n';
            std::exit(2);
        }
    });
}
template <class F> void until(F f, const char *message) {
    QElapsedTimer timer;
    timer.start();
    while (!f() && timer.elapsed() < 20000)
        QTest::qWait(10);
    check(f(), message);
}
QAction *action(QWidget &window, const char *name) {
    auto *a = window.findChild<QAction *>(name);
    check(a && a->isEnabled(), "missing action");
    return a;
}
template <class F> void modal(QAction *a, F f) {
    soon([=] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        check(dialog, "missing modal");
        f(*dialog);
    });
    a->trigger();
}
QPushButton *button(QDialog &dialog, const char *name) {
    auto *b = dialog.findChild<QPushButton *>(name);
    check(b, "missing button");
    return b;
}
void choose(QDialog &dialog, const std::string &key) {
    auto *combo = dialog.findChild<QComboBox *>("objectFieldKey");
    const auto index = combo->findData(QString::fromStdString(key));
    check(index >= 0, "scope field missing");
    combo->setCurrentIndex(index);
}
void workflow() {
    QTemporaryDir temp;
    JsonProjectStore store;
    Document d(store);
    d.create("프로젝트·자원 필드 화면", "2026-11-02", {"검토"});
    d.setDuration(0, 480);
    Resource r;
    r.name = "설계 인력";
    const auto r1 = d.putResource(r);
    r.id.clear();
    r.name = "측량 장비";
    r.type = "equipment";
    const auto r2 = d.putResource(r);
    const auto output = QDir::currentPath() + "/object-fields-qa";
    check(QDir().mkpath(output), "UI output directory");
    MainWindow window(d);
    window.resize(1100, 760);
    window.show();
    QTest::qWait(20);
    const auto create = [&](const char *menu, const QString &title, const QString &name, int type,
                            const QString &unit) {
        modal(action(window, menu), [&](QDialog &manager) {
            check(manager.windowTitle() == title, "wrong dictionary scope");
            soon([=] {
                auto *editor = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                check(editor && editor->objectName() == "customFieldEditor", "field editor missing");
                editor->findChild<QLineEdit *>("fieldName")->setText(name);
                editor->findChild<QComboBox *>("fieldType")->setCurrentIndex(type);
                editor->findChild<QLineEdit *>("fieldUnit")->setText(unit);
                editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
            });
            button(manager, "addCustomField")->click();
            manager.reject();
        });
    };
    create("manageProjectFields", "프로젝트 사용자 정의 필드", "발주 담당", 0, "");
    create("manageResourceFields", "자원 사용자 정의 필드", "정격 수량", 1, "m²");
    check(d.project()->projectFields.size() == 1 && d.project()->resourceFields.size() == 1 &&
              d.project()->customFields.empty(),
          "UI put into wrong dictionary");
    auto *task = window.findChild<QTableView *>("taskTable");
    check(task && task->model()->columnCount() == 14, "object fields leaked into task columns");
    const auto projectKey = "field:" + d.project()->projectFields[0].id;
    const auto resourceKey = "field:" + d.project()->resourceFields[0].id;
    d.putCustomField({"due", "승인일", CustomFieldType::Date, {}, {}}, FieldScope::Project);
    d.putCustomField({"phase", "검토 상태", CustomFieldType::Choice, {}, {{"review", "검토"}, {"done", "완료"}}},
                     FieldScope::Resource);
    modal(action(window, "editProjectAttributes"), [&](QDialog &dialog) {
        check(dialog.objectName() == "projectAttributesDialog", "project editor absent");
        choose(dialog, projectKey);
        auto *text = dialog.findChild<QPlainTextEdit *>("objectFieldText");
        text->setPlainText("서부 발주처\n검토 담당");
        button(dialog, "applyObjectField")->click();
        until([&] { return button(dialog, "applyObjectField")->isEnabled(); }, "project submission stalled");
        check(attributeValue(d.project()->projectAttributes, projectKey) == "서부 발주처\n검토 담당",
              "multiline text lost");
        check(dialog.grab().save(output + "/project-fields.png"), "project capture");
        text->clear();
        button(dialog, "applyObjectField")->click();
        until([&] { return button(dialog, "applyObjectField")->isEnabled(); }, "empty text stalled");
        check(attributeValue(d.project()->projectAttributes, projectKey) == "", "empty text became missing");
        button(dialog, "clearObjectField")->click();
        until([&] { return button(dialog, "applyObjectField")->isEnabled(); }, "clear stalled");
        check(!attributeValue(d.project()->projectAttributes, projectKey), "clear did not unset");
        choose(dialog, "field:due");
        const auto before = *d.project();
        dialog.findChild<QLineEdit *>("objectFieldInput")->setText("2026-02-30");
        button(dialog, "applyObjectField")->click();
        until([&] { return button(dialog, "applyObjectField")->isEnabled(); }, "bad date stalled");
        check(*d.project() == before && !dialog.findChild<QLabel *>("objectFieldMessage")->text().isEmpty(),
              "invalid input partially applied");
        dialog.findChild<QLineEdit *>("objectFieldInput")->setText("2026-11-05");
        button(dialog, "applyObjectField")->click();
        until([&] { return button(dialog, "applyObjectField")->isEnabled(); }, "date stalled");
        check(attributeValue(d.project()->projectAttributes, "field:due") == "2026-11-05", "date not applied");
        dialog.reject();
    });
    modal(action(window, "editResourceAttributes"), [&](QDialog &dialog) {
        choose(dialog, resourceKey);
        auto *values = dialog.findChild<QTableView *>("objectFieldValues");
        check(values->model()->rowCount() == 2 && values->editTriggers() == QAbstractItemView::NoEditTriggers,
              "resource viewer grain/editability");
        values->selectAll();
        auto *line = dialog.findChild<QLineEdit *>("objectFieldInput");
        line->setText("7.250001");
        button(dialog, "applyObjectField")->click();
        until([&] { return button(dialog, "applyObjectField")->isEnabled(); }, "resource batch stalled");
        for (const auto &resource : d.project()->resources)
            check(attributeValue(resource.attributes, resourceKey) == "7.250001", "batch missed resource");
        check(values->model()->index(1, 2).data().toString() == "7.250001 m²", "typed current value missing");
        const auto before = *d.project();
        line->setText("99");
        button(dialog, "applyObjectField")->click();
        button(dialog, "cancelObjectField")->click();
        until([&] { return button(dialog, "applyObjectField")->isEnabled(); }, "cancellation stalled");
        check(*d.project() == before, "cancelled edit committed");
        choose(dialog, "field:phase");
        auto *choice = dialog.findChild<QComboBox *>("objectFieldChoice");
        choice->setCurrentIndex(choice->findData("review"));
        button(dialog, "applyObjectField")->click();
        until([&] { return button(dialog, "applyObjectField")->isEnabled(); }, "choice stalled");
        for (const auto &resource : d.project()->resources)
            check(attributeValue(resource.attributes, "field:phase") == "review", "choice lost stable ID");
        choose(dialog, resourceKey);
        check(dialog.grab().save(output + "/resource-fields.png"), "resource capture");
        dialog.resize(800, 600);
        QTest::qWait(10);
        check(dialog.grab().save(output + "/resource-fields-small.png"), "small resource capture");
        dialog.reject();
    });
    // Referenced dictionary deletion is blocked through the real scope manager.
    modal(action(window, "manageResourceFields"), [&](QDialog &manager) {
        auto *list = manager.findChild<QListWidget *>("customFields");
        list->setCurrentRow(0);
        const auto before = *d.project();
        button(manager, "removeCustomField")->click();
        check(*d.project() == before && !manager.findChild<QLabel *>("classificationError")->text().isEmpty(),
              "UI deletion dropped referenced values");
        manager.reject();
    });
    d.saveAs(temp.filePath("ui.ppm").toStdString());
    modal(action(window, "manageProjectFields"), [&](QDialog &manager) {
        d.open(d.path());
        const auto reopened = *d.project();
        manager.findChild<QListWidget *>("customFields")->setCurrentRow(0);
        button(manager, "removeCustomField")->click();
        check(*d.project() == reopened && !manager.findChild<QLabel *>("classificationError")->text().isEmpty(),
              "stale manager deleted definition");
        manager.reject();
    });
    modal(action(window, "editProjectAttributes"), [&](QDialog &dialog) {
        d.open(d.path());
        const auto reopened = *d.project();
        check(!button(dialog, "applyObjectField")->isEnabled(), "same-ID reopen did not invalidate input");
        button(dialog, "applyObjectField")->click();
        check(*d.project() == reopened, "stale form committed");
        dialog.reject();
    });
    modal(action(window, "editResourceAttributes"), [&](QDialog &dialog) {
        choose(dialog, resourceKey);
        dialog.findChild<QTableView *>("objectFieldValues")->selectAll();
        dialog.findChild<QLineEdit *>("objectFieldInput")->setText("123");
        button(dialog, "applyObjectField")->click();
        dialog.reject();
    });
    check(QThreadPool::globalInstance()->waitForDone(20000), "worker did not finish after close");
    check(attributeValue(d.project()->resources[0].attributes, resourceKey) == "7.250001", "closed dialog committed");
    Document ro(store);
    ro.open(d.path(), true);
    MainWindow reader(ro);
    modal(action(reader, "editResourceAttributes"), [&](QDialog &dialog) {
        check(!button(dialog, "applyObjectField")->isEnabled() && !button(dialog, "clearObjectField")->isEnabled() &&
                  dialog.findChild<QTableView *>("objectFieldValues")->model()->rowCount() == 2,
              "readonly viewer/edit guards");
        dialog.reject();
    });
    modal(action(reader, "manageProjectFields"), [&](QDialog &manager) {
        check(!button(manager, "addCustomField")->isEnabled() && !button(manager, "removeCustomField")->isEnabled(),
              "readonly definition editing enabled");
        manager.reject();
    });
    check(ro.project()->resources == d.project()->resources &&
              ro.project()->projectAttributes == d.project()->projectAttributes,
          "readonly source changed");
    check(r1 != r2, "resource identity fixture");
}
void scale() {
    QTemporaryDir temp;
    JsonProjectStore store;
    Project p;
    p.documentId = "object-field-scale";
    p.name = "1만 자원 필드";
    p.startDate = "2026-11-02";
    p.resourceFields = {{"score", "분류 수치", CustomFieldType::Number, "점", {}}};
    for (int i = 0; i < 10000; ++i) {
        Resource resource;
        resource.id = "R" + std::to_string(i);
        resource.name = "자원 " + std::to_string(i);
        resource.attributes = {{"field:score", std::to_string(i)}};
        p.resources.push_back(std::move(resource));
    }
    const auto path = temp.filePath("scale.ppm").toStdString();
    store.save(path, p, {});
    Document d(store);
    d.open(path);
    QElapsedTimer timer;
    timer.start();
    qint64 openMs = 0, batchMs = 0;
    soon([&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        check(dialog, "scale dialog");
        auto *view = dialog->findChild<QTableView *>("objectFieldValues");
        check(view && view->model()->rowCount() == 10000 &&
                  view->model()->index(9999, 2).data().toString() == "9999 점",
              "large resource model identity/value");
        openMs = timer.elapsed();
        view->selectAll();
        dialog->findChild<QLineEdit *>("objectFieldInput")->setText("1");
        timer.restart();
        button(*dialog, "applyObjectField")->click();
        until([&] { return button(*dialog, "applyObjectField")->isEnabled(); }, "10k batch stalled");
        batchMs = timer.elapsed();
        for (const auto &resource : d.project()->resources)
            check(attributeValue(resource.attributes, "field:score") == "1", "10k batch value omitted");
        dialog->reject();
    });
    editObjectAttributes(d, FieldScope::Resource, nullptr);
    d.undo();
    check(d.project()->resources == p.resources && !d.dirty(), "10k batch undo/dirty");
    QFile file(QDir::currentPath() + "/object-fields-qa/scale.json");
    check(file.open(QIODevice::WriteOnly), "scale metrics");
    file.write(QJsonDocument(QJsonObject{{"resource_rows", 10000},
                                         {"open_from_loaded_document_ms", openMs},
                                         {"selected_all_apply_ms", batchMs},
                                         {"all_values_verified", true},
                                         {"undo_restored_saved_state", true}})
                   .toJson());
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
        workflow();
        scale();
        std::cout << "Object field UI: scope dictionaries, typed values/batch/cancel/stale/close/readonly passed\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
