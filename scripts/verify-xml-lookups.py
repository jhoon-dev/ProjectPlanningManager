"""Independent XML lookup/reference and report inspection; does not run Microsoft Project."""
import argparse
from decimal import Decimal
import json
from pathlib import Path
import re
import uuid
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
parser.add_argument("--reports", action="store_true")
parser.add_argument("--reader", action="store_true")
args = parser.parse_args()
ns = {"p": "http://schemas.microsoft.com/project"}

def text(node, name, default=""):
    return node.findtext("p:" + name, default, ns)

def read(path):
    root = ET.parse(path).getroot()
    tables, ids, guids = {}, set(), set()
    for table in root.findall("p:OutlineCodes/p:OutlineCode", ns):
        key = str(uuid.UUID(text(table, "Guid")))
        assert key not in tables
        values = {}
        for node in table.findall("p:Values/p:Value", ns):
            identifier = text(node, "ValueID")
            guid = str(uuid.UUID(text(node, "FieldGUID")))
            assert not identifier or identifier not in ids
            assert guid not in guids
            ids.add(identifier)
            guids.add(guid)
            values[guid] = (identifier, text(node, "Value"), text(node, "ParentValueID"), text(node, "Type"), text(node, "Description"))
        tables[key] = values
    definitions = {}
    for node in root.findall("p:ExtendedAttributes/p:ExtendedAttribute", ns):
        identifier = text(node, "FieldID")
        assert identifier not in definitions
        table = text(node, "Ltuid")
        if table:
            assert table in tables
            values = tables[table]
            default_guid = text(node, "DefaultGuid")
            if default_guid:
                assert default_guid in values
                assert values[default_guid][0] == text(node, "Default")
        else:
            values = {}
            for entry in node.findall("p:ValueList/p:Value", ns):
                value_id = text(entry, "ID")
                assert value_id not in ids
                ids.add(value_id)
                values[value_id] = (value_id, text(entry, "Value"), "", "", text(entry, "Description"))
            assert text(node, "Default") in values
        definitions[identifier] = (text(node, "Alias"), table, values, int(text(node, "MaxMultiValues", "1")))
    selected = {}
    for wrapper, tag, scope in (("Tasks", "Task", "TASK"), ("Resources", "Resource", "RESOURCE"), ("Assignments", "Assignment", "ASSIGNMENT")):
        for owner in root.findall(f"p:{wrapper}/p:{tag}", ns):
            owner_id = text(owner, "UID")
            for entry in owner:
                name = entry.tag.split("}")[-1]
                if name in ("ExtendedAttribute", "OutlineCode", "EnterpriseExtendedAttribute"):
                    field_id = text(entry, "FieldID")
                    if name == "EnterpriseExtendedAttribute":
                        assert int(text(entry, "FieldIDInHex"), 16) == int(field_id)
                    alias, table_id, values, maximum = definitions[field_id]
                    guid = text(entry, "FieldValueGUID") or text(entry, "ValueGUID")
                    value_id = text(entry, "ValueID")
                    raw = text(entry, "Value")
                    if guid:
                        assert guid in values
                        value = values[guid]
                        assert not value_id or value_id == value[0]
                        if entry.find("p:Value", ns) is not None:
                            assert raw == value[1]
                    elif value_id:
                        value = next(v for v in values.values() if v[0] == value_id)
                    else:
                        matches = [v for v in values.values() if v[1] == raw]
                        assert len(matches) <= 1
                        value = matches[0] if matches else ("", raw, "", "", "")
                elif re.fullmatch("[bcf][0-9a-f]{6}", name):
                    field_id = str(int(name, 16))
                    alias, table_id, values, maximum = definitions[field_id]
                    assert table_id
                    value = values[str(uuid.UUID(entry.text))]
                else:
                    continue
                key = (scope, owner_id, alias)
                selected.setdefault(key, []).append(value[1])
                assert not maximum or len(selected[key]) <= maximum
    expected = {
        ("TASK", "1", "지역"): ["서울 & <설계>"],
        ("TASK", "1", "기업 다중 지역"): ["서울 & <설계>", "부산"],
        ("TASK", "2", "기업 다중 지역"): ["부산"],
        ("TASK", "0", "프로젝트 등급"): ["0"],
        ("RESOURCE", "1", "자원 검토일"): ["2026-11-11T00:00:00"],
        ("RESOURCE", "1", "자원 계층"): ["서울 & <설계>"],
        ("ASSIGNMENT", "1", "배정 다중 지역"): ["서울 & <설계>", "부산"],
        ("ASSIGNMENT", "2", "배정 다중 지역"): ["부산"],
        ("ASSIGNMENT", "1", "배정 열린 목록"): ["목록 밖 직접 입력"],
    }
    assert {key: sorted(values) for key, values in selected.items()} == {key: sorted(values) for key, values in expected.items()}
    assert len(definitions) == 7 and len(tables) == 4
    assert Decimal(next(v[1] for vs in tables.values() for v in vs.values() if v[1] == "-2.123456")) == Decimal("-2.123456")
    return definitions, tables

