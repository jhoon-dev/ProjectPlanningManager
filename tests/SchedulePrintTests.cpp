#include "application/Document.h"
#include "persistence/JsonProjectStore.h"
#include "reporting/SchedulePrint.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPrinter>
#include <algorithm>
#include <iostream>
namespace {
using namespace ppm;
void check(bool good, const char *message) {
    if (!good)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "expected rejection");
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "cannot read print artifact");
    return file.readAll();
}
void workflow() {
    JsonProjectStore store;
    Document doc(store);
    std::vector<std::string> names;
    for (int i = 0; i < 80; ++i)
        names.push_back("행_" + std::to_string(i));
    names[0] = (QString("긴 한국어 작업명 ").repeated(70) + "END_LONG_NAME").toStdString();
    doc.create("일정 인쇄 독립 검증 프로젝트", "2026-11-02", names);
    for (int i = 0; i < 80; ++i)
        doc.setDuration(i, 480);
    const auto baseline = doc.captureBaseline("인쇄 기준선", "PM", "검증", "2026-11-03T00:00:00Z");
    doc.setStatusAt("2026-11-02 18:00");
    ProgressEntry actual;
    actual.taskId = doc.project()->tasks[0].id;
    actual.actualStart = "2026-11-02 09:00";
    actual.actualFinish = "2026-11-02 18:00";
    actual.actualDurationMinutes = 480;
    actual.remainingMinutes = 0;
    actual.reporter = "PM";
    actual.reason = "완료";
    actual.recordedAt = "2026-11-03T00:00:00Z";
    doc.recordProgress(actual);
    SchedulePrintSource source;
    source.project = doc.snapshot();
    source.session = doc.sessionGeneration();
    source.unsaved = true;
    source.scope = "전체 표시 · 화면 순서 · 숨긴 열 제외";
    source.generatedAt = "2026-11-03T03:00:00Z";
    source.printId = "schedule-print-contract";
    source.baselineId = QString::fromStdString(baseline);
    source.columns = {{"number", "원본 번호", 36},
                      {"name", "작업", 140},
                      {"start", "시작", 110},
                      {"finish", "완료", 110},
                      {"totalFloat", "총여유", 70},
                      {"critical", "상태", 90},
                      {"field:note", "아주 긴 사용자 정의 열 제목의 전체 원문을 출처에서도 보존한다 END_HEADER", 110}};
    for (int i = 0; i < 80; ++i) {
        const auto &t = doc.project()->tasks[i];
        QString note = "설명_" + QString::number(i);
        if (i == 0)
            note = QString("이어지는 상세 입력\n").repeated(65) + "END_LONG_CELL";
        source.rows.push_back({t.id,
                               i + 1,
                               {QString::number(i + 1), QString::fromUtf8(t.name), "STALE_START", "STALE_FINISH",
                                "STALE_FLOAT", "STALE_STATE", note}});
    }
    const auto before = *doc.project();
    SchedulePrintOptions options;
    options.font = QApplication::font();
    const auto layout = buildSchedulePrint(source, options);
    check(layout.taskCount == 80 && layout.pages.size() > 4, "multi-page layout missing");
    check(layout.rows[0].cells[2] == "2026-11-02 09:00" && layout.rows[1].cells[2] == "2026-11-03 09:00" &&
              layout.rows[0].cells[4] == "—",
          "stale UI dates/actual float leaked into print");
    check(std::any_of(layout.rows[0].bars.begin(), layout.rows[0].bars.end(),
                      [](const auto &b) { return b.kind == ScheduleBarKind::Actual; }) &&
              std::any_of(layout.rows[1].bars.begin(), layout.rows[1].bars.end(),
                          [](const auto &b) { return b.kind == ScheduleBarKind::Baseline; }),
          "actual/baseline bars missing");
    std::set<int> columnPages;
    std::set<std::pair<std::size_t, int>> fragments;
    for (const auto &page : layout.pages) {
        double y = 0;
        for (const auto &f : page.fragments) {
            check(f.height > 0 && y + f.height <= layout.bodyHeight + 0.01, "row fragments escaped printable body");
            y += f.height;
            if (!page.provenance)
                fragments.emplace(f.row, f.line);
        }
        if (!page.provenance) {
            check(page.columns[0] == 0 && page.columns[1] == 1, "repeat columns missing");
            columnPages.insert(page.columnPage);
        }
    }
    check(columnPages.size() > 1 && fragments.size() > 80, "horizontal split/long row continuation not exercised");
    auto selectedProject = before;
    selectedProject.statusAt.reset();
    selectedProject.progress.clear();
    LevelingSelection decision;
    decision.policy = levelingPolicyFor(selectedProject);
    decision.adoptedAt = "2026-11-03T00:00:00Z";
    decision.reason = "인쇄 채택 검증";
    decision.sourceDocumentId = selectedProject.documentId;
    decision.sourceRevision = selectedProject.revision;
    selectedProject.planLeveling = decision;
    auto leveledSource = source;
    leveledSource.project = std::make_shared<const Project>(selectedProject);
    const auto leveled = buildSchedulePrint(leveledSource, options);
    check(leveled.mode.contains("평준화") && leveled.rows[1].cells[4] == "0" &&
              leveled.rows[1].cells[5] == "평준화 임계",
          "selected print leaked logical float/critical");
    auto splitProject = before;
    splitProject.calculationProfile = effortProfile;
    splitProject.tasks[1].breaks = {{"2026-11-03 11:00", "2026-11-03 15:00"}};
    auto splitSource = source;
    splitSource.project = std::make_shared<const Project>(splitProject);
    const auto split = buildSchedulePrint(splitSource, options);
    check(std::any_of(split.rows[1].bars.begin(), split.rows[1].bars.end(),
                      [](const auto &bar) {
                          return bar.kind != ScheduleBarKind::Baseline && bar.finish == "2026-11-03 11:00";
                      }),
          "manual split was bridged by print bar");
    QDir().mkpath("schedule-print-qa");
    for (const auto &name :
         {"schedule.pdf", "printer.pdf", "range.pdf", "portrait.pdf", "tiled.pdf", "selected.pdf", "cancelled.pdf"})
        QFile::remove("schedule-print-qa/" + QString(name));
    check(exportSchedulePdf(layout, "schedule-print-qa/schedule.pdf") == int(layout.pages.size()),
          "PDF page count mismatch");
    exportSchedulePdf(leveled, "schedule-print-qa/selected.pdf");
    auto bytes = read("schedule-print-qa/schedule.pdf");
    rejects([&] { exportSchedulePdf(layout, "schedule-print-qa/schedule.pdf"); });
    check(read("schedule-print-qa/schedule.pdf") == bytes, "existing PDF overwritten");
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName("schedule-print-qa/printer.pdf");
    printer.setResolution(300);
    check(printer.setPageLayout(layout.options.page), "virtual printer rejected page");
    printer.setFullPage(true);
    check(paintSchedule(printer, layout) == int(layout.pages.size()), "virtual printer page mismatch");
    QPrinter subset(QPrinter::HighResolution);
    subset.setOutputFormat(QPrinter::PdfFormat);
    subset.setOutputFileName("schedule-print-qa/range.pdf");
    subset.setPageLayout(layout.options.page);
    subset.setFullPage(true);
    check(paintSchedule(subset, layout, 2, 3, {}, 2, true, false) == 4, "reverse/range/software copies wrong");
    auto portraitOptions = options;
    portraitOptions.fontPoints = 12;
    portraitOptions.page = QPageLayout(QPageSize(QPageSize::A3), QPageLayout::Portrait, QMarginsF(15, 15, 15, 15),
                                       QPageLayout::Millimeter);
    const auto portrait = buildSchedulePrint(source, portraitOptions);
    exportSchedulePdf(portrait, "schedule-print-qa/portrait.pdf");
    auto tiles = options;
    tiles.fitTime = false;
    tiles.daysPerPage = 7;
    tiles.from = QDate(2026, 11, 1);
    tiles.to = QDate(2026, 11, 15);
    const auto tiled = buildSchedulePrint(source, tiles);
    check(std::any_of(tiled.pages.begin(), tiled.pages.end(),
                      [](const auto &p) {
                          return !p.provenance && p.timePage == 3 && p.from == QDate(2026, 11, 15) &&
                                 p.to == QDate(2026, 11, 15);
                      }),
          "partial last timeline tile missing");
    exportSchedulePdf(tiled, "schedule-print-qa/tiled.pdf");
    auto selected = source;
    selected.selected = {source.rows[3].taskId};
    auto selectedOptions = options;
    selectedOptions.selectedOnly = true;
    check(buildSchedulePrint(selected, selectedOptions).taskCount == 1, "selected scope expanded");
    selected.selected.clear();
    check(buildSchedulePrint(selected, selectedOptions).taskCount == 0, "empty selection expanded to project");
    auto bad = options;
    bad.from = QDate(2026, 11, 4);
    bad.to = QDate(2026, 11, 3);
    rejects([&] { buildSchedulePrint(source, bad); });
    bad = options;
    bad.daysPerPage = 0;
    rejects([&] { buildSchedulePrint(source, bad); });
    auto broken = source;
    broken.rows[0].taskId = "missing";
    rejects([&] { buildSchedulePrint(broken, options); });
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool canceled = false;
    try {
        buildSchedulePrint(source, options, control);
    } catch (const CalculationCancelled &) {
        canceled = true;
    }
    check(canceled, "layout cancellation swallowed");
    canceled = false;
    try {
        exportSchedulePdf(layout, "schedule-print-qa/cancelled.pdf", control);
    } catch (const CalculationCancelled &) {
        canceled = true;
    }
    check(canceled && !QFile::exists("schedule-print-qa/cancelled.pdf"), "cancelled PDF published");
    QImage image(1200, 850, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.scale(1.4, 1.4);
    paintSchedulePage(painter, layout, 0);
    painter.end();
    check(image.save("schedule-print-qa/page.png"), "page image failed");
    QJsonArray pageRows;
    for (const auto &page : layout.pages) {
        QJsonArray rows;
        for (const auto &f : page.fragments)
            rows.append(int(f.row));
        pageRows.append(QJsonObject{{"provenance", page.provenance},
                                    {"rows", rows},
                                    {"column_page", page.columnPage},
                                    {"time_page", page.timePage}});
    }
    QJsonObject proof{{"pages", int(layout.pages.size())},
                      {"selected_pages", int(leveled.pages.size())},
                      {"portrait_pages", int(portrait.pages.size())},
                      {"tiled_pages", int(tiled.pages.size())},
                      {"tasks", 80},
                      {"document_id", QString::fromUtf8(doc.project()->documentId)},
                      {"source_revision", qint64(doc.project()->revision)},
                      {"page_rows", pageRows}};
    QFile json("schedule-print-qa/layout.json");
    check(json.open(QIODevice::WriteOnly), "proof JSON failed");
    json.write(QJsonDocument(proof).toJson());
    check(*doc.project() == before, "printing mutated document/facts/baseline");
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).first(), 9));
#endif
    try {
        workflow();
        std::cout << "schedule print: row/column/time pagination, wrapping/continuation, actual/baseline, fresh dates, "
                     "selected/empty, PDF/printer/range/reverse/copies, atomic/cancel passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
