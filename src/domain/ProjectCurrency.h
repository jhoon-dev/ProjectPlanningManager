#pragma once
#include "domain/Project.h"
#include <functional>
namespace ppm {
void validateCurrencyRate(const CurrencyRate &);
std::optional<std::int64_t> convertCurrencyMoney(std::optional<std::int64_t>, const CurrencyRate &);
XerFinancialSnapshot convertCurrencyFinancials(const XerFinancialSnapshot &, const CurrencyRate &);
Project convertProjectCurrency(const Project &, const CurrencyRate &);
void clearTransitionHistories(Project &);
void validateCurrencyConversions(const Project &);
void validateCurrencyCurrent(const Project &);
struct CurrencyMoneyRow {
    std::string path, currency;
    std::optional<std::int64_t> amount;
};
std::vector<CurrencyMoneyRow> currencyMoneyRows(const Project &);
} // namespace ppm
