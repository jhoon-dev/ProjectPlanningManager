"""Independent exhaustive minute-lattice CPM/resource/inventory oracle for tiny float fixtures."""
import argparse
import hashlib
import itertools
import json
import re
from collections import defaultdict
from datetime import datetime
from pathlib import Path

import openpyxl
from pypdf import PdfReader

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
root = parser.parse_args().root
cases = json.loads((root / "cases.json").read_text(encoding="utf-8"))
assert len(cases) == 27
candidate_count = feasible_count = comparisons = tasks_checked = 0

def minute(stamp):
    dt = datetime.fromisoformat(stamp)
    assert dt.date().isoformat() == "2026-11-17"
    return dt.hour * 60 + dt.minute - 540

for case in cases:
    shared = case.get("kind") == "shared"
    projects = case["projects"] if shared else [case["input"]]
    tasks, current, materials, receipts, uses, assignments, capacities = {}, {}, {}, [], [], [], {}
    qualify = lambda p, key: p["documentId"] + "/" + key if shared else key
    for p in projects:
        for t in p["tasks"]:
            t = dict(t)
            key = qualify(p, t["id"])
            t["predecessors"] = [qualify(p, d) for d in t["predecessors"]]
            for d in t.get("dependencyRules", []):
                d["predecessorId"] = qualify(p, d["predecessorId"])
            tasks[key] = t
        plan = p["resourcePlan"]
        for m in plan["materials"]:
            materials[qualify(p, m["id"])] = m
        receipts.extend((qualify(p, r["materialId"]), minute(r["at"]), r["quantityMilli"]) for r in plan["receipts"])
        uses.extend((qualify(p, u["materialId"]), qualify(p, u["taskId"]), u["quantityMilli"], u["accrual"]) for u in plan["uses"])
        for a in p["assignments"]:
            assignments.append((qualify(p, a["taskId"]), "pool" if shared else a["resourceId"], a))
        for r in p["resources"]:
            capacities["pool" if shared else r["id"]] = r["maxUnits"]
    if shared:
        for r in case["result"]["tasks"]:
            current[r["projectId"] + "/" + r["taskId"]] = dict(start=minute(r["start"]), finish=minute(r["finish"]),
                tf=r["totalFloatMinutes"], ff=r["freeFloatMinutes"], critical=r["critical"])
        ordered = sorted((current[k]["start"], current[k]["finish"], k) for k, _, _ in assignments)
        order = [(a[2], b[2]) for a, b in zip(ordered, ordered[1:])]
    else:
        current = {r["id"]: dict(r, start=minute(r["start"]), finish=minute(r["finish"])) for r in case["rows"]}
        order = [(r["from"], r["to"]) for r in case["links"]]
    horizon = max(r["finish"] for r in current.values())
    curved = case.get("kind") == "curved"

    def dates(key, start):
        t = tasks[key]
        if curved and key == "A":
            # Independent integration of relative units against the resource's 09:04..09:08 calendar.
            work, active = 0, []
            for at in range(start, 8):
                rel = at - start
                units = next((period["units"] for period in assignments[0][2]["periods"]
                              if period["fromMinute"] <= rel < period["toMinute"]), assignments[0][2]["units"])
                if at >= 4 and units:
                    work += units
                    active.append((at, units))
                if work >= t["workUnitMinutes"]:
                    return dict(start=start, finish=at + 1, active=active)
            return None
        finish = start + t["durationMinutes"]
        return dict(start=start, finish=finish, active=[(at, None) for at in range(start, finish)])

    def fits(trial):
        for key, r in trial.items():
            t = tasks[key]
            if r["finish"] > horizon:
                return False
            if t.get("constraintType") == "MSO" and r["start"] != minute(t["constraintAt"]):
                return False
            if t.get("constraintType") == "SNET" and r["start"] < minute(t["constraintAt"]):
                return False
            for d in t["predecessors"]:
                override = next((v for v in t.get("dependencyRules", []) if v["predecessorId"] == d), {})
                kind = override.get("type", "FS")
                left = trial[d]["finish" if kind[0] == "F" else "start"] + override.get("lagMinutes", 0)
                if left > r["finish" if kind[1] == "F" else "start"]:
                    return False
        if any(trial[a]["finish"] > trial[b]["start"] for a, b in order):
            return False
        load = defaultdict(int)
        for key, resource, assignment in assignments:
            for at, units in trial[key]["active"]:
                load[resource, at] += assignment["units"] if units is None else units
        if any(units > capacities[resource] for (resource, _), units in load.items()):
            return False
        events = defaultdict(lambda: defaultdict(int))
        for key, m in materials.items():
            events[key][minute(m.get("stockAt", "2026-11-17 09:00"))] += m["stockMilli"]
        for m, at, qty in receipts:
            events[m][at] += qty
        for m, key, qty, accrual in uses:
            r = trial[key]
            if accrual != "uniform":
                events[m][r[accrual]] -= qty
            else:
                slots = r["active"]
                prior, total = 0, len(slots)
                for n, (at, _) in enumerate(slots, 1):
                    cumulative = (2 * qty * n + total) // (2 * total)
                    events[m][at + 1] -= cumulative - prior
                    prior = cumulative
        for changes in events.values():
            stock = 0
            for _, delta in sorted(changes.items()):
                stock += delta
                if stock < 0:
                    return False
        return True

    keys = list(tasks)
    domains = {key: [r for start in range(current[key]["start"], horizon + 1)
                     if (r := dates(key, start)) is not None and r["finish"] <= horizon] for key in keys}
    maximum = {key: current[key]["start"] for key in keys}
    free = maximum.copy()
    for combination in itertools.product(*(domains[key] for key in keys)):
        candidate_count += 1
        trial = dict(zip(keys, combination))
        if not fits(trial):
            continue
        feasible_count += 1
        for key in keys:
            maximum[key] = max(maximum[key], trial[key]["start"])
            if all(trial[other]["start"] == current[other]["start"] for other in keys if other != key):
                free[key] = max(free[key], trial[key]["start"])
    for key in keys:
        r = current[key]
        tf, ff = maximum[key] - r["start"], free[key] - r["start"]
        assert (r["tf"], r["ff"], r["critical"]) == (tf, ff, tf == 0), (case.get("kind"), key, r, tf, ff)
        comparisons += 3
        tasks_checked += 1

