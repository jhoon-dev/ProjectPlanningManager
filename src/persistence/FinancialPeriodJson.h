#pragma once
#include "domain/FinancialPeriodTypes.h"
#include <QJsonArray>
namespace ppm::json {
QJsonArray encodeFinancialPeriodEvents(const std::vector<FinancialPeriodEvent> &);
std::vector<FinancialPeriodEvent> decodeFinancialPeriodEvents(const QJsonValue &);
} // namespace ppm::json
