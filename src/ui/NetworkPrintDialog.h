#pragma once
#include "application/Document.h"
#include <QPicture>
#include <QRectF>
#include <QStringList>
class QWidget;
namespace ppm {
void showNetworkPrint(Document &, QPicture, const QRectF &, const QString &, QWidget *,
                      const QStringList &details = {});
}