for name, schema in (("baseline84.ppm", 84), ("historical82.ppm", 82)):
    native = json.loads((root / name).read_text(encoding="utf-8-sig"))
    assert native["schemaVersion"] == schema
    digest = hashlib.sha256(json.dumps(native["payload"], ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    assert digest == native["sha256"]
    expected = "jobs-adopted-material-leveling-1.0" if schema == 84 else "jobs-adopted-leveling-1.8"
    assert native["payload"]["baselines"][0]["engineVersion"] == expected
shared = json.loads((root / "shared.json").read_text(encoding="utf-8"))
assert shared["floatPolicy"].endswith("/resource-material-float-v1")
book = openpyxl.load_workbook(root / "shared.xlsx", data_only=True)
sheet = book["프로젝트 간 링크·통합 일정"]
report_rows = {r[0] + "/" + r[2]: r for r in sheet.values if r[0] in ("P", "Q")}
for r in shared["tasks"]:
    actual = report_rows[r["projectId"] + "/" + r["taskId"]]
    assert actual[10:13] == (r["totalFloatMinutes"], r["freeFloatMinutes"], "임계" if r["critical"] else "비임계")
pdf = PdfReader(root / "shared.pdf")
pdf_text = "".join(p.extract_text() for p in pdf.pages)
assert "resource-material-float-v1" in pdf_text
assert not re.search(r"task-\d+-\d+|release-\d+", pdf_text)
print(json.dumps(dict(status="PASS", cases=len(cases), tasks=tasks_checked, exact_float_critical_comparisons=comparisons,
                      exhaustive_combinations=candidate_count, feasible_combinations=feasible_count,
                      native_schemas=[82, 84], shared_export_rows=len(report_rows), pdf_pages=len(pdf.pages)), indent=2))
