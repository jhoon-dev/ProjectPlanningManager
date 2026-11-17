"""Independent two-project ledger, resource reservation and remaining-date oracle."""
import argparse
from bisect import bisect_left
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


def inventory(project):
    status = project["statusAt"]
    latest = {}
    for e in project["materialActuals"]:
        if e["statusAt"] <= status:
            latest[e["transactionId"]] = e
    received = consumed = movement_revision = 0
    remaining = {}
    delivered = defaultdict(int)
    confirm = None
    for e in latest.values():
        if e["kind"] == "receipt":
            received += e["quantityMilli"]
            delivered[e["receiptId"]] += e["quantityMilli"]
        if e["kind"] == "consume":
            consumed += e["quantityMilli"]
        if e["kind"] in ("receipt", "consume"):
            movement_revision = max(movement_revision, e["recordedRevision"])
        if e["kind"] == "remaining" and e["statusAt"] == status:
            old = remaining.get(e["useId"])
            if not old or old["recordedRevision"] < e["recordedRevision"]:
                remaining[e["useId"]] = e
        if e["kind"] == "confirm" and e["statusAt"] == status:
            if not confirm or confirm["recordedRevision"] < e["recordedRevision"]:
                confirm = e
    opening = project["resourcePlan"]["materials"][0]["stockMilli"]
    on_hand = opening + received - consumed
    assert confirm and confirm["quantityMilli"] == on_hand >= 0
    assert confirm["recordedRevision"] >= movement_revision
    uses = [dict(u, quantityMilli=remaining[u["id"]]["quantityMilli"])
            for u in project["resourcePlan"]["uses"]]
    receipts = [(datetime.fromisoformat(r["at"]), max(0, r["quantityMilli"] - delivered[r["id"]]))
                for r in project["resourcePlan"]["receipts"] if r["at"] > status]
    return dict(opening=opening, received=received, consumed=consumed, on_hand=on_hand,
                uses=uses, receipts=receipts)


