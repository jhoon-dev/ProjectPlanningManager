"""Independent XML/Decimal and optional XLSX/PDF checks for external baseline exchange."""
import argparse
import json
import re
from datetime import datetime
from decimal import Decimal
from pathlib import Path
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
parser.add_argument("--reports", action="store_true")
parser.add_argument("--reader", action="store_true")
args = parser.parse_args()
load = lambda name: json.loads((args.root / name).read_text(encoding="utf-8"))
ns = {"p": "http://schemas.microsoft.com/project"}
text = lambda node, name: node.findtext("p:" + name, None, ns)

def duration(raw):
    if raw is None or raw == "NA":
        return None
    match = re.fullmatch(r"PT(?:(\d+)H)?(?:(\d+)M)?(?:(\d+(?:\.\d+)?)S)?", raw)
    assert match, raw
    h, m, s = (Decimal(value or 0) for value in match.groups())
    return h * 60 + m + s / 60

def date(raw):
    return datetime.fromisoformat(raw).isoformat() if raw and raw != "NA" else None

def records(filename, mapping):
    ids = {(r["kind"], r["xmlUid"]): r["jobsId"] for r in mapping["mapping"] if "kind" in r and "xmlUid" in r}
    root = ET.parse(args.root / filename).getroot()
    result = {}
    for wrapper, tag in [("Tasks", "Task"), ("Resources", "Resource"), ("Assignments", "Assignment")]:
        for parent in root.findall(f"p:{wrapper}/p:{tag}", ns):
            uid = text(parent, "UID")
            kind = "project" if tag == "Task" and uid == "0" else "task" if tag == "Task" and text(parent, "Summary") != "1" else "wbs" if tag == "Task" else "resource" if tag == "Resource" else "assignment"
            owner = "" if kind == "project" else ids[kind, uid]
            for baseline in parent.findall("p:Baseline", ns):
                number = int(text(baseline, "Number"))
                key = number, kind, owner
                assert key not in result, key
                assert 0 <= number <= 10
                result[key] = baseline, uid
    return result

native = load("native.json")
envelope = load("source.ppm")
assert envelope["schemaVersion"] == 75 and envelope["payload"] == native
assert not native["baselines"] and native["xmlBaselines"]["reporter"] == "PM"
rows = native["xmlBaselines"]["rows"]
mapping = load("mapping.json")
exported = records("export.xml", mapping)
source = records("source.xml", load("source-mapping.json"))
assert len(exported) == len(rows) == 6
assert len(source) == 16 and set(key[0] for key in source) == set(range(11))
timephased = 0
for row in rows:
    owner = row["id"] + ("/" + row["resourceId"] if row["kind"] == "assignment" else "")
    key = row["number"], row["kind"], owner
    output, uid = exported[key]
    original, _ = source[key]
    for node in (output, original):
        assert date(text(node, "Start")) == row["start"]
        assert date(text(node, "Finish")) == row["finish"]
        for xml, field in [("Duration", "duration"), ("Work", "work")]:
            want = Decimal(row[field]) / 1_000_000 if row[field] is not None else None
            assert duration(text(node, xml)) == want, (key, field, text(node, xml), want)
        raw = text(node, "Cost")
        assert (Decimal(raw) if raw is not None else None) == row["cost"]
        for flag, value in row["flags"].items():
            assert text(node, flag) == value
        for money, value in row["money"].items():
            assert Decimal(text(node, money)) == value
    times = output.findall("p:TimephasedData", ns)
    assert len(times) == len(row["timephased"])
    for node, saved in zip(times, row["timephased"]):
        assert text(node, "UID") == uid
        assert int(text(node, "Type")) == saved["type"]
        assert text(node, "Value") == saved["value"]
        assert date(text(node, "Start")) == saved["start"]
        assert date(text(node, "Finish")) == saved["finish"]
        timephased += 1
converted = records("converted.xml", mapping)
assert ET.parse(args.root / "converted.xml").getroot().findtext("p:CurrencyCode", None, ns) == "EUR"
for key, (node, _) in exported.items():
    other, _ = converted[key]
    if text(node, "Cost") is not None:
        assert Decimal(text(other, "Cost")) == Decimal(text(node, "Cost")) / 2
    for name in ["BCWS", "BCWP", "FixedCost"]:
        if text(node, name) is not None:
            assert Decimal(text(other, name)) == Decimal(text(node, name)) / 2
    for time in other.findall("p:TimephasedData", ns):
        if int(text(time, "Type")) in (17, 19):
            assert Decimal(text(time, "Value")) == Decimal("617")
