#pragma once
#include "domain/Project.h"
#include <QJsonObject>
#include <QJsonArray>
namespace ppm {
QJsonObject encodeCurrencyRate(const CurrencyRate &);
CurrencyRate decodeCurrencyRate(const QJsonValue &);
QJsonArray encodeCurrencyConversions(const std::vector<CurrencyConversion> &);
std::vector<CurrencyConversion> decodeCurrencyConversions(const QJsonValue &);
} // namespace ppm
