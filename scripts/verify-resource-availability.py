"""Independent civil-minute capacity oracle for the retained resource availability fixture."""
import argparse
import json
import re
from datetime import datetime, timedelta
from decimal import Decimal
from pathlib import Path
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
parser.add_argument("--reader", action="store_true")
args = parser.parse_args()
native = json.loads((args.root / "native.json").read_text(encoding="utf-8"))
resource = native["resources"][0]
ns = {"p": "http://schemas.microsoft.com/project"}


def capacity(at):
    if resource["availableFrom"] and at < resource["availableFrom"]:
        return 0
    if resource["availableTo"] and at > resource["availableTo"]:
        return 0
    for day in resource["exceptions"]:
        if day["date"] == at[:10]:
            return day["units"]
    for period in resource["availability"]:
        if (not period["from"] or at >= period["from"]) and (not period["to"] or at <= period["to"]):
            return period["units"]
    return resource["maxUnits"]


def text(node, tag):
    return node.findtext("p:" + tag, None, ns)


comparisons = 0
for name in ("project.xml", "reexported.xml"):
    root = ET.parse(args.root / name).getroot()
    row = root.find("p:Resources/p:Resource", ns)
    assert text(row, "AvailableFrom") == resource["availableFrom"].replace(" ", "T") + ":00"
    assert text(row, "AvailableTo") == resource["availableTo"].replace(" ", "T") + ":59"
    periods = []
    for period in row.findall("p:AvailabilityPeriods/p:AvailabilityPeriod", ns):
        begin, end = text(period, "AvailableFrom"), text(period, "AvailableTo")
        periods.append((None if begin is None else datetime.fromisoformat(begin),
                        None if end is None else datetime.fromisoformat(end),
                        None if text(period, "AvailableUnits") is None else Decimal(text(period, "AvailableUnits")) * 100))
    assert periods[0][0] is None and periods[-1][1] is None
    for before, after in zip(periods, periods[1:]):
        assert before[1] + timedelta(seconds=1) == after[0], "Canonical table must have neither gaps nor overlaps"
    at = datetime(2026, 11, 15)
    while at < datetime(2026, 11, 19):
        matching = [value for start, end, value in periods if (start is None or at >= start) and (end is None or at <= end)]
        assert len(matching) == 1
        assert matching[0] == capacity(at.strftime("%Y-%m-%d %H:%M")), (name, at, matching)
        comparisons += 1
        at += timedelta(minutes=1)

from openpyxl import load_workbook
from pypdf import PdfReader

book = load_workbook(args.root / "fields.xlsx", data_only=True)
rows = list(book["자원 등록과 가용량"].values)
assert any("가용 경계" in row for row in rows)
assert any("기간" in row for row in rows)
fields = list(book["자원 사용자 필드"].values)
assert any("field:q" in row and 0.5 in row for row in fields)
pdf = PdfReader(args.root / "fields.pdf")
content = "".join(page.extract_text() or "" for page in pdf.pages)
assert "PPMUnitAvailability" in re.sub(r"\s+", "", content)
result = dict(status="PASS", xml_minute_comparisons=comparisons, canonical_periods=len(periods),
              xlsx_availability_rows=len(rows) - 6, pdf_pages=len(pdf.pages), external_product_run=False)
if args.reader:
    import jpype
    import mpxj
    if not jpype.isJVMStarted():
        jpype.startJVM()
    reader = jpype.JClass("org.mpxj.mspdi.MSPDIReader")()
    reader.setIgnoreErrors(False)
    project = reader.read(str((args.root / "project.xml").resolve()))
    assert not list(project.getIgnoredErrors())
    periods = list(project.getResources())[0].getAvailability()
    java_time = jpype.JClass("java.time.LocalDateTime")
    checked = 0
    at = datetime(2026, 11, 15)
    while at < datetime(2026, 11, 19):
        stamp = java_time.parse(at.isoformat())
        values = [p.getUnits() for p in periods if p.getRange().compareTo(stamp) == 0]
        assert len(values) == 1
        assert (None if values[0] is None else Decimal(str(values[0]))) == capacity(at.strftime("%Y-%m-%d %H:%M"))
        at += timedelta(minutes=1)
        checked += 1
    result["mpxj_minute_comparisons"] = checked
print(json.dumps(result, ensure_ascii=False, indent=2))