first = read(args.root / "export.xml")
second = read(args.root / "reexport.xml")
assert first == second
native = json.loads((args.root / "native.json").read_text(encoding="utf-8"))
assert sum(len(native[key]) for key in ("customFields", "projectFields", "resourceFields", "assignmentFields")) == 7
assert all("xmlLookup" in f for key in ("customFields", "projectFields", "resourceFields", "assignmentFields") for f in native[key])
assert native["tasks"][0]["attributes"][1]["value"].startswith("@choices:")
result = {"status": "PASS", "definitions": 7, "shared_guid_tables": 4, "selected_cells": 9, "native_lookup_definitions": 7, "external_product_run": False}
if args.reports:
    import openpyxl
    from pypdf import PdfReader
    book = openpyxl.load_workbook(args.root / "fields.xlsx", data_only=True)
    sheet = book["배정 사용자 필드"]
    cells = [str(c.value) for row in sheet for c in row if c.value is not None]
    assert any("국내 › " in v and "서울" in v and "부산" in v for v in cells)
    assert "목록 밖 직접 입력" in cells and "미정" in cells
    assert not any("@choices:" in v or "xml-choice-" in v for v in cells)
    pdf = PdfReader(args.root / "fields.pdf")
    pdf_text = re.sub(r"\s+", "", "".join(page.extract_text() for page in pdf.pages))
    assert "배정사용자필드" in pdf_text and "목록밖직접입력" in pdf_text and "xml-choice-" not in pdf_text
    result["reports"] = {"pdf_pages": len(pdf.pages), "labels_in_xlsx_pdf": True}
if args.reader:
    import jpype
    import mpxj
    jpype.startJVM("-Dlog4j2.statusLoggerLevel=OFF")
    try:
        from org.mpxj.reader import UniversalProjectReader
        proxy = UniversalProjectReader().getProjectReaderProxy(str((args.root / "export.xml").resolve()))
        try:
            reader = proxy.getProjectReader()
            reader.setIgnoreErrors(False)
            model = proxy.read()
            assert not list(model.getIgnoredErrors())
            fields = {str(f.getAlias()): f.getFieldType() for f in model.getCustomFields()}
            region = next(t for t in model.getTasks() if str(t.getName()) == "설계").get(fields["지역"])
            code = next(r for r in model.getResources() if str(r.getName()) == "설계 인력").get(fields["자원 계층"])
            assert str(region) == "서울 & <설계>" and str(code) == "서울 & <설계>"
            result["strict_reader"] = {"ignored_errors": 0, "local_values": 2, "limitation": "MPXJ16.10.0 does not consume enterprise hex/EnterpriseExtendedAttribute values; GUID cells verified independently"}
        finally:
            proxy.close()
    finally:
        jpype.shutdownJVM()
print(json.dumps(result, ensure_ascii=False, indent=2))
