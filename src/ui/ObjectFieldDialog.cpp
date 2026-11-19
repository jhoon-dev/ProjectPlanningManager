#include "ui/ClassificationDialogs.h"
#include "ui/EditorUi.h"
#include "domain/Classification.h"
#include "domain/XmlRollDown.h"
#include "domain/FormulaFields.h"
#include "calculation/ScheduleFormulaScope.h"
#include "formatting/QtDiagnosticText.h"
#include <QAbstractTableModel>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <unordered_map>

namespace ppm {
namespace {
QString q(const std::string &s) {
    return QString::fromUtf8(s);
}
class ObjectValueModel final : public QAbstractTableModel {
public:
    ObjectValueModel(Document &doc, std::shared_ptr<const Project> source, FieldScope scope, QObject *parent)
        : QAbstractTableModel(parent), source(std::move(source)), scope(scope), doc(doc) {
        schedule = makeScheduleFormulaValues(*this->source, doc.calculation() ? &*doc.calculation() : nullptr);
        XmlScheduleScope binding(*this->source, schedule);
        if (scope == FieldScope::Assignment) {
            std::unordered_map<std::string, std::string> tasks, resources;
            for (const auto &t : this->source->tasks)
                tasks.emplace(t.id, t.name);
            for (const auto &r : this->source->resources)
                resources.emplace(r.id, r.name);
            for (const auto &a : this->source->assignments)
                assignmentLabels.push_back(tasks.at(a.taskId) + " / " + resources.at(a.resourceId));
            refreshRollDown();
        }
    }
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid()                  ? 0
               : scope == FieldScope::Project    ? 1
               : scope == FieldScope::Assignment ? int(source->assignments.size())
                                                 : int(source->resources.size());
    }
    int columnCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : 3;
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (role != Qt::DisplayRole)
            return {};
        if (orientation == Qt::Vertical)
            return section + 1;
        return QStringList{"프로젝트/자원/배정", "안정 ID", "선택 필드의 현재 값"}.value(section);
    }
    const std::vector<TaskAttribute> &values(int row) const {
        return scope == FieldScope::Project      ? source->projectAttributes
               : scope == FieldScope::Assignment ? rolledValues.at(std::size_t(row))
                                                 : source->resources.at(std::size_t(row)).attributes;
    }
    QVariant data(const QModelIndex &index, int role) const override {
        XmlScheduleScope binding(*source, schedule);
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
            return {};
        const auto &id = scope == FieldScope::Project ? source->documentId
                         : scope == FieldScope::Assignment
                             ? assignmentAttributeId(source->assignments[std::size_t(index.row())])
                             : source->resources[std::size_t(index.row())].id;
        if (role == Qt::UserRole)
            return q(id);
        if (role != Qt::DisplayRole && role != Qt::ToolTipRole)
            return {};
        if (index.column() == 0)
            return q(scope == FieldScope::Project      ? source->name
                     : scope == FieldScope::Assignment ? assignmentLabels[std::size_t(index.row())]
                                                       : source->resources[std::size_t(index.row())].name);
        if (index.column() == 1 && scope == FieldScope::Assignment) {
            const auto &a = source->assignments[std::size_t(index.row())];
            return q(a.taskId + " / " + a.resourceId);
        }
        if (index.column() == 1)
            return q(id);
        const auto *definition = customField(*source, key, scope);
        if (definition && calculatedField(*definition)) {
            const auto computed = formulaValues(
                *source, scope, nullptr,
                scope == FieldScope::Resource ? &source->resources.at(std::size_t(index.row())) : nullptr,
                scope == FieldScope::Assignment ? &source->assignments.at(std::size_t(index.row())) : nullptr);
            const auto &v = computed->at(key);
            const auto raw = formulaRawValue(v);
            return (raw ? q(attributeText(*source, key, *raw, scope)) : q(formulaValueText(v))) +
                   (raw && !definition->unit.empty() ? " " + q(definition->unit) : "");
        }
        const auto value = attributeValue(values(index.row()), key);
        if (scope == FieldScope::Assignment && role == Qt::ToolTipRole)
            return q(rollDown->origin(source->assignments.at(std::size_t(index.row())), key)) + "\n" +
                   (value ? q(attributeText(*source, key, *value, scope)) : QString("미설정"));
        if (!value)
            return QStringLiteral("미설정");
        const auto *field = customField(*source, key, scope);
        return q(attributeText(*source, key, *value, scope)) +
               (field && !field->unit.empty() ? " " + q(field->unit) : "");
    }
    void refreshRollDown() {
        if (scope != FieldScope::Assignment || rolledSource == source.get())
            return;
        rollDown = std::make_unique<RollDownValues>(*source);
        rolledValues.clear();
        rolledValues.reserve(source->assignments.size());
        for (const auto &a : source->assignments)
            rolledValues.push_back(rollDown->attributes(a));
        rolledSource = source.get();
    }
    void changed() {
        schedule = makeScheduleFormulaValues(*source, doc.calculation() ? &*doc.calculation() : nullptr);
        XmlScheduleScope binding(*source, schedule);
        rolledSource = nullptr;
        refreshRollDown();
        if (rowCount())
            emit dataChanged(index(0, 2), index(rowCount() - 1, 2), {Qt::DisplayRole, Qt::ToolTipRole});
    }
    std::shared_ptr<const Project> source;
    FieldScope scope;
    std::string key;
    std::vector<std::string> assignmentLabels;
    std::unique_ptr<RollDownValues> rollDown;
    std::vector<std::vector<TaskAttribute>> rolledValues;
    const Project *rolledSource = nullptr;
    Document &doc;
    std::shared_ptr<const XmlScheduleValues> schedule;
};
class ObjectFieldDialog final : public QDialog {
public:
    ObjectFieldDialog(Document &doc, FieldScope scope, QWidget *parent)
        : QDialog(parent), doc_(doc), scope_(scope), session_(doc.sessionGeneration()) {
        setObjectName(scope == FieldScope::Project      ? "projectAttributesDialog"
                      : scope == FieldScope::Assignment ? "assignmentAttributesDialog"
                                                        : "resourceAttributesDialog");
        setWindowTitle(scope == FieldScope::Project      ? "프로젝트 사용자 필드 값"
                       : scope == FieldScope::Assignment ? "배정 사용자 필드 값"
                                                         : "자원 사용자 필드 값");
        auto *layout = new QVBoxLayout(this);
        auto *note = new QLabel(
            scope == FieldScope::Project ? "프로젝트 전체의 사용자 필드 값입니다."
            : scope == FieldScope::Assignment
                ? "작업/자원 배정을 선택해 값을 적용합니다. Ctrl/Shift 또는 Ctrl+A로 여러 배정을 선택할 수 있습니다."
                : "자원을 선택해 적용합니다. Ctrl/Shift로 여러 자원, Ctrl+A로 전체 자원을 선택할 수 있습니다.",
            this);
        note->setWordWrap(true);
        layout->addWidget(note);
        key_ = new QComboBox(this);
        key_->setObjectName("objectFieldKey");
        for (const auto &key : attributeKeys(*doc.snapshot(), scope))
            key_->addItem(q(attributeName(*doc.snapshot(), key, scope)), q(key));
        layout->addWidget(key_);
        table_ = new QTableView(this);
        table_->setObjectName("objectFieldValues");
        model_ = new ObjectValueModel(doc, doc.snapshot(), scope, table_);
        table_->setModel(model_);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(scope == FieldScope::Project ? QAbstractItemView::SingleSelection
                                                              : QAbstractItemView::ExtendedSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setWordWrap(false);
        table_->setColumnWidth(0, 170);
        table_->setColumnWidth(1, 260);
        table_->horizontalHeader()->setStretchLastSection(true);
        layout->addWidget(table_, 1);
        description_ = new QLabel(this);
        description_->setTextFormat(Qt::PlainText);
        description_->setWordWrap(true);
        layout->addWidget(description_);
        editor_ = new QStackedWidget(this);
        text_ = new QPlainTextEdit(editor_);
        text_->setObjectName("objectFieldText");
        text_->setMaximumHeight(100);
        line_ = new QLineEdit(editor_);
        line_->setObjectName("objectFieldInput");
        choice_ = new QComboBox(editor_);
        choice_->setObjectName("objectFieldChoice");
        editor_->addWidget(text_);
        editor_->addWidget(line_);
        editor_->addWidget(choice_);
        choices_ = new QListWidget(editor_);
        choices_->setObjectName("objectFieldChoices");
        choices_->setSelectionMode(QAbstractItemView::ExtendedSelection);
        editor_->addWidget(choices_);
        layout->addWidget(editor_);
        message_ = new QLabel(this);
        message_->setObjectName("objectFieldMessage");
        message_->setTextFormat(Qt::PlainText);
        message_->setWordWrap(true);
        layout->addWidget(message_);
        buttons_ = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, this);
        apply_ = buttons_->button(QDialogButtonBox::Apply);
        apply_->setText("적용");
        buttons_->button(QDialogButtonBox::Close)->setText("닫기");
        apply_->setObjectName("applyObjectField");
        clear_ = buttons_->addButton("값 제거", QDialogButtonBox::ActionRole);
        clear_->setObjectName("clearObjectField");
        cancel_ = buttons_->addButton("적용 준비 취소", QDialogButtonBox::ActionRole);
        cancel_->setObjectName("cancelObjectField");
        layout->addWidget(buttons_);
        connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(apply_, &QPushButton::clicked, this, [this] { submit(false); });
        connect(clear_, &QPushButton::clicked, this, [this] { submit(true); });
        connect(cancel_, &QPushButton::clicked, this, [this] {
            if (control_)
                control_->cancel();
        });
        connect(key_, &QComboBox::currentIndexChanged, this, [this] { load(); });
        connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { load(); });
        token_ = doc_.subscribe([this](Document::Event) {
            if (!committing_ && current())
                model_->changed();
            if (!committing_ && !current()) {
                if (control_)
                    control_->cancel();
                message_->setText("문서가 변경되었습니다. 이 창은 이전 값의 조회만 제공합니다. 창을 다시 열어 주세요.");
                controls();
            }
        });
        auto *calculationTimer = new QTimer(this);
        calculationTimer->setInterval(150);
        connect(calculationTimer, &QTimer::timeout, this, [this, generation = doc_.calculationGeneration()]() mutable {
            if (!committing_ && current() && generation != doc_.calculationGeneration()) {
                generation = doc_.calculationGeneration();
                model_->changed();
            }
        });
        calculationTimer->start();
        if (model_->rowCount())
            table_->selectRow(0);
        load();
        resize(930, 660);
        prepareEditorDialog(*this);
    }
    ~ObjectFieldDialog() override {
        if (control_)
            control_->cancel();
        doc_.unsubscribe(token_);
    }
    void reject() override {
        if (control_)
            control_->cancel();
        QDialog::reject();
    }

