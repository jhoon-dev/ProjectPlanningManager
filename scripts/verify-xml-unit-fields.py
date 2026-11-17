"""Check native/standard XML units and static reports independently of the C++ evaluator."""
import argparse
import json
import re
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
parser.add_argument("--reader", action="store_true")
args = parser.parse_args()
native = json.loads((args.root / "native.json").read_text(encoding="utf-8"))
mapping = json.loads((args.root / "mapping.json").read_text(encoding="utf-8"))["mapping"]
ns = {"p": "http://schemas.microsoft.com/project"}
xml = ET.parse(args.root / "project.xml").getroot()


def text(node, tag):
    return node.findtext("p:" + tag, None, ns)


def ratio(value):
    return None if value is None else Decimal(value) / 100


def availability(resource, day):
    return ratio(next((x["units"] for x in resource["exceptions"] if x["date"] == day), resource["maxUnits"]))


def rounded(value):
    return value.quantize(Decimal("0.000001"), rounding=ROUND_HALF_UP)


ids = {(r["kind"], r["xmlUid"]): r["jobsId"] for r in mapping if "xmlUid" in r}
fields = {(r["scope"], r["externalId"]): r["jobsId"] for r in mapping if r["kind"] == "udf"}
resources = {r["id"]: r for r in native["resources"]}
assignments = {r["taskId"] + "/" + r["resourceId"]: r for r in native["assignments"]}
expected = {}
for owner, a in assignments.items():
    u, m = ratio(a["units"]), ratio(resources[a["resourceId"]]["maxUnits"])
    expected[owner] = dict(U=u, UA=u * 2, M=m, MI=m, D=availability(resources[a["resourceId"]], "2026-11-17"),
                           ratio=rounded(u / m) if m else None, percent=f"{u * 100:.2f}%")
resource_expected = {}
for owner, r in resources.items():
    m = ratio(r["maxUnits"])
    day = next(a["value"][:10] for a in r["attributes"] if a["key"] == "field:when")
    resource_expected[owner] = dict(M=m, D=availability(r, day), U=None,
                                    native=None if m is None else m * 100,
                                    chain=None if m is None else m * 100 + 1)


def values(node, scope):
    result = {fields[(scope, text(x, "FieldID"))]: text(x, "Value")
              for x in node.findall("p:ExtendedAttribute", ns)}
    for (field_scope, external), name in fields.items():
        if field_scope == scope:
            raw = text(node, format(int(external), "x"))
            if raw is not None:
                result[name] = raw
    return result


def check_values(actual, wanted):
    for name, want in wanted.items():
        got = actual.get(name)
        if want is None:
            assert got is None, (name, got, want)
        elif isinstance(want, str):
            assert got == want, (name, got, want)
        else:
            assert got is not None and Decimal(str(got)) == want, (name, got, want)


checks = periods = 0
for x in xml.findall("p:Assignments/p:Assignment", ns):
    owner = ids[("assignment", text(x, "UID"))]
    assert Decimal(text(x, "Units")) == ratio(assignments[owner]["units"])
    check_values(values(x, "ASSIGNMENT"), expected[owner])
    checks += len(expected[owner])
for x in xml.findall("p:Resources/p:Resource", ns):
    owner = ids[("resource", text(x, "UID"))]
    r = resources[owner]
    assert (None if text(x, "MaxUnits") is None else Decimal(text(x, "MaxUnits"))) == ratio(r["maxUnits"])
    check_values(values(x, "RSRC"), resource_expected[owner])
    checks += len(resource_expected[owner])
    actual = x.findall("p:AvailabilityPeriods/p:AvailabilityPeriod", ns)
    assert len(actual) == len(r["exceptions"])
    for period, day in zip(actual, r["exceptions"]):
        assert text(period, "AvailableFrom") == day["date"] + "T00:00:00"
        assert text(period, "AvailableTo") == day["date"] + "T23:59:00"
        got = text(period, "AvailableUnits")
        assert (None if got is None else Decimal(got)) == ratio(day["units"])
        periods += 1
sources = [text(x, "Formula") for x in xml.findall("p:ExtendedAttributes/p:ExtendedAttribute", ns)]
assert "[Resource.Max Units]" in sources and "[Units]" in sources
assert "PPMUnitAvailability([Date1])" in sources

# XER supports the static values, not formula execution or the full availability model.
tables, name, columns = {}, None, []
for line in (args.root / "project.xer").read_text(encoding="utf-8").splitlines():
    row = line.split("\t")
    if row[0] == "%T":
        name, columns = row[1], []
        tables[name] = []
    elif row[0] == "%F":
        columns = row[1:]
    elif row[0] == "%R":
        tables[name].append(dict(zip(columns, row[1:])))
types = {r["udf_type_id"]: (r["table_name"], r["udf_type_label"]) for r in tables["UDFTYPE"]}
xer_values = {}
for r in tables["UDFVALUE"]:
    scope, label = types[r["udf_type_id"]]
    xer_values[(scope, r["fk_id"], label)] = r["udf_number"] or r["udf_text"]
for i, owner in enumerate(assignments, 1):
    check_values({k: xer_values.get(("TASKRSRC", str(i), k)) for k in expected[owner]}, expected[owner])

from openpyxl import load_workbook
from pypdf import PdfReader

book = load_workbook(args.root / "fields.xlsx", data_only=True)
sheet = book["배정 사용자 필드"]
report_values = {(str(a), str(b), str(c)): d for a, b, c, d, _ in list(sheet.values)[6:]}
for owner, wanted in expected.items():
    task, resource = owner.split("/")
    for key, value in wanted.items():
        got = report_values[(task, resource, key)]
        if value is None:
            assert got in ("미정", "오류"), (owner, key, got)
        elif isinstance(value, str):
            assert got == value
        else:
            assert Decimal(str(got)) == value
pdf = PdfReader(args.root / "fields.pdf")
pdf_text = re.sub(r"\s+", "", "".join(p.extract_text() or "" for p in pdf.pages))
for phrase in ("PPMUnitAvailability", "0.666667", "50.00%", "0으로나눌수없습니다."):
    assert phrase in pdf_text, phrase
result = dict(status="PASS", xml_formula_values=checks, availability_periods=periods,
              xer_assignment_values=sum(map(len, expected.values())), xlsx_assignment_values=len(report_values),
              pdf_pages=len(pdf.pages), external_product_run=False)
if args.reader:
    import jpype
    import mpxj
    if not jpype.isJVMStarted():
        jpype.startJVM()
    reader = jpype.JClass("org.mpxj.mspdi.MSPDIReader")()
    reader.setIgnoreErrors(False)
    project = reader.read(str((args.root / "project.xml").resolve()))
    assert not list(project.getIgnoredErrors())
    for a in project.getResourceAssignments():
        owner = ids[("assignment", str(a.getUniqueID()))]
        assert Decimal(str(a.getUnits())) == Decimal(assignments[owner]["units"])
    checked = 0
    for r in project.getResources():
        owner = ids[("resource", str(r.getUniqueID()))]
        want = resources[owner]["exceptions"]
        if not want:
            continue
        got = list(r.getAvailability())
        assert len(got) == len(want)
        for period, day in zip(got, want):
            assert str(period.getRange().getStart()).startswith(day["date"] + "T00:00")
            assert str(period.getRange().getEnd()).startswith(day["date"] + "T23:59")
            assert (None if period.getUnits() is None else Decimal(str(period.getUnits()))) == day["units"]
            checked += 1
    result.update(mpxj_assignments=4, mpxj_availability_periods=checked)
print(json.dumps(result, ensure_ascii=False, indent=2))
