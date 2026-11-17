#include "ui/XmlFormulaDialog.h"
#include "ui/XmlRollUpDialog.h"
#include "ui/ClassificationDialogs.h"
#include "ui/XmlCodeDialogs.h"
#include "ui/XmlRollDownDialog.h"
#include "ui/EditorUi.h"
#include "formatting/QtDiagnosticText.h"
#include "domain/Classification.h"
#include "domain/FormulaFields.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QUuid>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QSpinBox>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace jobs {
namespace {
QString qs(const std::string &s) {
    return QString::fromUtf8(s);
}
std::string str(const QString &s) {
    return s.toUtf8().toStdString();
}
QPushButton *button(QBoxLayout *layout, const QString &label, const char *id) {
    auto *b = new QPushButton(label);
    b->setObjectName(id);
    layout->addWidget(b);
    return b;
}
QLabel *errorLabel(QVBoxLayout *layout) {
    auto *label = new QLabel;
    label->setObjectName("classificationError");
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    layout->addWidget(label);
    return label;
}
void guardRevision(const Document &doc, const std::string &id, std::uint64_t revision) {
    if (!doc.project() || doc.project()->documentId != id || doc.project()->revision != revision)
        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"편집 중 문서가 변경되었습니다. 창을 다시 열어 주세요."});
}
void fillKeys(QComboBox &combo, const PlanInput &p, bool empty) {
    if (empty)
        combo.addItem("없음", "");
    combo.addItem("WBS", "wbs");
    for (const auto &key : attributeKeys(p))
        combo.addItem((key.starts_with("code:") ? "코드 · " : "필드 · ") + qs(attributeName(p, key)), qs(key));
}
void fillValue(QComboBox &combo, const PlanInput &p, const std::string &key, const std::string &operation,
               const std::optional<std::string> &value) {
    const QSignalBlocker blocker(&combo);
    combo.clear();
    const auto *field = customField(p, key);
    const bool choice = key == "wbs" || codeType(p, key) ||
                        (field && (field->type == CustomFieldType::Choice || field->type == CustomFieldType::Flag));
    combo.setEditable(!choice || operation == "contains");
    if (choice && operation == "equals") {
        combo.addItem("선택하세요", QVariant{});
        const auto labels = attributeLabels(p);
        std::vector<std::pair<std::string, std::string>> values;
        for (const auto &[id, name] : labels.at(key))
            values.push_back({id, name});
        std::sort(values.begin(), values.end(), [](const auto &a, const auto &b) { return a.second < b.second; });
        for (const auto &[id, name] : values) {
            combo.addItem(qs(name), qs(id));
            if (field && field->xmlLookup && field->xmlLookup->leafOnly &&
                std::any_of(field->xmlLookup->values.begin(), field->xmlLookup->values.end(),
                            [&](const auto &v) { return v.parentId == id; }))
                qobject_cast<QStandardItemModel *>(combo.model())->item(combo.count() - 1)->setEnabled(false);
        }
        if (value) {
            if (field && multipleChoice(*field) && combo.findData(qs(*value)) < 0)
                combo.addItem(qs(attributeText(p, key, *value)), qs(*value));
            combo.setCurrentIndex(combo.findData(qs(*value)));
        }
        if (combo.currentIndex() < 0)
            combo.setCurrentIndex(0);
    } else
        combo.setEditText(value ? qs(*value) : QString{});
    combo.setToolTip(field && field->type == CustomFieldType::Duration ? "분 단위: 90 m, 경과 90 em, 추정 90 m?"
                     : field && field->type == CustomFieldType::Cost ? "프로젝트 통화의 금액 · 음수/0/미설정 구분"
                                                                     : "");
    if (field && dateTimeCustomField(field->type))
        combo.setToolTip(
            "일시: YYYY-MM-DDTHH:MM:SS[.소수6자리] · 시간대 없는 일정 시각 · 실제 일정 제약으로 자동 채택하지 않음");
    combo.setEnabled(operation != "missing" && operation != "present");
}
std::string readValue(const QComboBox &combo, const PlanInput &p, const std::string &key,
                      const std::string &operation) {
    if (operation == "missing" || operation == "present")
        return {};
    if (operation == "contains")
        return str(combo.currentText());
    if (!combo.isEditable()) {
        if (!combo.currentData().isValid())
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"목록 또는 코드 값을 선택하세요."});
        return normalizeAttributeValue(p, key, str(combo.currentData().toString()), FieldScope::Task, true);
    }
    return normalizeAttributeValue(p, key, str(combo.currentText()), FieldScope::Task, true);
}
} // namespace
void manageCustomFields(Document &doc, QWidget *parent, FieldScope scope) {
    if (!doc.project())
        return;
    const auto managerSession = doc.sessionGeneration();
    auto managerRevision = doc.project()->revision;
    QDialog dialog(parent);
    dialog.setObjectName("customFieldsDialog");
    dialog.setWindowTitle(scope == FieldScope::Task         ? "작업 사용자 정의 필드"
                          : scope == FieldScope::Project    ? "프로젝트 사용자 정의 필드"
                          : scope == FieldScope::Assignment ? "배정 사용자 정의 필드"
                                                            : "자원 사용자 정의 필드");
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(
        new QLabel("텍스트·숫자·날짜·목록 필드. 값이나 저장된 보기에서 사용 중인 정의는 먼저 참조를 해제하세요."));
    auto *list = new QListWidget;
    list->setObjectName("customFields");
    layout->addWidget(list);
    auto *actions = new QHBoxLayout;
    layout->addLayout(actions);
    auto *add = button(actions, "추가", "addCustomField"), *edit = button(actions, "편집", "editCustomField"),
         *remove = button(actions, "삭제", "removeCustomField");
    auto *maskActions = new QHBoxLayout;
    layout->addLayout(maskActions);
    auto *mask = button(maskActions, "XML 코드 마스크…", "editXmlLookupMask");
    auto *generateCode = button(maskActions, "XML 목록 코드 생성…", "createXmlLookupCode");
    auto *rollActions = new QHBoxLayout;
    layout->addLayout(rollActions);
    auto *rollDown = button(rollActions, "배정 roll-down…", "editXmlRollDown");
    auto *rollUp = button(rollActions, "요약 roll-up…", "editXmlRollUp");
    auto *xmlFormula = button(rollActions, "XML 계산식…", "editXmlFormula");
    rollUp->setVisible(scope == FieldScope::Task || scope == FieldScope::Resource);
    rollDown->setVisible(scope == FieldScope::Task || scope == FieldScope::Resource);
    auto *error = errorLabel(layout);
    auto reload = [&](const std::string &id = std::string{}) {
        managerRevision = doc.project()->revision;
        list->clear();
        for (const auto &f : customFields(*doc.project(), scope)) {
            auto *item = new QListWidgetItem(qs(f.name) + " · " + qs(fieldTypeToken(f.type)), list);
            item->setData(Qt::UserRole, qs(f.id));
            item->setToolTip("<qt>" + qs(f.id).toHtmlEscaped() +
                             (f.expression.empty() ? QString{} : "<br>계산식: " + qs(f.expression).toHtmlEscaped()) +
                             (f.xmlFormula ? "<br>XML 식 (" + QString(f.xmlFormula->enabled ? "활성" : "비활성") +
                                                 "): " + qs(f.xmlFormula->source).toHtmlEscaped()
                                           : QString{}) +
                             (f.xmlIdentity ? "<br>XML FieldID: " + qs(f.xmlIdentity->fieldId).toHtmlEscaped() +
                                                  "<br>GUID: " + qs(f.xmlIdentity->guid).toHtmlEscaped()
                                            : QString{}) +
                             "</qt>");
            if (f.id == id)
                list->setCurrentItem(item);
        }
    };
    auto editField = [&](bool creating) {
        if (doc.sessionGeneration() != managerSession || doc.readOnly() || (!creating && !list->currentItem()))
            return;
        if (!doc.project() || doc.project()->revision != managerRevision) {
            error->setText("문서가 변경되었습니다. 창을 다시 열어 주세요.");
            return;
        }
        CustomField field;
        const auto session = doc.sessionGeneration();
        if (!creating)
            field =
                *customField(*doc.project(), "field:" + str(list->currentItem()->data(Qt::UserRole).toString()), scope);
        const auto source = doc.project()->documentId;
        const auto revision = doc.project()->revision;
        QDialog form(&dialog);
        form.setObjectName("customFieldEditor");
        form.setWindowTitle(creating ? "사용자 필드 추가" : "사용자 필드 편집");
        auto *body = new QVBoxLayout(&form);
        auto *fields = new QFormLayout;
        body->addLayout(fields);
        auto *name = new QLineEdit(qs(field.name));
        name->setObjectName("fieldName");
        fields->addRow("이름", name);
        auto *type = new QComboBox;
        type->setObjectName("fieldType");
        type->addItems({"텍스트", "숫자", "날짜/일시", "목록", "참/거짓 (Flag)", "금액 (Cost)", "기간 (Duration)",
                        "시작 일시 (Start)", "종료 일시 (Finish)"});
        type->setCurrentIndex(int(field.type));
        fields->addRow("형식", type);
        auto *unit = new QLineEdit(qs(field.unit));
        unit->setObjectName("fieldUnit");
        fields->addRow("단위(선택)", unit);
        auto *expression = new QPlainTextEdit(qs(field.expression));
        expression->setObjectName("fieldExpression");
        expression->setMaximumHeight(85);
        expression->setPlaceholderText("선택 · 숫자 계산식. 참조 삽입으로 필드를 선택하세요. 예: [durationHours] * 2");
        fields->addRow("계산식 (숫자 전용)", expression);
        auto *references = new QComboBox(&form);
        references->setObjectName("formulaReferences");
        for (const auto &[label, token] : formulaReferenceChoices(*doc.project(), scope))
            references->addItem(qs(label), qs(token));
        auto *referenceTools = new QHBoxLayout;
        referenceTools->addWidget(references);
        auto *insertReference = button(referenceTools, "참조 삽입", "insertFormulaReference");
        auto *checkFormula = button(referenceTools, "수식 검사", "checkFormula");
        body->addLayout(referenceTools);
        auto *formulaNote = new QLabel(
            "사칙 연산·min/max/coalesce/abs · 숫자 상수는 소수 6자리 · 단위 상수 예: 8{h}, 100{%}\n"
            "덧셈 등은 같은 단위, 곱셈은 한쪽 무단위입니다. 계산 결과는 읽기 전용이며 일정/원가에 합산하지 않습니다.\n"
            "미설정은 전파하고 coalesce로 대체합니다. 0 나눗셈/범위 초과는 오류이며 0으로 대체하지 않습니다.",
            &form);
        formulaNote->setWordWrap(true);
        body->addWidget(formulaNote);
        QObject::connect(insertReference, &QPushButton::clicked, &form, [=] {
            auto cursor = expression->textCursor();
            cursor.insertText(references->currentData().toString());
            expression->setTextCursor(cursor);
            expression->setFocus();
        });
        auto *options = new QTableWidget(int(field.options.size()), 1);
        options->setObjectName("fieldOptions");
        options->setHorizontalHeaderLabels({"목록 옵션 이름"});
        options->horizontalHeader()->setStretchLastSection(true);
        for (int i = 0; i < options->rowCount(); ++i) {
            auto *item = new QTableWidgetItem(qs(field.options[std::size_t(i)].name));
            item->setData(Qt::UserRole, qs(field.options[std::size_t(i)].id));
            options->setItem(i, 0, item);
        }
        body->addWidget(options);
        if (field.xmlLookup) {
            auto *note =
                new QLabel("외부 목록의 ID·GUID·계층 사전은 읽기 전용입니다. 값 편집 창에서 항목을 선택하세요.");
            note->setWordWrap(true);
            body->addWidget(note);
        }
        auto *optionActions = new QHBoxLayout;
        body->addLayout(optionActions);
        auto *addOption = button(optionActions, "옵션 추가", "addFieldOption"),
             *removeOption = button(optionActions, "선택 옵션 삭제", "removeFieldOption");
        QObject::connect(addOption, &QPushButton::clicked, &form, [=] {
            const int row = options->rowCount();
            options->insertRow(row);
            options->setItem(row, 0, new QTableWidgetItem("새 옵션"));
            options->setCurrentCell(row, 0);
        });
        QObject::connect(removeOption, &QPushButton::clicked, &form, [=] {
            if (options->currentRow() >= 0)
                options->removeRow(options->currentRow());
        });
        auto enable = [=] {
            const bool choice = type->currentIndex() == 3;
            type->setEnabled(!field.xmlLookup);
            unit->setEnabled(type->currentIndex() != int(CustomFieldType::Cost));
            const bool number = type->currentIndex() == 1 && scope != FieldScope::Assignment;
            expression->setEnabled(number);
            references->setEnabled(number);
            insertReference->setEnabled(number && references->count());
            checkFormula->setEnabled(number);
            expression->setVisible(number);
            fields->labelForField(expression)->setVisible(number);
            for (auto *widget : std::vector<QWidget *>{references, insertReference, checkFormula, formulaNote})
                widget->setVisible(number);
            options->setEnabled(choice);
            options->setEditTriggers(field.xmlLookup
                                         ? QAbstractItemView::NoEditTriggers
                                         : QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
            options->setVisible(choice);
            addOption->setVisible(choice);
            removeOption->setVisible(choice);
            addOption->setEnabled(choice && !field.xmlLookup);
            removeOption->setEnabled(choice && !field.xmlLookup);
        };
        QObject::connect(type, &QComboBox::currentIndexChanged, &form, enable);
        enable();
        auto *failure = errorLabel(body);
        const auto previewId =
            field.id.empty() ? QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString() : field.id;
        QObject::connect(checkFormula, &QPushButton::clicked, &form, [&] {
            try {
                guardRevision(doc, source, revision);
                if (doc.sessionGeneration() != session)
                    throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
                auto probe = *doc.project();
                auto candidate = field;
                candidate.id = previewId;
                candidate.type = CustomFieldType::Number;
                candidate.unit = str(unit->text().trimmed());
                candidate.expression = str(expression->toPlainText().trimmed());
                candidate.options.clear();
                if (candidate.expression.empty())
                    throw AppError(ErrorCode::FORMULA_INVALID, {"계산식을 입력하세요."});
                auto &dictionary = customFields(probe, scope);
                auto found = std::find_if(dictionary.begin(), dictionary.end(),
                                          [&](const auto &f) { return f.id == candidate.id; });
                if (found == dictionary.end())
                    dictionary.push_back(candidate);
                else
                    *found = candidate;
                validateFormulaFields(probe);
                if ((scope == FieldScope::Task && probe.tasks.empty()) ||
                    (scope == FieldScope::Resource && probe.resources.empty()))
                    failure->setText("문법·참조·단위 검사 통과 · 계산할 객체가 없습니다.");
                else {
                    const auto values =
                        formulaValues(probe, scope, scope == FieldScope::Task ? &probe.tasks.front() : nullptr,
                                      scope == FieldScope::Resource ? &probe.resources.front() : nullptr);
                    failure->setText("문법·참조·단위 검사 통과 · 첫 객체 예시: " +
                                     qs(formulaValueText(values->at("field:" + candidate.id))));
                }
            } catch (const std::exception &e) {
                showEditorError(*failure, e);
            }
        });
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        body->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &form, &QDialog::reject);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &form, [&] {
            try {
                guardRevision(doc, source, revision);
                if (doc.sessionGeneration() != session)
                    throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
                field.name = str(name->text().trimmed());
                field.type = CustomFieldType(type->currentIndex());
                field.unit = field.type == CustomFieldType::Cost ? "" : str(unit->text().trimmed());
                field.expression = field.type == CustomFieldType::Number && scope != FieldScope::Assignment
                                       ? str(expression->toPlainText().trimmed())
                                       : std::string{};
                field.options.clear();
                if (field.type == CustomFieldType::Choice)
                    for (int row = 0; row < options->rowCount(); ++row) {
                        const auto *item = options->item(row, 0);
                        if (!item)
                            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"빈 옵션 행을 확인하세요."});
                        field.options.push_back(
                            {str(item->data(Qt::UserRole).toString()), str(item->text().trimmed())});
                    }
                field.id = doc.putCustomField(field, scope);
                form.accept();
            } catch (const std::exception &e) {
                showEditorError(*failure, e);
            }
        });
        prepareEditorDialog(form);
        if (form.exec() == QDialog::Accepted)
            reload(field.id);
    };
    QObject::connect(add, &QPushButton::clicked, &dialog, [&] { editField(true); });
    QObject::connect(edit, &QPushButton::clicked, &dialog, [&] { editField(false); });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
        if (!list->currentItem())
            return;
        try {
            if (!doc.project() || doc.sessionGeneration() != managerSession ||
                doc.project()->revision != managerRevision)
                throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
            doc.removeCustomField(str(list->currentItem()->data(Qt::UserRole).toString()), scope);
            error->clear();
            reload();
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    auto codeAction = [&](bool generate) {
        try {
            if (!list->currentItem() || !doc.project() || doc.sessionGeneration() != managerSession ||
                doc.project()->revision != managerRevision)
                throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
            const auto id = str(list->currentItem()->data(Qt::UserRole).toString());
            if (generate)
                createXmlLookupCode(doc, scope, "field:" + id, &dialog);
            else
                editXmlLookupMask(doc, scope, "field:" + id, &dialog);
            reload(id);
            error->clear();
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    };
    QObject::connect(mask, &QPushButton::clicked, &dialog, [&] { codeAction(false); });
    QObject::connect(generateCode, &QPushButton::clicked, &dialog, [&] { codeAction(true); });
    QObject::connect(rollDown, &QPushButton::clicked, &dialog, [&] {
        try {
            if (!list->currentItem() || !doc.project() || doc.sessionGeneration() != managerSession ||
                doc.project()->revision != managerRevision)
                throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
            const auto id = str(list->currentItem()->data(Qt::UserRole).toString());
            editXmlRollDown(doc, scope, "field:" + id, &dialog);
            reload(id);
            error->clear();
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    QObject::connect(rollUp, &QPushButton::clicked, &dialog, [&] {
        try {
            if (!list->currentItem() || !doc.project() || doc.sessionGeneration() != managerSession ||
                doc.project()->revision != managerRevision)
                throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
            const auto id = str(list->currentItem()->data(Qt::UserRole).toString());
            editXmlRollUp(doc, scope, "field:" + id, &dialog);
            reload(id);
            error->clear();
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    QObject::connect(xmlFormula, &QPushButton::clicked, &dialog, [&] {
        try {
            if (!list->currentItem() || !doc.project() || doc.sessionGeneration() != managerSession ||
                doc.project()->revision != managerRevision)
                throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
            auto id = str(list->currentItem()->data(Qt::UserRole).toString());
            editXmlFormula(doc, scope, "field:" + id, &dialog);
            reload(id);
            error->clear();
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    auto controls = [&] {
        const auto *f =
            list->currentItem() && doc.project()
                ? customField(*doc.project(), "field:" + str(list->currentItem()->data(Qt::UserRole).toString()), scope)
                : nullptr;
        const bool enabled = !doc.readOnly() && f && f->xmlLookup && !f->xmlLookup->tableGuid.empty() &&
                             f->xmlLookup->valueType == CustomFieldType::Text;
        rollDown->setEnabled(!doc.readOnly() && f && f->expression.empty() &&
                             (scope == FieldScope::Task || scope == FieldScope::Resource));
        xmlFormula->setEnabled(!doc.readOnly() && f && f->expression.empty());
        rollUp->setEnabled(!doc.readOnly() && f && (scope == FieldScope::Task || scope == FieldScope::Resource));
        mask->setEnabled(enabled);
        generateCode->setEnabled(enabled && !f->xmlLookup->masks.empty());
    };
    QObject::connect(list, &QListWidget::currentRowChanged, &dialog, controls);
    for (auto *b : {add, edit, remove})
        b->setEnabled(!doc.readOnly());
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(close);
    QObject::connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    reload();
    controls();
    dialog.resize(640, 500);
    prepareEditorDialog(dialog);
    dialog.exec();
}
void manageActivityCodes(Document &doc, QWidget *parent) {
    if (!doc.project())
        return;
    QDialog dialog(parent);
    dialog.setObjectName("activityCodesDialog");
    dialog.setWindowTitle("작업 코드 사전");
    auto *layout = new QVBoxLayout(&dialog);
    auto *types = new QComboBox;
    types->setObjectName("activityCodeTypes");
    layout->addWidget(types);
    auto *typeActions = new QHBoxLayout;
    layout->addLayout(typeActions);
    auto *addType = button(typeActions, "종류 추가", "addCodeType"),
         *renameType = button(typeActions, "종류 이름 변경", "renameCodeType"),
         *removeType = button(typeActions, "종류 삭제", "removeCodeType");
    auto *tree = new QTreeWidget;
    tree->setObjectName("activityCodeTree");
    tree->setHeaderLabels({"코드 계층", "안정 ID"});
    layout->addWidget(tree);
    auto *nodeActions = new QHBoxLayout;
    layout->addLayout(nodeActions);
    auto *addNode = button(nodeActions, "코드 추가", "addActivityCode"),
         *editNode = button(nodeActions, "이름·부모 편집", "editActivityCode"),
         *removeNode = button(nodeActions, "코드 삭제", "removeActivityCode");
    auto *error = errorLabel(layout);
    auto currentType = [&]() -> ActivityCodeType {
        const auto *type = codeType(*doc.project(), "code:" + str(types->currentData().toString()));
        if (!type)
            throw AppError(ErrorCode::CLASSIFICATION_NOT_FOUND, {"작업 코드 종류"});
        return *type;
    };
    auto reloadNodes = [&] {
        tree->clear();
        if (!types->currentData().isValid())
            return;
        const auto type = currentType();
        std::unordered_map<std::string, QTreeWidgetItem *> items;
        for (const auto &v : type.values) {
            auto *item = new QTreeWidgetItem(QStringList{qs(v.name), qs(v.id)});
            item->setData(0, Qt::UserRole, qs(v.id));
            items.emplace(v.id, item);
        }
        for (const auto &v : type.values) {
            if (v.parentId.empty())
                tree->addTopLevelItem(items.at(v.id));
            else
                items.at(v.parentId)->addChild(items.at(v.id));
        }
        tree->expandToDepth(1);
    };
    auto reload = [&](const std::string &id = std::string{}) {
        const QSignalBlocker blocker(types);
        types->clear();
        for (const auto &type : doc.project()->codeTypes)
            types->addItem(qs(type.name), qs(type.id));
        if (!id.empty())
            types->setCurrentIndex(types->findData(qs(id)));
        reloadNodes();
    };
    QObject::connect(types, &QComboBox::currentIndexChanged, &dialog, reloadNodes);
    auto nameType = [&](bool creating) {
        if (doc.readOnly())
            return;
        try {
            auto type = creating ? ActivityCodeType{} : currentType();
            const auto source = doc.project()->documentId;
            const auto revision = doc.project()->revision;
            bool okay = false;
            const auto name =
                QInputDialog::getText(&dialog, "작업 코드 종류", "이름", QLineEdit::Normal, qs(type.name), &okay);
            if (!okay)
                return;
            guardRevision(doc, source, revision);
            type.name = str(name.trimmed());
            reload(doc.putActivityCodeType(type));
            error->clear();
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    };
    QObject::connect(addType, &QPushButton::clicked, &dialog, [&] { nameType(true); });
    QObject::connect(renameType, &QPushButton::clicked, &dialog, [&] { nameType(false); });
    QObject::connect(removeType, &QPushButton::clicked, &dialog, [&] {
        try {
            doc.removeActivityCodeType(currentType().id);
            reload();
            error->clear();
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    auto editCode = [&](bool creating) {
        if (doc.readOnly())
            return;
        try {
            auto type = currentType();
            ActivityCode node;
            if (!creating) {
                if (!tree->currentItem())
                    return;
                const auto id = str(tree->currentItem()->data(0, Qt::UserRole).toString());
                node = *std::find_if(type.values.begin(), type.values.end(), [&](const auto &v) { return v.id == id; });
            } else if (tree->currentItem())
                node.parentId = str(tree->currentItem()->data(0, Qt::UserRole).toString());
            const auto source = doc.project()->documentId;
            const auto revision = doc.project()->revision;
            QDialog form(&dialog);
            form.setObjectName("activityCodeEditor");
            form.setWindowTitle("작업 코드 편집");
            auto *body = new QVBoxLayout(&form);
            auto *fields = new QFormLayout;
            body->addLayout(fields);
            auto *name = new QLineEdit(qs(node.name));
            name->setObjectName("activityCodeName");
            fields->addRow("이름", name);
            auto *parentCode = new QComboBox;
            parentCode->setObjectName("activityCodeParent");
            parentCode->addItem("최상위", "");
            const auto labels = attributeLabels(*doc.project());
            for (const auto &v : type.values)
                if (v.id != node.id)
                    parentCode->addItem(qs(labels.at("code:" + type.id).at(v.id)), qs(v.id));
            parentCode->setCurrentIndex(parentCode->findData(qs(node.parentId)));
            fields->addRow("부모 코드", parentCode);
            auto *failure = errorLabel(body);
            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
            body->addWidget(buttons);
            QObject::connect(buttons, &QDialogButtonBox::rejected, &form, &QDialog::reject);
            QObject::connect(buttons, &QDialogButtonBox::accepted, &form, [&] {
                try {
                    guardRevision(doc, source, revision);
                    auto proposal = type;
                    auto edited = node;
                    edited.name = str(name->text().trimmed());
                    edited.parentId = str(parentCode->currentData().toString());
                    if (creating)
                        proposal.values.push_back(edited);
                    else
                        *std::find_if(proposal.values.begin(), proposal.values.end(),
                                      [&](const auto &v) { return v.id == node.id; }) = edited;
                    doc.putActivityCodeType(proposal);
                    form.accept();
                } catch (const std::exception &e) {
                    showEditorError(*failure, e);
                }
            });
            prepareEditorDialog(form);
            if (form.exec() == QDialog::Accepted) {
                reload(type.id);
                error->clear();
            }
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    };
    QObject::connect(addNode, &QPushButton::clicked, &dialog, [&] { editCode(true); });
    QObject::connect(editNode, &QPushButton::clicked, &dialog, [&] { editCode(false); });
    QObject::connect(removeNode, &QPushButton::clicked, &dialog, [&] {
        if (!tree->currentItem())
            return;
        try {
            auto type = currentType();
            const auto id = str(tree->currentItem()->data(0, Qt::UserRole).toString());
            if (std::any_of(type.values.begin(), type.values.end(), [&](const auto &v) { return v.parentId == id; }))
                throw AppError(ErrorCode::CLASSIFICATION_IN_USE, {"하위 코드가 있습니다."});
            std::erase_if(type.values, [&](const auto &v) { return v.id == id; });
            doc.putActivityCodeType(type);
            reload(type.id);
            error->clear();
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    for (auto *b : {addType, renameType, removeType, addNode, editNode, removeNode})
        b->setEnabled(!doc.readOnly());
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(close);
    QObject::connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    reload();
    dialog.resize(640, 560);
    prepareEditorDialog(dialog);
    dialog.exec();
}
void editTaskAttributes(Document &doc, const std::vector<std::string> &ids, QWidget *parent) {
    if (!doc.project() || ids.empty())
        return;
    QDialog dialog(parent);
    dialog.setObjectName("taskAttributesDialog");
    dialog.setWindowTitle("선택 작업 분류 값");
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QString("선택 작업 %1개 · 적용한 필드/코드만 변경합니다.").arg(ids.size())));
    auto *key = new QComboBox;
    key->setObjectName("taskAttributeKey");
    for (const auto &k : attributeKeys(*doc.project()))
        key->addItem(qs(attributeName(*doc.project(), k)), qs(k));
    auto *value = new QComboBox;
    value->setObjectName("taskAttributeValue");
    layout->addWidget(key);
    layout->addWidget(value);
    auto *choices = new QListWidget;
    choices->setObjectName("taskAttributeChoices");
    choices->setMaximumHeight(160);
    choices->setToolTip("Ctrl/Shift로 여러 목록 항목을 선택할 수 있습니다.");
    choices->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(choices);
    auto *error = errorLabel(layout);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close);
    layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Apply)->setObjectName("applyTaskAttribute");
    buttons->button(QDialogButtonBox::Apply)->setText("적용");
    buttons->button(QDialogButtonBox::Close)->setText("닫기");
    auto *clear = buttons->addButton("값 제거", QDialogButtonBox::ActionRole);
    clear->setObjectName("clearTaskAttribute");
    auto source = doc.project()->documentId;
    auto revision = doc.project()->revision;
    auto reload = [&] {
        const auto k = str(key->currentData().toString());
        std::optional<std::string> common;
        bool first = true, mixed = false;
        for (const auto &id : ids) {
            const auto found = std::find_if(doc.project()->tasks.begin(), doc.project()->tasks.end(),
                                            [&](const auto &t) { return t.id == id; });
            if (found == doc.project()->tasks.end())
                continue;
            const auto existing = resolvedAttributeValue(*doc.project(), *found, k);
            if (first) {
                common = existing;
                first = false;
            } else if (common != existing)
                mixed = true;
        }
        fillValue(*value, *doc.project(), k, "equals", mixed ? std::nullopt : common);
        if (mixed && value->isEditable())
            value->lineEdit()->setPlaceholderText("여러 값 · 새 값을 입력하세요");
        const auto *f = customField(*doc.project(), k);
        const bool multi = f && multipleChoice(*f);
        choices->setVisible(multi);
        value->setVisible(!multi);
        choices->clear();
        if (multi) {
            const auto selected = common && !mixed ? choiceSelection(*f, *common) : std::vector<std::string>{};
            for (const auto &o : f->options) {
                auto *item = new QListWidgetItem(qs(o.name), choices);
                item->setData(Qt::UserRole, qs(o.id));
                if (f->xmlLookup->leafOnly && std::any_of(f->xmlLookup->values.begin(), f->xmlLookup->values.end(),
                                                          [&](const auto &v) { return v.parentId == o.id; }))
                    item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
                item->setSelected(std::find(selected.begin(), selected.end(), o.id) != selected.end());
            }
        }
        const bool calculated = f && calculatedField(*f);
        const bool editable = !doc.readOnly() && key->count() && !calculated;
        value->setEnabled(editable);
        choices->setEnabled(editable);
        buttons->button(QDialogButtonBox::Apply)->setEnabled(editable);
        clear->setEnabled(editable);
        if (calculated)
            error->setText("계산식 · " + qs(f->expression) +
                           " · 값은 읽기 전용입니다. 미설정/오류는 작업 표에서 확인하세요.");
        else if (error->text().startsWith("계산식 ·"))
            error->clear();
    };
    QObject::connect(key, &QComboBox::currentIndexChanged, &dialog, reload);
    auto apply = [&](bool clearing) {
        try {
            guardRevision(doc, source, revision);
            const auto k = str(key->currentData().toString());
            std::optional<std::string> input;
            if (!clearing) {
                const auto *f = customField(*doc.project(), k);
                if (f && multipleChoice(*f)) {
                    std::vector<std::string> selected;
                    for (const auto *item : choices->selectedItems())
                        selected.push_back(str(item->data(Qt::UserRole).toString()));
                    input = choiceValue(*f, selected);
                } else
                    input = readValue(*value, *doc.project(), k, "equals");
            }
            doc.setTaskAttribute(ids, k, input);
            revision = doc.project()->revision;
            error->setText(clearing ? "선택 작업의 분류 값을 제거했습니다." : "선택 작업에 적용했습니다.");
            reload();
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    };
    QObject::connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog, [&] { apply(false); });
    QObject::connect(clear, &QPushButton::clicked, &dialog, [&] { apply(true); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    buttons->button(QDialogButtonBox::Apply)->setEnabled(!doc.readOnly() && key->count());
    clear->setEnabled(!doc.readOnly() && key->count());
    if (key->count())
        reload();
    else
        error->setText("먼저 사용자 필드 또는 작업 코드 종류를 등록하세요.");
    dialog.resize(600, 440);
    prepareEditorDialog(dialog);
    dialog.exec();
}
std::optional<TaskViewSettings> editTaskViewSettings(const Project &p, const TaskViewSettings &original,
                                                     const std::vector<std::pair<std::string, QString>> &columns,
                                                     QWidget *parent) {
    QDialog dialog(parent);
    dialog.setObjectName("taskViewSettingsDialog");
    dialog.setWindowTitle("분류·필터·정렬·열 배치");
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    auto *wbs = new QComboBox;
    wbs->setObjectName("viewWbs");
    wbs->addItem("프로젝트 전체", "");
    for (const auto &n : p.wbsNodes)
        wbs->addItem(qs(n.name), qs(n.id));
    wbs->setCurrentIndex(wbs->findData(qs(original.wbsId)));
    form->addRow("WBS 범위", wbs);
    auto *query = new QLineEdit(qs(original.query));
    query->setObjectName("viewQuery");
    form->addRow("이름·ID 검색", query);
    auto *filter = new QComboBox;
    filter->setObjectName("viewBaseFilter");
    filter->addItems({"전체 작업", "계획 임계", "마일스톤", "미배정"});
    filter->setCurrentIndex(original.filter);
    form->addRow("기본 필터", filter);
    auto *sort = new QComboBox;
    sort->setObjectName("viewBaseSort");
    sort->addItems({"원본 순서", "이름 ↑", "이름 ↓", "기간 ↑", "기간 ↓", "시작일 ↑", "시작일 ↓"});
    sort->setCurrentIndex(original.sort);
    form->addRow("기본 정렬", sort);
    std::vector<QComboBox *> groups;
    for (int i = 0; i < 3; ++i) {
        auto *g = new QComboBox;
        g->setObjectName("viewGroup" + QString::number(i));
        fillKeys(*g, p, true);
        if (std::size_t(i) < original.groupBy.size())
            g->setCurrentIndex(g->findData(qs(original.groupBy[std::size_t(i)])));
        groups.push_back(g);
        form->addRow(QString("그룹 %1").arg(i + 1), g);
    }
    auto *sortKey = new QComboBox;
    sortKey->setObjectName("viewAttributeSort");
    fillKeys(*sortKey, p, true);
    sortKey->setCurrentIndex(sortKey->findData(qs(original.sortKey)));
    form->addRow("분류 정렬", sortKey);
    auto *descending = new QCheckBox("내림차순 · 미설정은 뒤로");
    descending->setObjectName("viewDescending");
    descending->setChecked(original.descending);
    form->addRow("", descending);
    auto *filters = new QTableWidget(0, 3);
    filters->setObjectName("viewAttributeFilters");
    filters->setHorizontalHeaderLabels({"분류", "조건", "값"});
    filters->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(filters);
    auto addFilter = [&](const AttributeFilter &f) {
        if (filters->rowCount() >= 8)
            return;
        const int row = filters->rowCount();
        filters->insertRow(row);
        auto *key = new QComboBox;
        fillKeys(*key, p, false);
        if (!f.key.empty())
            key->setCurrentIndex(key->findData(qs(f.key)));
        filters->setCellWidget(row, 0, key);
        auto *operation = new QComboBox;
        operation->addItem("같음", "equals");
        operation->addItem("포함", "contains");
        operation->addItem("미설정", "missing");
        operation->addItem("설정됨", "present");
        operation->setCurrentIndex(operation->findData(qs(f.operation)));
        filters->setCellWidget(row, 1, operation);
        auto *value = new QComboBox;
        filters->setCellWidget(row, 2, value);
        auto update = [=, &p] {
            fillValue(*value, p, str(key->currentData().toString()), str(operation->currentData().toString()), {});
        };
        QObject::connect(key, &QComboBox::currentIndexChanged, &dialog, update);
        QObject::connect(operation, &QComboBox::currentIndexChanged, &dialog, update);
        fillValue(*value, p, str(key->currentData().toString()), f.operation, f.value);
    };
    for (const auto &f : original.attributeFilters)
        addFilter(f);
    auto *filterButtons = new QHBoxLayout;
    layout->addLayout(filterButtons);
    auto *add = button(filterButtons, "필터 추가", "addAttributeFilter"),
         *remove = button(filterButtons, "마지막 필터 삭제", "removeAttributeFilter");
    QObject::connect(add, &QPushButton::clicked, &dialog, [&] { addFilter({"", "equals", ""}); });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [=] {
        if (filters->rowCount())
            filters->removeRow(filters->rowCount() - 1);
    });
    layout->addWidget(new QLabel("열은 끌어서 순서를 바꾸고 체크로 표시 여부를 선택합니다."));
    auto *list = new QListWidget;
    list->setObjectName("viewColumns");
    list->setDragDropMode(QAbstractItemView::InternalMove);
    layout->addWidget(list);
    auto columnName = [&](const std::string &key) {
        for (const auto &[id, name] : columns)
            if (id == key)
                return name;
        return qs(key);
    };
    for (const auto &c : original.columns) {
        auto *item = new QListWidgetItem(columnName(c.key), list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        item->setCheckState(c.hidden ? Qt::Unchecked : Qt::Checked);
        item->setData(Qt::UserRole, qs(c.key));
        item->setData(Qt::UserRole + 1, c.width);
    }
    auto *width = new QSpinBox;
    width->setObjectName("viewColumnWidth");
    width->setRange(40, 2000);
    width->setSuffix(" px");
    layout->addWidget(width);
    QObject::connect(list, &QListWidget::currentItemChanged, &dialog, [=](QListWidgetItem *item) {
        if (item) {
            const QSignalBlocker b(width);
            width->setValue(item->data(Qt::UserRole + 1).toInt());
        }
    });
    QObject::connect(width, &QSpinBox::valueChanged, &dialog, [=](int value) {
        if (list->currentItem())
            list->currentItem()->setData(Qt::UserRole + 1, value);
    });
    auto *error = errorLabel(layout);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    std::optional<TaskViewSettings> result;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            TaskViewSettings s;
            s.wbsId = str(wbs->currentData().toString());
            s.query = str(query->text());
            s.filter = filter->currentIndex();
            s.sort = sort->currentIndex();
            for (auto *group : groups)
                if (!group->currentData().toString().isEmpty())
                    s.groupBy.push_back(str(group->currentData().toString()));
            s.sortKey = str(sortKey->currentData().toString());
            s.descending = descending->isChecked();
            for (int row = 0; row < filters->rowCount(); ++row) {
                const auto *key = qobject_cast<QComboBox *>(filters->cellWidget(row, 0)),
                           *operation = qobject_cast<QComboBox *>(filters->cellWidget(row, 1)),
                           *value = qobject_cast<QComboBox *>(filters->cellWidget(row, 2));
                const auto k = str(key->currentData().toString()), op = str(operation->currentData().toString());
                s.attributeFilters.push_back({k, op, readValue(*value, p, k, op)});
            }
            for (int i = 0; i < list->count(); ++i) {
                const auto *item = list->item(i);
                s.columns.push_back({str(item->data(Qt::UserRole).toString()), item->data(Qt::UserRole + 1).toInt(),
                                     item->checkState() != Qt::Checked});
            }
            validateTaskView(p, s);
            result = std::move(s);
            dialog.accept();
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    dialog.resize(720, 760);
    prepareEditorDialog(dialog);
    dialog.exec();
    return result;
}
} // namespace jobs
