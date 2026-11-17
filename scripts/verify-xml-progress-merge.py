"""Independent XML/native/append-chain and report checks for existing-document progress merge."""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
args = parser.parse_args()
root = args.root
load = lambda name: json.loads((root / name).read_text(encoding="utf-8"))
before, merged, corrected = [load(name + ".json") for name in ("before", "merged", "corrected")]
raw = (root / "original.xml").read_bytes()
digest = hashlib.sha256(raw).hexdigest()
ns = {"p": "http://schemas.microsoft.com/project"}
text = lambda element, key: element.findtext("p:" + key, namespaces=ns)
tasks = {text(t, "UID"): t for t in ET.fromstring(raw).findall("p:Tasks/p:Task", ns)}
assert merged["progress"][:len(before["progress"])] == before["progress"]
assert merged["revision"] == before["revision"] + 1
assert merged["documentId"] == before["documentId"]
assert merged["statusAt"] == before["statusAt"]
assert {k: v for k, v in before.items() if k not in ("progress", "revision")} == {
    k: v for k, v in merged.items() if k not in ("progress", "revision")}
assert len(merged["progress"]) == 5 and len(corrected["progress"]) == 10
assert corrected["progress"][:5] == merged["progress"]
def stamp(value):
    return None if value in (None, "", "NA") else datetime.fromisoformat(value).strftime("%Y-%m-%d %H:%M")
def duration(value):
    match = re.fullmatch(r"PT(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)S)?", value)
    assert match
    hours, minutes, seconds = [int(v or 0) for v in match.groups()]
    assert seconds % 60 == 0
    return hours * 60 + minutes + seconds // 60
for entry in merged["progress"][1:]:
    marker = re.match(r"\[XML-PROGRESS sha256=([0-9a-f]{64}) uid=(\d+)\] ", entry["reason"])
    assert marker and marker[1] == digest
    task = tasks[marker[2]]
    assert entry["taskId"] == "xml-task-" + marker[2] + "-native"
    assert entry["actualStart"] == stamp(text(task, "ActualStart"))
    assert entry["actualFinish"] == stamp(text(task, "ActualFinish"))
    assert entry["actualDurationMinutes"] == duration(text(task, "ActualDuration"))
    assert entry["remainingMinutes"] == duration(text(task, "RemainingDuration"))
    assert entry["recordedRevision"] == merged["revision"]
    assert entry["sourceDocumentId"] == merged["documentId"]
    assert not entry.get("remainingWorkUnitMinutes")
previous = {}
for entry in corrected["progress"]:
    assert entry["previousEntryId"] == previous.get(entry["taskId"], "")
    previous[entry["taskId"]] = entry["id"]
assert not corrected.get("costEntries") and not corrected.get("periodActuals")
envelope = load("source.ppm")
assert envelope["schemaVersion"] == 48 and envelope["payload"] == corrected

import openpyxl
from pypdf import PdfReader
book = openpyxl.load_workbook(root / "audit.xlsx", data_only=True)
sheet = next(s for s in book if s.title.startswith("XML 채택 작업의 진척 원장"))
entries = {e["id"]: e for e in corrected["progress"]}
rows = [r for r in sheet.values if r[1] in entries]
assert len(rows) == len(entries) == 10
for row in rows:
    entry = entries[row[1]]
    assert (row[0], row[2], row[5], row[6], row[9] or "", row[11]) == (
        entry["taskId"], entry["statusAt"], entry["actualDurationMinutes"], entry["remainingMinutes"],
        entry["previousEntryId"], entry["reason"])
    assert (row[10] == "선택") == (previous[entry["taskId"]] == entry["id"])
pdf = PdfReader(root / "audit.pdf")
content = "".join("".join(p.extract_text() or "" for p in pdf.pages).split())
assert digest in content and "XML채택작업의진척원장" in content
print(json.dumps(dict(status="PASS", source_sha256=digest, schema=48, original_entries=1,
                     merged_entries=5, corrected_entries=10, audit_rows=len(rows), pdf_pages=len(pdf.pages),
                     external_product_run=False), ensure_ascii=False, indent=2))
