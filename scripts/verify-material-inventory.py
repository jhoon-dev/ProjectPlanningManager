"""Independent integer/civil-minute replay of planned material receipts and consumption."""
import argparse
import csv
import hashlib
import json
import re
from datetime import datetime, timedelta
from decimal import Decimal
from pathlib import Path

import openpyxl
from pypdf import PdfReader

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
root = parser.parse_args().root
envelope = json.loads((root / "source.ppm").read_text(encoding="utf-8-sig"))
assert envelope["schemaVersion"] == 81
payload = envelope["payload"]
canonical = json.dumps(payload, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()
assert hashlib.sha256(canonical).hexdigest() == envelope["sha256"]
plan = payload["resourcePlan"]
material = next(m for m in plan["materials"] if m["id"] == "M")
base = datetime.fromisoformat(material["stockAt"])


def working(stamp):
    n = stamp.hour * 60 + stamp.minute
    return stamp.weekday() < 5 and (540 <= n < 720 or 780 <= n < 1080)


# The unconstrained fixture uses the standard task calendar; no resource assignments.
timelines = {}
for task in payload["tasks"]:
    assert not task["predecessors"] and not task["breaks"] and task["taskType"] == "fixed-duration"
    at = datetime.fromisoformat(payload["startDate"] + " 09:00")
    start = at
    ends = []
    while len(ends) < task["durationMinutes"]:
        if working(at):
            ends.append(at + timedelta(minutes=1))
        at += timedelta(minutes=1)
    timelines[task["id"]] = start, ends[-1], ends


def cumulative(use, stamp):
    start, end, minutes = timelines[use["taskId"]]
    amount = use["quantityMilli"]
    if use["accrual"] == "start":
        return amount if stamp >= start else 0
    if use["accrual"] == "finish":
        return amount if stamp >= end else 0
    done = sum(t <= stamp for t in minutes)
    return (2 * amount * done + len(minutes)) // (2 * len(minutes))


def expected(stamp):
    if stamp < base:
        return [None] * 5
    received = sum(r["quantityMilli"] for r in plan["receipts"] if r["materialId"] == "M" and datetime.fromisoformat(r["at"]) <= stamp)
    consumed = sum(cumulative(u, stamp) - cumulative(u, base - timedelta(minutes=1)) for u in plan["uses"] if u["materialId"] == "M")
    left = material["stockMilli"] + received - consumed
    return received, consumed, left, max(0, left), max(0, -left)


with (root / "points.tsv").open(encoding="utf-8", newline="") as f:
    rows = list(csv.DictReader(f, delimiter="\t"))
assert len(rows) == 1440 and len({r["at"] for r in rows}) == 1440
negative = []
for row in rows:
    actual = [None if row[k] == "?" else int(row[k]) for k in ("received", "consumed", "remaining", "available", "shortage")]
    wanted = list(expected(datetime.fromisoformat(row["at"])))
    assert actual == wanted, (row, wanted)
    if wanted[2] is not None and wanted[2] < 0:
        negative.append(row["at"])
assert negative[0] == "2026-11-17 10:01" and len(negative) == 1
data = json.loads((root / "point.json").read_text(encoding="utf-8"))
assert data["contract"] == "ppm-material-inventory-v1" and data["mode"] == "plan" and data["quantityScale"] == 1000
assert data["at"] == negative[0] and data["documentId"] == payload["documentId"]
assert data["points"][0]["remainingMilli"] == -1 and data["points"][0]["consumedMilli"] == 7001
assert data["points"][0]["name"] == material["name"] and data["points"][0]["unit"] == material["unit"]
assert data["points"][0]["firstShortage"] == negative[0]
assert data["points"][1]["remainingMilli"] is None and data["points"][1]["receivedMilli"] == 1000
book = openpyxl.load_workbook(root / "point.xlsx", data_only=True)
values = list(book.worksheets[0].values)
assert Decimal(values[6][6]) == Decimal("-.001") and values[7][6] == "미정"
assert values[6][9] == negative[0]
inputs = openpyxl.load_workbook(root / "inputs.xlsx", data_only=True)
assert inputs["계획 자재 입고"].max_row == 11
assert inputs["자재 개시 재고 기준"].cell(7, 2).value == material["stockAt"]
pages = 0
for name in ("point", "inputs"):
    pdf = PdfReader(root / (name + ".pdf"))
    text = re.sub(r"\s+", "", "".join(p.extract_text() for p in pdf.pages))
    assert ("-0.001" in text and "미정" in text) if name == "point" else "계획자재입고" in text
    pages += len(pdf.pages)
print(json.dumps(dict(status="PASS", civil_minutes=len(rows), scalar_comparisons=len(rows) * 5,
                     first_shortage=negative[0], native_schema=81, json_points=2, xlsx_point_rows=2, pdf_pages=pages), indent=2))
