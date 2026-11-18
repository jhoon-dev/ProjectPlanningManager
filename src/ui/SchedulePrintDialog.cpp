#include "ui/SchedulePrintDialog.h"
#include "ui/TaskTableModel.h"
#include "ui/TaskTableView.h"
#include "ui/SandboxAccess.h"
#include "formatting/QtDiagnosticText.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPrinterInfo>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <unordered_map>
namespace jobs {
namespace {
class PrintPreview final : public QWidget {
    std::shared_ptr<const SchedulePrintLayout> layout_;
    int page_;
    double zoom_;

public:
    PrintPreview(std::shared_ptr<const SchedulePrintLayout> layout, int page, int zoom)
        : layout_(std::move(layout)), page_(page), zoom_(zoom / 100.0) {
        const auto size = layout_->options.page.fullRect(QPageLayout::Point).size() * zoom_;
        setFixedSize(int(std::ceil(size.width())), int(std::ceil(size.height())));
        setObjectName("schedulePrintCanvas");
    }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::white);
        painter.scale(zoom_, zoom_);
        paintSchedulePage(painter, *layout_, page_);
    }
};
struct Prepared {
    std::shared_ptr<const SchedulePrintLayout> layout;
    QString error;
};
QSpinBox *spin(QWidget *parent, const char *name, int lo, int hi, int value) {
    auto *s = new QSpinBox(parent);
    s->setObjectName(name);
    s->setRange(lo, hi);
    s->setValue(value);
    return s;
}
} // namespace
SchedulePrintSource captureSchedulePrint(Document &doc, TaskTableView &table, const QString &scope,
                                         const QString &baseline) {
    if (!doc.project())
        throw std::runtime_error("인쇄할 프로젝트가 없습니다.");
    auto *model = dynamic_cast<TaskTableModel *>(table.model());
    if (!model)
        throw std::runtime_error("작업 표를 확인하세요.");
    SchedulePrintSource source;
    source.project = doc.snapshot();
    source.session = doc.sessionGeneration();
    source.unsaved = !doc.revisionSaved();
    source.scope = scope;
    source.baselineId = baseline;
    source.generatedAt = QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH:mm:ssZ");
    source.printId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    source.columns = {{"number", "원본 번호", 36}, {"name", "작업", 140}};
    std::vector<int> columns;
    for (int v = 0; v < model->columnCount(); ++v) {
        int c = table.horizontalHeader()->logicalIndex(v);
        if (c >= 0 && !table.isColumnHidden(c) && model->columnKey(c) != "name") {
            columns.push_back(c);
            source.columns.push_back({QString::fromStdString(model->columnKey(c)),
                                      model->headerData(c, Qt::Horizontal).toString(), table.columnWidth(c) * 0.75});
        }
    }
    std::unordered_map<std::string, std::size_t> indices;
    for (std::size_t i = 0; i < source.project->tasks.size(); ++i)
        indices.emplace(source.project->tasks[i].id, i);
    std::set<int> selected;
    for (const auto &index : table.selectionModel()->selectedIndexes())
        selected.insert(index.row());
    for (int r = 0; r < model->rowCount(); ++r) {
        if (table.isRowHidden(r))
            continue;
        const auto id = model->index(r, 0).data(Qt::UserRole).toString().toStdString();
        if (!indices.contains(id) || model->cachedTask(indices.at(id)) != source.project->tasks[indices.at(id)])
            throw std::runtime_error("작업 표가 오래되었습니다. 화면 갱신 후 다시 여세요.");
        SchedulePrintInputRow row;
        row.taskId = id;
        row.number = int(indices.at(id)) + 1;
        row.cells = {QString::number(row.number), QString::fromUtf8(source.project->tasks[indices.at(id)].name)};
        for (int c : columns)
            row.cells << model->index(r, c).data().toString();
        source.rows.push_back(std::move(row));
        if (selected.contains(r))
            source.selected.insert(id);
    }
    return source;
}
SchedulePrintDialog::SchedulePrintDialog(Document &doc, SchedulePrintSource source, QWidget *parent)
    : QDialog(parent), document_(doc), source_(std::move(source)) {
    setObjectName("schedulePrintDialog");
    setWindowTitle("일정표·간트 · 페이지 설정 / 인쇄 미리보기");
    resize(1180, 850);
    auto *body = new QVBoxLayout(this);
    auto *presets = new QHBoxLayout;
    body->addLayout(presets);
    presets->addWidget(new QLabel("출력 구성", this));
    preset_ = new QComboBox(this);
    preset_->setObjectName("printPreset");
    preset_->addItems({"회의용 · 전체 일정", "회의용 · 다음 주 시간축", "상세 · 화면 표시 열 / 저장된 페이지 설정"});
    preset_->setToolTip("회의용은 전체 프로젝트·핵심 열·A4 가로를 적용합니다. 다음 주는 상태일(없으면 프로젝트 "
                        "시작일)의 다음 월~일입니다. 프리셋 선택은 문서를 변경하지 않습니다.");
    preset_->setCurrentIndex(source_.project->printPreferences ? 2 : 0);
    presets->addWidget(preset_);
    presets->addStretch();
    context_ = new QLabel(this);
    context_->setObjectName("printContext");
    context_->setTextFormat(Qt::PlainText);
    context_->setWordWrap(true);
    body->addWidget(context_);
    auto *settings = new QHBoxLayout;
    body->addLayout(settings);
    auto add = [&](QHBoxLayout *row, const QString &label, QWidget *widget) {
        row->addWidget(new QLabel(label, this));
        row->addWidget(widget);
    };
    scope_ = new QComboBox(this);
    scope_->setObjectName("printScope");
    scope_->addItems({"현재 표시 작업", "표시 작업 중 선택"});
    if (preset_->currentIndex() != 2)
        scope_->addItem("전체 프로젝트 (화면 필터 제외)");
    add(settings, "범위", scope_);
    paper_ = new QComboBox(this);
    paper_->setObjectName("printPaper");
    paper_->addItems({"A4", "A3"});
    add(settings, "용지", paper_);
    orientation_ = new QComboBox(this);
    orientation_->setObjectName("printOrientation");
    orientation_->addItems({"가로", "세로"});
    add(settings, "방향", orientation_);
    margin_ = spin(this, "printMargin", 5, 30, 10);
    margin_->setSuffix(" mm");
    add(settings, "여백", margin_);
    font_ = spin(this, "printFont", 8, 12, 9);
    add(settings, "글자", font_);
    settings->addStretch();
    settings = new QHBoxLayout;
    body->addLayout(settings);
    tablePercent_ = spin(this, "printTablePercent", 40, 70, 52);
    tablePercent_->setSuffix("%");
    add(settings, "표", tablePercent_);
    nameWidth_ = spin(this, "printNameWidth", 80, 240, 140);
    add(settings, "작업명 폭(pt)", nameWidth_);
    relations_ = new QCheckBox("관계선", this);
    relations_->setObjectName("printRelations");
    settings->addWidget(relations_);
    wbs_ = new QCheckBox("WBS 요약 (표시 범위)", this);
    wbs_->setObjectName("printWbsSummary");
    settings->addWidget(wbs_);
    auto *dates = new QHBoxLayout;
    body->addLayout(dates);
    automatic_ = new QCheckBox("기간 자동", this);
    automatic_->setObjectName("printAutoDates");
    automatic_->setChecked(true);
    dates->addWidget(automatic_);
    const auto date = QDate::fromString(QString::fromUtf8(source_.project->startDate), Qt::ISODate);
    from_ = new QDateEdit(date, this);
    from_->setObjectName("printFrom");
    to_ = new QDateEdit(date.addDays(13), this);
    to_->setObjectName("printTo");
    for (auto *d : {from_, to_}) {
        d->setDisplayFormat("yyyy-MM-dd");
        d->setCalendarPopup(true);
        d->setEnabled(false);
        dates->addWidget(d);
    }
    fit_ = new QCheckBox("기간 가로 한 장 맞춤", this);
    fit_->setObjectName("printFitTime");
    fit_->setChecked(true);
    dates->addWidget(fit_);
    days_ = spin(this, "printDays", 1, 366, 14);
    days_->setSuffix(" 일/쪽");
    days_->setEnabled(false);
    dates->addWidget(days_);
    baseline_ = new QCheckBox("선택 기준선", this);
    baseline_->setObjectName("printBaseline");
    baseline_->setChecked(!source_.baselineId.isEmpty());
    baseline_->setEnabled(!source_.baselineId.isEmpty());
    dates->addWidget(baseline_);
    message_ = new QLabel(this);
    message_->setObjectName("schedulePrintMessage");
    message_->setTextFormat(Qt::PlainText);
    message_->setWordWrap(true);
    body->addWidget(message_);
    auto *navigation = new QHBoxLayout;
    body->addLayout(navigation);
    page_ = spin(this, "printPage", 1, 1, 1);
    add(navigation, "미리보기 페이지", page_);
    zoom_ = spin(this, "printZoom", 40, 200, 100);
    zoom_->setSuffix("%");
    add(navigation, "화면 배율", zoom_);
    navigation->addStretch();
    preview_ = new QScrollArea(this);
    preview_->setObjectName("schedulePrintPreview");
    preview_->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    body->addWidget(preview_, 1);
    auto *actions = new QHBoxLayout;
    body->addLayout(actions);
    prepare_ = new QPushButton("설정 적용 / 미리보기", this);
    prepare_->setObjectName("prepareSchedulePrint");
    pdf_ = new QPushButton("동일 배치 PDF 저장…", this);
    pdf_->setObjectName("exportSchedulePdf");
    print_ = new QPushButton("인쇄…", this);
    print_->setObjectName("printSchedule");
    cancel_ = new QPushButton("준비 / 출력 취소", this);
    cancel_->setObjectName("cancelSchedulePrint");
    saveSettings_ = new QPushButton("페이지 설정만 문서에 저장", this);
    saveSettings_->setObjectName("savePrintPreferences");
    saveSettings_->setEnabled(!document_.readOnly());
    saveSettings_->setToolTip(
        "기존 용지·범위·기간·표시 옵션만 저장합니다. 회의용 프리셋과 열 구성은 이 창에서 선택합니다.");
    actions->addWidget(saveSettings_);
    auto *close = new QPushButton("닫기", this);
    for (auto *b : {prepare_, pdf_, print_, cancel_, close})
        actions->addWidget(b);
    if (source_.project->printPreferences) {
        const auto &s = *source_.project->printPreferences;
        scope_->setCurrentIndex(s.selectedOnly ? 1 : 0);
        paper_->setCurrentIndex(s.paper);
        orientation_->setCurrentIndex(s.orientation);
        margin_->setValue(s.margin);
        font_->setValue(s.fontPoints);
        tablePercent_->setValue(s.tablePercent);
        nameWidth_->setValue(s.nameWidth);
        days_->setValue(s.daysPerPage);
        fit_->setChecked(s.fitTime);
        days_->setEnabled(!s.fitTime);
        baseline_->setChecked(s.baseline && !source_.baselineId.isEmpty());
        automatic_->setChecked(s.automaticDates);
        if (!s.automaticDates) {
            from_->setDate(QDate::fromString(QString::fromStdString(s.from), Qt::ISODate));
            to_->setDate(QDate::fromString(QString::fromStdString(s.to), Qt::ISODate));
        }
        from_->setEnabled(!s.automaticDates);
        to_->setEnabled(!s.automaticDates);
        relations_->setChecked(s.relations);
        wbs_->setChecked(s.wbs);
    }
    if (preset_->currentIndex() != 2) {
        scope_->setCurrentIndex(2);
        tablePercent_->setValue(65);
        nameWidth_->setValue(110);
    }
    connect(saveSettings_, &QPushButton::clicked, this, [this] {
        try {
            if (!current() || working_ || document_.readOnly())
                throw std::runtime_error("문서가 변경되어 설정을 저장하지 않았습니다. 다시 여세요.");
            document_.setPrintPreferences(preferences());
            source_.project = document_.snapshot();
            source_.unsaved = !document_.revisionSaved();
            prepare();
        } catch (const std::exception &e) {
            message_->setText(qErrorText(e));
        }
    });
    connect(close, &QPushButton::clicked, this, &SchedulePrintDialog::reject);
    connect(prepare_, &QPushButton::clicked, this, [this] { prepare(); });
    connect(pdf_, &QPushButton::clicked, this, [this] { output(true); });
    connect(print_, &QPushButton::clicked, this, [this] { output(false); });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (control_)
            control_->cancel();
    });
    for (auto *combo : {scope_, paper_, orientation_})
        connect(combo, &QComboBox::currentIndexChanged, this, [this] { invalidate(); });
    for (auto *s : {margin_, font_, tablePercent_, nameWidth_, days_})
        connect(s, &QSpinBox::valueChanged, this, [this] { invalidate(); });
    for (auto *d : {from_, to_})
        connect(d, &QDateEdit::dateChanged, this, [this] { invalidate(); });
    connect(automatic_, &QCheckBox::toggled, this, [this](bool on) {
        from_->setEnabled(!on);
        to_->setEnabled(!on);
        invalidate();
    });
    connect(fit_, &QCheckBox::toggled, this, [this](bool on) {
        days_->setEnabled(!on);
        invalidate();
    });
    connect(baseline_, &QCheckBox::toggled, this, [this] { invalidate(); });
    connect(relations_, &QCheckBox::toggled, this, [this] { invalidate(); });
    connect(wbs_, &QCheckBox::toggled, this, [this] { invalidate(); });
    connect(page_, &QSpinBox::valueChanged, this, [this] { showPage(); });
    connect(zoom_, &QSpinBox::valueChanged, this, [this] { showPage(); });
    connect(preset_, &QComboBox::currentIndexChanged, this, [this] { applyPreset(); });
    prepare();
}
SchedulePrintDialog::~SchedulePrintDialog() {
    if (control_)
        control_->cancel();
}
void SchedulePrintDialog::reject() {
    if (control_)
        control_->cancel();
    QDialog::reject();
}
bool SchedulePrintDialog::current() const {
    return document_.project() && document_.sessionGeneration() == source_.session &&
           document_.project()->documentId == source_.project->documentId &&
           document_.project()->revision == source_.project->revision && !document_.revisionSaved() == source_.unsaved;
}
void SchedulePrintDialog::ready(bool working) {
    working_ = working;
    saveSettings_->setEnabled(!working && !document_.readOnly() && current());
    prepare_->setEnabled(!working);
    cancel_->setEnabled(working);
    pdf_->setEnabled(!working && layout_ && current());
    print_->setEnabled(!working && layout_ && current() && !QPrinterInfo::availablePrinters().isEmpty());
    if (QPrinterInfo::availablePrinters().isEmpty())
        print_->setToolTip("등록된 프린터가 없습니다. PDF 저장은 사용할 수 있습니다.");
    for (auto *widget : std::vector<QWidget *>{preset_, scope_, paper_, orientation_, margin_, font_, tablePercent_,
                                               nameWidth_, automatic_, fit_, baseline_, relations_, wbs_})
        widget->setEnabled(!working);
    baseline_->setEnabled(!working && !source_.baselineId.isEmpty());
    days_->setEnabled(!working && !fit_->isChecked());
    from_->setEnabled(!working && !automatic_->isChecked());
    to_->setEnabled(!working && !automatic_->isChecked());
}
void SchedulePrintDialog::invalidate() {
    if (control_)
        control_->cancel();
    ++generation_;
    layout_.reset();
    delete preview_->takeWidget();
    ready(false);
    message_->setText("설정이 변경되었습니다. 미리보기를 다시 준비하세요.");
    updateContext();
}
void SchedulePrintDialog::updateContext() {
    const auto &p = *source_.project;
    QString base = "없음/미표시";
    if (baseline_->isChecked())
        for (const auto &b : p.baselines)
            if (QString::fromStdString(b.id) == source_.baselineId)
                base = "v" + QString::number(b.version) + " · " + QString::fromStdString(b.name);
    context_->setText("범위: " +
                      (scope_->currentIndex() == 2 ? QString("전체 프로젝트 · 화면 필터 제외")
                                                   : source_.scope + " · " + scope_->currentText()) +
                      "\n상태일: " + (p.statusAt ? QString::fromStdString(*p.statusAt) : "미설정") +
                      " · 기준선: " + base + " · 일정: " +
                      (p.remainingLeveling ? QString("잔여 예측 · 자원 조정 채택")
                       : p.planLeveling    ? QString("계획 · 자원 조정 채택")
                       : p.statusAt        ? QString("잔여 예측 · 자원 조정 전")
                                           : QString("현재 계획")) +
                      "\n시간축: " +
                      (automatic_->isChecked()
                           ? QString("전체 출력 작업·기준선 범위")
                           : from_->date().toString(Qt::ISODate) + " ~ " + to_->date().toString(Qt::ISODate)) +
                      " · 기간을 제한해도 작업 행은 제외하지 않습니다. 전체 프로젝트에서 계산합니다.");
}
void SchedulePrintDialog::applyPreset() {
    const bool meeting = preset_->currentIndex() != 2;
    if (meeting && scope_->count() == 2)
        scope_->addItem("전체 프로젝트 (화면 필터 제외)");
    if (!meeting && scope_->count() == 3) {
        scope_->setCurrentIndex(0);
        scope_->removeItem(2);
    }
    const auto saved = source_.project->printPreferences.value_or(PrintPreferences{});
    scope_->setCurrentIndex(meeting ? 2 : saved.selectedOnly ? 1 : 0);
    paper_->setCurrentIndex(meeting ? 0 : saved.paper);
    orientation_->setCurrentIndex(meeting ? 0 : saved.orientation);
    margin_->setValue(meeting ? 10 : saved.margin);
    font_->setValue(meeting ? 9 : saved.fontPoints);
    tablePercent_->setValue(meeting ? 65 : saved.tablePercent);
    nameWidth_->setValue(meeting ? 110 : saved.nameWidth);
    days_->setValue(saved.daysPerPage);
    fit_->setChecked(meeting || saved.fitTime);
    relations_->setChecked(!meeting && saved.relations);
    wbs_->setChecked(!meeting && saved.wbs);
    baseline_->setChecked((meeting || saved.baseline) && !source_.baselineId.isEmpty());
    automatic_->setChecked(meeting || saved.automaticDates);
    if (preset_->currentIndex() == 1) {
        const auto &p = *source_.project;
        const auto anchor = QDate::fromString(
            QString::fromStdString(p.statusAt ? p.statusAt->substr(0, 10) : p.startDate), Qt::ISODate);
        const auto nextMonday = anchor.addDays(8 - anchor.dayOfWeek());
        automatic_->setChecked(false);
        from_->setDate(nextMonday);
        to_->setDate(nextMonday.addDays(6));
    } else if (!meeting && !saved.automaticDates) {
        from_->setDate(QDate::fromString(QString::fromStdString(saved.from), Qt::ISODate));
        to_->setDate(QDate::fromString(QString::fromStdString(saved.to), Qt::ISODate));
    }
    prepare();
}
PrintPreferences SchedulePrintDialog::preferences() const {
    auto s = source_.project->printPreferences.value_or(PrintPreferences{});
    s.paper = paper_->currentIndex();
    s.orientation = orientation_->currentIndex();
    s.margin = margin_->value();
    s.fontPoints = font_->value();
    s.tablePercent = tablePercent_->value();
    s.nameWidth = nameWidth_->value();
    s.daysPerPage = days_->value();
    s.selectedOnly = scope_->currentIndex() == 1;
    s.fitTime = fit_->isChecked();
    s.baseline = baseline_->isChecked();
    s.automaticDates = automatic_->isChecked();
    s.relations = relations_->isChecked();
    s.wbs = wbs_->isChecked();
    s.from = s.automaticDates ? std::string{} : from_->date().toString(Qt::ISODate).toStdString();
    s.to = s.automaticDates ? std::string{} : to_->date().toString(Qt::ISODate).toStdString();
    return s;
}
void SchedulePrintDialog::prepare() {
    if (working_)
        return;
    invalidate();
    if (!current()) {
        message_->setText("문서/저장 상태가 바뀌었습니다. 창을 닫고 새로 여세요.");
        return;
    }
    SchedulePrintOptions options;
    options.page = QPageLayout(QPageSize(paper_->currentIndex() ? QPageSize::A3 : QPageSize::A4),
                               orientation_->currentIndex() ? QPageLayout::Portrait : QPageLayout::Landscape,
                               QMarginsF(margin_->value(), margin_->value(), margin_->value(), margin_->value()),
                               QPageLayout::Millimeter);
    options.font = QApplication::font();
    options.fontPoints = font_->value();
    options.tablePercent = tablePercent_->value();
    options.nameWidth = nameWidth_->value();
    options.daysPerPage = days_->value();
    options.selectedOnly = scope_->currentIndex() == 1;
    options.fitTime = fit_->isChecked();
    options.baseline = baseline_->isChecked();
    options.relations = relations_->isChecked();
    options.wbs = wbs_->isChecked();
    options.meetingSummary = preset_->currentIndex() != 2;
    if (!automatic_->isChecked()) {
        options.from = from_->date();
        options.to = to_->date();
    }
    control_ = std::make_shared<CalculationControl>();
    ready(true);
    const auto generation = generation_;
    message_->setText("전체 일정 계산과 출력 페이지 준비 중…");
    auto *watcher = new QFutureWatcher<Prepared>(this);
    connect(watcher, &QFutureWatcher<Prepared>::finished, this, [this, watcher, generation] {
        auto result = watcher->result();
        watcher->deleteLater();
        if (generation != generation_)
            return;
        ready(false);
        if (control_ && control_->cancelRequested.load()) {
            message_->setText("인쇄 준비를 취소했습니다.");
            return;
        }
        if (!current()) {
            message_->setText("문서가 바뀌어 이전 미리보기를 적용하지 않았습니다. 새로 여세요.");
            return;
        }
        if (!result.error.isEmpty()) {
            message_->setText(result.error);
            return;
        }
        layout_ = std::move(result.layout);
        page_->setRange(1, int(layout_->pages.size()));
        page_->setValue(1);
        showPage();
        ready(false);
        message_->setText(
            QString("%1 작업 · %2 페이지 · %3 · 원본 번호/작업명 반복 · 표/막대 같은 행 · 출처는 마지막 페이지")
                .arg(layout_->taskCount)
                .arg(layout_->pages.size())
                .arg(layout_->mode));
    });
    auto outputSource = source_;
    if (scope_->currentIndex() == 2) {
        outputSource.scope = "전체 프로젝트 · 화면 필터 제외 · 원본 작업 순서";
        outputSource.rows.clear();
        int number = 0;
        for (const auto &task : outputSource.project->tasks) {
            QStringList cells;
            for (std::size_t i = 0; i < outputSource.columns.size(); ++i)
                cells << QString{};
            outputSource.rows.push_back({task.id, ++number, std::move(cells)});
        }
    }
    watcher->setFuture(QtConcurrent::run([source = std::move(outputSource), options, control = control_] {
        Prepared r;
        try {
            r.layout = std::make_shared<const SchedulePrintLayout>(buildSchedulePrint(source, options, control));
        } catch (const CalculationCancelled &) {
            r.error = "인쇄 준비를 취소했습니다.";
        } catch (const std::exception &e) {
            r.error = qErrorText(e);
        }
        return r;
    }));
}
void SchedulePrintDialog::showPage() {
    if (!layout_)
        return;
    auto *old = preview_->takeWidget();
    preview_->setWidget(new PrintPreview(layout_, page_->value() - 1, zoom_->value()));
    delete old;
}
void SchedulePrintDialog::output(bool pdf) {
    if (working_ || !layout_)
        return;
    if (!current()) {
        invalidate();
        message_->setText("문서/저장 상태가 바뀌어 출력을 차단했습니다. 새로 여세요.");
        return;
    }
    const auto layout = layout_;
    QString path;
    std::shared_ptr<QPrinter> printer;
    if (pdf) {
        path = QFileDialog::getSaveFileName(this, "일정표·간트 PDF · 새 파일명",
                                            QString("PPM_schedule_%1.pdf").arg(source_.printId.left(8)), "PDF (*.pdf)",
                                            nullptr, QFileDialog::DontConfirmOverwrite);
        if (path.isEmpty())
            return;
        if (QFileInfo(path).suffix().isEmpty())
            path += ".pdf";
        if (!authorizeSandboxFile(this, path))
            return;
    } else {
        printer = std::make_shared<QPrinter>(QPrinter::HighResolution);
        printer->setPageLayout(layout->options.page);
        printer->setFullPage(true);
        QPrintDialog dialog(printer.get(), this);
        dialog.setObjectName("schedulePrinterDialog");
        dialog.setMinMax(1, int(layout->pages.size()));
        dialog.setFromTo(1, int(layout->pages.size()));
        dialog.setOption(QAbstractPrintDialog::PrintToFile, false);
        dialog.setOption(QAbstractPrintDialog::PrintSelection, false);
        if (dialog.exec() != QDialog::Accepted)
            return;
        const auto min = printer->pageLayout().minimumMargins();
        const auto margins = layout->options.page.margins(printer->pageLayout().units());
        if (margins.left() < min.left() || margins.top() < min.top() || margins.right() < min.right() ||
            margins.bottom() < min.bottom()) {
            message_->setText("프린터의 최소 여백보다 작습니다. 페이지 여백을 늘린 후 다시 인쇄하세요.");
            return;
        }
        printer->setFullPage(true);
    }
    if (!current() || layout_ != layout) {
        invalidate();
        message_->setText("파일/프린터 선택 중 문서가 바뀌어 출력을 취소했습니다.");
        return;
    }
    control_ = std::make_shared<CalculationControl>();
    ready(true);
    message_->setText(pdf ? "PDF 저장 중…" : "프린터로 출력 중… · 취소 시 이미 출력한 페이지는 회수되지 않습니다.");
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
        const auto error = watcher->result();
        watcher->deleteLater();
        ready(false);
        message_->setText(error.isEmpty() ? "출력을 완료했습니다." : error);
    });
    watcher->setFuture(QtConcurrent::run([layout, path, printer, control = control_] {
        try {
            if (printer) {
                int first = 1, last = int(layout->pages.size());
                if (printer->printRange() == QPrinter::PageRange) {
                    first = printer->fromPage();
                    last = printer->toPage();
                }
                const bool reverse = printer->pageOrder() == QPrinter::LastPageFirst;
                printer->setPageOrder(QPrinter::FirstPageFirst);
                const bool collate = printer->collateCopies();
                const int copies = printer->supportsMultipleCopies() ? 1 : printer->copyCount();
                if (copies > 1)
                    printer->setCopyCount(1);
                paintSchedule(*printer, *layout, first, last, control, copies, reverse, collate);
            } else
                exportSchedulePdf(*layout, path, control);
            return QString{};
        } catch (const CalculationCancelled &) {
            if (printer)
                printer->abort();
            return QString("출력을 취소했습니다.");
        } catch (const std::exception &e) {
            return qErrorText(e);
        }
    }));
}
} // namespace jobs
