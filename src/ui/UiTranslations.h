#pragma once
class QCoreApplication;
namespace jobs {
// PPM currently has a Korean UI, independent of the operating system language.
void installUiTranslations(QCoreApplication &);
} // namespace jobs
