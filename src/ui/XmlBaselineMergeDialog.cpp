#include "ui/XmlBaselineDialog.h"
#include "ui/ExchangeOperation.h"
#include "ui/EditorUi.h"
#include "domain/XmlBaselines.h"
#include "domain/Classification.h"
#include "persistence/XmlBaselineExchange.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QVBoxLayout>
#include <set>
namespace jobs {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
QString kindName(const std::string &kind) {
    return kind == "task" ? "작업" : kind == "wbs" ? "WBS 요약" : kind == "resource" ? "자원" : "프로젝트 요약";
}
QString values(const XmlBaselineRow &r, const std::string &currency) {
    auto amount = [](const auto &v) { return v ? q(formatFieldNumber(*v)) : QString("미설정"); };
    return QString("B%1 · %2 → %3 · 기간 %4분 · 공수 %5분 · 원가 %6 · 시간분포 %7")
        .arg(r.number)
        .arg(q(r.start.value_or("미설정")), q(r.finish.value_or("미설정")), amount(r.duration), amount(r.work),
             r.cost ? q(formatFieldCost(*r.cost, currency) + " " + currency) : QString("미설정"))
        .arg(r.timephased.size());
}
} // namespace
bool reviewXmlBaselineMerge(QWidget *parent, Document &doc, const ProjectXmlExchange &input) {
    if (!doc.project() || doc.readOnly())
        throw AppError(ErrorCode::DOCUMENT_NOT_EDITABLE);
    const auto snapshot = doc.snapshot();
    const auto session = doc.sessionGeneration();
    auto projected = prepareExchange<std::vector<XmlBaselineRow>>(parent, doc, "현재 기준선 값 준비 중…", [snapshot] {
        QJsonArray diagnostics;
        return projectXmlBaselines(*snapshot, diagnostics);
    });
    if (!projected)
        return false;
    const auto &current = *projected;
    QDialog dialog(parent);
    dialog.setObjectName("xmlBaselineMergeDialog");
    dialog.setWindowTitle("외부 XML 기준선 · 현재 문서 병합");
    dialog.resize(1100, 700);
    auto *layout = new QVBoxLayout(&dialog);
    auto *note = new QLabel(
        "선택한 번호 전체를 교체합니다. 작업·WBS·자원을 현재 객체에 연결하거나 명시 제외하세요. "
        "배정은 연결된 작업/자원의 기존 배정에 대응합니다. 선택 번호의 누락·제외 객체/필드는 미설정이 됩니다. "
        "선택하지 않은 번호와 현재 계획·실적·native 승인본은 유지합니다. 다른 통화는 먼저 명시 전환해야 합니다. 원본 "
        "XML 파일은 함께 보관하세요.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *numbersTable = new QTableWidget(0, 5);
    numbersTable->setObjectName("xmlBaselineMergeSlots");
    numbersTable->setHorizontalHeaderLabels({"선택", "번호", "수신 행", "현재 값 출처", "검토"});
    numbersTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    numbersTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    numbersTable->horizontalHeader()->setStretchLastSection(true);
    std::map<int, std::pair<int, QString>> summaries;
    XmlBaselineSet all;
    for (const auto &c : input.baselineCandidates) {
        auto &s = summaries[c.row.number];
        ++s.first;
        if (!c.issue.empty())
            s.second = q(c.issue);
        if (c.issue.empty() && c.row.number >= 0)
            all.rows.push_back(c.row);
    }
    for (const auto &[number, summary] : summaries) {
        const int i = numbersTable->rowCount();
        numbersTable->insertRow(i);
        auto *select = new QTableWidgetItem;
        select->setFlags(summary.second.isEmpty() && number >= 0 ? Qt::ItemIsEnabled | Qt::ItemIsUserCheckable
                                                                 : Qt::NoItemFlags);
        select->setCheckState(Qt::Unchecked);
        select->setData(Qt::UserRole, number);
        numbersTable->setItem(i, 0, select);
        numbersTable->setItem(i, 1, new QTableWidgetItem(QString("Baseline%1").arg(number)));
        numbersTable->setItem(i, 2, new QTableWidgetItem(QString::number(summary.first)));
        QString origin = "채택 없음";
        if (snapshot->xmlBaselines &&
            std::any_of(snapshot->xmlBaselines->rows.begin(), snapshot->xmlBaselines->rows.end(),
                        [&](const auto &r) { return r.number == number; })) {
            const auto &s = xmlBaselineOrigin(*snapshot->xmlBaselines, number);
            origin = q(s.sourceName + " · " + s.sourceSha256.substr(0, 12));
        } else
            for (const auto &b : snapshot->baselines)
                if (b.version == std::uint64_t(number + 1))
                    origin = q("native v" + std::to_string(b.version) + " · " + b.name);
        numbersTable->setItem(i, 3, new QTableWidgetItem(origin));
        numbersTable->setItem(
            i, 4, new QTableWidgetItem(summary.second.isEmpty() ? "번호 전체 교체 · 누락은 미설정" : summary.second));
    }
    numbersTable->setMaximumHeight(210);
    layout->addWidget(numbersTable);
    const auto links = xmlBaselineLinks(all);
    auto *table = new QTableWidget(int(links.size()), 5);
    table->setObjectName("xmlBaselineMergeLinks");
    table->setHorizontalHeaderLabels({"범위", "원본 객체", "현재 대상 · 명시 제외", "수신 값", "현재 값"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->setColumnWidth(0, 85);
    table->setColumnWidth(1, 180);
    table->setColumnWidth(2, 260);
    table->setColumnWidth(3, 400);
    table->setColumnWidth(4, 400);
    std::map<std::string, QStandardItemModel *> choices;
    for (const auto &kind : {"project", "task", "resource", "wbs"}) {
        auto *model = new QStandardItemModel(&dialog);
        auto add = [&](const QString &name, const std::string &id, bool exclude, bool unresolved) {
            auto *item = new QStandardItem(name);
            item->setData(q(id), Qt::UserRole);
            item->setData(exclude, Qt::UserRole + 1);
            item->setData(unresolved, Qt::UserRole + 2);
            model->appendRow(item);
        };
        add("대상 선택…", "", false, true);
        add("명시 제외", "", true, false);
        if (std::string(kind) == "project")
            add(q(snapshot->name), "", false, false);
        if (std::string(kind) == "task")
            for (const auto &t : snapshot->tasks)
                add(q(t.id + " · " + t.name), t.id, false, false);
        if (std::string(kind) == "resource")
            for (const auto &r : snapshot->resources)
                add(q(r.id + " · " + r.name), r.id, false, false);
        if (std::string(kind) == "wbs")
            for (const auto &w : snapshot->wbsNodes)
                add(q(w.id + " · " + w.name), w.id, false, false);
        choices[kind] = model;
    }
    auto sourceName = [&](const XmlBaselineLink &l) {
        std::string name = input.project.name;
        if (l.kind == "task")
            for (const auto &t : input.project.tasks)
                if (t.id == l.sourceId)
                    name = t.name;
        if (l.kind == "resource")
            for (const auto &r : input.project.resources)
                if (r.id == l.sourceId)
                    name = r.name;
        if (l.kind == "wbs")
            for (const auto &w : input.project.wbsNodes)
                if (w.id == l.sourceId)
                    name = w.name;
        return q(name + (l.sourceId.empty() ? "" : " [" + l.sourceId + "]"));
    };
    for (std::size_t i = 0; i < links.size(); ++i) {
        const auto &l = links[i];
        table->setItem(int(i), 0, new QTableWidgetItem(kindName(l.kind)));
        table->setItem(int(i), 1, new QTableWidgetItem(sourceName(l)));
        auto *target = new QComboBox;
        target->setModel(choices.at(l.kind));
        target->setObjectName("xmlBaselineMergeTarget");
        table->setCellWidget(int(i), 2, target);
        table->setItem(int(i), 3, new QTableWidgetItem);
        table->setItem(int(i), 4, new QTableWidgetItem);
    }
    auto selectedSet = [&] {
        XmlBaselineSet result;
        std::set<int> selected;
        for (int i = 0; i < numbersTable->rowCount(); ++i)
            if (numbersTable->item(i, 0)->checkState() == Qt::Checked)
                selected.insert(numbersTable->item(i, 0)->data(Qt::UserRole).toInt());
        for (const auto &c : input.baselineCandidates)
            if (selected.contains(c.row.number))
                result.rows.push_back(c.row);
        return result;
    };
    auto refresh = [&] {
        auto selected = selectedSet();
        const auto required = xmlBaselineLinks(selected);
        std::set<int> numbers;
        for (const auto &r : selected.rows)
            numbers.insert(r.number);
        for (std::size_t i = 0; i < links.size(); ++i) {
            const auto &l = links[i];
            const bool needed = std::any_of(required.begin(), required.end(), [&](const auto &v) {
                return v.kind == l.kind && v.sourceId == l.sourceId;
            });
            table->setRowHidden(int(i), !needed);
            auto *target = qobject_cast<QComboBox *>(table->cellWidget(int(i), 2));
            const auto id = target->currentData().toString().toStdString();
            QStringList incoming, previous;
            for (const auto &r : selected.rows)
                if (r.kind == l.kind && r.id == l.sourceId)
                    incoming.push_back(values(r, input.project.currency));
                else if (r.kind == "assignment" && ((l.kind == "task" && r.id == l.sourceId) ||
                                                    (l.kind == "resource" && r.resourceId == l.sourceId)))
                    incoming.push_back("배정 " + q(r.id + "/" + r.resourceId) + " · " +
                                       values(r, input.project.currency));
            if (!target->currentData(Qt::UserRole + 1).toBool() && !target->currentData(Qt::UserRole + 2).toBool())
                for (const auto &r : current)
                    if (numbers.contains(r.number) &&
                        ((r.kind == l.kind && r.id == id) ||
                         (r.kind == "assignment" &&
                          ((l.kind == "task" && r.id == id) || (l.kind == "resource" && r.resourceId == id)))))
                        previous.push_back(
                            (r.kind == "assignment" ? "배정 " + q(r.id + "/" + r.resourceId) + " · " : QString{}) +
                            values(r, snapshot->currency));
            table->item(int(i), 3)->setText(incoming.join('\n'));
            table->item(int(i), 4)->setText(previous.isEmpty() ? "명시 값 없음 / 대상 미선택" : previous.join('\n'));
            for (int c : {1, 3, 4})
                table->item(int(i), c)->setToolTip(table->item(int(i), c)->text());
        }
    };
    QObject::connect(numbersTable, &QTableWidget::itemChanged, &dialog, refresh);
    for (std::size_t i = 0; i < links.size(); ++i)
        QObject::connect(qobject_cast<QComboBox *>(table->cellWidget(int(i), 2)), &QComboBox::currentIndexChanged,
                         &dialog, refresh);
    refresh();
    layout->addWidget(table);
    auto *replace = new QCheckBox(
        "선택 번호의 기존 외부/native 참조를 수신 값으로 교체하고, 누락 객체·필드는 미설정으로 전환함을 확인");
    replace->setObjectName("xmlBaselineMergeReplace");
    layout->addWidget(replace);
    auto *form = new QFormLayout;
    auto *reporter = new QLineEdit;
    reporter->setObjectName("xmlBaselineMergeReporter");
    reporter->setMaxLength(128);
    auto *reason = new QLineEdit;
    reason->setObjectName("xmlBaselineMergeReason");
    reason->setMaxLength(512);
    form->addRow("검토자", reporter);
    form->addRow("병합 사유", reason);
    layout->addLayout(form);
    auto *history = new QPushButton(QString("보존된 병합 이력 %1건…").arg(snapshot->xmlBaselineMerges.size()));
    history->setEnabled(!snapshot->xmlBaselineMerges.empty());
    layout->addWidget(history);
    QObject::connect(history, &QPushButton::clicked, &dialog, [&] {
        QDialog view(&dialog);
        view.setWindowTitle("XML 기준선 병합 이력");
        view.resize(800, 500);
        auto *body = new QVBoxLayout(&view);
        auto *text = new QPlainTextEdit;
        text->setReadOnly(true);
        for (const auto &e : snapshot->xmlBaselineMerges) {
            std::set<int> numbers;
            for (const auto &r : e.incoming.rows)
                numbers.insert(r.number);
            QStringList labels;
            for (int n : numbers)
                labels.append(QString::number(n));
            text->appendPlainText(
                q(e.incoming.recordedAt + " · " + e.incoming.reporter + " · " + e.incoming.sourceName) + "\n번호 " +
                labels.join(", ") + q(" · " + e.incoming.reason + "\nSHA256 " + e.incoming.sourceSha256) +
                QString("\n이전 %1행 → 적용 %2행 · 연결 %3건\n")
                    .arg(e.before ? e.before->rows.size() : 0)
                    .arg(e.after.rows.size())
                    .arg(e.links.size()));
        }
        body->addWidget(text);
        auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
        body->addWidget(close);
        QObject::connect(close, &QDialogButtonBox::rejected, &view, &QDialog::reject);
        prepareEditorDialog(view);
        view.exec();
    });
    auto *error = new QLabel;
    error->setObjectName("xmlBaselineMergeError");
    error->setWordWrap(true);
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("연결·교체 검토 완료 · 현재 문서에 병합");
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (session != doc.sessionGeneration() || !doc.project() || doc.project()->revision != snapshot->revision)
                throw AppError(ErrorCode::EXCHANGE_SOURCE_CHANGED);
            if (reporter->text().trimmed().isEmpty() || reason->text().trimmed().isEmpty())
                throw AppError(ErrorCode::FORMULA_INVALID, {"검토자와 병합 사유를 입력하세요."});
            XmlBaselineOptions options;
            for (int i = 0; i < numbersTable->rowCount(); ++i)
                if (numbersTable->item(i, 0)->checkState() == Qt::Checked)
                    options.numbers.push_back(numbersTable->item(i, 0)->data(Qt::UserRole).toInt());
            options.reporter = reporter->text().trimmed().toStdString();
            options.reason = reason->text().trimmed().toStdString();
            options.recordedAt = QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH:mm:ssZ").toStdString();
            options.confirmed = true;
            std::vector<XmlBaselineLink> mapping;
            for (std::size_t i = 0; i < links.size(); ++i)
                if (!table->isRowHidden(int(i))) {
                    auto *target = qobject_cast<QComboBox *>(table->cellWidget(int(i), 2));
                    if (target->currentData(Qt::UserRole + 2).toBool())
                        throw AppError(ErrorCode::FORMULA_INVALID, {"대상을 연결하거나 명시 제외하세요."});
                    auto l = links[i];
                    l.targetId = target->currentData().toString().toStdString();
                    l.exclude = target->currentData(Qt::UserRole + 1).toBool();
                    mapping.push_back(std::move(l));
                }
            const bool overwrite = replace->isChecked();
            auto submission = prepareExchange<XmlBaselineMergeSubmission>(
                &dialog, doc, "XML 원본·연결·기준선 재대조 중…", [snapshot, input, options, mapping, overwrite] {
                    return prepareXmlBaselineMerge(*snapshot, input, options, mapping, overwrite);
                });
            if (!submission)
                return;
            auto request = doc.makeEditRequest(XmlBaselineMergeEdit{std::move(*submission)});
            auto prepared = prepareExchange<std::shared_ptr<Document::PreparedEdit>>(
                &dialog, doc, "기준선 병합·이력 검증 중…",
                [request = std::move(request)]() mutable { return Document::prepareEdit(std::move(request), {}); });
            if (!prepared)
                return;
            const auto state = doc.applyPreparedEdit(**prepared);
            if (state == EditState::Applied || state == EditState::NoChange)
                dialog.accept();
            else if ((*prepared)->failure())
                throw AppError(*(*prepared)->failure());
            else
                throw AppError(ErrorCode::EXCHANGE_SOURCE_CHANGED);
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    prepareEditorDialog(dialog);
    return dialog.exec() == QDialog::Accepted;
}
} // namespace jobs
