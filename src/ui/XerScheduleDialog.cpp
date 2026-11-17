#include "ui/XerScheduleDialog.h"
#include "ui/EditorUi.h"
#include "ui/XerProjectDialog.h"
#include "domain/XerScheduleAdoption.h"
#include "persistence/XerScheduleImport.h"
#include "formatting/DiagnosticText.h"
#include "formatting/PlanTokens.h"
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <map>
namespace jobs {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
class Links final : public QAbstractTableModel {
    std::vector<XerScheduleLink> links_;
    std::vector<QString> names_;
    std::map<std::string, QString> targets_;

public:
    using QAbstractTableModel::QAbstractTableModel;
    std::function<void()> changed;
    void reset(const PlanInput &a, const PlanInput &b) {
        beginResetModel();
        links_ = suggestXerScheduleLinks(a, b);
        names_.clear();
        targets_.clear();
        for (const auto &t : a.tasks)
            names_.push_back(q(t.name));
        for (const auto &t : b.tasks)
            targets_[t.id] = q(t.name);
        endResetModel();
    }
    const auto &links() const {
        return links_;
    }
    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : int(links_.size());
    }
    int columnCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : 4;
    }
    QVariant headerData(int c, Qt::Orientation o, int role) const override {
        return role == Qt::DisplayRole && o == Qt::Horizontal
                   ? QVariant(QStringList{"원본 작업 ID", "원본 작업", "채택 대상", "현재 작업 ID"}.at(c))
                   : QVariant{};
    }
    QVariant data(const QModelIndex &i, int role) const override {
        if (!i.isValid())
            return {};
        const auto &l = links_.at(i.row());
        if (role == Qt::EditRole)
            return l.create ? QString("new") : l.targetId.empty() ? QString{} : "id:" + q(l.targetId);
        if (role != Qt::DisplayRole && role != Qt::ToolTipRole)
            return {};
        return QStringList{q(l.sourceId), names_.at(i.row()),
                           l.create             ? "새 작업 추가"
                           : l.targetId.empty() ? "제외 / 현재 작업 보존"
                                                : targets_.at(l.targetId),
                           q(l.targetId)}
            .at(i.column());
    }
    Qt::ItemFlags flags(const QModelIndex &i) const override {
        return QAbstractTableModel::flags(i) | (i.column() == 2 ? Qt::ItemIsEditable : Qt::NoItemFlags);
    }
    bool setData(const QModelIndex &i, const QVariant &v, int role) override {
        if (!i.isValid() || i.column() != 2 || role != Qt::EditRole)
            return false;
        const auto token = v.toString();
        const auto target = token.startsWith("id:") ? token.mid(3).toStdString() : std::string{};
        if ((!target.empty() && !targets_.contains(target)) ||
            (!token.isEmpty() && token != "new" && !token.startsWith("id:")))
            return false;
        auto &l = links_.at(i.row());
        l.create = token == "new";
        l.targetId = target;
        emit dataChanged(index(i.row(), 2), index(i.row(), 3));
        if (changed)
            changed();
        return true;
    }
};
class Delegate final : public QStyledItemDelegate {
    QStandardItemModel choices_;

public:
    Delegate(const PlanInput &p, QObject *parent) : QStyledItemDelegate(parent) {
        auto add = [&](const QString &name, const QString &token) {
            auto *i = new QStandardItem(name);
            i->setData(token, Qt::UserRole);
            choices_.appendRow(i);
        };
        add("제외 / 현재 작업 보존", {});
        add("새 작업 추가", "new");
        for (const auto &t : p.tasks)
            add(q(t.name) + " · " + q(t.id), "id:" + q(t.id));
    }
    QWidget *createEditor(QWidget *p, const QStyleOptionViewItem &, const QModelIndex &) const override {
        auto *c = new QComboBox(p);
        c->setModel(const_cast<QStandardItemModel *>(&choices_));
        return c;
    }
    void setEditorData(QWidget *w, const QModelIndex &i) const override {
        auto *c = qobject_cast<QComboBox *>(w);
        c->setCurrentIndex(c->findData(i.data(Qt::EditRole)));
    }
    void setModelData(QWidget *w, QAbstractItemModel *m, const QModelIndex &i) const override {
        m->setData(i, qobject_cast<QComboBox *>(w)->currentData(), Qt::EditRole);
    }
};
class Rows final : public QAbstractTableModel {
    std::vector<QStringList> rows_;

public:
    Rows(std::vector<QStringList> rows, QObject *p) : QAbstractTableModel(p), rows_(std::move(rows)) {}
    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : int(rows_.size());
    }
    int columnCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : 16;
    }
    QVariant data(const QModelIndex &i, int role) const override {
        return i.isValid() && (role == Qt::DisplayRole || role == Qt::ToolTipRole)
                   ? QVariant(rows_.at(i.row()).at(i.column()))
                   : QVariant{};
    }
    QVariant headerData(int c, Qt::Orientation o, int role) const override {
        return role == Qt::DisplayRole && o == Qt::Horizontal
                   ? QVariant(QStringList{"현재 작업 ID", "작업", "이전 기간(분)", "채택 기간(분)", "이전 시작",
                                          "채택 시작", "이전 완료", "채택 완료", "이전 WBS", "채택 WBS", "이전 달력",
                                          "채택 달력", "이전 선행", "채택 선행", "이전 유형/공수", "채택 유형/공수"}
                                  .at(c))
                   : QVariant{};
    }
};
std::vector<QStringList> previewRows(const PlanInput &before, const PlanInput &after) {
    auto project = [](const PlanInput &p) {
        Project v;
        static_cast<PlanInput &>(v) = p;
        v.documentId = "schedule-preview";
        return v;
    };
    const auto a = calculatePlan(project(before)), b = calculatePlan(project(after));
    std::map<std::string, const Task *> old;
    std::map<std::string, TaskSchedule> da, db;
    std::map<std::string, QString> wa, wb, ca, cb;
    for (const auto &t : before.tasks)
        old[t.id] = &t;
    if (a.status == CalculationStatus::Valid)
        for (const auto &t : a.tasks)
            da[t.taskId] = t;
    if (b.status == CalculationStatus::Valid)
        for (const auto &t : b.tasks)
            db[t.taskId] = t;
    for (const auto &w : before.wbsNodes)
        wa[w.id] = q(w.name);
    for (const auto &w : after.wbsNodes)
        wb[w.id] = q(w.name);
    for (const auto &c : before.calendars)
        ca[c.id] = q(c.name);
    for (const auto &c : after.calendars)
        cb[c.id] = q(c.name);
    auto duration = [](const Task *t) {
        return !t ? QString("새 작업") : t->durationMinutes ? QString::number(*t->durationMinutes) : QString("미정");
    };
    auto deps = [](const Task *t) {
        QStringList out;
        if (t)
            for (const auto &e : dependencies(*t))
                out << q(e.predecessorId) + " " + q(enumToken(e.type)) + " " + QString::number(e.lagMinutes);
        return out.join(" / ");
    };
    auto effort = [](const Task *t) {
        return t ? q(enumToken(t->taskType)) + " / " +
                       (t->workUnitMinutes ? QString::number(*t->workUnitMinutes) + "u" : "미정")
                 : QString{};
    };
    std::vector<QStringList> out;
    for (const auto &t : after.tasks) {
        calculationCheckpoint();
        const auto *x = old.contains(t.id) ? old.at(t.id) : nullptr;
        out.push_back({q(t.id), q(t.name), duration(x), duration(&t),
                       da.contains(t.id) ? q(da.at(t.id).start) : "미정/새 작업",
                       db.contains(t.id) ? q(db.at(t.id).start) : "미정",
                       da.contains(t.id) ? q(da.at(t.id).finish) : "미정/새 작업",
                       db.contains(t.id) ? q(db.at(t.id).finish) : "미정", x ? wa[x->wbsId] : QString{}, wb[t.wbsId],
                       x ? ca[x->calendarId] : QString{}, cb[t.calendarId], deps(x), deps(&t), effort(x), effort(&t)});
    }
    return out;
}
struct Read {
    QByteArray bytes;
    std::vector<XerProjectInfo> projects;
    std::optional<XerScheduleSource> source;
    QString error;
};
class Details final : public QAbstractTableModel {
    std::vector<QStringList> rows_;

public:
    Details(std::vector<QStringList> rows, QObject *p) : QAbstractTableModel(p), rows_(std::move(rows)) {}
    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : int(rows_.size());
    }
    int columnCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : 7;
    }
    QVariant data(const QModelIndex &i, int role) const override {
        return i.isValid() && (role == Qt::DisplayRole || role == Qt::ToolTipRole)
                   ? QVariant(rows_.at(i.row()).at(i.column()))
                   : QVariant{};
    }
    QVariant headerData(int c, Qt::Orientation o, int role) const override {
        return role == Qt::DisplayRole && o == Qt::Horizontal
                   ? QVariant(QStringList{"대상", "기준", "ID", "이름", "부모 / 근무 요일", "근무 구간", "휴일"}.at(c))
                   : QVariant{};
    }
};
std::vector<QStringList> structureRows(const PlanInput &before, const PlanInput &after) {
    std::vector<QStringList> rows;
    for (const auto &[scope, input] : {std::pair{"이전", &before}, std::pair{"채택", &after}}) {
        QStringList holidays;
        for (const auto &h : input->holidays)
            holidays << q(h);
        rows.push_back(
            {"기본 달력", scope, {}, "월~금 8시간", "1, 2, 3, 4, 5", "09:00~12:00 / 13:00~18:00", holidays.join(", ")});
        for (const auto &w : input->wbsNodes)
            rows.push_back({"WBS", scope, q(w.id), q(w.name), q(w.parentId), {}, {}});
        for (const auto &c : input->calendars) {
            calculationCheckpoint();
            QStringList days, periods, dates;
            for (auto d : c.weekdays)
                days << QString::number(d);
            auto stamp = [](int n) {
                return QString("%1:%2").arg(n / 60, 2, 10, QChar('0')).arg(n % 60, 2, 10, QChar('0'));
            };
            for (const auto &p : c.periods)
                periods << stamp(p.begin) + "~" + stamp(p.end);
            for (const auto &d : c.holidays)
                dates << q(d);
            rows.push_back({"달력", scope, q(c.id), q(c.name), days.join(", "), periods.join(" / "), dates.join(", ")});
        }
    }
    return rows;
}
struct Preview {
    std::shared_ptr<Document::PreparedEdit> edit;
    std::vector<QStringList> rows;
    std::vector<QStringList> structure;
    QString summary, error;
};
} // namespace
struct XerScheduleDialog::Impl {
    XerScheduleDialog &dialog;
    Document &doc;
    std::shared_ptr<const Project> source;
    std::uint64_t session, listener = 0;
    bool busy = false, committing = false;
    std::shared_ptr<CalculationControl> control;
    std::optional<XerScheduleSource> loaded;
    std::shared_ptr<Document::PreparedEdit> prepared;
    QComboBox *encoding;
    QCheckBox *schedule, *wbs, *calendars, *confirm;
    QLineEdit *reporter, *reason;
    QLabel *info, *message;
    QPushButton *open, *analyze, *submit, *cancel, *report;
    QTabWidget *tabs;
    QTableView *mapping, *preview, *structure;
    Links *links;
    Impl(XerScheduleDialog &d, Document &v) : dialog(d), doc(v), source(v.snapshot()), session(v.sessionGeneration()) {
        auto *layout = new QVBoxLayout(&dialog);
        info = new QLabel("새 XER의 작업을 현재 작업에 연결한 뒤 일정·WBS·달력 채택 범위를 선택하세요.\n현재에만 있는 "
                          "작업·배정·원가·관측 원장·기준선은 보존합니다. 새 작업은 명시적으로 추가하며, 원본 "
                          "실적·상태일·통화·단가를 가져오지 않습니다.",
                          &dialog);
        info->setObjectName("xerScheduleSource");
        info->setTextFormat(Qt::PlainText);
        info->setWordWrap(true);
        layout->addWidget(info);
        auto *file = new QHBoxLayout;
        encoding = new QComboBox(&dialog);
        encoding->addItems({"UTF-8", "Windows-1252"});
        file->addWidget(encoding);
        open = new QPushButton("새 XER 버전 선택…", &dialog);
        open->setObjectName("xerScheduleOpen");
        file->addWidget(open);
        report = new QPushButton("원본 교환 진단…", &dialog);
        report->setObjectName("xerScheduleReport");
        file->addWidget(report);
        file->addStretch();
        layout->addLayout(file);
        auto *options = new QHBoxLayout;
        schedule = new QCheckBox("일정·작업명·계획 공수/유형·프로젝트 시작일", &dialog);
        schedule->setObjectName("xerScheduleTiming");
        wbs = new QCheckBox("WBS 계층·작업 소속", &dialog);
        wbs->setObjectName("xerScheduleWbs");
        calendars = new QCheckBox("작업 달력·프로젝트 기본 달력/휴일", &dialog);
        calendars->setObjectName("xerScheduleCalendars");
        for (auto *c : {schedule, wbs, calendars}) {
            c->setChecked(true);
            options->addWidget(c);
        }
        layout->addLayout(options);
        auto *note = new QLabel("기존 WBS·달력은 보존하고 채택본을 새 ID로 추가합니다. 기본 달력·휴일은 로컬 작업에도 "
                                "적용됩니다. 기존 배정 구간과 맞지 않는 기간, 누락 선행·순환·달력 충돌은 거절합니다. "
                                "채택 시 평준화 선택은 해제되며, 계획 날짜를 다시 계산합니다.",
                                &dialog);
        note->setWordWrap(true);
        layout->addWidget(note);
        tabs = new QTabWidget(&dialog);
        tabs->setObjectName("xerScheduleTabs");
        layout->addWidget(tabs, 1);
        mapping = new QTableView(tabs);
        mapping->setObjectName("xerScheduleLinks");
        links = new Links(mapping);
        auto *proxy = new QSortFilterProxyModel(mapping);
        proxy->setSourceModel(links);
        proxy->setFilterKeyColumn(-1);
        proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
        mapping->setModel(proxy);
        auto *search = new QLineEdit(&dialog);
        search->setObjectName("xerScheduleSearch");
        search->setPlaceholderText("원본·현재 작업 이름 또는 ID 검색");
        layout->insertWidget(2, search);
        QObject::connect(search, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
        if (source)
            mapping->setItemDelegateForColumn(2, new Delegate(*source, mapping));
        mapping->horizontalHeader()->setDefaultSectionSize(230);
        tabs->addTab(mapping, "원본 작업 연결");
        preview = new QTableView(tabs);
        preview->setObjectName("xerSchedulePreview");
        preview->horizontalHeader()->setDefaultSectionSize(155);
        tabs->addTab(preview, "일정·소속·달력 전후");
        structure = new QTableView(tabs);
        structure->setObjectName("xerScheduleStructure");
        structure->horizontalHeader()->setDefaultSectionSize(190);
        tabs->addTab(structure, "WBS 계층·달력 정의 전후");
        auto *history = new QTableWidget(source ? int(source->xerScheduleAdoptions.size()) : 0, 5, tabs);
        history->setObjectName("xerScheduleHistory");
        history->setHorizontalHeaderLabels({"기록 UTC", "원본", "작성자", "사유", "이력 ID"});
        history->setEditTriggers(QAbstractItemView::NoEditTriggers);
        history->horizontalHeader()->setDefaultSectionSize(190);
        if (source)
            for (std::size_t i = 0; i < source->xerScheduleAdoptions.size(); ++i) {
                const auto &e = source->xerScheduleAdoptions[i];
                const QStringList values{q(e.recordedAt), q(e.source.sourceName), q(e.reporter), q(e.reason), q(e.id)};
                for (int col = 0; col < 5; ++col)
                    history->setItem(int(i), col, new QTableWidgetItem(values[col]));
            }
        tabs->addTab(history, "채택 이력 · 두 번 눌러 조회");
        auto *form = new QFormLayout;
        reporter = new QLineEdit(&dialog);
        reporter->setObjectName("xerScheduleReporter");
        reason = new QLineEdit(&dialog);
        reason->setObjectName("xerScheduleReason");
        reporter->setMaxLength(1024);
        reason->setMaxLength(4096);
        form->addRow("작성자", reporter);
        form->addRow("채택 사유", reason);
        layout->addLayout(form);
        confirm = new QCheckBox("원본 교환 진단·작업 연결·채택 범위·전후 결과와 보존 범위를 확인했습니다.", &dialog);
        confirm->setObjectName("xerScheduleConfirm");
        layout->addWidget(confirm);
        message = new QLabel(&dialog);
        message->setObjectName("xerScheduleMessage");
        message->setTextFormat(Qt::PlainText);
        message->setWordWrap(true);
        layout->addWidget(message);
        auto *buttons = new QHBoxLayout;
        analyze = new QPushButton("채택 검증·미리보기", &dialog);
        analyze->setObjectName("xerScheduleAnalyze");
        submit = new QPushButton("검증한 채택 제출", &dialog);
        submit->setObjectName("xerScheduleSubmit");
        cancel = new QPushButton("작업 취소", &dialog);
        cancel->setObjectName("xerScheduleCancel");
        auto *close = new QPushButton("닫기", &dialog);
        for (auto *b : {analyze, submit, cancel})
            buttons->addWidget(b);
        buttons->addStretch();
        buttons->addWidget(close);
        layout->addLayout(buttons);
        auto invalidate = [this] {
            prepared.reset();
            confirm->setChecked(false);
            controls();
        };
        links->changed = invalidate;
        for (auto *c : {schedule, wbs, calendars})
            QObject::connect(c, &QCheckBox::toggled, &dialog, invalidate);
        for (auto *e : {reporter, reason})
            QObject::connect(e, &QLineEdit::textChanged, &dialog, invalidate);
        QObject::connect(confirm, &QCheckBox::toggled, &dialog, [this] { controls(); });
        QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
        QObject::connect(cancel, &QPushButton::clicked, &dialog, [this] {
            if (control)
                control->cancel();
        });
        QObject::connect(open, &QPushButton::clicked, &dialog, [this] {
            const auto path = QFileDialog::getOpenFileName(&dialog, "새 XER 일정", {}, "Primavera XER (*.xer)");
            if (!path.isEmpty())
                load(path, encoding->currentIndex() ? XerEncoding::Windows1252 : XerEncoding::Utf8);
        });
        QObject::connect(analyze, &QPushButton::clicked, &dialog, [this] { prepare(); });
        QObject::connect(submit, &QPushButton::clicked, &dialog, [this] {
            if (!editable() || busy || !prepared || !confirm->isChecked())
                return;
            committing = true;
            const auto state = doc.applyPreparedEdit(*prepared);
            committing = false;
            prepared.reset();
            if (state == EditState::Applied)
                dialog.accept();
            else {
                message->setText("취소·권한·문서 변경으로 제출하지 않았습니다.");
                controls();
            }
        });
        QObject::connect(report, &QPushButton::clicked, &dialog, [this] {
            if (!loaded)
                return;
            QDialog child(&dialog);
            child.setWindowTitle("원본 XER 교환 진단");
            auto *l = new QVBoxLayout(&child);
            auto *text = new QPlainTextEdit(q(loaded->reportJson), &child);
            text->setReadOnly(true);
            l->addWidget(text);
            auto *b = new QDialogButtonBox(QDialogButtonBox::Close, &child);
            QObject::connect(b, &QDialogButtonBox::rejected, &child, &QDialog::reject);
            l->addWidget(b);
            child.resize(850, 600);
            prepareEditorDialog(child);
            child.exec();
        });
        QObject::connect(history, &QTableWidget::cellDoubleClicked, &dialog, [this](int row, int) {
            if (!source || row < 0 || busy)
                return;
            const auto e = source->xerScheduleAdoptions.at(row);
            loaded.reset();
            links->reset(PlanInput{}, *source);
            info->setText("채택 이력 원본 " + q(e.source.sourceName) + " · 프로젝트 [" + q(e.source.projectId) +
                          "]\nSHA256 " + q(e.source.sourceSha256) + "\n작성자 " + q(e.reporter) + " · " +
                          q(e.recordedAt));
            prepared.reset();
            confirm->setChecked(false);
            busy = true;
            control = std::make_shared<CalculationControl>();
            controls();
            auto *watcher = new QFutureWatcher<std::shared_ptr<Preview>>(&dialog);
            connectPreview(watcher);
            watcher->setFuture(QtConcurrent::run([e, c = control] {
                auto r = std::make_shared<Preview>();
                try {
                    EvaluationScope run(c);
                    r->rows = previewRows(e.before, e.after);
                    r->structure = structureRows(e.before, e.after);
                    r->summary = "저장 이력 · " + q(e.source.sourceName) + " · " + q(e.reason);
                } catch (const CalculationCancelled &) {
                    r->error = "취소했습니다.";
                } catch (const std::exception &x) {
                    r->error = q(errorText(x));
                }
                return r;
            }));
        });
        listener = doc.subscribe([this](Document::Event) {
            if (!committing && !current()) {
                if (control)
                    control->cancel();
                prepared.reset();
                message->setText("문서·권한이 바뀌었습니다. 창을 다시 여세요.");
                controls();
            }
        });
        controls();
        dialog.resize(1120, 760);
        prepareEditorDialog(dialog);
    }
    ~Impl() {
        if (control)
            control->cancel();
        doc.unsubscribe(listener);
    }
    bool current() const {
        return source && doc.project() && doc.sessionGeneration() == session &&
               doc.project()->documentId == source->documentId && doc.project()->revision == source->revision;
    }
    bool editable() const {
        return current() && !doc.readOnly();
    }
    void controls() {
        const bool enabled = editable() && !busy;
        open->setEnabled(enabled);
        encoding->setEnabled(enabled);
        mapping->setEnabled(enabled && bool(loaded));
        for (auto *c : {schedule, wbs, calendars})
            c->setEnabled(enabled && bool(loaded));
        reporter->setEnabled(enabled);
        reason->setEnabled(enabled);
        analyze->setEnabled(enabled && bool(loaded));
        submit->setEnabled(enabled && prepared && confirm->isChecked());
        confirm->setEnabled(enabled && bool(prepared));
        cancel->setEnabled(busy);
        report->setEnabled(!busy && bool(loaded));
    }
    void connectPreview(QFutureWatcher<std::shared_ptr<Preview>> *watcher) {
        QObject::connect(watcher, &QFutureWatcher<std::shared_ptr<Preview>>::finished, &dialog, [this, watcher] {
            auto r = watcher->result();
            watcher->deleteLater();
            busy = false;
            if (current() && !control->cancelRequested.load() && r->error.isEmpty()) {
                auto *old = preview->model();
                preview->setModel(new Rows(std::move(r->rows), preview));
                if (old)
                    old->deleteLater();
                auto *oldStructure = structure->model();
                structure->setModel(new Details(std::move(r->structure), structure));
                if (oldStructure)
                    oldStructure->deleteLater();
                prepared = std::move(r->edit);
                message->setText(r->summary);
                tabs->setCurrentWidget(preview);
            } else {
                prepared.reset();
                message->setText(r->error.isEmpty() ? "취소되었거나 문서가 바뀌었습니다." : r->error);
            }
            controls();
        });
    }
    void prepare() {
        if (!editable() || busy || !loaded)
            return;
        try {
            XerScheduleSubmission s;
            s.source = *loaded;
            s.links = links->links();
            s.schedule = schedule->isChecked();
            s.wbs = wbs->isChecked();
            s.calendars = calendars->isChecked();
            s.confirmed = true;
            s.reporter = reporter->text().trimmed().toStdString();
            s.reason = reason->text().trimmed().toStdString();
            s.recordedAt = QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd'T'HH:mm:ss'Z'").toStdString();
            auto request = doc.makeEditRequest(XerScheduleEdit{std::move(s)});
            prepared.reset();
            confirm->setChecked(false);
            busy = true;
            control = std::make_shared<CalculationControl>();
            controls();
            message->setText("계획·달력·원장 참조와 날짜를 검증하고 있습니다…");
            auto *watcher = new QFutureWatcher<std::shared_ptr<Preview>>(&dialog);
            connectPreview(watcher);
            watcher->setFuture(QtConcurrent::run([request = std::move(request), p = source, c = control]() mutable {
                auto r = std::make_shared<Preview>();
                try {
                    EvaluationScope run(c);
                    r->edit = Document::prepareEdit(std::move(request), c);
                    if (r->edit->state() != EditState::Ready) {
                        r->error = r->edit->failure() ? q(diagnosticMessage(*r->edit->failure()))
                                                      : "취소되었거나 채택할 변경이 없습니다.";
                        return r;
                    }
                    const auto after = r->edit->previewProject();
                    r->rows = previewRows(*p, *after);
                    r->structure = structureRows(*p, *after);
                    r->summary = "채택 검증 완료 · 작업 " + QString::number(p->tasks.size()) + " → " +
                                 QString::number(after->tasks.size()) + " · WBS " +
                                 QString::number(p->wbsNodes.size()) + " → " + QString::number(after->wbsNodes.size()) +
                                 " · 달력 " + QString::number(p->calendars.size()) + " → " +
                                 QString::number(after->calendars.size()) + "\n프로젝트 시작일 " + q(p->startDate) +
                                 " → " + q(after->startDate) + " · 현재 관측 원장 보존";
                } catch (const CalculationCancelled &) {
                    r->error = "취소했습니다.";
                } catch (const std::exception &e) {
                    r->error = q(errorText(e));
                }
                return r;
            }));
        } catch (const std::exception &e) {
            showEditorError(*message, e);
        }
    }
    void load(QString path, XerEncoding charset) {
        if (!editable() || busy)
            return;
        loaded.reset();
        prepared.reset();
        confirm->setChecked(false);
        links->reset(PlanInput{}, *source);
        busy = true;
        control = std::make_shared<CalculationControl>();
        controls();
        message->setText("새 XER의 프로젝트 목록을 읽고 있습니다…");
        auto *watcher = new QFutureWatcher<std::shared_ptr<Read>>(&dialog);
        QObject::connect(
            watcher, &QFutureWatcher<std::shared_ptr<Read>>::finished, &dialog, [this, watcher, path, charset] {
                auto r = watcher->result();
                watcher->deleteLater();
                if (!current() || control->cancelRequested.load() || !r->error.isEmpty()) {
                    busy = false;
                    message->setText(r->error.isEmpty() ? "취소되었거나 문서가 바뀌었습니다." : r->error);
                    controls();
                    return;
                }
                const auto id = r->projects.size() == 1 ? std::optional<QString>{r->projects[0].id}
                                                        : chooseXerProject(&dialog, r->projects);
                if (!id) {
                    busy = false;
                    controls();
                    return;
                }
                auto *reader = new QFutureWatcher<std::shared_ptr<Read>>(&dialog);
                QObject::connect(reader, &QFutureWatcher<std::shared_ptr<Read>>::finished, &dialog, [this, reader] {
                    auto r = reader->result();
                    reader->deleteLater();
                    busy = false;
                    if (current() && !control->cancelRequested.load() && r->source) {
                        loaded = std::move(r->source);
                        links->reset(loaded->input, *source);
                        info->setText("원본 " + q(loaded->sourceName) + " · 프로젝트 " + q(loaded->input.name) + " [" +
                                      q(loaded->projectId) + "]\n원본 시작일 " + q(loaded->input.startDate) +
                                      " · 원본 상태일 " + q(loaded->statusAt.value_or("없음")) +
                                      " (현재 상태일 유지)\nSHA256 " + q(loaded->sourceSha256));
                        message->setText("원본 교환 진단과 작업 연결·채택 범위를 확인하고 미리보기를 실행하세요.");
                        tabs->setCurrentWidget(mapping);
                    } else
                        message->setText(r->error.isEmpty() ? "취소되었거나 문서가 바뀌었습니다." : r->error);
                    controls();
                });
                reader->setFuture(QtConcurrent::run(
                    [bytes = r->bytes, name = QFileInfo(path).fileName(), id = *id, charset, c = control] {
                        auto out = std::make_shared<Read>();
                        try {
                            EvaluationScope run(c);
                            out->source = importXerScheduleSource(bytes, name, charset, id);
                        } catch (const CalculationCancelled &) {
                            out->error = "취소했습니다.";
                        } catch (const std::exception &e) {
                            out->error = q(errorText(e));
                        }
                        return out;
                    }));
            });
        watcher->setFuture(QtConcurrent::run([path, charset, c = control] {
            auto r = std::make_shared<Read>();
            try {
                EvaluationScope run(c);
                r->bytes = readPrimaveraXer(path);
                r->projects = listPrimaveraXerProjects(r->bytes, charset);
            } catch (const CalculationCancelled &) {
                r->error = "취소했습니다.";
            } catch (const std::exception &e) {
                r->error = q(errorText(e));
            }
            return r;
        }));
    }
};
XerScheduleDialog::XerScheduleDialog(Document &d, QWidget *parent) : QDialog(parent) {
    setObjectName("xerScheduleDialog");
    setWindowTitle("새 XER 일정·WBS·달력 채택");
    impl_ = std::make_unique<Impl>(*this, d);
}
XerScheduleDialog::~XerScheduleDialog() = default;
void XerScheduleDialog::loadSource(const QString &path, XerEncoding encoding) {
    impl_->load(path, encoding);
}
void XerScheduleDialog::reject() {
    if (impl_->control)
        impl_->control->cancel();
    QDialog::reject();
}
} // namespace jobs
