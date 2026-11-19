#pragma once
#include "domain/Diagnostics.h"
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>

namespace ppm {
inline QFont printFont(QFont font) {
#ifdef Q_OS_MACOS
    // CoreText's private UI fonts and Apple SD Gothic Neo can map PDF spaces
    // to controls. Resolve system aliases too, using a public Latin face and
    // Qt's Korean fallback so exported text remains searchable.
    const auto resolved = QFontInfo(font).family();
    if (font.family().isEmpty() || font.family().startsWith('.') || resolved.startsWith('.') ||
        QFontDatabase::isPrivateFamily(resolved) || resolved == "Apple SD Gothic Neo" ||
        font.family() == QFontDatabase::systemFont(QFontDatabase::GeneralFont).family()) {
        const auto families = QFontDatabase::families();
        for (const auto &family : QStringList{"Arial", "Helvetica", "Noto Sans"})
            if (families.contains(family)) {
                font.setFamily(family);
                font.setStyleName({});
                return font;
            }
        throw AppError(ErrorCode::PDFEXPORT_011, {});
    }
#endif
    return font;
}
} // namespace ppm
