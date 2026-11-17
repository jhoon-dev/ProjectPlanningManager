"""Independent ledger reconciliation and minute-lattice remaining-material scheduler."""
import argparse
from collections import defaultdict
from datetime import datetime, timedelta
import hashlib
import json
from pathlib import Path
import re

import openpyxl
from pypdf import PdfReader

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
root = parser.parse_args().root
cases = json.loads((root / "cases.json").read_text(encoding="utf-8-sig"))
assert len(cases) == 24
comparisons = candidates = 0


def inventory(project):
    status = project["statusAt"]
    plan = project["resourcePlan"]
    latest = {}
    for e in project["materialActuals"]:
        if e["statusAt"] <= status:
            latest[e["transactionId"]] = e
    stock = plan["materials"][0]["stockMilli"]
    delivered = defaultdict(int)
    remaining = {}
    confirmation = None
    last_movement = 0
    for e in latest.values():
        if e["at"] > status:
            continue
        if e["kind"] == "receipt":
            stock += e["quantityMilli"]
            delivered[e["receiptId"]] += e["quantityMilli"]
        if e["kind"] == "consume":
            stock -= e["quantityMilli"]
        if e["kind"] in ("receipt", "consume"):
            last_movement = max(last_movement, e["recordedRevision"])
        if e["kind"] == "remaining" and e["statusAt"] == status:
            if e["useId"] not in remaining or e["recordedRevision"] > remaining[e["useId"]]["recordedRevision"]:
                remaining[e["useId"]] = e
        if e["kind"] == "confirm" and e["statusAt"] == status:
            if not confirmation or e["recordedRevision"] > confirmation["recordedRevision"]:
                confirmation = e
    assert confirmation and confirmation["quantityMilli"] == stock >= 0
    assert confirmation["recordedRevision"] >= last_movement
    receipts = [(datetime.fromisoformat(r["at"]), max(0, r["quantityMilli"] - delivered[r["id"]]))
                for r in plan["receipts"] if r["at"] > status]
    uses = [dict(u, quantityMilli=remaining[u["id"]]["quantityMilli"]) for u in plan["uses"]]
    return stock, receipts, uses


for case in cases:
    p = case["input"]
    assert p["resourcePlan"]["autoDelayRemainingForReceipts"]
    stock, receipts, uses = inventory(p)
    status = datetime.fromisoformat(p["statusAt"])
    clock = []
    at = datetime.fromisoformat(p["startDate"] + " 00:00")
    for _ in range(2 * 1440):
        m = at.hour * 60 + at.minute
        if at.weekday() < 5 and (540 <= m < 720 or 780 <= m < 1080):
            clock.append(at)
        at += timedelta(minutes=1)
    tasks = {t["id"]: t for t in p["tasks"]}
    index = {t["id"]: n for n, t in enumerate(p["tasks"])}
    duration = {t: tasks[t]["durationMinutes"] for t in tasks}
    actual_start = {}
    for e in p["progress"]:
        if e["statusAt"] <= p["statusAt"]:
            duration[e["taskId"]] = e["remainingMinutes"]
            actual_start[e["taskId"]] = e["actualStart"]
    capacity = p["resources"][0]["maxUnits"]
    units = {a["taskId"]: a["units"] for a in p["assignments"]}
    committed = {}
    reserved = defaultdict(int)

    def fits(rows):
        events = defaultdict(int)
        events[status] += stock
        for at, qty in receipts:
            events[at] += qty
        for use in uses:
            if use["taskId"] not in rows:
                continue
            slots = rows[use["taskId"]]["slots"]
            qty = use["quantityMilli"]
            if use["accrual"] == "start":
                events[slots[0]] -= qty
            elif use["accrual"] == "finish":
                events[slots[-1] + timedelta(minutes=1)] -= qty
            else:
                previous = 0
                for n, at in enumerate(slots, 1):
                    cumulative = (2 * qty * n + len(slots)) // (2 * len(slots))
                    events[at + timedelta(minutes=1)] -= cumulative - previous
                    previous = cumulative
        balance = 0
        for at, qty in sorted(events.items()):
            assert at >= status
            balance += qty
            if balance < 0:
                return False
        return True

    while len(committed) < len(tasks):
        ready = [t for t in tasks if t not in committed and all(d in committed for d in tasks[t]["predecessors"])]
        priority = case["priority"]
        rank = lambda t: (duration[t] if priority == "shortest-first" else
                          -duration[t] if priority == "longest-first" else 0, index[t])
        task = min(ready, key=rank)
        first = next(n for n, at in enumerate(clock) if at >= status)
        for pred in tasks[task]["predecessors"]:
            first = max(first, committed[pred]["offset"] + duration[pred])
        accepted = None
        for start in range(first, len(clock) - duration[task] + 1):
            candidates += 1
            slots = clock[start:start + duration[task]]
            if any(reserved[at] + units[task] > capacity for at in slots):
                continue
            row = dict(offset=start, slots=slots)
            if fits({**committed, task: row}):
                accepted = row
                break
        assert accepted
        committed[task] = accepted
        for at in accepted["slots"]:
            reserved[at] += units[task]
    assert fits(committed)
    for row in case["rows"]:
        task = row["taskId"]
        slots = committed[task]["slots"]
        remaining_start = slots[0].strftime("%Y-%m-%d %H:%M")
        assert row["start"] == actual_start.get(task, remaining_start), (case, row)
        assert row["remainingStart"] == remaining_start, (case, row)
        assert row["finish"] == (slots[-1] + timedelta(minutes=1)).strftime("%Y-%m-%d %H:%M"), (case, row)
        comparisons += 3

native = json.loads((root / "source.ppm").read_text(encoding="utf-8-sig"))
assert native["schemaVersion"] == 83
body = native["payload"]
canonical = json.dumps(body, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
assert hashlib.sha256(canonical).hexdigest() == native["sha256"]
assert inventory(body)[0] == 500
assert len(body["materialActuals"]) == 9
assert len(body["scenarios"][0]["materialActuals"]) == 5
book = openpyxl.load_workbook(root / "report.xlsx", data_only=True)
assert "실제 자재 원장" in book.sheetnames and "상태일 실제 자재 재고" in book.sheetnames
assert any(v == "serial-material-remaining-v1" for row in book["잔여 자재 자동 대기"].values for v in row)
pdf = PdfReader(root / "report.pdf")
text = re.sub(r"\s+", "", "".join(p.extract_text() for p in pdf.pages))
assert "serial-material-remaining-v1" in text and "실제자재원장" in text
print(json.dumps(dict(status="PASS", cases=len(cases), tasks=comparisons // 3,
                     exact_date_comparisons=comparisons, exhaustive_candidates=candidates,
                     actual_stock_milli=500, ledger_rows=9, frozen_ledger_rows=5,
                     native_schema=83, pdf_pages=len(pdf.pages)), indent=2))
