#pragma once
class QCoreApplication;
namespace ppm {
// PPM currently has a Korean UI, independent of the operating system language.
void installUiTranslations(QCoreApplication &);
} // namespace ppm
