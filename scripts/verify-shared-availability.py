"""Independent civil-minute oracle and JSON/XLSX/PDF checks for the two-project fixture."""
import argparse
import csv
import json
import re
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
    return json.loads((root / name).read_text(encoding="utf-8"))


def working(stamp):
    minute = stamp.hour * 60 + stamp.minute
    return stamp.weekday() < 5 and (540 <= minute < 720 or 780 <= minute < 1080)


# Deliberate fixture contract: one shared 100% pool, P/R+R2 and Q/R explicitly bound.
# Local capacities (75000%, 0%, 10%) must never be summed into this pool's supply.
demand = {}
for filename, bound in (("p.json", {"R", "R2"}), ("q.json", {"R"})):
    project = read(filename)
    tasks = {t["id"]: t for t in project["tasks"]}
    for assignment in project["assignments"]:
        if assignment["resourceId"] not in bound:
            continue
        task = tasks[assignment["taskId"]]
        assert task["taskType"] == "fixed-duration" and not task["predecessors"] and not task["breaks"]
        stamp = datetime.fromisoformat(project["startDate"] + " 09:00")
        if task.get("constraintType") == "SNET":
            stamp = max(stamp, datetime.fromisoformat(task["constraintAt"]))
        remaining = task["durationMinutes"]
        while remaining:
            if working(stamp):
                demand[stamp] = demand.get(stamp, 0) + assignment["units"]
                remaining -= 1
            stamp += timedelta(minutes=1)
with (root / "points.tsv").open(encoding="utf-8", newline="") as file:
    rows = list(csv.DictReader(file, delimiter="\t"))
assert len(rows) == 1440 and len({r["at"] for r in rows}) == 1440
for row in rows:
    stamp = datetime.fromisoformat(row["at"])
    capacity = 100 if working(stamp) else 0
    used = demand.get(stamp, 0)
    left = capacity - used
    assert [int(row[k]) for k in ("capacity", "demand", "remaining", "free", "shortage")] == [
        capacity, used, left, max(0, left), max(0, -left)], row

full, partial, forecast = [read(name + ".json") for name in ("full", "partial", "forecast")]
for result in (full, partial, forecast):
    assert result["contract"] == "ppm-shared-remaining-availability-v1"
    assert result["workspaceId"] == "shared-remaining" and result["generation"] == 5
    assert result["unit"] == "100=one-resource"
    assert result["unmappedResources"] == 1


def pool(result, name):
    return next(p for p in result["points"] if p["sharedId"] == name)


main = pool(full, "main")
assert [main[k] for k in ("capacity", "demand", "knownDemand", "remaining", "freeUnits", "shortage")] == [100, 150, 150, -50, 0, 50]
assert [(c["projectId"], c["revision"], c["demand"]) for c in main["contributions"]] == [("P", 3, 100), ("Q", 7, 50)]
assert full["occupancyCount"] == 3
assert pool(full, "unknown")["capacity"] is None and pool(full, "unknown")["demand"] == 0
assert pool(full, "unknown")["remaining"] is None
main = pool(partial, "main")
assert main["knownDemand"] == 100 and main["issue"]
assert all(main[k] is None for k in ("demand", "remaining", "freeUnits", "shortage"))
missing = next(s for s in partial["sources"] if s["projectId"] == "Q")
assert not missing["available"] and missing["revision"] is None and missing["forecast"] is None
assert main["contributions"][1]["revision"] is None
assert pool(partial, "spare")["freeUnits"] == 200
main = pool(forecast, "main")
assert main["knownDemand"] == 100 and main["demand"] is None and main["remaining"] is None
assert [(s["projectId"], s["forecast"], s["statusAt"]) for s in forecast["sources"]] == [
    ("P", True, "2026-11-16 10:00"), ("Q", True, "2026-11-16 10:30")]

sheet_rows = 0
pdf_pages = 0
for name, result in (("full", full), ("partial", partial)):
    book = openpyxl.load_workbook(root / (name + ".xlsx"), data_only=True)
    assert book.sheetnames == ["시각별 공유 잔여 가용량", "프로젝트별 수요 기여", "입력 프로젝트와 계산 기준"]
    sheet = book.worksheets[0]
    for row in list(sheet.values)[6:]:
        expected = pool(result, row[0])
        assert row[2] == result["at"]
        for column, key in enumerate(("capacity", "demand", "knownDemand", "remaining", "freeUnits", "shortage"), 3):
            value = expected[key]
            assert row[column] == "미정" if value is None else Decimal(str(row[column])) == Decimal(value) / 100
        sheet_rows += 1
    if name == "partial":
        # Missing project's revision and total contribution stay unknown in the detail export, too.
        assert book.worksheets[1].cell(8, 3).value == "미정"
        assert book.worksheets[1].cell(8, 4).value == "미정"
        assert book.worksheets[2].cell(8, 3).value == "미정"
    pdf = PdfReader(root / (name + ".pdf"))
    text = re.sub(r"\s+", "", "".join(page.extract_text() for page in pdf.pages))
    for label in ("시각별공유잔여가용량", "프로젝트별수요기여", "입력프로젝트와계산기준", "미정"):
        assert label in text, label
    if name == "full":
        assert "150.0%" in text and "-50.0%" in text
    else:
        assert "열려있지않습니다" in text and "확인된수요는하한" in text
    pdf_pages += len(pdf.pages)

print(json.dumps({"status": "PASS", "civil_minutes": len(rows), "scalar_comparisons": len(rows) * 5,
                  "json_cases": 3, "xlsx_pool_rows": sheet_rows, "pdf_pages": pdf_pages}, ensure_ascii=False, indent=2))
