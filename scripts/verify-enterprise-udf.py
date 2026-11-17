"""Independent XML/TSV/native/report verification; no MSP/P6 execution claim."""
import argparse
from decimal import Decimal
import importlib.util
import json
import re
from pathlib import Path
import uuid
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
parser.add_argument("--reader", action="store_true")
parser.add_argument("--reports", action="store_true")
args = parser.parse_args()
root = args.root
ns = {"p": "http://schemas.microsoft.com/project"}
matrix = {}
for filename in ("export.xml", "reexport.xml"):
    xml = ET.parse(root / filename).getroot()
    definitions = {}
    guids = set()
    for node in xml.findall("p:ExtendedAttributes/p:ExtendedAttribute", ns):
        identifier = int(node.findtext("p:FieldID", namespaces=ns))
        assert identifier not in definitions
        scope = next(scope for lo, hi, scope in (
            (0xB408000, 0xB417FFF, "TASK"), (0xB608000, 0xB617FFF, "PROJECT"),
            (0xC408000, 0xC417FFF, "RESOURCE"), (0xF408000, 0xF417FFF, "ASSIGNMENT"))
            if lo <= identifier <= hi)
        guid = uuid.UUID(node.findtext("p:Guid", namespaces=ns))
        assert guid.int and guid not in guids
        guids.add(guid)
        cf_type = node.findtext("p:CFType", namespaces=ns)
        assert cf_type in ("1", "5", "7")
        assert node.findtext("p:ElemType", namespaces=ns) == {
            "TASK": "20", "PROJECT": "20", "RESOURCE": "21", "ASSIGNMENT": "23"}[scope]
        definitions[identifier] = (scope, node.findtext("p:Alias", namespaces=ns), cf_type, str(guid))
    assert len(definitions) == 7
    actual = {}
    for wrapper, owner_name, scope in (("Tasks", "Task", "TASK"), ("Resources", "Resource", "RESOURCE"),
                                        ("Assignments", "Assignment", "ASSIGNMENT")):
        for owner in xml.findall(f"p:{wrapper}/p:{owner_name}", ns):
            uid = owner.findtext("p:UID", namespaces=ns)
            for node in owner:
                tag = node.tag.split("}")[-1]
                if tag[0] not in "bcf" or len(tag) != 7:
                    continue
                identifier = int(tag, 16)
                field_scope, alias, kind, _ = definitions[identifier]
                assert field_scope == scope or (scope == "TASK" and field_scope == "PROJECT" and uid == "0")
                value = node.text or ""
                if kind == "5":
                    assert Decimal(value).as_tuple().exponent >= -6
                key = field_scope, uid, alias
                assert key not in actual
                actual[key] = value
    assert actual == {
        ("TASK", "1", "기업 작업명"): "서울 & <설계>", ("TASK", "2", "기업 작업명"): "",
        ("PROJECT", "0", "계약 번호"): "P-2026-서울", ("PROJECT", "0", "프로젝트 검토일"): "2026-11-11T00:00:00",
        ("RESOURCE", "1", "기업 자원 점수"): "-2.123456",
        ("ASSIGNMENT", "1", "배정 점수"): "0", ("ASSIGNMENT", "2", "배정 점수"): "-12.000001",
        ("ASSIGNMENT", "1", "배정 메모"): "", ("ASSIGNMENT", "1", "배정 검토일"): "2026-11-12T00:00:00",
    }
    matrix[filename] = {str(identifier): value for identifier, value in definitions.items()}
