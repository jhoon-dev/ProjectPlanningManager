"""Independently sum non-overlapping XML daily fragments and reconcile native ledgers/reports."""
import argparse
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
args = parser.parse_args()
root = args.root
load = lambda name: json.loads((root / name).read_text(encoding="utf-8-sig"))
before, merged, corrected = [load(name + ".json") for name in ("before", "merged", "corrected")]
raw = (root / "original.xml").read_bytes()
digest = hashlib.sha256(raw).hexdigest()
xml = ET.fromstring(raw)
ns = {"p": "http://schemas.microsoft.com/project"}
text = lambda node, tag: node.findtext("p:" + tag, namespaces=ns)
assignments = {text(a, "UID"): a for a in xml.findall("p:Assignments/p:Assignment", ns)}
tasks = {text(t, "UID"): t for t in xml.findall("p:Tasks/p:Task", ns)}
assert text(xml, "CurrencyCode") == merged["currency"] == "USD"
assert merged["revision"] == before["revision"] + 1 == 6
assert merged["statusAt"] == before["statusAt"] == "2026-11-12 18:00"
assert merged["periodActuals"][:2] == before["periodActuals"]
assert merged["costEntries"][:1] == before["costEntries"]
assert {k: v for k, v in before.items() if k not in ("revision", "periodActuals", "costEntries")} == {
    k: v for k, v in merged.items() if k not in ("revision", "periodActuals", "costEntries")}
assert len(merged["periodActuals"]) == 7 and len(merged["costEntries"]) == 4
assert len(corrected["periodActuals"]) == 13 and len(corrected["costEntries"]) == 8
assert corrected["periodActuals"][:7] == merged["periodActuals"]
assert not merged["progress"]

def key(entry):
    return entry["taskId"], entry["resourceId"], entry["date"]
def asof(entries, status):
    selected = {}
    for entry in entries:
        if entry["statusAt"] <= status:
            old = selected.get(key(entry))
            if not old or old["statusAt"] <= entry["statusAt"]:
                selected[key(entry)] = entry
    return selected
def work_units(value):
    match = re.fullmatch(r"PT(?:(\d+(?:\.\d+)?)H)?(?:(\d+(?:\.\d+)?)M)?(?:(\d+(?:\.\d+)?)S)?", value)
    assert match
    hours, minutes, seconds = [Decimal(v or "0") for v in match.groups()]
    units = hours * 6000 + minutes * 100 + seconds * Decimal(5) / Decimal(3)
    assert units == units.to_integral_value()
    return int(units)
def cents(value):
    result = Decimal(value)
    assert result == result.to_integral_value()
    return int(result)

old = asof(before["periodActuals"], before["statusAt"])
costs = {c["id"]: c for c in merged["costEntries"]}
fragment_count = 0
aggregated_measures = 0
for entry in merged["periodActuals"][2:]:
    marker = re.match(r"\[XML-PERIOD sha256=([0-9a-f]{64}) assignment=(\d+)\] ", entry["reason"])
    assert marker and marker[1] == digest
    assignment = assignments[marker[2]]
    task_uid, resource_uid = text(assignment, "TaskUID"), text(assignment, "ResourceUID")
    assert entry["taskId"] == {"1": "TN-A", "2": "TN-B"}[task_uid]
    assert entry["resourceId"] == {"1": "NR-1", "2": "NR-2"}[resource_uid]
    values = {}
    for data in assignment.findall("p:TimephasedData", ns):
        kind = text(data, "Type")
        if kind not in ("2", "6") or text(data, "Start")[:10] != entry["date"]:
            continue
        assert text(data, "Unit") == "2"
        values.setdefault(kind, []).append(data)
    for kind, records in values.items():
        intervals = sorted((text(d, "Start"), text(d, "Finish")) for d in records)
        assert all(start < finish for start, finish in intervals)
        assert all(left[1] <= right[0] for left, right in zip(intervals, intervals[1:]))
        fragment_count += len(records)
        aggregated_measures += len(records) > 1
        parse = work_units if kind == "2" else cents
        values[kind] = sum(parse(text(d, "Value")) for d in records)
    previous = old.get(key(entry), {})
    assert entry["workUnitMinutes"] == (values["2"] if "2" in values else previous.get("workUnitMinutes"))
    assert entry["actualMinor"] == (values["6"] if "6" in values else previous.get("actualMinor"))
    assert entry["recordedRevision"] == 6 and entry["sourceDocumentId"] == merged["documentId"]
    assert entry["resourceType"] == ("equipment" if resource_uid == "1" else "labor")
    cost = costs[entry["costEntryId"]]
    assert cost["actualMinor"] == cents(text(tasks[task_uid], "ActualCost"))
    for field in ("reason", "reporter", "recordedAt", "statusAt", "recordedRevision", "sourceDocumentId", "currency"):
        assert entry[field] == cost[field]

