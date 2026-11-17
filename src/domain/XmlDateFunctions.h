#pragma once
#include <cstdint>
#include <string>
namespace jobs {
// Gregorian civil dates; no current time, OS locale or work calendar lookup.
std::int64_t xmlDatePart(const std::string &interval, const std::string &date, int firstDay = 1, int firstWeek = 1);
std::string xmlDateAdd(const std::string &interval, std::int64_t count, const std::string &date);
std::int64_t xmlDateDifference(const std::string &interval, const std::string &from, const std::string &to,
                               int firstDay = 1, int firstWeek = 1);
std::string xmlTimeSerial(std::int64_t hour, std::int64_t minute, std::int64_t second);
std::string xmlTimeValue(const std::string &);
} // namespace jobs
