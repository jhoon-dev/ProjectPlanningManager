#include "reporting/NetworkPrint.h"
#include "reporting/ExportFile.h"
#include "reporting/PrintFont.h"
#include <QPainter>
#include <QPdfWriter>
#include <cmath>
#include <QTextLayout>
namespace ppm {
NetworkPrintLayout buildNetworkPrint(QPicture picture, const QRectF &bounds, const PrintPreferences &s, QString title,
                                     QString provenance, const QStringList &details) {
    Project validation;
    validation.printPreferences = s;
    validatePresentation(validation);
    if (!bounds.isValid() || !std::isfinite(bounds.x()) || !std::isfinite(bounds.y()) ||
        !std::isfinite(bounds.width()) || !std::isfinite(bounds.height()) || bounds.width() > 2100000 ||
        bounds.height() > 2100000)
        throw std::runtime_error("네트워크 출력 범위를 확인하세요.");
    NetworkPrintLayout l;
    l.picture = std::move(picture);
    l.bounds = bounds;
    l.title = std::move(title);
    l.provenance = std::move(provenance);
    l.page = QPageLayout(QPageSize(s.paper ? QPageSize::A3 : QPageSize::A4),
                         s.orientation ? QPageLayout::Portrait : QPageLayout::Landscape,
                         QMarginsF(s.margin, s.margin, s.margin, s.margin), QPageLayout::Millimeter);
    const auto body = l.page.paintRect(QPageLayout::Point).adjusted(0, 65, 0, -35);
    l.scale = s.networkFit ? std::min(body.width() / bounds.width(), body.height() / bounds.height())
                           : 0.75 * s.networkScale / 100.;
    l.columns = s.networkFit ? 1 : std::max(1, int(std::ceil(bounds.width() * l.scale / body.width())));
    l.rows = s.networkFit ? 1 : std::max(1, int(std::ceil(bounds.height() * l.scale / body.height())));
    if (l.columns > 2000 || l.rows > 2000 || std::int64_t(l.columns) * l.rows > 2000)
        throw std::runtime_error("네트워크 인쇄 한도 2,000쪽 초과. 배율 또는 표시 범위를 줄이세요.");
    if (!details.isEmpty()) {
        const int maxLines = std::max(1, int(body.height() / 12));
        QStringList current;
        qsizetype chars = 0;
        auto font = printFont(QFont{});
        font.setPixelSize(8);
        for (const auto &text : details) {
            chars += text.size();
            if (chars > 16 * 1024 * 1024)
                throw std::runtime_error("네트워크 출처 텍스트 한도 초과");
            for (const auto &paragraph : text.split('\n')) {
                QTextLayout lines(paragraph, font);
                QTextOption opt;
                opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
                lines.setTextOption(opt);
                lines.beginLayout();
                while (true) {
                    auto line = lines.createLine();
                    if (!line.isValid())
                        break;
                    line.setLineWidth(body.width());
                    current << paragraph.mid(line.textStart(), line.textLength());
                    if (current.size() >= maxLines) {
                        l.notes.push_back(current);
                        current.clear();
                        if (l.pageCount() > 2000)
                            throw std::runtime_error("네트워크 페이지 한도 초과");
                    }
                }
                lines.endLayout();
            }
        }
        if (!current.empty())
            l.notes.push_back(current);
        if (l.pageCount() > 2000)
            throw std::runtime_error("네트워크 페이지 한도 초과");
    }
    return l;
}
void paintNetworkPage(QPainter &p, const NetworkPrintLayout &l, int page) {
    if (page < 0 || page >= l.pageCount())
        throw std::runtime_error("네트워크 페이지 범위 오류");
    const auto content = l.page.paintRect(QPageLayout::Point), body = content.adjusted(0, 65, 0, -35);
    p.save();
    p.setPen(Qt::black);
    auto f = printFont(p.font());
    f.setPixelSize(10);
    p.setFont(f);
    p.drawText(QRectF(content.left(), content.top(), content.width(), 18), l.title);
    f.setPixelSize(7);
    p.setFont(f);
    p.drawText(QRectF(content.left(), content.top() + 20, content.width(), 40), Qt::TextWordWrap, l.provenance);
    if (page < l.columns * l.rows) {
        p.drawText(
            QRectF(content.left(), content.bottom() - 28, content.width(), 28), Qt::TextWordWrap,
            QString("네트워크 %1/%2 · 행 %3/%4 열 %5/%6 · 표시망만 출력 · 배치 간격은 날짜 축이 아닙니다. 페이지 "
                    "경계의 선은 인접 타일에서 이어집니다.")
                .arg(page + 1)
                .arg(l.pageCount())
                .arg(page / l.columns + 1)
                .arg(l.rows)
                .arg(page % l.columns + 1)
                .arg(l.columns));
    } else
        p.drawText(QRectF(content.left(), content.bottom() - 28, content.width(), 28),
                   QString("네트워크 출처 %1/%2 · 축약 전 작업명/ID·표시 관계")
                       .arg(page - l.columns * l.rows + 1)
                       .arg(l.notes.size()));
    if (page >= l.columns * l.rows) {
        f.setPixelSize(8);
        p.setFont(f);
        double y = body.top();
        for (const auto &line : l.notes.at(page - l.columns * l.rows)) {
            p.drawText(QRectF(body.left(), y, body.width(), 12), line);
            y += 12;
        }
        p.restore();
        return;
    }
    p.setClipRect(body);
    p.translate(body.topLeft());
    p.translate(-(page % l.columns) * body.width(), -(page / l.columns) * body.height());
    p.scale(l.scale, l.scale);
    p.translate(-l.bounds.topLeft());
    // QPicture applies its recording-device DPI on playback. Page coordinates are
    // already points here, so cancel that extra conversion for every output device.
    p.scale(double(l.picture.logicalDpiX()) / p.device()->logicalDpiX(),
            double(l.picture.logicalDpiY()) / p.device()->logicalDpiY());
    p.drawPicture(QPointF(0, 0), l.picture);
    p.restore();
}
int paintNetwork(QPagedPaintDevice &device, const NetworkPrintLayout &l, int first, int last,
                 const std::shared_ptr<CalculationControl> &control) {
    EvaluationScope evaluation(control);
    if (!last)
        last = l.pageCount();
    if (first < 1 || last < first || last > l.pageCount())
        throw std::runtime_error("네트워크 인쇄 쪽 범위 오류");
    auto page = l.page;
    page.setMode(QPageLayout::FullPageMode);
    if (!device.setPageLayout(page))
        throw std::runtime_error("네트워크 용지 설정 실패");
    QPainter p(&device);
    if (!p.isActive())
        throw std::runtime_error("네트워크 인쇄 시작 실패");
    for (int i = first; i <= last; ++i) {
        calculationCheckpoint();
        if (i != first && !device.newPage())
            throw std::runtime_error("네트워크 다음 쪽 실패");
        p.save();
        p.scale(device.logicalDpiX() / 72., device.logicalDpiY() / 72.);
        paintNetworkPage(p, l, i - 1);
        p.restore();
    }
    if (!p.end())
        throw std::runtime_error("네트워크 인쇄 완료 실패");
    return last - first + 1;
}
int exportNetworkPdf(const NetworkPrintLayout &l, const QString &path,
                     const std::shared_ptr<CalculationControl> &control) {
    ExportFile out(path, "pdf");
    int count = 0;
    {
        QPdfWriter writer(&out.file);
        writer.setResolution(72);
        writer.setTitle(l.title);
        writer.setCreator("PPM");
        count = paintNetwork(writer, l, 1, 0, control);
    }
    if (control && control->cancelRequested.load())
        throw CalculationCancelled{};
    out.commit();
    return count;
}
} // namespace ppm