cases = json.loads((root / "cases.json").read_text(encoding="utf-8-sig"))
assert len(cases) == 24
candidate_count = comparisons = balances = 0
for case in cases:
    projects = {pid: case[pid] for pid in ("P", "Q")}
    inventories = {pid: inventory(p) for pid, p in projects.items()}
    status = datetime.fromisoformat(projects["P"]["statusAt"])
    assert projects["P"]["statusAt"] == projects["Q"]["statusAt"]
    clock = []
    at = datetime.fromisoformat(projects["P"]["startDate"] + " 00:00")
    for _ in range(3 * 1440):
        m = at.hour * 60 + at.minute
        if at.weekday() < 5 and (540 <= m < 720 or 780 <= m < 1080):
            clock.append(at)
        at += timedelta(minutes=1)
    duration = {}
    edges = defaultdict(list)
    actual_start = {}
    nodes = []
    # Release nodes participate in serial priority, even though they use no resources.
    for pid, p in projects.items():
        gate = (pid, "release")
        nodes.append(gate)
        duration[gate] = 0
        progress = {e["taskId"]: e for e in p["progress"] if e["statusAt"] <= p["statusAt"]}
        for task in p["tasks"]:
            key = (pid, task["id"])
            nodes.append(key)
            fact = progress.get(task["id"])
            duration[key] = fact["remainingMinutes"] if fact else task["durationMinutes"]
            if fact and fact["actualStart"]:
                actual_start[key] = fact["actualStart"]
            else:
                edges[key].append((gate, "SS", 0))
            for pred in task["predecessors"]:
                edges[key].append(((pid, pred), "FS", 0))
    if case["linked"]:
        edges[("Q", "A")].append((("P", "B"), "FS", 15))
    index = {key: n for n, key in enumerate(nodes)}
    committed = {}
    occupied = defaultdict(int)

    def fits(rows):
        for pid, inv in inventories.items():
            events = defaultdict(int)
            events[status] += inv["on_hand"]
            for at, qty in inv["receipts"]:
                events[at] += qty
            for use in inv["uses"]:
                key = (pid, use["taskId"])
                if key not in rows:
                    continue
                row = rows[key]
                slots = row["slots"]
                qty = use["quantityMilli"]
                if use["accrual"] == "start":
                    events[row["start"]] -= qty
                elif use["accrual"] == "finish":
                    events[row["finish"]] -= qty
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

    while len(committed) < len(nodes):
        ready = [key for key in nodes if key not in committed and
                 all(pred in committed for pred, _, _ in edges[key])]
        priority = case["priority"]
        rank = lambda key: (duration[key] if priority == "shortest-first" else
                            -duration[key] if priority == "longest-first" else 0, index[key])
        key = min(ready, key=rank)
        first = bisect_left(clock, status)
        for pred, relation, lag in edges[key]:
            source = committed[pred]["finish"] if relation == "FS" else committed[pred]["start"]
            first = max(first, bisect_left(clock, source) + lag)
        accepted = None
        for start in range(first, len(clock) - max(1, duration[key]) + 1):
            candidate_count += 1
            slots = clock[start:start + duration[key]]
            row = dict(start=clock[start], finish=slots[-1] + timedelta(minutes=1) if slots else clock[start],
                       slots=slots)
            if slots and any(occupied[t] + 100 > case["capacity"] for t in slots):
                continue
            if fits({**committed, key: row}):
                accepted = row
                break
        assert accepted
        committed[key] = accepted
        for t in accepted["slots"]:
            occupied[t] += 100
    assert fits(committed)
    result = case["result"]
    assert result["policy"].endswith("/material-remaining-v1")
    for row in result["tasks"]:
        key = row["projectId"], row["taskId"]
        expected = committed[key]
        remaining_start = expected["start"].strftime("%Y-%m-%d %H:%M")
        assert row["start"] == actual_start.get(key, remaining_start), (case, row)
        assert row["remainingStart"] == remaining_start, (case, row, expected)
        assert row["finish"] == expected["finish"].strftime("%Y-%m-%d %H:%M"), (case, row, expected)
        if result.get("floatPolicy", "").endswith("/resource-material-float-v1"):
            assert 0 <= row["freeFloatMinutes"] <= row["totalFloatMinutes"]
            assert row["critical"] == (row["totalFloatMinutes"] == 0)
        else:
            assert row["totalFloatMinutes"] is None
        comparisons += 3
    for row in result["materialStocks"]:
        inv = inventories[row["projectId"]]
        assert row["materialId"] == "M" and row["confirmed"]
        for field, expected in (("onHandMilli", inv["on_hand"]), ("openingMilli", inv["opening"]),
                                ("receivedMilli", inv["received"]), ("consumedMilli", inv["consumed"]),
                                ("remainingUseMilli", sum(u["quantityMilli"] for u in inv["uses"])),
                                ("incomingMilli", sum(qty for _, qty in inv["receipts"]))):
            assert isinstance(row[field], str) and int(row[field]) == expected
            balances += 1

for pid in ("P", "Q"):
    native = json.loads((root / f"{pid}.ppm").read_text(encoding="utf-8-sig"))
    assert native["schemaVersion"] == 83
    canonical = json.dumps(native["payload"], ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
    assert hashlib.sha256(canonical).hexdigest() == native["sha256"]
    assert inventory(native["payload"])["on_hand"] == 0
workspace = json.loads((root / "workspace.ppmworkspace").read_text(encoding="utf-8-sig"))
assert workspace["schemaVersion"] == 2
book = openpyxl.load_workbook(root / "report.xlsx", data_only=True)
sheet = book["통합 예측의 실제 자재 재고"]
rows = [r for r in sheet.values if r[0] in ("P", "Q")]
assert len(rows) == 2
assert all(r[6:12] == ("2", "1", "3", "0", "2", "2") for r in rows)
pdf = PdfReader(root / "report.pdf")
text = re.sub(r"\s+", "", "".join(p.extract_text() for p in pdf.pages))
assert "통합예측의실제자재재고" in text and "material-remaining-v1" in text
print(json.dumps(dict(status="PASS", cases=len(cases), tasks=comparisons // 3,
                     exact_date_comparisons=comparisons, exact_balance_comparisons=balances,
                     exhaustive_candidates=candidate_count, native_schema=83, workspace_schema=2,
                     pdf_pages=len(pdf.pages)), indent=2))
