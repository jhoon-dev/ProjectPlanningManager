#include "calculation/ScheduleFormulaScope.h"
#include "ui/ResourceViewDialog.h"
#include "domain/FormulaFields.h"
#include "ui/EditorUi.h"
#include "calculation/ResourceViewReport.h"
#include "reporting/ReportExport.h"
#include "formatting/QtDiagnosticText.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
namespace jobs {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
struct Projection {
    std::shared_ptr<const XmlScheduleValues> schedule;
    std::vector<ResourceViewRow> rows;
    std::map<std::string, std::map<std::string, std::pair<QString, QString>>> summaries;
    QString error;
};
void keys(QComboBox *box, const PlanInput &p, bool empty = false) {
    box->clear();
    if (empty)
        box->addItem("그룹 없음", QString{});
    for (const auto &key : resourceViewKeys(p))
        box->addItem(q(resourceViewName(p, key)), q(key));
}
QString displayValue(const PlanInput &p, const AttributeFilter &f) {
    if (f.operation == "contains" || f.operation == "missing" || f.operation == "present")
        return q(f.value);
    if (f.key == "type" || f.key == "assigned" || f.key == "calendar")
        return q(resourceViewText(p, f.key, f.value));
    const auto *field = customField(p, f.key, FieldScope::Resource);
    return field && (field->type == CustomFieldType::Choice || field->type == CustomFieldType::Flag)
               ? q(attributeText(p, f.key, f.value, FieldScope::Resource))
               : q(f.value);
}
std::string filterValue(const PlanInput &p, const std::string &key, const std::string &op, QString value) {
    if (op == "missing" || op == "present")
        return {};
    if (op == "contains")
        return value.toStdString();
    auto s = value.toStdString();
    if (key == "type") {
        if (s == "인력")
            return "labor";
        if (s == "장비")
            return "equipment";
    }
    if (key == "assigned") {
        if (s == "배정됨")
            return "yes";
        if (s == "미배정")
            return "no";
    }
    if (key == "calendar") {
        if (s == "프로젝트 기본 달력")
            return {};
        for (const auto &c : p.calendars)
            if (c.name == s)
                return c.id;
    }
    if (const auto *f = customField(p, key, FieldScope::Resource)) {
        if (f->type == CustomFieldType::Choice)
            for (const auto &o : f->options)
                if (o.name == s)
                    return normalizeAttributeValue(p, key, o.id, FieldScope::Resource);
        if (numericCustomField(f->type) || dateTimeCustomField(f->type) || f->type == CustomFieldType::Flag)
            return normalizeAttributeValue(p, key, value.trimmed().toStdString(), FieldScope::Resource, true);
    }
    if (key == "capacity")
        return formatFieldNumber(parseFieldNumber(value.trimmed().toStdString()));
    return s;
}
} // namespace
ResourceViewDialog::ResourceViewDialog(Document &doc, QWidget *parent) : QDialog(parent), doc_(doc) {
    setObjectName("resourceViewDialog");
    setWindowTitle("자원 그룹·필터 · 저장된 보기");
    auto *layout = new QVBoxLayout(this);
    auto *note = new QLabel("자원별 한 행 · 조건은 모두 충족(AND) · 검색/포함은 대소문자를 구분합니다.\n"
                            "미설정·빈 텍스트·숫자 0을 구분합니다. 미설정은 정렬 마지막이며 동률은 ID 순입니다. "
                            "기본 가용률을 합산하지 않습니다. 필터 값은 목록 이름 또는 숫자·날짜를 입력하세요.",
                            this);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *views = new QHBoxLayout;
    saved_ = new QComboBox(this);
    saved_->setObjectName("savedResourceViews");
    views->addWidget(saved_);
    name_ = new QLineEdit(this);
    name_->setObjectName("resourceViewName");
    name_->setPlaceholderText("저장할 보기 이름");
    views->addWidget(name_);
    save_ = new QPushButton("보기 저장·갱신", this);
    save_->setObjectName("saveResourceView");
    views->addWidget(save_);
    remove_ = new QPushButton("보기 삭제", this);
    remove_->setObjectName("removeResourceView");
    views->addWidget(remove_);
    layout->addLayout(views);
    auto *tools = new QHBoxLayout;
    query_ = new QLineEdit(this);
    query_->setObjectName("resourceViewQuery");
    query_->setPlaceholderText("자원 이름·ID 검색");
    tools->addWidget(query_);
    for (int i = 0; i < 3; ++i) {
        groups_[i] = new QComboBox(this);
        groups_[i]->setObjectName(QString("resourceGroup%1").arg(i));
        tools->addWidget(groups_[i]);
    }
    sort_ = new QComboBox(this);
    sort_->setObjectName("resourceViewSort");
    tools->addWidget(sort_);
    descending_ = new QCheckBox("내림차순", this);
    descending_->setObjectName("resourceViewDescending");
    tools->addWidget(descending_);
    layout->addLayout(tools);
    filters_ = new QTableWidget(0, 3, this);
    filters_->setObjectName("resourceViewFilters");
    filters_->setHorizontalHeaderLabels({"필드", "조건", "값"});
    filters_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    filters_->setMaximumHeight(175);
    layout->addWidget(filters_);
    auto *filterTools = new QHBoxLayout;
    auto *add = new QPushButton("조건 추가", this);
    add->setObjectName("addResourceFilter");
    filterTools->addWidget(add);
    auto *erase = new QPushButton("선택 조건 삭제", this);
    erase->setObjectName("removeResourceFilter");
    filterTools->addWidget(erase);
    layout->addLayout(filterTools);
    tree_ = new QTreeWidget(this);
    tree_->setObjectName("resourceViewTree");
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setSortingEnabled(false);
    layout->addWidget(tree_, 1);
    message_ = new QLabel(this);
    message_->setObjectName("resourceViewMessage");
    message_->setTextFormat(Qt::PlainText);
    message_->setWordWrap(true);
    layout->addWidget(message_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *refresh = buttons->addButton("최신 문서로 다시 열기", QDialogButtonBox::ActionRole);
    refresh->setObjectName("reloadResourceView");
    xlsx_ = buttons->addButton("보기 XLSX…", QDialogButtonBox::ActionRole);
    xlsx_->setObjectName("exportResourceViewXlsx");
    pdf_ = buttons->addButton("보기 PDF…", QDialogButtonBox::ActionRole);
    pdf_->setObjectName("exportResourceViewPdf");
    layout->addWidget(buttons);
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    timer_->setInterval(150);
    connect(timer_, &QTimer::timeout, this, [this] { preview(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &ResourceViewDialog::reject);
    connect(refresh, &QPushButton::clicked, this, [this] { reload(); });
    connect(query_, &QLineEdit::textChanged, this, [this] { changed(); });
    for (auto *box : groups_)
        connect(box, &QComboBox::currentIndexChanged, this, [this] { changed(); });
    connect(sort_, &QComboBox::currentIndexChanged, this, [this] { changed(); });
    connect(descending_, &QCheckBox::toggled, this, [this] { changed(); });
    connect(add, &QPushButton::clicked, this, [this] {
        if (filters_->rowCount() < 8)
            addFilter();
        changed();
    });
    connect(erase, &QPushButton::clicked, this, [this] {
        if (filters_->currentRow() >= 0)
            filters_->removeRow(filters_->currentRow());
        changed();
    });
    connect(saved_, &QComboBox::currentIndexChanged, this, [this] {
        if (loading_ || !source_)
            return;
        const auto id = saved_->currentData().toString().toStdString();
        if (id.empty()) {
            name_->clear();
            controls();
            return;
        }
        for (const auto &v : source_->savedResourceViews)
            if (v.id == id) {
                name_->setText(q(v.name));
                fill(v.settings);
                break;
            }
    });
    connect(save_, &QPushButton::clicked, this, [this] {
        if (!current() || doc_.readOnly())
            return;
        try {
            auto settings = read();
            validateResourceView(*source_, settings);
            committing_ = true;
            const auto id = doc_.putSavedResourceView(
                {saved_->currentData().toString().toStdString(), name_->text().trimmed().toStdString(), settings});
            committing_ = false;
            source_ = doc_.snapshot();
            session_ = doc_.sessionGeneration();
            savedChoices(id);
            changed();
        } catch (const std::exception &e) {
            committing_ = false;
            showEditorError(*message_, e);
        }
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        if (!current() || doc_.readOnly())
            return;
        try {
            committing_ = true;
            doc_.removeSavedResourceView(saved_->currentData().toString().toStdString());
            committing_ = false;
            source_ = doc_.snapshot();
            session_ = doc_.sessionGeneration();
            savedChoices();
            name_->clear();
            changed();
        } catch (const std::exception &e) {
            committing_ = false;
            showEditorError(*message_, e);
        }
    });
    connect(xlsx_, &QPushButton::clicked, this, [this] { exportView(false); });
    connect(pdf_, &QPushButton::clicked, this, [this] { exportView(true); });
    listener_ = doc_.subscribe([this](Document::Event) {
        if (committing_ || current())
            return;
        ++generation_;
        timer_->stop();
        if (control_)
            control_->cancel();
        displayed_.reset();
        message_->setText("문서·권한이 변경되었습니다. ‘최신 문서로 다시 열기’를 눌러 다시 조회하세요.");
        controls();
    });
    reload();
    resize(1200, 800);
    prepareEditorDialog(*this);
}
ResourceViewDialog::~ResourceViewDialog() {
    if (control_)
        control_->cancel();
    doc_.unsubscribe(listener_);
}
void ResourceViewDialog::reject() {
    ++generation_;
    timer_->stop();
    if (control_)
        control_->cancel();
    QDialog::reject();
}
bool ResourceViewDialog::current() const {
    return source_ && doc_.project() && doc_.sessionGeneration() == session_ &&
           doc_.project()->documentId == source_->documentId && doc_.project()->revision == source_->revision;
}
void ResourceViewDialog::controls() {
    save_->setEnabled(current() && !doc_.readOnly());
    remove_->setEnabled(current() && !doc_.readOnly() && !saved_->currentData().toString().isEmpty());
    xlsx_->setEnabled(current() && displayed_.has_value());
    pdf_->setEnabled(xlsx_->isEnabled());
}
void ResourceViewDialog::savedChoices(const std::string &id) {
    QSignalBlocker block(saved_);
    saved_->clear();
    saved_->addItem("새 보기", QString{});
    if (source_)
        for (const auto &v : source_->savedResourceViews)
            saved_->addItem(q(v.name), q(v.id));
    saved_->setCurrentIndex(std::max(0, saved_->findData(q(id))));
    controls();
}
void ResourceViewDialog::reload() {
    ++generation_;
    if (control_)
        control_->cancel();
    timer_->stop();
    source_ = doc_.snapshot();
    session_ = doc_.sessionGeneration();
    loading_ = true;
    name_->clear();
    if (source_) {
        for (auto *g : groups_)
            keys(g, *source_, true);
        keys(sort_, *source_);
    }
    savedChoices();
    loading_ = false;
    fill({});
}
void ResourceViewDialog::addFilter(const AttributeFilter &f) {
    if (!source_)
        return;
    const int row = filters_->rowCount();
    filters_->insertRow(row);
    auto *key = new QComboBox(filters_);
    keys(key, *source_);
    key->setCurrentIndex(std::max(0, key->findData(q(f.key))));
    filters_->setCellWidget(row, 0, key);
    auto *op = new QComboBox(filters_);
    for (const auto &v : std::vector<std::pair<QString, QString>>{{"같음", "equals"},
                                                                  {"표시 이름 포함", "contains"},
                                                                  {"미설정", "missing"},
                                                                  {"설정됨", "present"},
                                                                  {"작음", "lt"},
                                                                  {"이하", "le"},
                                                                  {"큼", "gt"},
                                                                  {"이상", "ge"}})
        op->addItem(v.first, v.second);
    op->setCurrentIndex(std::max(0, op->findData(q(f.operation))));
    filters_->setCellWidget(row, 1, op);
    auto *value = new QLineEdit(filters_);
    value->setText(displayValue(*source_, f));
    value->setProperty("originalKey", q(f.key));
    value->setProperty("originalOperation", q(f.operation));
    value->setProperty("originalText", value->text());
    value->setProperty("originalValue", q(f.value));
    filters_->setCellWidget(row, 2, value);
    auto change = [this, op, value] {
        value->setEnabled(op->currentData() != "missing" && op->currentData() != "present");
        changed();
    };
    connect(key, &QComboBox::currentIndexChanged, this, [this, value] {
        value->clear();
        changed();
    });
    connect(op, &QComboBox::currentIndexChanged, this, change);
    connect(value, &QLineEdit::textChanged, this, [this] { changed(); });
    value->setEnabled(f.operation != "missing" && f.operation != "present");
}
ResourceViewSettings ResourceViewDialog::read() const {
    ResourceViewSettings v;
    v.query = query_->text().toStdString();
    v.sortKey = sort_->currentData().toString().toStdString();
    v.descending = descending_->isChecked();
    for (auto *g : groups_)
        if (!g->currentData().toString().isEmpty())
            v.groupBy.push_back(g->currentData().toString().toStdString());
    for (int i = 0; i < filters_->rowCount(); ++i) {
        auto key = static_cast<QComboBox *>(filters_->cellWidget(i, 0))->currentData().toString().toStdString();
        auto op = static_cast<QComboBox *>(filters_->cellWidget(i, 1))->currentData().toString().toStdString();
        const auto *editor = static_cast<QLineEdit *>(filters_->cellWidget(i, 2));
        const auto value = editor->text();
        const bool retained = editor->property("originalKey").toString() == q(key) &&
                              editor->property("originalOperation").toString() == q(op) &&
                              editor->property("originalText").toString() == value;
        v.filters.push_back({key, op,
                             retained ? editor->property("originalValue").toString().toStdString()
                                      : filterValue(*source_, key, op, value)});
    }
    return v;
}
void ResourceViewDialog::fill(const ResourceViewSettings &v) {
    loading_ = true;
    query_->setText(q(v.query));
    sort_->setCurrentIndex(std::max(0, sort_->findData(q(v.sortKey))));
    descending_->setChecked(v.descending);
    for (int i = 0; i < 3; ++i)
        groups_[i]->setCurrentIndex(i < int(v.groupBy.size()) ? std::max(0, groups_[i]->findData(q(v.groupBy[i]))) : 0);
    filters_->setRowCount(0);
    for (const auto &f : v.filters)
        addFilter(f);
    loading_ = false;
    changed();
}
void ResourceViewDialog::changed() {
    if (loading_)
        return;
    ++generation_;
    if (control_)
        control_->cancel();
    displayed_.reset();
    controls();
    if (current()) {
        message_->setText("자원 보기 계산 중…");
        timer_->start();
    }
}
void ResourceViewDialog::preview() {
    if (!current())
        return;
    ResourceViewSettings settings;
    try {
        settings = read();
        validateResourceView(*source_, settings);
    } catch (const std::exception &e) {
        showEditorError(*message_, e);
        return;
    }
    const auto generation = generation_;
    control_ = std::make_shared<CalculationControl>();
    auto *watcher = new QFutureWatcher<Projection>(this);
    connect(watcher, &QFutureWatcher<Projection>::finished, this, [this, watcher, generation, settings] {
        auto out = watcher->result();
        watcher->deleteLater();
        if (generation != generation_ || !current())
            return;
        if (!out.error.isEmpty()) {
            message_->setText(out.error);
            return;
        }
        QString selected;
        if (tree_->currentItem())
            selected = tree_->currentItem()->data(0, Qt::UserRole).toString();
        tree_->setUpdatesEnabled(false);
        tree_->clear();
        std::vector<std::string> columns{"name", "id", "type", "capacity", "calendar", "assigned"};
        const auto fields = attributeKeys(*source_, FieldScope::Resource);
        columns.insert(columns.end(), fields.begin(), fields.end());
        QStringList headers;
        for (const auto &key : columns)
            headers << q(resourceViewName(*source_, key));
        tree_->setHeaderLabels(headers);
        std::unordered_map<std::string, QTreeWidgetItem *> nodes;
        EvaluationScope evaluation;
        XmlScheduleScope formulaSchedule(*source_, out.schedule);
        std::unordered_set<std::string> assigned;
        for (const auto &a : source_->assignments)
            assigned.insert(a.resourceId);
        std::size_t errors = 0;
        for (const auto &row : out.rows) {
            auto *parent = tree_->invisibleRootItem();
            std::string path;
            for (std::size_t i = 0; i < row.groups.size(); ++i) {
                const auto &g = row.groups[i];
                path += std::to_string(g.token.size()) + ":" + g.token;
                auto [it, created] = nodes.emplace(path, nullptr);
                if (created) {
                    it->second = new QTreeWidgetItem(parent);
                    if (out.summaries.contains(path))
                        for (std::size_t c = 0; c < columns.size(); ++c)
                            if (out.summaries.at(path).contains(columns[c])) {
                                const auto &cell = out.summaries.at(path).at(columns[c]);
                                it->second->setText(int(c), cell.first);
                                it->second->setToolTip(int(c), cell.second);
                            }

                    it->second->setData(0, Qt::UserRole + 1,
                                        q(resourceViewName(*source_, settings.groupBy[i]) + " · " + g.label));
                }
                parent = it->second;
                const int count = parent->data(0, Qt::UserRole + 2).toInt() + 1;
                parent->setData(0, Qt::UserRole + 2, count);
            }
            const auto &r = source_->resources[row.resourceIndex];
            auto *leaf = new QTreeWidgetItem(parent);
            leaf->setData(0, Qt::UserRole, q(r.id));
            const auto computed = formulaValues(*source_, FieldScope::Resource, nullptr, &r);
            for (int c = 0; c < int(columns.size()); ++c) {
                const auto it = computed->find(columns[c]);
                if (it != computed->end()) {
                    auto raw = formulaRawValue(it->second);
                    leaf->setText(c, raw ? q(attributeText(*source_, columns[c], *raw, FieldScope::Resource))
                                         : q(formulaValueText(it->second)));
                    if (!formulaRawValue(it->second) && !it->second.missing)
                        ++errors;
                    continue;
                }
                const auto value = columns[c] == "assigned"
                                       ? std::optional<std::string>{assigned.contains(r.id) ? "yes" : "no"}
                                       : resourceViewValue(*source_, r, columns[c]);
                leaf->setText(c, value ? q(resourceViewText(*source_, columns[c], *value)) : "(미설정)");
            }
            if (q(r.id) == selected) {
                tree_->setCurrentItem(leaf);
                leaf->setSelected(true);
            }
        }
        for (const auto &[path, node] : nodes) {
            (void)path;
            node->setText(0, node->data(0, Qt::UserRole + 1).toString() +
                                 QString(" (%1)").arg(node->data(0, Qt::UserRole + 2).toInt()));
        }
        tree_->expandToDepth(2);
        tree_->setColumnWidth(0, 260);
        tree_->setUpdatesEnabled(true);
        displayed_ = settings;
        message_->setText(
            QString("%1 / %2 자원 · %3개 그룹 · %4 · 계산 오류 %5개")
                .arg(out.rows.size())
                .arg(source_->resources.size())
                .arg(nodes.size())
                .arg(doc_.readOnly() ? "읽기 전용 · 조회/출력 가능" : "보기 저장 가능 · 계산 입력은 변경하지 않음")
                .arg(errors));
        controls();
    });
    watcher->setFuture(QtConcurrent::run([source = source_, settings, control = control_] {
        Projection out;
        try {
            EvaluationScope scheduleEvaluation(control);
            ScheduleFormulaScope formulaSchedule(*source);
            out.schedule = formulaSchedule.values();
            out.rows = projectResources(*source, settings, control);
            EvaluationScope evaluation(control);
            const auto groups = resourceRollUpGroups(out.rows);
            const RollUpValues values(*source, FieldScope::Resource, groups);
            for (const auto &g : groups)
                for (const auto *f : values.fields()) {
                    const auto key = "field:" + f->id;
                    const auto &cell = values.cell(g.id, key);
                    out.summaries[g.id][key] = {
                        cell.value ? q(attributeText(*source, key, *cell.value, FieldScope::Resource)) : "미설정",
                        q(cell.origin + " · " + rollUpMethodName(f->xmlRollUp->rollupType) + " · " + cell.issue)};
                }

        } catch (const CalculationCancelled &) {
            out.error = "조회 취소";
        } catch (const std::exception &e) {
            out.error = qErrorText(e);
        }
        return out;
    }));
}
void ResourceViewDialog::exportView(bool pdf) {
    if (!current() || !displayed_)
        return;
    auto path = QFileDialog::getSaveFileName(this, pdf ? "자원 보기 PDF" : "자원 보기 XLSX", {},
                                             pdf ? "PDF (*.pdf)" : "Excel (*.xlsx)");
    if (path.isEmpty())
        return;
    try {
        const auto report = resourceViewReport(*source_, *displayed_,
                                               {{},
                                                {},
                                                "budget-remaining-v1",
                                                {},
                                                {},
                                                QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(),
                                                QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString(),
                                                doc_.dirty()});
        if (pdf)
            exportPdf(report, path, ReportFamily::Resources);
        else
            exportXlsx(report, path, ReportFamily::Resources);
        message_->setText("현재 자원 보기 조건과 같은 고유 자원 행을 내보냈습니다.");
    } catch (const std::exception &e) {
        showEditorError(*message_, e);
    }
}
} // namespace jobs