projection = records("native-baselines.xml", load("native-projection-mapping.json"))
assert duration(text(projection[0, "task", "A"][0], "Work")) == Decimal("5.64")
assert duration(text(projection[1, "task", "A"][0], "Work")) == Decimal("2.07")
assert duration(text(projection[0, "task", "A"][0], "Duration")) == 8
assert duration(text(projection[0, "project", ""][0], "Work")) == Decimal("10.65")
assert duration(text(projection[0, "wbs", "G"][0], "Duration")) == 481
assert duration(text(projection[0, "assignment", "A/R"][0], "Work")) == 3
result = dict(status="PASS", external_rows=len(rows), source_rows=len(source), timephased=timephased,
              native_projection_rows=len(projection), currency="USD→EUR 1/2", external_product_run=False)
if args.reports:
    import openpyxl
    from pypdf import PdfReader
    workbook = openpyxl.load_workbook(args.root / "fields.xlsx", data_only=True)
    fields = {field["name"]: "field:" + field["id"] for field in native["customFields"]}
    sheet = next(sheet for sheet in workbook if sheet.title.startswith("작업 사용자 필드"))
    cells = {(row[0], row[2]): row[4] for row in sheet.values if isinstance(row[2], str) and row[2].startswith("field:")}
    first = native["tasks"][0]["id"]
    assert Decimal(str(cells[first, fields["TW"]])) == 30
    assert Decimal(str(cells[first, fields["TC"]])) == Decimal("12.34")
    pdf = PdfReader(args.root / "fields.pdf")
    content = "".join((page.extract_text() or "") for page in pdf.pages)
    assert "12.34" in content and "Baseline1" in content
    result.update(xlsx_sheets=len(workbook.sheetnames), pdf_pages=len(pdf.pages))
if args.reader:
    import jpype
    import mpxj
    if not jpype.isJVMStarted():
        jpype.startJVM()
    Reader = jpype.JClass("org.mpxj.mspdi.MSPDIReader")
    Minutes = jpype.JClass("org.mpxj.TimeUnit").MINUTES
    ET.register_namespace("", ns["p"])
    files = []
    for filename, cost, work in [("export.xml", Decimal("12.34"), Decimal(30)),
                                  ("converted.xml", Decimal("6.17"), Decimal(30)),
                                  ("native-baselines.xml", None, Decimal("2.07"))]:
        reader = Reader()
        reader.setIgnoreErrors(False)
        original = args.root / filename
        tree = ET.parse(original)
        changed = 0
        for element in tree.iter():
            if element.text and re.fullmatch(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{1,6}", element.text):
                element.text = element.text.split(".")[0]
                changed += 1
        if changed:
            try:
                reader.read(str(original.resolve()))
            except Exception as error:
                assert "DateTimeParseException" in str(error), error
            else:
                changed = 0
        target = original
        if changed:
            target = args.root / ("reader-seconds-" + filename)
            tree.write(target, encoding="utf-8", xml_declaration=True)
        project = reader.read(str(target.resolve()))
        assert not list(project.getIgnoredErrors())
        report = load("native-projection-mapping.json") if filename == "native-baselines.xml" else mapping
        owner = "A" if filename == "native-baselines.xml" else native["tasks"][0]["id"]
        uid = next(row["xmlUid"] for row in report["mapping"] if row.get("kind") == "task" and row["jobsId"] == owner)
        task = next(task for task in project.getTasks() if str(task.getUniqueID()) == uid)
        actual = Decimal(str(task.getBaselineWork(1).convertUnits(Minutes, project.getProjectProperties()).getDuration()))
        assert abs(actual - work) < Decimal("0.000000001"), (filename, actual, work)
        if cost is not None:
            assert Decimal(str(task.getBaselineCost(1))) == cost, (filename, task.getBaselineCost(1), cost)
        files.append(dict(file=filename, tasks=project.getTasks().size(), errors=0,
                          reader_only_date_fields_truncated=changed))
    result["mpxj"] = files
print(json.dumps(result, ensure_ascii=False, indent=2))