private:
    bool current() const {
        return doc_.project() && !doc_.readOnly() && doc_.sessionGeneration() == session_ &&
               doc_.project()->documentId == model_->source->documentId &&
               doc_.project()->revision == model_->source->revision;
    }
    void controls() {
        const auto *f = customField(*model_->source, model_->key, scope_);
        const bool editable = current() && !busy_ && key_->count() && f && !calculatedField(*f) &&
                              (scope_ == FieldScope::Project || !table_->selectionModel()->selectedRows().empty());
        apply_->setEnabled(editable);
        clear_->setEnabled(editable);
        editor_->setEnabled(editable);
        key_->setEnabled(!busy_);
        table_->setEnabled(!busy_);
        cancel_->setEnabled(busy_);
    }
    void load() {
        model_->key = key_->currentData().toString().toStdString();
        model_->changed();
        const auto *rollSource =
            scope_ == FieldScope::Assignment && model_->rollDown ? model_->rollDown->source(model_->key) : nullptr;
        clear_->setText(rollSource ? "원본 값 사용" : "값 제거");
        const auto *field = customField(*model_->source, model_->key, scope_);
        if (!field) {
            description_->setText("먼저 해당 범위의 사용자 필드를 정의하세요.");
            controls();
            return;
        }
        if (calculatedField(*field)) {
            description_->setText("계산식 · " + q(field->expression) +
                                  "\n계산 결과는 읽기 전용입니다. 미설정/오류는 0과 다릅니다.");
            text_->clear();
            line_->clear();
            editor_->setCurrentIndex(1);
            controls();
            return;
        }
        std::optional<std::string> common;
        bool first = true, mixed = false;
        const auto rows = table_->selectionModel()->selectedRows();
        for (const auto &row : rows) {
            const auto value = attributeValue(model_->values(row.row()), model_->key);
            if (first) {
                common = value;
                first = false;
            } else if (common != value)
                mixed = true;
        }
        const auto typeName =
            QStringList{"텍스트", "숫자", "날짜/일시", "목록", "참/거짓", "금액", "기간", "시작 일시", "종료 일시"}.at(
                int(field->type));
        const auto hint =
            field->type == CustomFieldType::Text ? "빈 문자열과 미설정은 다릅니다."
            : field->type == CustomFieldType::Number ? "소수 6자리까지 입력합니다. 숫자 0과 미설정은 다릅니다."
            : field->type == CustomFieldType::Cost ? "프로젝트 통화의 금액을 입력하세요. 음수·0·미설정을 구분합니다."
            : field->type == CustomFieldType::Duration ? "분 단위입니다. 예: 90 m, 경과 90 em, 추정 90 m?"
            : field->type == CustomFieldType::Flag     ? "예/아니요와 미설정은 다릅니다."
            : field->type == CustomFieldType::Date ? "날짜 또는 일시를 입력하세요: YYYY-MM-DD / YYYY-MM-DDTHH:MM:SS"
            : dateTimeCustomField(field->type)
                ? "시간대 없는 일시: YYYY-MM-DDTHH:MM:SS[.소수6자리]. 실제 일정 제약으로 자동 채택하지 않습니다."
            : multipleChoice(*field) ? "Ctrl/Shift로 목록의 여러 항목을 선택하세요."
                                     : "목록에서 값을 선택하세요.";
        description_->setText(QString("선택 %1개 · %2 %3\n%4 미설정은 ‘값 제거’를 사용하세요.")
                                  .arg(rows.size())
                                  .arg(typeName, q(attributeUnit(*model_->source, model_->key, scope_)), hint));
        if (rollSource) {
            QString origins;
            if (rows.size() == 1)
                origins = "현재 출처: " +
                          q(model_->rollDown->origin(model_->source->assignments.at(std::size_t(rows.front().row())),
                                                     model_->key)) +
                          " · ";
            description_->setText(
                description_->text().replace(" 미설정은 ‘값 제거’를 사용하세요.", "") + "\n" + origins +
                "직접 입력이 우선합니다. ‘원본 값 사용’은 직접 값/명시 미설정을 지우고 roll-down으로 복원합니다.");
        }
        editor_->setMaximumHeight(multipleChoice(*field)                 ? 150
                                  : field->type == CustomFieldType::Text ? 100
                                                                         : std::max(32, fontMetrics().height() + 16));
        text_->setPlainText(common && !mixed ? q(*common) : "");
        text_->setPlaceholderText(mixed ? "여러 값 · 새 값을 입력하세요" : "텍스트 (빈 문자열 입력 가능)");
        line_->setText(common && !mixed ? q(*common) : "");
        line_->setPlaceholderText(mixed                                  ? "여러 값 · 새 값을 입력하세요"
                                  : field->type == CustomFieldType::Date ? "YYYY-MM-DD / YYYY-MM-DDTHH:MM:SS"
                                  : dateTimeCustomField(field->type)     ? "YYYY-MM-DDTHH:MM:SS"
                                  : field->type == CustomFieldType::Cost ? q(model_->source->currency) + " 금액"
                                  : field->type == CustomFieldType::Duration ? "90 m · 90 em · 90 m?"
                                                                             : "숫자 · 소수 6자리까지");
        choices_->clear();
        const auto selected = common && !mixed && field->type == CustomFieldType::Choice
                                  ? choiceSelection(*field, *common)
                                  : std::vector<std::string>{};
        for (const auto &option : field->options) {
            auto *item = new QListWidgetItem(q(option.name), choices_);
            item->setData(Qt::UserRole, q(option.id));
            if (field->xmlLookup) {
                const auto &l = *field->xmlLookup;
                const auto entry = std::find_if(l.values.begin(), l.values.end(),
                                                [&](const auto &v) { return v.optionId == option.id; });
                if (entry != l.values.end())
                    item->setToolTip(q(entry->description));
                if (l.leafOnly && std::any_of(l.values.begin(), l.values.end(),
                                              [&](const auto &v) { return v.parentId == option.id; }))
                    item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            }
            item->setSelected(std::find(selected.begin(), selected.end(), option.id) != selected.end());
        }
        choice_->clear();
        choice_->addItem(mixed ? "여러 값 · 옵션을 선택하세요" : "옵션을 선택하세요", QVariant{});
        for (const auto &option : field->options) {
            choice_->addItem(q(option.name), q(option.id));
            if (field->xmlLookup && field->xmlLookup->leafOnly &&
                std::any_of(field->xmlLookup->values.begin(), field->xmlLookup->values.end(),
                            [&](const auto &v) { return v.parentId == option.id; }))
                qobject_cast<QStandardItemModel *>(choice_->model())->item(choice_->count() - 1)->setEnabled(false);
        }
        if (field->type == CustomFieldType::Flag) {
            choice_->addItem("아니요", "0");
            choice_->addItem("예", "1");
        }
        if (common && !mixed)
            choice_->setCurrentIndex(std::max(0, choice_->findData(q(*common))));
        editor_->setCurrentIndex(field->type == CustomFieldType::Text ? 0
                                 : (field->type == CustomFieldType::Choice || field->type == CustomFieldType::Flag)
                                     ? (multipleChoice(*field) ? 3 : 2)
                                     : 1);
        controls();
    }
    void submit(bool clearing) {
        try {
            if (!current() || busy_)
                throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
            const auto key = model_->key;
            const auto *field = customField(*model_->source, key, scope_);
            if (!field)
                throw AppError(ErrorCode::CLASSIFICATION_NOT_FOUND, {key});
            std::optional<std::string> value;
            if (!clearing) {
                if ((field->type == CustomFieldType::Choice || field->type == CustomFieldType::Flag) &&
                    !multipleChoice(*field) && !choice_->currentData().isValid())
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"목록 옵션을 선택하세요."});
                value = field->type == CustomFieldType::Text ? text_->toPlainText().toStdString()
                        : (field->type == CustomFieldType::Choice || field->type == CustomFieldType::Flag)
                            ? choice_->currentData().toString().toStdString()
                            : line_->text().toStdString();
                if (multipleChoice(*field)) {
                    std::vector<std::string> ids;
                    for (const auto *item : choices_->selectedItems())
                        ids.push_back(item->data(Qt::UserRole).toString().toStdString());
                    value = choiceValue(*field, ids);
                }
            }
            std::vector<std::string> ids;
            if (scope_ == FieldScope::Resource || scope_ == FieldScope::Assignment)
                for (const auto &row : table_->selectionModel()->selectedRows())
                    ids.push_back(model_->data(row, Qt::UserRole).toString().toStdString());
            auto request = doc_.makeEditRequest(ObjectAttributeEdit{scope_, std::move(ids), key, std::move(value)});
            control_ = std::make_shared<CalculationControl>();
            busy_ = true;
            controls();
            message_->setText("사용자 필드 검증·적용 준비 중…");
            auto *watcher = new QFutureWatcher<std::shared_ptr<Document::PreparedEdit>>(this);
            connect(watcher, &QFutureWatcher<std::shared_ptr<Document::PreparedEdit>>::finished, this, [this, watcher] {
                auto result = watcher->result();
                watcher->deleteLater();
                committing_ = true;
                const auto state = doc_.applyPreparedEdit(*result);
                committing_ = false;
                busy_ = false;
                if (state == EditState::Applied || state == EditState::NoChange) {
                    model_->source = doc_.snapshot();
                    message_->setText("문서에 반영했습니다. 파일 저장으로 보존하세요.");
                    load();
                } else {
                    if (result->failure())
                        showEditorError(*message_, AppError(*result->failure()));
                    else
                        message_->setText(state == EditState::Cancelled
                                              ? "적용을 취소했습니다."
                                              : "문서가 변경되어 적용하지 않았습니다. 창을 다시 열어 주세요.");
                    controls();
                }
            });
            watcher->setFuture(QtConcurrent::run([request = std::move(request), control = control_]() mutable {
                return Document::prepareEdit(std::move(request), control);
            }));
        } catch (const std::exception &error) {
            busy_ = false;
            controls();
            showEditorError(*message_, error);
        }
    }
    Document &doc_;
    FieldScope scope_;
    std::uint64_t session_, token_ = 0;
    ObjectValueModel *model_;
    QTableView *table_;
    QComboBox *key_, *choice_;
    QLineEdit *line_;
    QListWidget *choices_;
    QPlainTextEdit *text_;
    QStackedWidget *editor_;
    QLabel *description_, *message_;
    QDialogButtonBox *buttons_;
    QPushButton *apply_, *clear_, *cancel_;
    std::shared_ptr<CalculationControl> control_;
    bool busy_ = false, committing_ = false;
};
} // namespace
void editObjectAttributes(Document &doc, FieldScope scope, QWidget *parent) {
    if (!doc.project() ||
        (scope != FieldScope::Project && scope != FieldScope::Resource && scope != FieldScope::Assignment))
        return;
    ObjectFieldDialog dialog(doc, scope, parent);
    dialog.exec();
}
} // namespace ppm
