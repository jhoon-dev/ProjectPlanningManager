"""Independent Decimal/civil-minute checks for remaining availability artifacts."""
import argparse
import csv
import json
import re
from datetime import datetime, timedelta
from decimal import Decimal
from pathlib import Path
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
args = parser.parse_args()
native = json.loads((args.root / "native.json").read_text(encoding="utf-8"))
resource = native["resources"][0]
task_minutes = {t["id"]: t["durationMinutes"] for t in native["tasks"]}


def working(stamp):
    return stamp.weekday() < 5 and stamp.strftime("%Y-%m-%d") not in native["holidays"] and (
        9 * 60 <= stamp.hour * 60 + stamp.minute < 12 * 60 or 13 * 60 <= stamp.hour * 60 + stamp.minute < 18 * 60)


def capacity(stamp):
    at = stamp.strftime("%Y-%m-%d %H:%M")
    if not working(stamp) or (resource["availableFrom"] and at < resource["availableFrom"]) or (resource["availableTo"] and at > resource["availableTo"]):
        return 0
    for day in resource["exceptions"]:
        if day["date"] == at[:10]:
            return day["units"]
    for period in resource["availability"]:
        if (period["from"] is None or at >= period["from"]) and (period["to"] is None or at <= period["to"]):
            return period["units"]
    return resource["maxUnits"]


# The fixture's independent task clock starts all three unconstrained tasks at09:00.
demand = {}
for assignment in native["assignments"]:
    if assignment["resourceId"] != "R":
        continue
    at = datetime.fromisoformat(native["startDate"] + "T09:00:00")
    duration = task_minutes[assignment["taskId"]]
    while duration:
        if working(at):
            demand[at] = demand.get(at, 0) + assignment["units"]
            duration -= 1
        at += timedelta(minutes=1)
with (args.root / "points.tsv").open(encoding="utf-8", newline="") as f:
    points = list(csv.DictReader(f, delimiter="\t"))
for row in points:
    stamp = datetime.fromisoformat(row["at"])
    supply, used = capacity(stamp), demand.get(stamp, 0)
    balance = supply - used
    assert [int(row[k]) for k in ("capacity", "demand", "remaining", "free", "shortage")] == [supply, used, balance, max(0, balance), max(0, -balance)], row
assert len(points) == 1440

ns = {"p": "http://schemas.microsoft.com/project"}
xml = ET.parse(args.root / "project.xml").getroot()
mapping = json.loads((args.root / "mapping.json").read_text(encoding="utf-8"))["mapping"]
fields = {(r["scope"], r["externalId"]): r["jobsId"] for r in mapping if r["kind"] == "udf"}
expected = {"R": dict(load=Decimal("1.25"), remaining=Decimal("-.75"), native=Decimal(-75), chain=Decimal(-74)),
            "S": dict(load=Decimal(".25"), remaining=None, native=None, chain=None),
            "I": dict(load=Decimal(0), remaining=Decimal(2), native=Decimal(200), chain=Decimal(201))}
checked = 0
for node in xml.findall("p:Resources/p:Resource", ns):
    name = node.findtext("p:Name", None, ns)
    actual = {fields[("RSRC", a.findtext("p:FieldID", None, ns))]: a.findtext("p:Value", None, ns)
              for a in node.findall("p:ExtendedAttribute", ns)}
    for key, wanted in expected[name].items():
        got = actual.get(key)
        assert (None if got is None else Decimal(got)) == wanted, (name, key, got, wanted)
        checked += 1
formulas = [node.findtext("p:Formula", "", ns) for node in xml.findall("p:ExtendedAttributes/p:ExtendedAttribute", ns)]
assert "PPMResourceLoad([Date1])" in formulas and "PPMRemainingAvailability([Date1])" in formulas

tables, name, columns = {}, None, []
for line in (args.root / "project.xer").read_text(encoding="utf-8").splitlines():
    row = line.split("\t")
    if row[0] == "%T":
        name = row[1]
        tables[name] = []
    elif row[0] == "%F":
        columns = row[1:]
    elif row[0] == "%R":
        tables[name].append(dict(zip(columns, row[1:])))
types = {row["udf_type_id"]: row for row in tables["UDFTYPE"]}
actual = {}
for row in tables["UDFVALUE"]:
    field = types[row["udf_type_id"]]
    if field["table_name"] == "RSRC" and field["udf_type_label"] in expected["R"]:
        actual[(int(row["fk_id"]), field["udf_type_label"])] = Decimal(row["udf_number"])
for i, name in enumerate(expected, 1):
    for key, wanted in expected[name].items():
        assert actual.get((i, key)) == wanted

from openpyxl import load_workbook
from pypdf import PdfReader

book = load_workbook(args.root / "fields.xlsx", data_only=True)
rows = list(book["현재 부하와 잔여 가용량"].values)[6:]
assert len(rows) == 3
r = rows[0]
assert [Decimal(str(x)) for x in r[3:8]] == [Decimal(1), Decimal("1.25"), Decimal("-.25"), Decimal(0), Decimal(".25")], r
assert rows[1][3] == "미정" and rows[1][4] == .25 and rows[1][5] == "미정"
pdf = PdfReader(args.root / "fields.pdf")
content = re.sub(r"\s+", "", "".join(page.extract_text() or "" for page in pdf.pages))
assert "PPMRemainingAvailability" in content and "PPMResourceLoad" in content
assert "잔여가용량" in content and "-25.0%" in content
print(json.dumps(dict(status="PASS", civil_minute_points=len(points), scalar_comparisons=len(points) * 5,
                     xml_resource_values=checked, xer_resource_values=len(actual), xlsx_resource_rows=len(rows),
                     pdf_pages=len(pdf.pages), external_product_run=False), ensure_ascii=False, indent=2))
