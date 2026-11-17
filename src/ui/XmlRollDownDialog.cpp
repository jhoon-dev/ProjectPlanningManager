#include "calculation/ScheduleFormulaScope.h"
#include "ui/XmlRollDownDialog.h"
#include "ui/EditorUi.h"
#include "domain/Classification.h"
#include "domain/XmlRollDown.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <unordered_map>
namespace jobs {
namespace {
QString q(const std::string &s) {
    return QString::fromUtf8(s);
}
std::string s(const QString &s) {
    return s.toUtf8().toStdString();
}
} // namespace
void editXmlRollDown(Document &doc, FieldScope scope, const std::string &key, QWidget *parent) {
    if (!doc.project() || doc.readOnly() || (scope != FieldScope::Task && scope != FieldScope::Resource))
        return;
    const auto input = doc.snapshot();
    const auto *source = customField(*input, key, scope);
    if (!source)
        return;
    const auto session = doc.sessionGeneration(), revision = input->revision;
    auto guard = [&] {
        if (!doc.project() || doc.sessionGeneration() != session || doc.project()->documentId != input->documentId ||
            doc.project()->revision != revision || doc.readOnly())
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    };
    QDialog dialog(parent);
    dialog.setObjectName("xmlRollDownDialog");
    dialog.setWindowTitle("배정 roll-down · " + q(source->name));
    auto *body = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    body->addLayout(form);
    auto *target = new QComboBox;
    target->setObjectName("rollDownTarget");
    target->addItem("연결 없음", "");
    target->addItem("새 배정 필드 만들기", "@new");
    for (const auto &f : input->assignmentFields)
        if (rollDownCompatible(*source, f))
            target->addItem(q(f.name), q(f.id));
    target->setCurrentIndex(
        source->xmlRollDown ? std::max(0, target->findData(q(source->xmlRollDown->assignmentFieldId))) : 0);
    form->addRow("받을 배정 필드", target);
    auto *name = new QLineEdit(q(source->name) + " · 배정");
    name->setObjectName("rollDownFieldName");
    form->addRow("새 배정 필드 이름", name);
    auto *automatic = new QCheckBox("자동 전달 · 배정에서 직접 입력한 값은 유지");
    automatic->setObjectName("rollDownAutomatic");
    automatic->setChecked(!source->xmlRollDown || source->xmlRollDown->automatic);
    form->addRow(automatic);
    auto *help = new QLabel("작업 또는 자원의 값을 각 배정에 같은 값으로 전달합니다. 금액·기간도 나누거나 일정/원가에 "
                            "합산하지 않습니다.\n배정 값 편집의 ‘원본 값 사용’으로 직접 입력/명시 미설정을 해제할 수 "
                            "있습니다. 자동 전달을 끄거나 연결을 없애면 자동 값은 표시하지 않고 직접 값은 유지합니다.");
    help->setWordWrap(true);
    help->setTextFormat(Qt::PlainText);
    body->addWidget(help);
    auto *preview = new QPushButton("영향 미리보기");
    preview->setObjectName("previewRollDown");
    body->addWidget(preview);
    auto *summary = new QLabel;
    summary->setObjectName("rollDownSummary");
    summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(true);
    body->addWidget(summary);
    auto *table = new QTableWidget(0, 3);
    table->setObjectName("rollDownPreview");
    table->setHorizontalHeaderLabels({"작업 / 자원", "유효 값", "출처"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    body->addWidget(table, 1);
    auto *error = new QLabel;
    error->setObjectName("rollDownError");
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    body->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText("저장");
    buttons->button(QDialogButtonBox::Cancel)->setText("취소");
    body->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto controls = [&] {
        const auto id = target->currentData().toString();
        name->setVisible(id == "@new");
        form->labelForField(name)->setVisible(id == "@new");
        automatic->setEnabled(!id.isEmpty());
        summary->clear();
        table->setRowCount(0);
    };
    QObject::connect(target, &QComboBox::currentIndexChanged, &dialog, controls);
    controls();
    auto probe = [&] {
        guard();
        auto p = *input;
        auto id = s(target->currentData().toString());
        auto &fields = customFields(p, scope);
        auto found = std::find_if(fields.begin(), fields.end(), [&](const auto &f) { return "field:" + f.id == key; });
        if (id == "@new") {
            auto field = *source;
            field.id = "preview-roll-down";
            while (customField(p, "field:" + field.id, FieldScope::Assignment))
                field.id += 'x';
            field.name = s(name->text().trimmed());
            field.xmlIdentity.reset();
            field.xmlRollDown.reset();
            field.xmlFormula.reset();
            field.xmlRollUp.reset();
            if (field.xmlLookup) {
                field.xmlLookup->fieldGuid.clear();
                field.xmlLookup->fieldName.clear();
            }
            id = field.id;
            p.assignmentFields.push_back(field);
        }
        found->xmlRollDown = id.empty() ? std::nullopt : std::optional<XmlRollDown>{{id, automatic->isChecked()}};
        extendRollDownLookups(p);
        validate(p);
        return std::pair{std::move(p), id};
    };
    auto render = [&] {
        auto [p, id] = probe();
        ScheduleFormulaScope formulaSchedule(p, doc.calculation() ? &*doc.calculation() : nullptr,
                                             ScheduleFormulaMode::Active, false);
        RollDownValues values(p);
        std::unordered_map<std::string, std::string> taskNames, resourceNames;
        for (const auto &t : p.tasks)
            taskNames.emplace(t.id, t.name);
        for (const auto &r : p.resources)
            resourceNames.emplace(r.id, r.name);
        table->setRowCount(0);
        std::size_t manual = 0, missing = 0, inherited = 0;
        for (const auto &a : p.assignments) {
            const auto targetKey = "field:" + id;
            if (attributeValue(a.attributes, targetKey))
                ++manual;
            else if (std::find(a.rollDownMissing.begin(), a.rollDownMissing.end(), targetKey) !=
                     a.rollDownMissing.end())
                ++missing;
            else if (values.value(a, targetKey))
                ++inherited;
            if (!id.empty() && table->rowCount() < 20) {
                const auto row = table->rowCount();
                table->insertRow(row);
                auto *owner = new QTableWidgetItem(q(taskNames.at(a.taskId) + " / " + resourceNames.at(a.resourceId)));
                owner->setToolTip(q(a.taskId + " / " + a.resourceId));
                table->setItem(row, 0, owner);
                const auto value = values.value(a, targetKey);
                table->setItem(row, 1,
                               new QTableWidgetItem(
                                   value ? q(attributeText(p, targetKey, *value, FieldScope::Assignment)) : "미설정"));
                table->setItem(row, 2, new QTableWidgetItem(q(values.origin(a, targetKey))));
            }
        }
        summary->setText(id.empty() ? QString("연결 없음 · 배정의 직접 값은 유지합니다.")
                                    : QString("배정 %1개 · 자동 값 %2 · 직접 값 %3 · 명시 미설정 %4 · 첫20개 표시")
                                          .arg(p.assignments.size())
                                          .arg(inherited)
                                          .arg(manual)
                                          .arg(missing));
        error->clear();
    };
    QObject::connect(preview, &QPushButton::clicked, &dialog, [&] {
        try {
            render();
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            (void)probe();
            const auto id = s(target->currentData().toString());
            if (id == "@new")
                doc.createRollDownField(scope, key, s(name->text().trimmed()), automatic->isChecked());
            else
                doc.setXmlRollDown(
                    scope, key, id.empty() ? std::nullopt : std::optional<XmlRollDown>{{id, automatic->isChecked()}});
            dialog.accept();
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    dialog.resize(800, 650);
    prepareEditorDialog(dialog);
    try {
        render();
    } catch (const std::exception &e) {
        showEditorError(*error, e);
    }
    dialog.exec();
}
} // namespace jobs
