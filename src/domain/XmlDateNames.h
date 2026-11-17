#pragma once
#include <string>
namespace jobs {
// Fixed Gregorian en-US names, independent of the operating system locale.
std::string xmlMonthName(int month, bool abbreviate = false);
std::string xmlWeekdayName(int weekday, bool abbreviate = false, int firstDay = 1);
} // namespace jobs
