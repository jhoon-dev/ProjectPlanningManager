#include "ui/XerSourceRelinkDialog.h"
#include "ui/XerFinancialMergeDialog.h"
#include "ui/XerConversionEditor.h"
#include "ui/XerProjectDialog.h"
#include "ui/EditorUi.h"
#include "persistence/XerSourceImport.h"
#include "formatting/DiagnosticText.h"
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>
#include <QtConcurrent>
namespace jobs {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
class LinksModel final : public QAbstractTableModel {
    struct Row {
        bool incoming;
        XerIdLink link;
        QString name;
    };
    std::vector<Row> rows_;
    std::map<std::pair<std::string, std::string>, QString> targetNames_;

public:
    std::function<void()> changed;
    explicit LinksModel(QObject *p) : QAbstractTableModel(p) {}
    void set(const Project &p, const XerRelinkAudit &audit) {
        beginResetModel();
        rows_.clear();
        targetNames_.clear();
        for (const auto &t : p.tasks)
            targetNames_[{"task", t.id}] = q(t.name);
        for (const auto &r : p.resources)
            targetNames_[{"resource", r.id}] = q(r.name);
        for (bool incoming : {true, false}) {
            const auto &s = incoming ? audit.incoming : *activeXerFinancials(p);
            const auto links = suggestXerLinks(s, p);
            std::map<std::pair<std::string, std::string>, QString> names;
            for (const auto &t : s.tasks)
                names[{"task", t.id}] = q(t.name);
            for (const auto &r : s.resources)
                names[{"resource", r.id}] = q(r.name);
            for (const auto &l : links)
                rows_.push_back({incoming, l, names.at({l.kind, l.sourceId})});
        }
        endResetModel();
    }
    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : int(rows_.size());
    }
    int columnCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : 6;
    }
    QVariant headerData(int c, Qt::Orientation o, int role) const override {
        if (role != Qt::DisplayRole)
            return {};
        return o == Qt::Vertical
                   ? QVariant(c + 1)
                   : QVariant(
                         QStringList{"기준", "종류", "원본 ID", "원본 이름", "현재 연결 대상", "연결 대상 ID"}.value(
                             c));
    }
    QVariant data(const QModelIndex &i, int role) const override {
        if (!i.isValid())
            return {};
        const auto &r = rows_.at(std::size_t(i.row()));
        if (role == Qt::UserRole)
            return q(r.link.kind);
        if (role == Qt::EditRole)
            return q(r.link.targetId);
        if (role != Qt::DisplayRole && role != Qt::ToolTipRole)
            return {};
        return QStringList{r.incoming ? "새 원본" : "이전 집계",
                           r.link.kind == "task" ? "작업" : "자원",
                           q(r.link.sourceId),
                           r.name,
                           r.link.targetId.empty() ? "미연결 / 관측 없는 항목 제외"
                                                   : targetNames_.at({r.link.kind, r.link.targetId}),
                           q(r.link.targetId)}
            .value(i.column());
    }
    Qt::ItemFlags flags(const QModelIndex &i) const override {
        return QAbstractTableModel::flags(i) | (i.column() == 4 ? Qt::ItemIsEditable : Qt::NoItemFlags);
    }
    bool setData(const QModelIndex &i, const QVariant &v, int role) override {
        if (role != Qt::EditRole || i.column() != 4)
            return false;
        auto &r = rows_.at(std::size_t(i.row()));
        const auto id = v.toString().toStdString();
        if (!id.empty() && !targetNames_.contains({r.link.kind, id}))
            return false;
        r.link.targetId = id;
        emit dataChanged(index(i.row(), 4), index(i.row(), 5));
        if (changed)
            changed();
        return true;
    }
    void apply(XerRelinkAudit &a) const {
        a.incomingLinks.clear();
        a.previousLinks.clear();
        for (const auto &r : rows_)
            (r.incoming ? a.incomingLinks : a.previousLinks).push_back(r.link);
    }
};
class LinkDelegate final : public QStyledItemDelegate {
    QStandardItemModel tasks_, resources_;

public:
    LinkDelegate(const Project &p, QObject *parent) : QStyledItemDelegate(parent) {
        for (auto *m : {&tasks_, &resources_}) {
            auto *item = new QStandardItem("미연결 / 관측 없는 항목 제외");
            item->setData(QString{}, Qt::UserRole);
            m->appendRow(item);
        }
        for (const auto &t : p.tasks) {
            auto *i = new QStandardItem(q(t.name) + " · " + q(t.id));
            i->setData(q(t.id), Qt::UserRole);
            tasks_.appendRow(i);
        }
        for (const auto &r : p.resources) {
            auto *i = new QStandardItem(q(r.name) + " · " + q(r.id));
            i->setData(q(r.id), Qt::UserRole);
            resources_.appendRow(i);
        }
    }
    QWidget *createEditor(QWidget *p, const QStyleOptionViewItem &, const QModelIndex &i) const override {
        auto *c = new QComboBox(p);
        c->setModel(const_cast<QStandardItemModel *>(i.data(Qt::UserRole) == "task" ? &tasks_ : &resources_));
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
struct LoadResult {
    QByteArray bytes;
    std::vector<XerProjectInfo> projects;
    std::optional<XerRelinkAudit> audit;
    QString error;
    bool cancelled = false;
};
} // namespace
struct XerSourceRelinkDialog::Impl {
    XerSourceRelinkDialog &dialog;
    Document &doc;
    std::shared_ptr<const Project> source;
    std::uint64_t session, listener = 0;
    std::optional<XerRelinkAudit> loaded;
    bool busy = false;
    std::shared_ptr<CalculationControl> control;
    LinksModel *links;
    QTableView *table;
    QLabel *label, *message;
    QCheckBox *confirm, *replace;
    QComboBox *encoding;
    QPushButton *open, *review, *cancel;
    QPushButton *preview;
    XerConversionEditor *incomingConversion, *previousConversion;
    Impl(XerSourceRelinkDialog &d, Document &p)
        : dialog(d), doc(p), source(std::make_shared<const Project>(p.project() ? *p.project() : Project{})),
          session(p.sessionGeneration()) {
        auto *layout = new QVBoxLayout(&dialog);
        label = new QLabel("새 XER를 선택하세요. 원본과 현재 원장은 바꾸지 않고 연결표와 새 원본을 이력으로 "
                           "등록합니다.\n같은 ID·유일한 동일 이름은 후보이며, 실제 연결 대상과 프로젝트를 확인해야 "
                           "합니다. 관측값이 있는 항목은 제외할 수 없습니다.",
                           &dialog);
        label->setObjectName("xerRelinkSource");
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        layout->addWidget(label);
        auto *fileRow = new QHBoxLayout;
        encoding = new QComboBox(&dialog);
        encoding->addItems({"UTF-8", "Windows-1252"});
        open = new QPushButton("새 XER 버전 선택…", &dialog);
        open->setObjectName("xerRelinkOpen");
        fileRow->addWidget(encoding);
        fileRow->addWidget(open);
        fileRow->addStretch();
        layout->addLayout(fileRow);
        auto *search = new QLineEdit(&dialog);
        search->setPlaceholderText("기준·작업·자원·ID·이름 검색");
        layout->addWidget(search);
        table = new QTableView(&dialog);
        table->setObjectName("xerRelinkLinks");
        links = new LinksModel(table);
        auto *proxy = new QSortFilterProxyModel(table);
        proxy->setSourceModel(links);
        proxy->setFilterKeyColumn(-1);
        proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
        table->setModel(proxy);
        table->setItemDelegateForColumn(4, new LinkDelegate(*source, table));
        table->horizontalHeader()->setDefaultSectionSize(175);
        const int widths[]{85, 65, 145, 145, 165, 220};
        for (int i = 0; i < 6; ++i)
            table->setColumnWidth(i, widths[i]);
        table->horizontalHeader()->setStretchLastSection(true);
        table->setWordWrap(false);
        layout->addWidget(table, 1);
        incomingConversion = new XerConversionEditor("새 원본 전환", "xerIncomingConversion", *source, &dialog);
        previousConversion = new XerConversionEditor("이전 집계 전환", "xerPreviousConversion", *source, &dialog);
        if (activeXerFinancials(*source))
            previousConversion->source(*activeXerFinancials(*source));
        layout->addWidget(incomingConversion);
        layout->addWidget(previousConversion);
        preview = new QPushButton("통화·자원 전환값 미리보기…", &dialog);
        preview->setObjectName("xerRelinkConversionPreview");
        layout->addWidget(preview);
        QObject::connect(search, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
        replace = new QCheckBox("새 원본의 빈 누계·빠진 기간을 미보고로 채택 (해제하면 이전 보존값 유지)", &dialog);
        replace->setObjectName("xerRelinkReplaceMissing");
        layout->addWidget(replace);
        confirm = new QCheckBox("후속 프로젝트·ID 연결·통화/공수 환산비와 전환 결과를 확인했습니다.", &dialog);
        confirm->setObjectName("xerRelinkConfirm");
        layout->addWidget(confirm);
        message = new QLabel(&dialog);
        message->setObjectName("xerRelinkMessage");
        message->setTextFormat(Qt::PlainText);
        message->setWordWrap(true);
        layout->addWidget(message);
        auto *buttons = new QHBoxLayout;
        review = new QPushButton("연결 검증·재무 병합 검토…", &dialog);
        review->setObjectName("xerRelinkReview");
        cancel = new QPushButton("읽기 취소", &dialog);
        cancel->setObjectName("xerRelinkCancel");
        auto *close = new QPushButton("닫기", &dialog);
        buttons->addWidget(review);
        buttons->addWidget(cancel);
        buttons->addStretch();
        buttons->addWidget(close);
        layout->addLayout(buttons);
        QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
        QObject::connect(cancel, &QPushButton::clicked, &dialog, [this] {
            if (control)
                control->cancel();
        });
        QObject::connect(open, &QPushButton::clicked, &dialog, [this] {
            const auto path = QFileDialog::getOpenFileName(&dialog, "새 XER 버전", {}, "Primavera XER (*.xer)");
            if (!path.isEmpty())
                load(path, encoding->currentIndex() ? XerEncoding::Windows1252 : XerEncoding::Utf8);
        });
        QObject::connect(confirm, &QCheckBox::toggled, &dialog, [this] {
            message->setText(confirm->isChecked()
                                 ? "후속 원본과 연결 대상을 확인했습니다. 다음 단계에서 재무 병합을 검토하세요."
                                 : "후속 원본과 연결 대상을 확인하세요.");
            controls();
        });
        links->changed = [this] {
            confirm->setChecked(false);
            message->setText("연결표가 바뀌었습니다. 대상을 다시 확인하세요.");
        };
        auto invalidate = [this] {
            confirm->setChecked(false);
            message->setText("전환 설정이 바뀌었습니다. 환산비·전환값과 ID 연결을 다시 확인하세요.");
        };
        incomingConversion->changed = invalidate;
        previousConversion->changed = invalidate;
        QObject::connect(replace, &QCheckBox::toggled, &dialog, [invalidate] { invalidate(); });
        QObject::connect(preview, &QPushButton::clicked, &dialog, [this] {
            try {
                if (!current() || !loaded || busy)
                    return;
                auto a = *loaded;
                a.incomingConversion = incomingConversion->conversion();
                a.previousConversion = previousConversion->conversion();
                previewXerConversions(&dialog, a.incoming, *activeXerFinancials(*source), a);
            } catch (const std::exception &e) {
                showEditorError(*message, e);
            }
        });
        QObject::connect(review, &QPushButton::clicked, &dialog, [this] {
            try {
                if (!current() || !loaded || busy)
                    return;
                auto a = *loaded;
                links->apply(a);
                a.confirmed = confirm->isChecked();
                a.replaceMissingData = replace->isChecked();
                a.incomingConversion = incomingConversion->conversion();
                a.previousConversion = previousConversion->conversion();
                projectForXerRelink(*source, a);
                XerFinancialMergeDialog child(doc, &dialog, a);
                if (child.exec() == QDialog::Accepted)
                    dialog.accept();
            } catch (const std::exception &e) {
                showEditorError(*message, e);
            }
        });
        listener = doc.subscribe([this](Document::Event) {
            if (!current()) {
                if (control)
                    control->cancel();
                message->setText("문서가 변경되었습니다. 창을 다시 열어 연결하세요.");
                controls();
            }
        });
        controls();
        dialog.resize(1100, 700);
        prepareEditorDialog(dialog);
    }
    ~Impl() {
        if (control)
            control->cancel();
        doc.unsubscribe(listener);
    }
    bool current() const {
        return doc.project() && !doc.readOnly() && activeXerFinancials(*source) && doc.sessionGeneration() == session &&
               doc.project()->documentId == source->documentId && doc.project()->revision == source->revision;
    }
    void controls() {
        const bool enabled = current() && !busy;
        open->setEnabled(enabled);
        encoding->setEnabled(enabled);
        table->setEnabled(enabled && bool(loaded));
        confirm->setEnabled(enabled && bool(loaded));
        replace->setEnabled(enabled && bool(loaded));
        preview->setEnabled(enabled && bool(loaded));
        incomingConversion->setEnabled(enabled && bool(loaded));
        previousConversion->setEnabled(enabled && bool(loaded));
        review->setEnabled(enabled && loaded && confirm->isChecked());
        cancel->setEnabled(busy);
    }
    void load(QString path, XerEncoding charset) {
        if (!current() || busy)
            return;
        confirm->setChecked(false);
        busy = true;
        control = std::make_shared<CalculationControl>();
        controls();
        message->setText("새 XER의 프로젝트 목록을 읽고 있습니다…");
        auto *watcher = new QFutureWatcher<std::shared_ptr<LoadResult>>(&dialog);
        QObject::connect(
            watcher, &QFutureWatcher<std::shared_ptr<LoadResult>>::finished, &dialog, [this, watcher, path, charset] {
                auto r = watcher->result();
                watcher->deleteLater();
                if (!current() || control->cancelRequested.load() || r->cancelled || !r->error.isEmpty()) {
                    busy = false;
                    message->setText(r->error.isEmpty() ? "취소되었거나 문서가 변경되었습니다." : r->error);
                    controls();
                    return;
                }
                auto id = r->projects.size() == 1 ? std::optional<QString>{r->projects[0].id}
                                                  : chooseXerProject(&dialog, r->projects);
                if (!id) {
                    busy = false;
                    controls();
                    return;
                }
                auto *reader = new QFutureWatcher<std::shared_ptr<LoadResult>>(&dialog);
                QObject::connect(
                    reader, &QFutureWatcher<std::shared_ptr<LoadResult>>::finished, &dialog, [this, reader] {
                        auto result = reader->result();
                        reader->deleteLater();
                        busy = false;
                        if (current() && !control->cancelRequested.load() && result->audit && !result->cancelled) {
                            loaded = std::move(result->audit);
                            incomingConversion->source(loaded->incoming);
                            previousConversion->source(*activeXerFinancials(*source));
                            links->set(*source, *loaded);
                            confirm->setChecked(false);
                            label->setText(
                                "새 원본 " + q(loaded->incoming.sourceName) + " · 프로젝트 " + q(loaded->projectName) +
                                " [" + q(loaded->incoming.projectId) + "]\n원본 상태일 " +
                                q(loaded->sourceStatusAt.value_or("없음 · 현재 문서 시점을 연결 기준으로 사용")) +
                                " · 통화 " + q(loaded->incoming.currency) + "\nSHA256 " +
                                q(loaded->incoming.sourceSha256));
                            message->setText("새 원본/이전 집계의 연결 대상을 확인한 뒤 재무 병합 검토를 여세요. 현재 "
                                             "원장·AC는 유지합니다.");
                        } else
                            message->setText(result->error.isEmpty() ? "취소되었거나 문서가 변경되었습니다."
                                                                     : result->error);
                        controls();
                    });
                reader->setFuture(QtConcurrent::run([bytes = r->bytes, name = QFileInfo(path).fileName(), charset,
                                                     id = *id, status = source->statusAt.value_or(""), c = control] {
                    auto out = std::make_shared<LoadResult>();
                    try {
                        EvaluationScope scope(c);
                        out->audit = importXerRelinkSource(bytes, name, charset, id, status);
                    } catch (const CalculationCancelled &) {
                        out->cancelled = true;
                    } catch (const std::exception &e) {
                        out->error = QString::fromStdString(errorText(e));
                    }
                    return out;
                }));
            });
        watcher->setFuture(QtConcurrent::run([path, charset, c = control] {
            auto out = std::make_shared<LoadResult>();
            try {
                EvaluationScope scope(c);
                out->bytes = readPrimaveraXer(path);
                out->projects = listPrimaveraXerProjects(out->bytes, charset);
            } catch (const CalculationCancelled &) {
                out->cancelled = true;
            } catch (const std::exception &e) {
                out->error = QString::fromStdString(errorText(e));
            }
            return out;
        }));
    }
};
XerSourceRelinkDialog::XerSourceRelinkDialog(Document &d, QWidget *parent) : QDialog(parent) {
    setObjectName("xerSourceRelinkDialog");
    setWindowTitle("새 XER 버전·삭제 ID 재연결");
    impl_ = std::make_unique<Impl>(*this, d);
}
XerSourceRelinkDialog::~XerSourceRelinkDialog() = default;
void XerSourceRelinkDialog::loadSource(const QString &path, XerEncoding encoding) {
    impl_->load(path, encoding);
}
void XerSourceRelinkDialog::reject() {
    if (impl_->control)
        impl_->control->cancel();
    QDialog::reject();
}
} // namespace jobs
