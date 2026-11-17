"""Independent civil-minute/native/OOXML/PDF checks for work-contour inversion."""
from datetime import datetime, timedelta
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re
import sys
import unicodedata
import zipfile

from openpyxl import load_workbook
from pypdf import PdfReader


def minute_oracles(root):
    cases = json.loads((root / "minute-oracle.json").read_text(encoding="utf-8-sig"))
    assert len(cases) == 80
    for case in cases:
        p = case["input"]
        task = p["tasks"][0]
        calendars = {c["id"]: c for c in p["calendars"]}
        resources = {r["id"]: r for r in p["resources"]}
        cal = calendars[task["calendarId"]]
        gaps = [(datetime.fromisoformat(b["start"]), datetime.fromisoformat(b["finish"])) for b in task["breaks"]]
        target = task["workUnitMinutes"]
        total = offset = 0
        finish = None
        start = datetime.fromisoformat(p["startDate"])
        for day in range(50):
            date = start + timedelta(days=day)
            iso = date.strftime("%Y-%m-%d")
            if date.isoweekday() not in cal["weekdays"] or iso in cal["holidays"]:
                continue
            for period in cal["periods"]:
                for minute in range(period["begin"], period["end"]):
                    at = date + timedelta(minutes=minute)
                    if any(a <= at < b for a, b in gaps):
                        continue
                    units = 0
                    for assignment in p["assignments"]:
                        resource = resources[assignment["resourceId"]]
                        rcal = calendars[resource["calendarId"]]
                        active = date.isoweekday() in rcal["weekdays"] and iso not in rcal["holidays"]
                        active = active and any(v["begin"] <= minute < v["end"] for v in rcal["periods"])
                        if active:
                            rates = [v["units"] for v in assignment["periods"] if v["fromMinute"] <= offset < v["toMinute"]]
                            assert len(rates) <= 1
                            units += rates[0] if rates else assignment["units"]
                    offset += 1
                    total += min(units, target - total)
                    if total == target:
                        finish = at + timedelta(minutes=1)
                        break
                if finish:
                    break
            if finish:
                break
        assert finish is not None
        assert finish.strftime("%Y-%m-%d %H:%M") == case["finish"], case["sample"]
        assert total == case["integerWork"] == case["workUnitMinutes"]
    return len(cases)


def verify(build):
    root = build / "work-contour-qa"
    native = json.loads((root / "work.ppm").read_text(encoding="utf-8-sig"))
    assert native["schemaVersion"] == 24
    p = native["payload"]
    canonical = json.dumps(p, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
    assert hashlib.sha256(canonical).hexdigest() == native["sha256"]
    task, assignment = p["tasks"][0], p["assignments"][0]
    assert task["taskType"] == "fixed-work" and task["workDriven"]
    assert task["durationMinutes"] == 960 and task["workUnitMinutes"] == 48000 and assignment["units"] == 100
    assert assignment["periods"][-1] == {"fromMinute": 1500, "toMinute": 1700, "units": 7}
    ledger = []
    for offset in range(960):
        rates = [v["units"] for v in assignment["periods"] if v["fromMinute"] <= offset < v["toMinute"]]
        assert len(rates) <= 1
        ledger.append(rates[0] if rates else assignment["units"])
    assert sum(ledger) == 48000 and sum(ledger[:-1]) < 48000
    assert sum(ledger[:480]) == sum(ledger[480:]) == 24000

    workbook = load_workbook(root / "work.xlsx", data_only=True)
    rows = lambda name: list(workbook[name].values)[6:]
    assert rows("계획 일정")[0][3:6] == (960, "2026-11-02 09:00", "2026-11-03 18:00")
    assert rows("공수와 작업 유형")[0][4:7] == (8, 8, 16)
    span_work = Decimal(0)
    for row in rows("실행 배정 구간"):
        a, b = (datetime.fromisoformat(row[i]) for i in (2, 3))
        rate = Decimal(str(row[4]))
        minutes = int((b - a).total_seconds() // 60)
        # Independent integration of exported actual civil spans, including breaks.
        assert a.date() == b.date() and a.weekday() < 5
        assert (a.hour >= 9 and b.hour <= 12) or (a.hour >= 13 and b.hour <= 18)
        span_work += minutes * rate * 100
        assert Decimal(str(row[5])) == Decimal(minutes) * rate / 60
    assert span_work == 48000
    assert rows("작업별 원가")[0][2] == 720
    assert [r[1] for r in rows("기간별 원가")] == [240, 480]
    for row in rows("기간별 계획 배정 정의"):
        assert isinstance(row[4], int) and isinstance(row[6], (int, float))
    usage = load_workbook(root / "usage.xlsx", data_only=True)
    entries = [r for r in list(usage.worksheets[0].values)[6:] if r[2] == "R"]
    assert [r[6] for r in entries] == [4, 4, 0]
    assert [r[8] for r in entries] == [240, 480, 0]
    for path in [root / "work.xlsx", root / "usage.xlsx"]:
        book = load_workbook(path, data_only=False)
        assert not any(c.data_type == "f" for s in book for row in s for c in row)
        with zipfile.ZipFile(path) as archive:
            assert not any("externalLinks" in n or "vbaProject" in n for n in archive.namelist())
    pdf = PdfReader(root / "work.pdf", strict=True)
    text = unicodedata.normalize("NFKC", "\n".join(page.extract_text() or "" for page in pdf.pages))
    compressed = re.sub(r"\s+", "", text)
    for phrase in ["고정공수", "기간별계획배정정의", "2026-11-03"]:
        assert phrase in compressed, phrase
    assert not any(ord(c) < 32 and c not in "\n\r\t" for c in text)
    return {"status": "passed", "schema": 24, "independent_minute_cases": minute_oracles(root),
            "duration_minutes": 960, "exact_work_unit_minutes": 48000,
            "daily_hours": [4, 4], "daily_cost": [240, 480], "cost_total": 720,
            "outside_contour_preserved": True, "xlsx_numeric_and_no_executable_formulas": True,
            "pdf_pages": len(pdf.pages)}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-governance")),
                     ensure_ascii=False, indent=2))
