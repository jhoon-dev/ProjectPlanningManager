#include "formatting/QtDiagnosticText.h"
#include "ui/MainWindow.h"
#include "ui/EditorUi.h"
#include "ui/ResourceComparisonDialog.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QSignalBlocker>
namespace ppm {
namespace {
QString qs(const std::string &s) {
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}
std::string str(const QString &s) {
    return s.toUtf8().toStdString();
}
} // namespace
void MainWindow::manageResources() {
    if (!document_.project())
        return;
    QDialog dialog(this);
    dialog.setObjectName("resourceDialog");
    dialog.setWindowTitle("자원 등록 · 날짜별 가용량");
    dialog.resize(820, 740);
    auto *layout = new QVBoxLayout(&dialog);
    auto *note =
        new QLabel("인력·장비 · 100% = 1명/1대 · 자원 달력과 작업 달력의 교집합에서 작업합니다.\n공수·작업 유형은 "
                   "프로젝트 메뉴에서, 단가·비용은 원가 메뉴에서 설정합니다. 물량/자원시간 생산성은 자원 → 생산성·외부 "
                   "고유 곡선에서 명시합니다.",
                   &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *table = new QTableWidget(0, 4, &dialog);
    table->setObjectName("resourceList");
    table->setHorizontalHeaderLabels({"자원", "종류", "기본 가용량", "날짜 예외"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(table);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    auto *name = new QLineEdit(&dialog);
    name->setObjectName("resourceName");
    form->addRow("이름", name);
    auto *type = new QComboBox(&dialog);
    type->setObjectName("resourceType");
    type->addItem("인력", "labor");
    type->addItem("장비", "equipment");
    form->addRow("종류", type);
    auto *calendar = new QComboBox(&dialog);
    calendar->setObjectName("resourceCalendar");
    calendar->addItem("프로젝트 기본 달력", QString{});
    for (const auto &c : document_.project()->calendars)
        calendar->addItem(qs(c.name), qs(c.id));
    form->addRow("자원 달력", calendar);
    calendar->setEnabled(!document_.readOnly());
    auto *units = new QSpinBox(&dialog);
    units->setObjectName("resourceCapacity");
    units->setRange(0, 100000);
    units->setSuffix("%");
    units->setValue(100);
    form->addRow("기본 가용량", units);
    auto *unknown = new QCheckBox("미정 (0%와 다름)", &dialog);
    unknown->setObjectName("capacityUnknown");
    form->addRow(unknown);
    connect(unknown, &QCheckBox::toggled, units, &QWidget::setDisabled);
    auto *dates = new QPlainTextEdit(&dialog);
    dates->setObjectName("capacityExceptions");
    dates->setPlaceholderText("2026-10-27=0\n2026-10-28=200\n2026-10-29=?");
    dates->setMaximumHeight(110);
    form->addRow("날짜별 대체 가용량\nYYYY-MM-DD=정수% / ?", dates);
    auto *availableFrom = new QLineEdit(&dialog);
    auto *availableTo = new QLineEdit(&dialog);
    availableFrom->setObjectName("resourceAvailableFrom");
    availableTo->setObjectName("resourceAvailableTo");
    for (auto *edit : {availableFrom, availableTo})
        edit->setPlaceholderText("YYYY-MM-DD HH:mm · 빈칸은 제한 없음");
    form->addRow("가용 시작 (해당 분 포함)", availableFrom);
    form->addRow("가용 종료 (해당 분 포함)", availableTo);
    auto *periods = new QPlainTextEdit(&dialog);
    periods->setObjectName("resourceAvailabilityPeriods");
    periods->setMaximumHeight(110);
    periods->setPlaceholderText(
        "2026-11-16 09:00 ~ 2026-11-16 11:59=50\n2026-11-17 00:00 ~ =200\n ~ 2026-11-15 23:59=?");
    form->addRow("기간별 가용량 · 종료 분 포함\n시작 ~ 종료=정수% / ?\n빈 경계는 제한 없음", periods);
    auto *availabilityNote = new QLabel("가용 경계 밖은 0%입니다. 경계 안에서는 날짜 예외 → 기간별 가용량 → 기본값 "
                                        "순서로 적용합니다. 기간끼리 겹칠 수 없습니다.",
                                        &dialog);
    availabilityNote->setWordWrap(true);
    form->addRow(availabilityNote);
    auto *error = new QLabel(&dialog);
    error->setObjectName("resourceError");
    error->setWordWrap(true);
    layout->addWidget(error);
    auto *actions = new QHBoxLayout;
    layout->addLayout(actions);
    auto *fresh = new QPushButton("새 자원", &dialog);
    fresh->setObjectName("newResource");
    actions->addWidget(fresh);
    auto *put = new QPushButton("등록 / 변경 저장", &dialog);
    put->setObjectName("saveResource");
    actions->addWidget(put);
    auto *remove = new QPushButton("선택 자원 삭제", &dialog);
    remove->setObjectName("removeResource");
    actions->addWidget(remove);
    const bool editable = !document_.readOnly();
    put->setEnabled(editable);
    remove->setEnabled(editable);
    fresh->setEnabled(editable);
    name->setReadOnly(!editable);
    type->setEnabled(editable);
    units->setEnabled(editable);
    unknown->setEnabled(editable);
    dates->setReadOnly(!editable);
    periods->setReadOnly(!editable);
    availableFrom->setReadOnly(!editable);
    availableTo->setReadOnly(!editable);
    std::string selected;
    auto fill = [&] {
        QSignalBlocker block(table);
        table->setRowCount(int(document_.project()->resources.size()));
        for (int i = 0; i < table->rowCount(); ++i) {
            const auto &r = document_.project()->resources[std::size_t(i)];
            QStringList values{qs(r.name), r.type == "labor" ? "인력" : "장비",
                               r.maxUnits ? QString::number(*r.maxUnits) + "%" : "미정",
                               QString::number(r.exceptions.size())};
            for (int c = 0; c < 4; ++c) {
                auto *item = new QTableWidgetItem(values[c]);
                item->setData(Qt::UserRole, qs(r.id));
                item->setToolTip(qs(r.id));
                table->setItem(i, c, item);
            }
            if (r.id == selected)
                table->selectRow(i);
        }
    };
    connect(table, &QTableWidget::itemSelectionChanged, &dialog, [&] {
        if (table->currentRow() < 0 || table->selectedItems().isEmpty())
            return;
        selected = str(table->item(table->currentRow(), 0)->data(Qt::UserRole).toString());
        for (const auto &r : document_.project()->resources)
            if (r.id == selected) {
                name->setText(qs(r.name));
                type->setCurrentIndex(type->findData(qs(r.type)));
                unknown->setChecked(!r.maxUnits);
                units->setValue(r.maxUnits.value_or(100));
                units->setEnabled(editable && r.maxUnits.has_value());
                calendar->setCurrentIndex(std::max(0, calendar->findData(qs(r.calendarId))));
                QStringList lines;
                for (const auto &d : r.exceptions)
                    lines << qs(d.date) + "=" + (d.units ? QString::number(*d.units) : "?");
                dates->setPlainText(lines.join('\n'));
                availableFrom->setText(qs(r.availableFrom.value_or("")));
                availableTo->setText(qs(r.availableTo.value_or("")));
                lines.clear();
                for (const auto &v : r.availability)
                    lines << qs(v.from.value_or("")) + " ~ " + qs(v.to.value_or("")) + "=" +
                                 (v.units ? QString::number(*v.units) : "?");
                periods->setPlainText(lines.join('\n'));
                error->clear();
            }
    });
    connect(fresh, &QPushButton::clicked, &dialog, [&] {
        selected.clear();
        table->clearSelection();
        name->clear();
        type->setCurrentIndex(0);
        calendar->setCurrentIndex(0);
        unknown->setChecked(false);
        units->setValue(100);
        dates->clear();
        periods->clear();
        availableFrom->clear();
        availableTo->clear();
        error->clear();
        name->setFocus();
    });
    connect(put, &QPushButton::clicked, &dialog, [&] {
        try {
            Resource r;
            for (const auto &old : document_.project()->resources)
                if (old.id == selected)
                    r = old;
            r.exceptions.clear();
            r.id = selected;
            r.name = str(name->text().trimmed());
            r.type = str(type->currentData().toString());
            r.maxUnits = unknown->isChecked() ? std::optional<int>{} : units->value();
            r.calendarId = calendar->currentData().toString().toStdString();
            for (auto line : dates->toPlainText().split('\n', Qt::SkipEmptyParts)) {
                auto parts = line.trimmed().split('=');
                if (parts.size() != 2)
                    throw AppError(ErrorCode::MAINWINDOWRESOURCES_001, {});
                auto value = parts[1].trimmed();
                std::optional<int> n;
                if (value != "?") {
                    bool ok;
                    n = value.toInt(&ok);
                    if (!ok)
                        throw AppError(ErrorCode::MAINWINDOWRESOURCES_002, {});
                }
                r.exceptions.push_back({str(parts[0].trimmed()), n});
            }
            const auto stamp = [](const QString &s) -> std::optional<std::string> {
                return s.trimmed().isEmpty() ? std::optional<std::string>{} : str(s.trimmed());
            };
            r.availableFrom = stamp(availableFrom->text());
            r.availableTo = stamp(availableTo->text());
            r.availability.clear();
            for (auto line : periods->toPlainText().split('\n', Qt::SkipEmptyParts)) {
                const auto parts = line.trimmed().split('=');
                if (parts.size() != 2 || parts[0].count('~') != 1)
                    throw AppError(ErrorCode::RESOURCE_EXCEPTION_INVALID);
                const auto bounds = parts[0].split('~');
                std::optional<int> amount;
                if (parts[1].trimmed() != "?") {
                    bool ok = false;
                    amount = parts[1].trimmed().toInt(&ok);
                    if (!ok)
                        throw AppError(ErrorCode::RESOURCE_EXCEPTION_INVALID);
                }
                r.availability.push_back({stamp(bounds[0]), stamp(bounds[1]), amount});
            }
            selected = document_.putResource(std::move(r));
            fill();
            error->setText("문서에 반영했습니다. 파일 저장으로 보존하세요.");
        } catch (const std::exception &e) {
            showEditorError(*error, e, name->text().trimmed().isEmpty() ? static_cast<QWidget *>(name) : dates);
        }
    });
    connect(remove, &QPushButton::clicked, &dialog, [&] {
        try {
            document_.removeResource(selected);
            selected.clear();
            fill();
            name->clear();
            dates->clear();
            error->setText("자원을 삭제했습니다.");
        } catch (const std::exception &e) {
            showEditorError(*error, e, name->text().trimmed().isEmpty() ? static_cast<QWidget *>(name) : dates);
        }
    });
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    fill();
    prepareEditorDialog(dialog);
    dialog.exec();
    refresh();
}
void MainWindow::assignResources() {
    if (!document_.project() || document_.readOnly() || selectedTask_.empty())
        return;
    const auto snapshot = static_cast<const PlanInput &>(*document_.project());
    const auto taskId = selectedTask_;
    const auto sourceId = document_.project()->documentId;
    const auto sourceRevision = document_.project()->revision;
    const auto sourceSession = document_.sessionGeneration();
    const Task *task = nullptr;
    for (const auto &t : snapshot.tasks)
        if (t.id == taskId)
            task = &t;
    if (!task)
        return;
    QDialog dialog(this);
    dialog.setObjectName("assignmentDialog");
    dialog.setWindowTitle("자원 배정 · " + qs(task->name));
    dialog.resize(720, 510);
    auto *layout = new QVBoxLayout(&dialog);
    auto *note =
        new QLabel("배정률 0% = 배정 해제(기간별 설정도 삭제) · 100% = 1명/1대\n기간별 설정은 보존되며 기본 배정률은 "
                   "빈 구간에만 적용됩니다.\n고정 기간은 기간을 유지합니다. 고정 공수/단위의 배정률 변경은 공수를 "
                   "유지해 완료일을 다시 계산합니다. 자원 달력과 날짜별 가용량은 자원 등록에서 설정합니다.",
                   &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *table = new QTableWidget(int(snapshot.resources.size()), 3, &dialog);
    table->setObjectName("assignmentTable");
    table->setHorizontalHeaderLabels({"자원", "기본 가용량", "배정률"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table);
    std::vector<QSpinBox *> spins;
    for (int i = 0; i < table->rowCount(); ++i) {
        const auto &r = snapshot.resources[std::size_t(i)];
        table->setItem(i, 0, new QTableWidgetItem(qs(r.name)));
        table->item(i, 0)->setToolTip(qs(r.id));
        table->setItem(i, 1, new QTableWidgetItem(r.maxUnits ? QString::number(*r.maxUnits) + "%" : "미정"));
        auto *spin = new QSpinBox(table);
        spin->setRange(0, 100000);
        spin->setSuffix("%");
        spin->setObjectName("assignmentUnits_" + QString::number(i));
        for (const auto &a : snapshot.assignments)
            if (a.taskId == taskId && a.resourceId == r.id)
                spin->setValue(a.units);
        table->setCellWidget(i, 2, spin);
        spins.push_back(spin);
    }
    auto *error = new QLabel(&dialog);
    error->setWordWrap(true);
    error->setObjectName("assignmentError");
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (!document_.project() || document_.project()->documentId != sourceId ||
                document_.project()->revision != sourceRevision || document_.sessionGeneration() != sourceSession)
                throw AppError(ErrorCode::MAINWINDOWRESOURCES_003, {});
            std::vector<Assignment> assignments;
            for (std::size_t i = 0; i < spins.size(); ++i)
                if (spins[i]->value()) {
                    Assignment value{taskId, snapshot.resources[i].id, spins[i]->value()};
                    for (const auto &old : snapshot.assignments)
                        if (old.taskId == taskId && old.resourceId == value.resourceId)
                            value.periods = old.periods;
                    assignments.push_back(std::move(value));
                }
            submitDialogEdit(dialog, error, buttons, AssignmentEdit{taskId, std::move(assignments)}, "자원 배정 계산",
                             [table, buttons](bool pending) {
                                 table->setEnabled(!pending);
                                 buttons->button(QDialogButtonBox::Ok)->setEnabled(!pending);
                             });
        } catch (const std::exception &e) {
            error->setText(qErrorText(e));
        }
    });
    prepareEditorDialog(dialog);
    dialog.exec();
    refresh();
}
void MainWindow::showResources() {
    if (!document_.project())
        return;
    ResourceComparisonDialog dialog(document_, this, selectedBaseline_);
    prepareEditorDialog(dialog);
    dialog.exec();
    refresh();
}
} // namespace ppm