assert matrix["export.xml"] == matrix["reexport.xml"]
module = importlib.util.spec_from_file_location("udf_oracle", Path(__file__).with_name("verify-dated-float-udf.py"))
oracle = importlib.util.module_from_spec(module)
module.loader.exec_module(oracle)
for filename, expected_projects in (("export.xer", 1), ("combined.xer", 2)):
    tables = oracle.tables(root / filename)
    definitions = {r["udf_type_id"]: r for r in tables["UDFTYPE"]}
    assignments = {r["taskrsrc_id"]: r for r in tables["TASKRSRC"]}
    tasks = {r["task_id"]: r for r in tables["TASK"]}
    assert len(tables["PROJECT"]) == expected_projects
    values = {}
    for row in tables["UDFVALUE"]:
        definition = definitions[row["udf_type_id"]]
        if definition["table_name"] != "TASKRSRC":
            continue
        assignment = assignments[row["fk_id"]]
        assert row["proj_id"] == assignment["proj_id"] == tasks[assignment["task_id"]]["proj_id"]
        key = row["proj_id"], tasks[assignment["task_id"]]["task_name"], definition["udf_type_label"]
        assert key not in values
        values[key] = row["udf_number"] if definition["logical_data_type"] == "FT_FLOAT" else row["udf_text"] if definition["logical_data_type"] == "FT_TEXT" else row["udf_date"]
    assert len(values) == 4 * expected_projects
    for project in {r["proj_id"] for r in tables["PROJECT"]}:
        assert values[project, "설계", "배정 점수"] == "0"
        assert values[project, "검토", "배정 점수"] == "-12.000001"
        assert values[project, "설계", "배정 메모"] == ""
native = json.loads((root / "native.json").read_text(encoding="utf-8"))
assert len(native["assignmentFields"]) == 3 and len(native["assignments"][0]["attributes"]) == 3
assert all("xmlIdentity" in f for key in ("customFields", "resourceFields", "projectFields", "assignmentFields") for f in native[key])
result = {"status": "PASS", "xml_definitions": 7, "xml_scalar_values": 9, "xer_projects": [1, 2],
          "native_assignment_fields": 3, "empty_zero_missing": "distinct"}
if args.reports:
    import openpyxl
    from pypdf import PdfReader
    book = openpyxl.load_workbook(root / "fields.xlsx", data_only=True)
    sheet = book["배정 사용자 필드"]
    rows = {(row[0].value, row[2].value): row for row in sheet.iter_rows()
            if row[0].value in ("T", "B")}
    assert len(rows) == 6
    zero = rows["T", "배정 점수"][3]
    assert zero.data_type == "n" and zero.value == 0
    assert Decimal(str(rows["B", "배정 점수"][3].value)) == Decimal("-12.000001")
    assert rows["T", "배정 메모"][3].value == "(빈 문자열)"
    assert rows["B", "배정 메모"][3].value == "미정"
    pdf = PdfReader(root / "fields.pdf")
    text = "\n".join(page.extract_text() for page in pdf.pages)
    normalized_text = re.sub(r"\s+", "", text)
    assert "배정사용자필드" in normalized_text and "-12.000001" in normalized_text
    result["reports"] = {"assignment_rows": 6, "numeric_zero": True, "pdf_pages": len(pdf.pages)}
if args.reader:
    import jpype
    import mpxj
    jpype.startJVM("-Dlog4j2.statusLoggerLevel=OFF")
    try:
        from org.mpxj.reader import UniversalProjectReader
        reader_results = []
        for filename in ("export.xml", "export.xer"):
            proxy = UniversalProjectReader().getProjectReaderProxy(str((root / filename).resolve()))
            try:
                reader = proxy.getProjectReader()
                if hasattr(reader, "setIgnoreErrors"):
                    reader.setIgnoreErrors(False)
                model = proxy.read()
                assert not list(model.getIgnoredErrors())
                fields = [f for f in model.getCustomFields() if str(f.getAlias()) == "배정 점수"]
                assert len(fields) == 1
                field_type = fields[0].getFieldType()
                raw_values = {str(a.getTask().getName()): a.get(field_type) for a in model.getResourceAssignments()}
                if filename.endswith("xml"):
                    # The pinned MSPDIReader only reads ExtendedAttribute values,
                    # not Microsoft's enterprise hex-element representation.
                    assert all(value is None for value in raw_values.values())
                    reader_results.append({"file": filename, "ignored_errors": 0,
                                           "definitions_read": True, "limitation": "MPXJ16.10.0 omits enterprise hex-element values; exact values checked independently above"})
                else:
                    values = {key: Decimal(str(value)) for key, value in raw_values.items()}
                    assert values == {"설계": Decimal(0), "검토": Decimal("-12.000001")}
                    reader_results.append({"file": filename, "ignored_errors": 0, "assignment_values": 2})
            finally:
                proxy.close()
        result["strict_reader"] = reader_results
    finally:
        jpype.shutdownJVM()
print(json.dumps(result, ensure_ascii=False, indent=2))
