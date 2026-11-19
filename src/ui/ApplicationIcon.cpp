#include "ui/ApplicationIcon.h"
#include <QResource>

static void initializePpmBrand() {
    Q_INIT_RESOURCE(ppm_brand);
}
namespace ppm {
QIcon applicationIcon() {
    static const bool initialized = [] {
        initializePpmBrand();
        return true;
    }();
    Q_UNUSED(initialized);
    return QIcon(":/ppm/app.ico");
}
} // namespace ppm
