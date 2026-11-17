"""Independent civil-time intersection oracle for the C++ summary-duration fixtures."""
import json
import sys
from datetime import datetime, time, timedelta
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path

root = Path(sys.argv[1]) / "xml-summary-qa"

def minutes(start, finish, weekdays, periods, holidays=()):
    a, b = datetime.fromisoformat(start), datetime.fromisoformat(finish)
    day = a.date()
    microseconds = 0
    while day <= b.date():
        if day.isoweekday() in weekdays and day.isoformat() not in holidays:
            midnight = datetime.combine(day, time())
            for first, last in periods:
                lo = max(a, midnight + timedelta(minutes=first))
                hi = min(b, midnight + timedelta(minutes=last))
                if lo < hi:
                    delta = hi - lo
                    microseconds += ((delta.days * 86400 + delta.seconds) * 1_000_000 + delta.microseconds)
        day += timedelta(days=1)
    return (Decimal(microseconds) / 60_000_000).quantize(Decimal("0.000001"), rounding=ROUND_HALF_UP)

data = json.loads((root / "remaining.json").read_text(encoding="utf-8"))
start = min(datetime.fromisoformat(row["start"]) for row in data["spans"]).isoformat()
finish = max(datetime.fromisoformat(row["finish"]) for row in data["spans"]).isoformat()
expected = minutes(start, finish, {1, 2, 3, 4, 5}, [(540, 548)])
assert Decimal(data["value"]) == expected, (data, expected)
cases = json.loads((root / "calendars.json").read_text(encoding="utf-8"))
for row in cases:
    expected = minutes(row["start"], row["finish"], row["weekdays"], [(540, 720), (780, row["lastMinute"])],
                       {"2026-11-11", "2026-11-16"})
    assert Decimal(row["value"]) == expected, (row, expected)
print(f"PASS independent summary durations: remaining forecast + {len(cases)} calendar/microsecond cases")
