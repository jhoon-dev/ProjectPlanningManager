#pragma once
#include "domain/Project.h"
#include "domain/CalculationControl.h"
#include <QPicture>
#include <QPageLayout>
#include <QRectF>
#include <QStringList>
class QPainter;
class QPagedPaintDevice;
namespace ppm {
struct NetworkPrintLayout {
    QPicture picture;
    QRectF bounds;
    QPageLayout page;
    QString title, provenance;
    std::vector<QStringList> notes;
    double scale = 1;
    int columns = 1, rows = 1;
    int pageCount() const {
        return columns * rows + int(notes.size());
    }
};
NetworkPrintLayout buildNetworkPrint(QPicture, const QRectF &, const PrintPreferences &, QString title,
                                     QString provenance, const QStringList &details = {});
void paintNetworkPage(QPainter &, const NetworkPrintLayout &, int page);
int paintNetwork(QPagedPaintDevice &, const NetworkPrintLayout &, int first = 1, int last = 0,
                 const std::shared_ptr<CalculationControl> &control = {});
int exportNetworkPdf(const NetworkPrintLayout &, const QString &,
                     const std::shared_ptr<CalculationControl> &control = {});
} // namespace ppm
