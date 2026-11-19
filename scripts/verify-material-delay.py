"""Independent minute-lattice serial resource/material scheduler for native test fixtures."""
import argparse
import hashlib
import json
import re
from collections import defaultdict
from datetime import datetime, timedelta
from pathlib import Path

import openpyxl
from pypdf import PdfReader

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
root = parser.parse_args().root
cases = json.loads((root / "cases.json").read_text(encoding="utf-8"))
assert len(cases) == 24
compared = 0
candidate_count = 0
for case in cases:
    project = case["input"]
    assert project["resourcePlan"]["autoDelayForReceipts"] is True
    plan = project["resourcePlan"]
    material = plan["materials"][0]
    assert material["id"] == "M" and len(project["resources"]) == 1
    clock = []
    at = datetime.fromisoformat(project["startDate"] + " 00:00")
    for _ in range(2 * 1440):
        n = at.hour * 60 + at.minute
        if at.weekday() < 5 and (540 <= n < 720 or 780 <= n < 1080):
            clock.append(at)
        at += timedelta(minutes=1)
    tasks = {t["id"]: t for t in project["tasks"]}
    index = {t["id"]: n for n, t in enumerate(project["tasks"])}
    units = {a["taskId"]: a["units"] for a in project["assignments"]}
    capacity = project["resources"][0]["maxUnits"]
    committed = {}
    reserved = defaultdict(int)
    base = datetime.fromisoformat(material.get("stockAt", project["startDate"] + " 09:00"))

    def dates(task_id, start):
        duration = tasks[task_id]["durationMinutes"]
        slots = clock[start:start + duration]
        assert len(slots) == duration
        return dict(start=slots[0], finish=slots[-1] + timedelta(minutes=1), slots=slots, offset=start)

    def fits(trial):
        events = defaultdict(int)
        events[base] += material["stockMilli"]
        for receipt in plan["receipts"]:
            events[datetime.fromisoformat(receipt["at"])] += receipt["quantityMilli"]
        for use in plan["uses"]:
            if use["taskId"] not in trial:
                continue
            row = trial[use["taskId"]]
            quantity = use["quantityMilli"]
            if use["accrual"] in ("start", "finish"):
                events[row[use["accrual"]]] -= quantity
            else:
                previous = 0
                total = len(row["slots"])
                for n, slot in enumerate(row["slots"], 1):
                    cumulative = (2 * quantity * n + total) // (2 * total)
                    events[slot + timedelta(minutes=1)] -= cumulative - previous
                    previous = cumulative
        balance = 0
        for at, change in sorted(events.items()):
            if at < base and change < 0:
                return False
            balance += change
            if balance < 0:
                return False
        return True

    while len(committed) < len(tasks):
        ready = [t for t in tasks if t not in committed and all(d in committed for d in tasks[t]["predecessors"])]
        assert ready
        priority = case["priority"]
        rank = lambda t: ((tasks[t]["durationMinutes"] if priority == "shortest-first" else
                           -tasks[t]["durationMinutes"] if priority == "longest-first" else 0), index[t])
        task_id = min(ready, key=rank)
        first = max([committed[d]["offset"] + len(committed[d]["slots"]) for d in tasks[task_id]["predecessors"]] or [0])
        accepted = None
        for start in range(first, len(clock) - tasks[task_id]["durationMinutes"] + 1):
            candidate_count += 1
            row = dates(task_id, start)
            if any(reserved[t] + units[task_id] > capacity for t in row["slots"]):
                continue
            if fits({**committed, task_id: row}):
                accepted = row
                break
        assert accepted, case
        committed[task_id] = accepted
        for slot in accepted["slots"]:
            reserved[slot] += units[task_id]
    assert fits(committed)
    for row in case["rows"]:
        expected = committed[row["taskId"]]
        assert row["start"] == expected["start"].strftime("%Y-%m-%d %H:%M"), (case, expected)
        assert row["finish"] == expected["finish"].strftime("%Y-%m-%d %H:%M"), (case, expected)
        compared += 2

native = json.loads((root / "source.ppm").read_text(encoding="utf-8-sig"))
assert native["schemaVersion"] in (82, 84)
body = native["payload"]
canonical = json.dumps(body, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
assert hashlib.sha256(canonical).hexdigest() == native["sha256"]
assert body["resourcePlan"]["autoDelayForReceipts"]
assert body["planLeveling"]["policy"] == "serial-material-arrivals-v1"
assert body["baselines"][0]["input"]["resourcePlan"]["autoDelayForReceipts"]
if native["schemaVersion"] == 84:
    assert body["baselines"][0]["engineVersion"] == "ppm-adopted-material-leveling-1.0"
assert body["scenarios"][0]["input"]["resourcePlan"]["autoDelayForReceipts"]
book = openpyxl.load_workbook(root / "report.xlsx", data_only=True)
assert any("serial-material-arrivals-v1" == value for row in book["자재 입고 자동 대기"].values for value in row)
pdf = PdfReader(root / "report.pdf")
text = re.sub(r"\s+", "", "".join(p.extract_text() for p in pdf.pages))
assert "자재입고자동대기" in text and "serial-material-arrivals-v1" in text
print(json.dumps(dict(status="PASS", cases=len(cases), scheduled_tasks=compared // 2,
                     exact_date_comparisons=compared, exhaustive_candidates=candidate_count,
                     native_schema=native["schemaVersion"], pdf_pages=len(pdf.pages)), indent=2))