assert fragment_count == 12 and aggregated_measures == 4
latest = asof(merged["periodActuals"], merged["statusAt"])
work = sum(e["workUnitMinutes"] or 0 for e in latest.values())
allocated = sum(e["actualMinor"] or 0 for e in latest.values())
assert work == 25501 and allocated == 4775
assert latest["TN-A", "NR-1", "2026-11-11"]["workUnitMinutes"] == 0
assert latest["TN-A", "NR-1", "2026-11-11"]["actualMinor"] == 0
previous_ids = {}
for entry in corrected["periodActuals"]:
    assert entry["previousEntryId"] == previous_ids.get(key(entry), "")
    previous_ids[key(entry)] = entry["id"]
previous_costs = {}
for cost in corrected["costEntries"]:
    assert cost["previousEntryId"] == previous_costs.get(cost["taskId"], "")
    previous_costs[cost["taskId"]] = cost["id"]
envelope = load("source.ppm")
assert envelope["schemaVersion"] == 11 and envelope["payload"] == corrected

import openpyxl
from pypdf import PdfReader
book = openpyxl.load_workbook(root / "audit.xlsx", data_only=True)
sheet = next(s for s in book if s.title.startswith("기간 실적 정정 이력"))
by_id = {e["id"]: e for e in corrected["periodActuals"]}
rows = [r for r in sheet.values if r[0] in by_id]
assert len(rows) == len(by_id) == 13
for row in rows:
    entry = by_id[row[0]]
    assert (row[1] or "", row[2], row[3], row[5], row[6], row[9], row[11], row[15] or "") == (
        entry["previousEntryId"], entry["taskId"], entry["resourceId"], entry["date"], entry["statusAt"],
        entry["currency"], entry["reason"], entry["costEntryId"])
    assert abs(Decimal(str(row[7])) * 6000 - Decimal(entry["workUnitMinutes"])) < Decimal("0.000001")
    if entry["actualMinor"] is None:
        assert row[8] == "미정"
    else:
        assert Decimal(str(row[8])) * 100 == entry["actualMinor"]
balance = next(s for s in book if s.title.startswith("누적 원가 배분 대사"))
balances = {r[0]: r for r in balance.values if r[0] in ("TN-A", "TN-B")}
assert balances["TN-A"][3:6] == (100, 39, 61)
assert balances["TN-B"][3:6] == (40, 10, 30)
pdf = PdfReader(root / "audit.pdf")
content = "".join("".join(p.extract_text() or "" for p in pdf.pages).split())
assert digest in content and "기간실적정정이력" in content
print(json.dumps(dict(status="PASS", source_sha256=digest, schema=11, merged_period_entries=7,
                     merged_cost_entries=4, corrected_period_entries=13, corrected_cost_entries=8,
                     work_units=work, allocated_minor=allocated, cumulative_minor=14000,
                     audit_rows=len(rows), pdf_pages=len(pdf.pages), source_fragments=fragment_count,
                     aggregated_measure_groups=aggregated_measures, external_product_run=False),
                 ensure_ascii=False, indent=2))
