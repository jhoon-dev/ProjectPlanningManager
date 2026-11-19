"""Independent XML/Decimal distribution projection check against frozen native execution output.

This reads native occupancy/day-charge QA inputs; it does not execute MS Project/P6.
"""
import argparse
import json
import re
from collections import defaultdict
from datetime import datetime, timedelta
from decimal import Decimal
from pathlib import Path
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
parser.add_argument("--reader", action="store_true")
args = parser.parse_args()
load = lambda name: json.loads((args.root / name).read_text(encoding="utf-8"))
native, mapping, adopted = load("native.json"), load("mapping.json"), load("adopted.json")
ns = {"p": "http://schemas.microsoft.com/project"}
text = lambda node, name: node.findtext("p:" + name, None, ns)
ids = {(r["kind"], r["xmlUid"]): r["ppmId"] for r in mapping["mapping"] if "kind" in r and "xmlUid" in r}
parents = {w["id"]: w["parentId"] for w in native["wbsNodes"]}
groups = {}
for task in native["tasks"]:
    values, group = set(), task["wbsId"]
    while group:
        assert group not in values
        values.add(group)
        group = parents[group]
    groups[task["id"]] = values

def duration(raw):
    if raw is None:
        return None
    m = re.fullmatch(r"PT(?:(\d+)H)?(?:(\d+)M)?(?:(\d+(?:\.\d+)?)S)?", raw)
    assert m, raw
    h, minute, second = (Decimal(s or "0") for s in m.groups())
    return h * 60 + minute + second / 60

def time_type(slot, kind, cost):
    return ((4 if kind == "assignment" else 7 if kind == "resource" else 9) if slot == 0 else
            16 + (slot - 1) * 6 + (0 if kind == "assignment" else 4 if kind == "resource" else 2)) + int(cost)

def records(name, projection):
    result = {}
    root = ET.parse(args.root / name).getroot()
    for wrapper, tag in [("Tasks", "Task"), ("Resources", "Resource"), ("Assignments", "Assignment")]:
        for parent in root.findall(f"p:{wrapper}/p:{tag}", ns):
            uid = text(parent, "UID")
            kind = ("project" if uid == "0" else "wbs" if text(parent, "Summary") == "1" else "task") if tag == "Task" else "resource" if tag == "Resource" else "assignment"
            owner = "" if kind == "project" else ids[(kind, uid)] if projection else uid
            resource = ""
            if kind == "assignment" and projection:
                owner, resource = owner.split("/")
            for baseline in parent.findall("p:Baseline", ns):
                slot = int(text(baseline, "Number"))
                assert not baseline.findall("p:TimephasedData", ns), "Native blocks must be in the parent, including resources"
                times = [t for t in parent.findall("p:TimephasedData", ns) if int(text(t, "Type")) in (time_type(slot, kind, False), time_type(slot, kind, True))]
                key = slot, kind, owner, resource
                assert key not in result
                result[key] = baseline, times, uid
    return result

source = {e["slot"]: e for e in load("execution.json")}
rows = records("project.xml", True)
assert set(k[0] for k in rows) == {0, 1}
count, minute_comparisons, unknown, zero_work = 0, 0, 0, 0
for (slot, kind, owner, resource), (baseline, times, uid) in rows.items():
    def included(item):
        return (kind == "project" or kind == "task" and item["task"] == owner or
                kind == "wbs" and owner in groups.get(item["task"], set()) or
                kind == "resource" and item["resource"] == owner or
                kind == "assignment" and (item["task"], item["resource"]) == (owner, resource))
    wanted = defaultdict(int)
    for item in source[slot]["work"]:
        if not included(item):
            continue
        at, finish = datetime.fromisoformat(item["start"]), datetime.fromisoformat(item["finish"])
        while at < finish:
            wanted[at] += item["units"]
            at += timedelta(minutes=1)
    want_cost = {}
    for item in source[slot]["cost"]:
        if included(item):
            date, amount = item["date"], item["amount"]
            want_cost[date] = None if amount is None or want_cost.get(date, 0) is None else want_cost.get(date, 0) + amount
    actual, costs, covered = {}, {}, set()
    total_work = Decimal(0)
    for item in times:
        count += 1
        assert text(item, "UID") == uid
        start, finish = datetime.fromisoformat(text(item, "Start")), datetime.fromisoformat(text(item, "Finish"))
        assert start < finish
        if int(text(item, "Type")) == time_type(slot, kind, True):
            assert text(item, "Unit") == "2" and finish - start == timedelta(days=1)
            assert start.hour == start.minute == start.second == 0
            key = start.date().isoformat()
            assert key not in costs
            value = text(item, "Value")
            costs[key] = None if value is None else int(Decimal(value))
            unknown += value is None
        else:
            assert text(item, "Unit") == "0"
            value = duration(text(item, "Value"))
            total_work += value
            zero_work += value == 0
            length = Decimal(int((finish - start).total_seconds())) / 60
            units = value * 100 / length
            assert units == units.to_integral_value(), "Native constant-density segment must be exact"
            at = start
            while at < finish:
                assert at not in covered, "Overlapping work blocks"
                covered.add(at)
                actual[at] = int(units)
                at += timedelta(minutes=1)
    assert {k: v for k, v in wanted.items() if v} == {k: v for k, v in actual.items() if v}, (slot, kind, owner)
    minute_comparisons += len(set(wanted) | set(actual))
    assert total_work == Decimal(sum(wanted.values())) / 100
    assert duration(text(baseline, "Work")) == total_work
    assert costs == want_cost, (slot, kind, owner, costs, want_cost)
    core = text(baseline, "Cost")
    assert (None if core is None else int(Decimal(core))) == (None if None in want_cost.values() else sum(want_cost.values()))
