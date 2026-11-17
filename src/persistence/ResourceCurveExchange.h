#pragma once
#include <QString>
#include <cstdint>
namespace jobs {
std::int64_t curveDecimal(const QString &, std::int64_t scale, std::int64_t maximum);
std::int64_t curveWorkDuration(const QString &);
QString curveWorkText(std::int64_t);
QString curveHourText(std::int64_t);
} // namespace jobs
