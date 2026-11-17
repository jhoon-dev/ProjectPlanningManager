#pragma once
#include "XmlPeriodActualFixture.h"
namespace jobs::test {
inline QByteArray xmlPeriodFragment(int type, const QByteArray &date, const QByteArray &from, const QByteArray &to,
                                    const QByteArray &value) {
    auto data = xmlPeriodData(1, type, date, value, 2, date + "T" + to + ":00");
    data.replace("<Start>" + date + "T09:00:00</Start>", "<Start>" + date + "T" + from + ":00</Start>");
    return data;
}
inline QByteArray xmlPeriodFragmentFixture(bool reversed = false) {
    auto bytes = xmlPeriodFixture(false);
    auto work = xmlPeriodFragment(2, "2026-11-10", "09:00", "10:00", "PT0H45M0.6S");
    auto work2 = xmlPeriodFragment(2, "2026-11-10", "10:00", "11:00", "PT0H45M0S");
    auto cost = xmlPeriodFragment(6, "2026-11-10", "09:00", "10:00", "500.00");
    auto cost2 = xmlPeriodFragment(6, "2026-11-10", "10:00", "11:00", "750.00");
    bytes.replace(xmlPeriodData(1, 2, "2026-11-10", "PT1H30M0.6S"), reversed ? work2 + work : work + work2);
    bytes.replace(xmlPeriodData(1, 6, "2026-11-10", "1250.00"), reversed ? cost2 + cost : cost + cost2);
    for (const int type : {2, 6}) {
        const auto value = type == 2 ? QByteArray("PT0H0M0S") : QByteArray("0.00");
        auto zero = xmlPeriodFragment(type, "2026-11-11", "09:00", "10:00", value);
        auto zero2 = xmlPeriodFragment(type, "2026-11-11", "11:00", "12:00", value);
        bytes.replace(xmlPeriodData(1, type, "2026-11-11", value), reversed ? zero2 + zero : zero + zero2);
    }
    return bytes;
}
} // namespace jobs::test
