#include "ui/XmlCodeDialogs.h"
#include "ui/EditorUi.h"
#include "domain/Classification.h"
#include "domain/XmlCodes.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
namespace ppm {
namespace {
QString q(const std::string &s) {
    return QString::fromUtf8(s);
}
std::string s(const QString &s) {
    return s.toUtf8().toStdString();
}
struct Guard {
    Document &doc;
    std::uint64_t session, revision;
    std::string id;
    explicit Guard(Document &d)
        : doc(d), session(d.sessionGeneration()), revision(d.project()->revision), id(d.project()->documentId) {}
    void check() const {
        if (!doc.project() || doc.readOnly() || doc.sessionGeneration() != session ||
            doc.project()->revision != revision || doc.project()->documentId != id)
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    }
};
class Masks : public QTableWidget {
public:
    explicit Masks(const std::vector<XmlCodeMask> &values) : QTableWidget(0, 4) {
        setObjectName("xmlCodeMasks");
        setHorizontalHeaderLabels({"수준", "형식", "길이 (0=자유)", "구분자"});
        horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        setMaximumHeight(230);
        for (const auto &v : values)
            add(v);
    }
    void add(XmlCodeMask m = {}) {
        if (rowCount() >= 16)
            return;
        const auto row = rowCount();
        insertRow(row);
        auto *level = new QTableWidgetItem(QString::number(row + 1));
        level->setFlags(level->flags() & ~Qt::ItemIsEditable);
        setItem(row, 0, level);
        auto *type = new QComboBox;
        type->addItems({"숫자", "영문 대문자", "영문 소문자", "문자열 (직접 입력)"});
        type->setCurrentIndex(m.type);
        setCellWidget(row, 1, type);
        auto *length = new QSpinBox;
        length->setRange(0, 255);
        length->setValue(m.length);
        setCellWidget(row, 2, length);
        auto *separator = new QLineEdit(q(m.separator));
        separator->setMaxLength(2);
        setCellWidget(row, 3, separator);
    }
    std::vector<XmlCodeMask> read() const {
        std::vector<XmlCodeMask> values;
        for (int row = 0; row < rowCount(); ++row)
            values.push_back({row + 1, qobject_cast<QComboBox *>(cellWidget(row, 1))->currentIndex(),
                              qobject_cast<QSpinBox *>(cellWidget(row, 2))->value(),
                              s(qobject_cast<QLineEdit *>(cellWidget(row, 3))->text())});
        validateXmlMasks(values);
        return values;
    }
};
Masks *maskEditor(QVBoxLayout *body, const std::vector<XmlCodeMask> &values) {
    auto *table = new Masks(values);
    body->addWidget(table);
    auto *actions = new QHBoxLayout;
    body->addLayout(actions);
    auto *add = new QPushButton("수준 추가"), *remove = new QPushButton("마지막 수준 삭제");
    add->setObjectName("addXmlMaskLevel");
    remove->setObjectName("removeXmlMaskLevel");
    actions->addWidget(add);
    actions->addWidget(remove);
    QObject::connect(add, &QPushButton::clicked, table, [=] { table->add(); });
    QObject::connect(remove, &QPushButton::clicked, table, [=] {
        if (table->rowCount())
            table->removeRow(table->rowCount() - 1);
    });
    return table;
}
QLabel *note(QVBoxLayout *body, const QString &text = {}) {
    auto *label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    body->addWidget(label);
    return label;
}
QDialogButtonBox *buttons(QVBoxLayout *body, QDialog &dialog) {
    auto *box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    body->addWidget(box);
    box->button(QDialogButtonBox::Save)->setText("저장");
    box->button(QDialogButtonBox::Cancel)->setText("취소");
    QObject::connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    return box;
}
} // namespace
void editXmlLookupMask(Document &doc, FieldScope scope, const std::string &key, QWidget *parent) {
    if (!doc.project() || doc.readOnly())
        return;
    const auto *field = customField(*doc.project(), key, scope);
    if (!field || !field->xmlLookup || field->xmlLookup->tableGuid.empty() ||
        field->xmlLookup->valueType != CustomFieldType::Text)
        return;
    Guard guard(doc);
    QDialog dialog(parent);
    dialog.setObjectName("xmlLookupMaskDialog");
    dialog.setWindowTitle("XML 코드 마스크 · " + q(field->name));
    auto *body = new QVBoxLayout(&dialog);
    note(body, "수준별 형식·고정 길이·구분자를 지정합니다. 같은 GUID 목록을 쓰는 모든 필드에 함께 적용합니다. 기존 "
               "값과 맞지 않는 변경은 적용할 수 없습니다.");
    auto *masks = maskEditor(body, field->xmlLookup->masks);
    auto *required = new QCheckBox("값 선택에 모든 수준 필요");
    required->setObjectName("xmlAllLevelsRequired");
    required->setChecked(field->xmlLookup->allLevelsRequired);
    body->addWidget(required);
    auto *error = note(body);
    error->setObjectName("xmlCodeError");
    auto *box = buttons(body, dialog);
    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            guard.check();
            doc.setXmlLookupMasks(scope, key, masks->read(), required->isChecked());
            dialog.accept();
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    dialog.resize(720, 450);
    prepareEditorDialog(dialog);
    dialog.exec();
}
void createXmlLookupCode(Document &doc, FieldScope scope, const std::string &key, QWidget *parent) {
    if (!doc.project() || doc.readOnly())
        return;
    const auto *field = customField(*doc.project(), key, scope);
    if (!field || !field->xmlLookup || field->xmlLookup->masks.empty())
        return;
    const auto lookup = *field->xmlLookup;
    Guard guard(doc);
    QDialog dialog(parent);
    dialog.setObjectName("xmlLookupCodeDialog");
    dialog.setWindowTitle("XML 목록 코드 생성 · " + q(field->name));
    auto *body = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    body->addLayout(form);
    auto *parentCode = new QComboBox;
    parentCode->setObjectName("xmlCodeParent");
    parentCode->addItem("최상위", "");
    for (const auto &v : lookup.values)
        if (v.listed)
            parentCode->addItem(q(xmlLookupCode(lookup, v.optionId)), q(v.optionId));
    form->addRow("부모 코드", parentCode);
    auto *automatic = new QCheckBox("다음 순번 자동 생성");
    automatic->setObjectName("xmlCodeAutomatic");
    automatic->setChecked(true);
    form->addRow(automatic);
    auto *value = new QLineEdit;
    value->setObjectName("xmlCodeSegment");
    form->addRow("새 수준의 코드", value);
    auto *description = new QLineEdit;
    description->setObjectName("xmlCodeDescription");
    form->addRow("설명", description);
    auto *preview = note(body);
    preview->setObjectName("xmlCodePreview");
    note(body, "기존 코드·선택 값은 유지하며 새 항목만 만듭니다. 문자/숫자 순번이 소진되면 길이 또는 부모를 바꾸세요. "
               "문자열 수준은 직접 입력합니다.");
    auto *error = note(body);
    error->setObjectName("xmlCodeError");
    auto *box = buttons(body, dialog);
    auto showPreview = [&] {
        const auto id = s(parentCode->currentData().toString());
        auto current = id;
        std::size_t depth = 0;
        while (!current.empty()) {
            auto found = std::find_if(lookup.values.begin(), lookup.values.end(),
                                      [&](const auto &v) { return v.optionId == current; });
            if (found == lookup.values.end() || ++depth > lookup.masks.size()) {
                preview->clear();
                return;
            }
            current = found->parentId;
        }
        preview->setText("생성 코드: " +
                         (id.empty() ? QString{} : q(xmlLookupCode(lookup, id) + lookup.masks[depth - 1].separator)) +
                         value->text());
    };
    QObject::connect(value, &QLineEdit::textChanged, &dialog, showPreview);
    auto update = [&] {
        error->clear();
        value->setReadOnly(automatic->isChecked());
        try {
            if (automatic->isChecked())
                value->setText(q(nextXmlLookupCode(lookup, s(parentCode->currentData().toString()))));
            showPreview();
        } catch (const std::exception &e) {
            value->clear();
            showEditorError(*error, e);
        }
    };
    QObject::connect(parentCode, &QComboBox::currentIndexChanged, &dialog, update);
    QObject::connect(automatic, &QCheckBox::toggled, &dialog, update);
    update();
    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            guard.check();
            doc.createXmlLookupCode(scope, key, s(parentCode->currentData().toString()),
                                    automatic->isChecked() ? std::nullopt
                                                           : std::optional<std::string>{s(value->text())},
                                    s(description->text()));
            dialog.accept();
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    dialog.resize(680, 400);
    prepareEditorDialog(dialog);
    dialog.exec();
}
void editXmlWbsCodes(Document &doc, QWidget *parent) {
    if (!doc.project() || doc.readOnly())
        return;
    const auto input = *doc.project();
    auto current = input.xmlWbs.value_or(XmlWbsCodes{});
    Guard guard(doc);
    QDialog dialog(parent);
    dialog.setObjectName("xmlWbsCodesDialog");
    dialog.setWindowTitle("XML WBS 마스크·자동 코드");
    auto *body = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    body->addLayout(form);
    auto *prefix = new QLineEdit(q(current.prefix));
    prefix->setObjectName("xmlWbsPrefix");
    prefix->setMaxLength(100);
    form->addRow("프로젝트 접두사", prefix);
    auto *generate = new QCheckBox("새 작업·WBS의 코드 자동 생성");
    generate->setObjectName("xmlWbsGenerate");
    generate->setChecked(current.generate);
    form->addRow(generate);
    auto *unique = new QCheckBox("코드 중복 허용 안 함");
    unique->setObjectName("xmlWbsUnique");
    unique->setChecked(current.verifyUnique);
    form->addRow(unique);
    auto *masks = maskEditor(body, current.masks);
    auto *values = new QTableWidget(0, 2);
    values->setObjectName("xmlWbsValues");
    values->setHorizontalHeaderLabels({"객체", "XML WBS 코드 (빈 값=미설정)"});
    values->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    body->addWidget(values, 1);
    auto row = [&](const std::string &kind, const std::string &id, const std::string &name) {
        const auto r = values->rowCount();
        values->insertRow(r);
        auto *owner = new QTableWidgetItem(q(name));
        owner->setFlags(owner->flags() & ~Qt::ItemIsEditable);
        owner->setData(Qt::UserRole, q(kind));
        owner->setData(Qt::UserRole + 1, q(id));
        values->setItem(r, 0, owner);
        values->setItem(r, 1, new QTableWidgetItem(q(xmlWbsCode(input, kind, id).value_or(""))));
    };
    for (const auto &g : input.wbsNodes)
        row("wbs", g.id, "WBS · " + g.name);
    for (const auto &t : input.tasks)
        row("task", t.id, t.name);
    note(body, "기존 코드는 이동·정렬해도 재번호하지 않습니다. ‘미설정 생성’으로 빈 코드만 미리 채우며 저장을 눌러 "
               "적용합니다. 문자열 수준과 부모 코드가 없는 하위 항목은 직접 입력하세요.");
    auto *preview = new QPushButton("미설정 코드 생성 미리보기");
    preview->setObjectName("previewXmlWbsCodes");
    body->addWidget(preview);
    auto *error = note(body);
    error->setObjectName("xmlCodeError");
    auto *box = buttons(body, dialog);
    auto read = [&] {
        XmlWbsCodes codes;
        codes.prefix = s(prefix->text());
        codes.generate = generate->isChecked();
        codes.verifyUnique = unique->isChecked();
        codes.masks = masks->read();
        for (int r = 0; r < values->rowCount(); ++r)
            if (!values->item(r, 1)->text().isEmpty())
                codes.values.push_back({s(values->item(r, 0)->data(Qt::UserRole).toString()),
                                        s(values->item(r, 0)->data(Qt::UserRole + 1).toString()),
                                        s(values->item(r, 1)->text())});
        auto p = input;
        p.xmlWbs = codes;
        validateXmlWbsCodes(p);
        return p;
    };
    QObject::connect(preview, &QPushButton::clicked, &dialog, [&] {
        try {
            guard.check();
            auto p = read();
            synchronizeXmlWbsCodes(p, nullptr, true);
            for (int r = 0; r < values->rowCount(); ++r)
                values->item(r, 1)->setText(q(xmlWbsCode(p, s(values->item(r, 0)->data(Qt::UserRole).toString()),
                                                         s(values->item(r, 0)->data(Qt::UserRole + 1).toString()))
                                                  .value_or("")));
            error->setText("미리보기를 채웠습니다. 저장을 눌러 적용하세요.");
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            guard.check();
            auto p = read();
            doc.setXmlWbsCodes(*p.xmlWbs);
            dialog.accept();
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    dialog.resize(800, 720);
    prepareEditorDialog(dialog);
    dialog.exec();
}
} // namespace ppm
