"""Independent civil-minute and unchanged-input checks for dated effort exports."""
import argparse
from collections import defaultdict
from datetime import datetime, timedelta
import json
from pathlib import Path


def verify(root):
    cases = json.loads((root / "oracle.json").read_text(encoding="utf-8"))
    checked_minutes = 0
    for index, case in enumerate(cases):
        p = case["input"]
        task = p["tasks"][0]
        anchor = datetime.fromisoformat(p["startDate"])
        start = datetime.fromisoformat(p["statusAt"])
        calendars = {c["id"]: c for c in p["calendars"]}
        resources = {r["id"]: r for r in p["resources"]}
        assignments = p["assignments"]
        plans = {(v["resourceId"], d["date"]): d["workUnitMinutes"]
                 for v in p["remainingWork"] for d in v["days"]}

        def task_working(stamp):
            calendar = calendars[task["calendarId"]]
            minute = stamp.hour * 60 + stamp.minute
            return (stamp.isoweekday() in calendar["weekdays"] and stamp.date().isoformat() not in calendar["holidays"]
                    and any(v["begin"] <= minute < v["end"] for v in calendar["periods"])
                    and not any(datetime.fromisoformat(b["start"]) <= stamp < datetime.fromisoformat(b["finish"])
                                for b in task["breaks"]))

        def available(resource, stamp):
            calendar = calendars[resources[resource]["calendarId"]]
            minute = stamp.hour * 60 + stamp.minute
            return (task_working(stamp) and stamp.isoweekday() in calendar["weekdays"]
                    and stamp.date().isoformat() not in calendar["holidays"]
                    and any(v["begin"] <= minute < v["end"] for v in calendar["periods"]))

        expected = defaultdict(int)
        for (resource, date), work in plans.items():
            day = datetime.fromisoformat(date)
            minutes = [day + timedelta(minutes=i) for i in range(1440)
                       if day + timedelta(minutes=i) >= start and available(resource, day + timedelta(minutes=i))]
            assert minutes or work == 0
            if minutes:
                rate, extra = divmod(work, len(minutes))
                assert rate + bool(extra) <= 100000
                for i, stamp in enumerate(minutes):
                    if rate + (i < extra):
                        expected[(int((stamp - anchor).total_seconds() // 60), resource)] += rate + (i < extra)
        left = task["workUnitMinutes"] - sum(plans.values())
        assert left >= 0
        relative = 0
        at = start
        while left and at < start + timedelta(days=60):
            if task_working(at):
                active = {}
                for assignment in assignments:
                    resource = assignment["resourceId"]
                    if (resource, at.date().isoformat()) in plans or not available(resource, at):
                        continue
                    rate = assignment["units"]
                    for period in assignment.get("periods", []):
                        if period["fromMinute"] <= relative < period["toMinute"]:
                            rate = period["units"]
                    if rate:
                        active[resource] = rate
                total = sum(active.values())
                if total:
                    consume = min(left, total)
                    allocated = {r: rate * consume // total for r, rate in active.items()}
                    extra = consume - sum(allocated.values())
                    for resource in sorted(allocated):
                        if extra:
                            allocated[resource] += 1
                            extra -= 1
                        if allocated[resource]:
                            expected[(int((at - anchor).total_seconds() // 60), resource)] += allocated[resource]
                    left -= consume
                relative += 1
            at += timedelta(minutes=1)
        assert left == 0
        actual = {}
        for span in case["spans"]:
            assert span["finish"] > span["start"] and 0 < span["units"] <= 100000
            for minute in range(span["start"], span["finish"]):
                key = minute, span["resource"]
                assert key not in actual, (index, "overlapping demand", key)
                actual[key] = span["units"]
        assert actual == dict(expected), (index, "civil-minute demand")
        assert sum(actual.values()) == task["workUnitMinutes"]
        finish = anchor + timedelta(minutes=max(m for m, _ in expected) + 1)
        assert finish.strftime("%Y-%m-%d %H:%M") == case["finish"]
        checked_minutes += len(actual)
    before = json.loads((root / "before.json").read_text(encoding="utf-8"))
    after = json.loads((root / "after.json").read_text(encoding="utf-8"))
    expected = dict(before)
    expected["revision"] = before["revision"] + 1
    expected["remainingWork"] = after["remainingWork"]
    assert expected == after, "non-target plans/facts/baselines/metadata changed"
    float_cases = json.loads((root / "float-oracle.json").read_text(encoding="utf-8"))
    for row in float_cases:
        feasible = []
        for a in range(4):
            for b in range(6, 11):
                demand = [0] * 12
                rate, extra = divmod(row["quota"], 4 - a)
                for minute in range(a, 4):
                    demand[minute] += rate + (minute - a < extra)
                for minute in range(4, 6):
                    demand[minute] += 100
                for minute in range(b, b + 2):
                    demand[minute] += 100
                if max(demand) <= 100:
                    feasible.append((a, b))
        assert row["totalA"] == max(a for a, b in feasible)
        assert row["freeA"] == max(a for a, b in feasible if b == 6)
        assert row["totalB"] == max(b - 6 for a, b in feasible)
        assert row["freeB"] == max(b - 6 for a, b in feasible if a == 0)
    return {"status": "PASS", "civil_minute_cases": len(cases), "nonzero_resource_minutes": checked_minutes,
            "float_exhaustive_cases": len(float_cases), "native_before_after": "all unrelated JSON fields identical",
            "method": "Python datetime calendar/holiday/break intersection, reserved quotas and separate free-work ledger; no app libraries"}


def reports(root):
    from openpyxl import load_workbook
    from pypdf import PdfReader
    report = load_workbook(root / "report.xlsx", data_only=True)
    rows = list(report["날짜별 잔여 공수 입력"].values)[6:]
    assert {(r[3], r[4]) for r in rows} == {("2026-11-03", 2), ("2026-11-04", 0)}
    usage = load_workbook(root / "usage.xlsx", data_only=True)
    work_rows = [r for r in list(usage["시간축 공수와 원가"].values)[6:] if r[0] == "T" and r[2] == "R"]
    assert {r[4]: r[12] for r in work_rows if r[12]} == {"2026-11-03": 2, "2026-11-05": 6}
    assert {r[4]: r[14] for r in work_rows if r[14]} == {"2026-11-03": 240, "2026-11-05": 720}
    assert sum(r[6] for r in work_rows) == 16 and sum(r[9] for r in work_rows) == 16
    assert sum(r[12] for r in work_rows) == 8 and sum(r[14] for r in work_rows) == 960
    pdf = PdfReader(root / "report.pdf")
    assert pdf.pages and all(p.mediabox.width > 0 and p.mediabox.height > 0 for p in pdf.pages)
    text = "".join("\n".join(p.extract_text() for p in pdf.pages).split())
    assert "날짜별잔여공수입력" in text and "2026-11-03" in text and "2026-11-04" in text
    return {"status": "PASS", "report_sheets": len(report.sheetnames), "usage_sheets": len(usage.sheetnames),
            "pdf_pages": len(pdf.pages), "explicit_zero_and_target": "numeric XLSX values; PDF dates/text present"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--reports", action="store_true")
    args = parser.parse_args()
    print(json.dumps(reports(args.root) if args.reports else verify(args.root), ensure_ascii=False, indent=2))