assert count > 50 and zero_work
assert int(Decimal(text(rows[(0, "project", "", "")][0], "Cost"))) == 1294
assert duration(text(rows[(0, "project", "", "")][0], "Work")) == Decimal("10.65")
assert int(Decimal(text(rows[(0, "task", "M", "")][0], "Cost"))) == 307
assert int(Decimal(text(rows[(0, "task", "D", "")][0], "Cost"))) == 244
assert sum(len(r["timephased"]) for r in adopted["xmlBaselines"]["rows"]) == count
assert all(r.get("timephasedOutside", False) for r in adopted["xmlBaselines"]["rows"] if r["timephased"])

def signatures(data):
    return {k: ([(child.tag, child.text) for child in b], [[(child.tag, child.text) for child in t] for t in ts]) for k, (b, ts, uid) in data.items()}

assert signatures(rows) == signatures(records("actuals.xml", True)), "Actuals export must use the same frozen baseline distribution"
again = records("reexported.xml", False)
assert sum(len(ts) for b, ts, uid in again.values()) == count
result = dict(status="PASS", baseline_rows=len(rows), timephased=count, minute_comparisons=minute_comparisons,
              zero_work_blocks=zero_work, unknown_cost_blocks=unknown, adopted_schema=77, external_product_run=False)
if args.reader:
    import jpype
    import mpxj
    if not jpype.isJVMStarted():
        jpype.startJVM()
    reader = jpype.JClass("org.mpxj.mspdi.MSPDIReader")()
    reader.setIgnoreErrors(False)
    project = reader.read(str((args.root / "project.xml").resolve()))
    assert not list(project.getIgnoredErrors())
    minute_unit = jpype.JClass("org.mpxj.TimeUnit").MINUTES
    assignment_uids = {r["ppmId"]: int(r["xmlUid"]) for r in mapping["mapping"] if r.get("kind") == "assignment"}
    checked = 0
    for assignment in project.getResourceAssignments():
        uid = int(assignment.getUniqueID())
        owner = next(k for k, v in assignment_uids.items() if v == uid)
        task, resource = owner.split("/")
        for slot in (0, 1):
            key = slot, "assignment", task, resource
            if key not in rows:
                continue
            baseline, ts, _ = rows[key]
            work = list(assignment.getRawTimephasedBaselineWork(slot))
            cost = list(assignment.getRawTimephasedBaselineCost(slot))
            total = sum((Decimal(str(w.getTotalAmount().convertUnits(minute_unit, project.getProjectProperties()).getDuration())) for w in work), Decimal(0))
            assert abs(total - duration(text(baseline, "Work"))) <= Decimal("0.000000001"), (key, total)
            expected_cost = Decimal(text(baseline, "Cost")) / 100
            assert abs(sum((Decimal(str(c.getTotalAmount())) for c in cost), Decimal(0)) - expected_cost) <= Decimal("0.000000001")
            checked += 1
    result["mpxj_assignments_checked"] = checked
print(json.dumps(result, ensure_ascii=False, indent=2))
