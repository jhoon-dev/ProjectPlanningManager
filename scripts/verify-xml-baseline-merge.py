"""Independently inspect retained XML baseline merge artifacts; does not run external products."""
import argparse
import hashlib
import json
import re
from decimal import Decimal
from pathlib import Path
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
parser.add_argument("--reports", action="store_true")
args = parser.parse_args()
load = lambda name: json.loads((args.root / name).read_text(encoding="utf-8"))
envelope = load("merged.ppm")
assert envelope["schemaVersion"] == 76
native = envelope["payload"]
events = native["xmlBaselineMerges"]
assert len(events) == 1
event = events[0]
assert event["confirmed"] is True and event["currency"] == "USD"
assert event["recordedRevision"] == native["revision"]
assert event["after"] == native["xmlBaselines"]
assert event["incoming"]["sourceSha256"] == hashlib.sha256((args.root / "source.xml").read_bytes()).hexdigest()
assert len(native["baselines"]) == 1 and native["baselines"][0]["id"] == "native-approved"
assert not native["progress"] and not native.get("periodActuals", [])
origins = {item["number"]: item["origin"] for item in event["after"]["origins"]}
assert set(origins) == {0, 1}
assert origins[0]["weekdays"] == [1] and origins[0]["sourceName"] == "previous.xml"
assert origins[1]["weekdays"] == event["incoming"]["weekdays"]

def key(row):
    return row["number"], row["kind"], row["id"], row["resourceId"]

links = {(link["kind"], link["sourceId"]): link for link in event["links"]}
selected = {row["number"] for row in event["incoming"]["rows"]}
expected = {key(row): row for row in event["before"]["rows"] if row["number"] not in selected}
for incoming in event["incoming"]["rows"]:
    row = dict(incoming)
    owner = links[("task" if row["kind"] == "assignment" else row["kind"], row["id"])]
    if owner["exclude"]:
        continue
    row["id"] = owner["targetId"]
    if row["kind"] == "assignment":
        resource = links[("resource", row["resourceId"])]
        if resource["exclude"]:
            continue
        row["resourceId"] = resource["targetId"]
    assert key(row) not in expected
    expected[key(row)] = row
actual = {key(row): row for row in event["after"]["rows"]}
assert actual == expected and len(actual) == 7

ns = {"p": "http://schemas.microsoft.com/project"}
text = lambda node, name: node.findtext("p:" + name, None, ns)
mapping = load("mapping.json")
ids = {(row["kind"], row["xmlUid"]): row["ppmId"] for row in mapping["mapping"] if "xmlUid" in row and "kind" in row}

def minutes(raw):
    if raw is None:
        return None
    match = re.fullmatch(r"PT(?:(\d+)H)?(?:(\d+)M)?(?:(\d+(?:\.\d+)?)S)?", raw)
    assert match, raw
    h, m, s = (Decimal(value or "0") for value in match.groups())
    return h * 60 + m + s / 60

root = ET.parse(args.root / "merged.xml").getroot()
seen = set()
timephased = 0
for wrapper, tag in [("Tasks", "Task"), ("Resources", "Resource"), ("Assignments", "Assignment")]:
    for parent in root.findall(f"p:{wrapper}/p:{tag}", ns):
        uid = text(parent, "UID")
        kind = ("project" if uid == "0" else "wbs" if text(parent, "Summary") == "1" else "task") if tag == "Task" else "resource" if tag == "Resource" else "assignment"
        owner = "" if kind == "project" else ids[(kind, uid)]
        rid = ""
        if kind == "assignment":
            owner, rid = owner.split("/")
        for node in parent.findall("p:Baseline", ns):
            row_key = int(text(node, "Number")), kind, owner, rid
            assert row_key not in seen
            seen.add(row_key)
            row = actual[row_key]
            for tag_name, field in [("Start", "start"), ("Finish", "finish")]:
                assert text(node, tag_name) == row[field]
            for tag_name, field in [("Duration", "duration"), ("Work", "work")]:
                assert minutes(text(node, tag_name)) == (Decimal(row[field]) / 1_000_000 if row[field] is not None else None)
            assert (Decimal(text(node, "Cost")) if text(node, "Cost") is not None else None) == row["cost"]
            for name, value in row["flags"].items():
                assert text(node, name) == value
            for name, value in row["money"].items():
                assert Decimal(text(node, name)) == value
            periods = node.findall("p:TimephasedData", ns)
            assert len(periods) == len(row["timephased"])
            for period, saved in zip(periods, row["timephased"]):
                assert text(period, "UID") == uid
                assert int(text(period, "Type")) == saved["type"]
                for tag_name, field in [("Value", "value"), ("Start", "start"), ("Finish", "finish")]:
                    assert text(period, tag_name) == saved[field]
                timephased += 1
assert seen == set(actual)
result = dict(status="PASS", baseline_rows=len(actual), explicit_links=len(links),
              preserved_old_number=0, adopted_number=1, timephased=timephased, schema=76,
              native_approvals_preserved=1, external_product_run=False)
if args.reports:
    import openpyxl
    from pypdf import PdfReader
    workbook = openpyxl.load_workbook(args.root / "fields.xlsx", data_only=True)
    fields = {field["name"]: "field:" + field["id"] for field in native["customFields"]}
    sheet = next(sheet for sheet in workbook if sheet.title.startswith("작업 사용자 필드"))
    cells = {(row[0], row[2]): row[4] for row in sheet.values if isinstance(row[2], str) and row[2].startswith("field:")}
    task = native["tasks"][0]["id"]
    assert Decimal(str(cells[task, fields["TW"]])) == 30
    assert Decimal(str(cells[task, fields["TC"]])) == Decimal("12.34")
    pdf = PdfReader(args.root / "fields.pdf")
    assert "12.34" in "".join(page.extract_text() or "" for page in pdf.pages)
    result.update(xlsx_sheets=len(workbook.sheetnames), pdf_pages=len(pdf.pages))
print(json.dumps(result, ensure_ascii=False, indent=2))
