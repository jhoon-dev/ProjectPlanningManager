"""Independent period/bounds/calendar oracle for the persisted shared-pool fixture."""
import argparse
import csv
import hashlib
import json
import re
from collections import defaultdict
from datetime import datetime, timedelta
from decimal import Decimal
from pathlib import Path

import openpyxl
from pypdf import PdfReader

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
args = parser.parse_args()
root = args.root


def read(name):
    return json.loads((root / name).read_text(encoding="utf-8-sig"))


def envelope(name):
    result = read(name)
    canonical = json.dumps(result["payload"], sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()
    assert hashlib.sha256(canonical).hexdigest() == result["sha256"], name
    return result


workspace = envelope("workspace.ppmworkspace")
assert workspace["schemaVersion"] == 4
resource = workspace["payload"]["resources"][0]
assert resource["availableFrom"] == "2026-11-16 09:15"
assert resource["availableTo"] == "2026-11-18 10:29"
assert resource["availability"][0]["from"] is None and resource["availability"][-1]["to"] is None
assert resource["availability"][1]["units"] is None
bindings = {(b["projectId"], b["resourceId"]): b["sharedId"] for b in workspace["payload"]["bindings"]}


def standard_work(stamp):
    minute = stamp.hour * 60 + stamp.minute
    return stamp.weekday() < 5 and (540 <= minute < 720 or 780 <= minute < 1080)


def working(stamp):
    calendar = resource["calendar"]
    return stamp.weekday() + 1 in calendar["weekdays"] and stamp.strftime("%Y-%m-%d") not in calendar["holidays"] and any(
        p["begin"] <= stamp.hour * 60 + stamp.minute < p["end"] for p in calendar["periods"])


def configured(stamp):
    at = stamp.strftime("%Y-%m-%d %H:%M")
    if resource["availableFrom"] and at < resource["availableFrom"] or resource["availableTo"] and at > resource["availableTo"]:
        return 0
    for day in resource["exceptions"]:
        if day["date"] == at[:10]:
            return day["units"]
    for period in resource["availability"]:
        if (period["from"] is None or at >= period["from"]) and (period["to"] is None or at <= period["to"]):
            return period["units"]
    return resource["maxUnits"]


demand = defaultdict(int)
for ref in workspace["payload"]["projects"]:
    project = envelope(ref["path"])["payload"]
    assert project["documentId"] == ref["documentId"]
    tasks = {t["id"]: t for t in project["tasks"]}
    for assignment in project["assignments"]:
        if bindings.get((ref["documentId"], assignment["resourceId"])) != resource["id"]:
            continue
        task = tasks[assignment["taskId"]]
        assert task["taskType"] == "fixed-duration" and not task["predecessors"] and not task["breaks"]
        stamp = datetime.fromisoformat(project["startDate"] + " 09:00")
        if task["constraintType"] == "SNET":
            stamp = max(stamp, datetime.fromisoformat(task["constraintAt"]))
        left = task["durationMinutes"]
        while left:
            if standard_work(stamp):
                demand[stamp] += assignment["units"]
                left -= 1
            stamp += timedelta(minutes=1)

with (root / "points.tsv").open(encoding="utf-8", newline="") as file:
    points = list(csv.DictReader(file, delimiter="\t"))
assert len(points) == 4320 and len({r["at"] for r in points}) == 4320
days = {}
for row in points:
    stamp = datetime.fromisoformat(row["at"])
    supply = configured(stamp) if working(stamp) else 0
    used = demand[stamp]
    left = None if supply is None else supply - used
    expected = [supply, used, left, None if left is None else max(0, left), None if left is None else max(0, -left)]
    actual = [None if row[k] == "?" else int(row[k]) for k in ("capacity", "demand", "remaining", "free", "shortage")]
    assert actual == expected, (row, expected)
    if used:
        date = row["at"][:10]
        daily = days.setdefault(date, dict(demandUnitMinutes=0, overloadMinutes=0, excessUnitMinutes=0, unknownCapacityMinutes=0, settings=set()))
        daily["settings"].add(configured(stamp))
        daily["demandUnitMinutes"] += used
        if supply is None:
            daily["unknownCapacityMinutes"] += 1
        elif used > supply:
            daily["overloadMinutes"] += 1
            daily["excessUnitMinutes"] += used - supply
for day in read("daily.json"):
    expected = days[day["date"]]
    settings = expected.pop("settings")
    expected["variableCapacity"] = len(settings) > 1
    expected["capacity"] = next(iter(settings)) if len(settings) == 1 else None
    assert all(day[k] == v for k, v in expected.items()), (day, expected)
integrated = read("integrated.json")
for day in integrated["sharedLoad"]:
    expected = days[day["date"]]
    for key in ("capacity", "variableCapacity", "overloadMinutes", "unknownCapacityMinutes"):
        assert day[key] == expected[key], (key, day)

pool_rows = 0
pdf_pages = 0
for name in ("known", "unknown"):
    data = read(name + ".json")["points"][0]
    assert data["demand"] == 125
    if name == "known":
        assert data["capacity"] == 200 and data["remaining"] == 75
    else:
        assert data["capacity"] is None and data["remaining"] is None
    book = openpyxl.load_workbook(root / (name + ".xlsx"), data_only=True)
    row = list(book.worksheets[0].values)[6]
    for column, key in enumerate(("capacity", "demand", "knownDemand", "remaining", "freeUnits", "shortage"), 3):
        value = data[key]
        assert row[column] == "미정" if value is None else Decimal(str(row[column])) == Decimal(value) / 100
    pool_rows += 1
    pdf = PdfReader(root / (name + ".pdf"))
    text = re.sub(r"\s+", "", "".join(p.extract_text() for p in pdf.pages))
    assert "125.0%" in text
    assert "75.0%" in text if name == "known" else "미정" in text
    pdf_pages += len(pdf.pages)
book = openpyxl.load_workbook(root / "integrated.xlsx", data_only=True)
load = book["통합 날짜의 공유 자원 부하"]
assert load.cell(7, 4).value == "시간별" and load.cell(8, 4).value == 75 and load.cell(9, 4).value == "시간별"
pdf = PdfReader(root / "integrated.pdf")
assert "시간별" in re.sub(r"\s+", "", "".join(p.extract_text() for p in pdf.pages))
pdf_pages += len(pdf.pages)
print(json.dumps(dict(status="PASS", civil_minutes=len(points), scalar_comparisons=len(points) * 5,
                     daily_rows=len(days), workspace_schema=4, xlsx_pool_rows=pool_rows, pdf_pages=pdf_pages), indent=2))
