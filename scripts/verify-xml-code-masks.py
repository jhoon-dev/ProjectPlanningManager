"""Independent code-mask XML/native/report checks and pinned MPXJ reader checks."""
import argparse
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
parser.add_argument("--reader", action="store_true")
parser.add_argument("--reports", action="store_true")
args = parser.parse_args()
ns = {"p": "http://schemas.microsoft.com/project"}

def text(node, name, default=""):
    return node.findtext("p:" + name, default, ns)

def masks(nodes):
    result = []
    for index, node in enumerate(nodes, 1):
        row = (int(text(node, "Level")), int(text(node, "Type")), int(text(node, "Length")), text(node, "Separator"))
        assert row[0] == index and 0 <= row[1] <= 3 and 0 <= row[2] <= 255 and len(row[3]) <= 1
        result.append(row)
    return result

def read(name):
    root = ET.parse(args.root / name).getroot()
    w = root.find("p:WBSMasks", ns)
    assert text(w, "Prefix") == "PR-" and text(w, "GenerateCodes") == "1" and text(w, "VerifyUniqueCodes") == "1"
    assert masks(w.findall("p:WBSMask", ns)) == [(1, 1, 2, "."), (2, 0, 3, "-"), (3, 2, 1, ".")]
    wbs = {text(t, "Name"): (text(t, "WBS"), text(t, "WBSLevel")) for t in root.findall("p:Tasks/p:Task", ns) if text(t, "UID") != "0"}
    assert wbs == {"설계": ("PR-AA", "AA"), "상세 설계": ("PR-AA.001", "001"), "설계 작업": ("PR-AA.001-a", "a"), "검토": ("PR-AA.002", "002")}
    tables = {}
    for table in root.findall("p:OutlineCodes/p:OutlineCode", ns):
        levels = masks(table.findall("p:Masks/p:Mask", ns))
        assert levels and text(table, "AllLevelsRequired") == "1"
        values = {text(v, "ValueID"): v for v in table.findall("p:Values/p:Value", ns)}
        expected_masks = {("AA", "01", "공정"): [(1, 1, 2, "."), (2, 0, 2, "/"), (3, 3, 0, "")],
                          ("01",): [(1, 0, 2, ".")], ("a",): [(1, 2, 1, ".")]}
        assert levels == expected_masks[tuple(text(v, "Value") for v in values.values())]
        assert text(table, "OnlyTableValuesAllowed") == "1"
        by_guid = {text(v, "FieldGUID"): k for k, v in values.items()}
        assert len(values) == len(by_guid)
        def path(identifier):
            parts = []
            seen = set()
            while identifier in values:
                assert identifier not in seen
                seen.add(identifier)
                node = values[identifier]
                parts.insert(0, text(node, "Value"))
                identifier = text(node, "ParentValueID")
            code = ""
            for index, part in enumerate(parts):
                _, kind, length, separator = levels[index]
                assert not length or len(part) == length
                assert kind == 3 or re.fullmatch([r"[0-9]+", r"[A-Z]+", r"[a-z]+"][kind], part)
                if index:
                    code += levels[index - 1][3]
                code += part
            return code
        tables[text(table, "Guid")] = (levels, {guid: path(identifier) for guid, identifier in by_guid.items()})
    assert len(tables) == 3 and sorted(len(v[0]) for v in tables.values()) == [1, 1, 3]
    definitions = {text(f, "FieldID"): (text(f, "Alias"), text(f, "Ltuid")) for f in root.findall("p:ExtendedAttributes/p:ExtendedAttribute", ns)}
    assert len(definitions) == 4
    selected = []
    for wrapper, kind in (("Tasks", "Task"), ("Resources", "Resource"), ("Assignments", "Assignment")):
        for owner in root.findall(f"p:{wrapper}/p:{kind}", ns):
            for node in owner:
                tag = node.tag.rsplit("}", 1)[-1]
                if re.fullmatch("[bcf][0-9a-f]{6}", tag):
                    identifier, guid = str(int(tag, 16)), node.text
                elif tag == "ExtendedAttribute":
                    identifier, guid = text(node, "FieldID"), text(node, "ValueGUID", text(node, "Value"))
                else:
                    continue
                alias, table = definitions[identifier]
                selected.append((alias, wrapper, text(owner, "UID"), tables[table][1][guid]))
    assert sorted(selected) == [("공정 코드 0", "Tasks", "3", "AA.01/공정"), ("공정 코드 1", "Tasks", "0", "AA.01/공정"), ("공정 코드 2", "Resources", "1", "01"), ("공정 코드 3", "Assignments", "1", "a")]
    return tables, wbs

assert read("export.xml") == read("reexport.xml")
native = json.loads((args.root / "native.json").read_text(encoding="utf-8"))
assert native["xmlWbs"]["generate"] and native["xmlWbs"]["verifyUnique"]
assert len(native["xmlWbs"]["values"]) == 4
assert all(f["xmlLookup"]["codeMask"]["allLevelsRequired"] for scope in ("customFields", "projectFields", "resourceFields", "assignmentFields") for f in native[scope])
generated = json.loads((args.root / "generated.json").read_text(encoding="utf-8"))
l = generated["customFields"][0]["xmlLookup"]
assert len(l["values"]) == 5 and l["values"][-2]["value"] == "02" and l["values"][-1]["value"] == "검토"
assert l["values"][-2]["valueId"] == "108" and l["values"][-1]["valueId"] == "109"
from uuid import UUID
assert len({str(UUID(v["guid"])) for v in l["values"]}) == 5
source = json.loads((args.root / "source.ppm").read_text(encoding="utf-8"))
assert source["schemaVersion"] == 41 and source["payload"] == generated
assert generated["projectFields"][0]["xmlLookup"]["values"] == l["values"]
assert len(generated["xmlWbs"]["values"]) == 5 and not generated["xmlWbs"]["generate"]
assert "PR-AA.001-b" in {v["code"] for v in generated["xmlWbs"]["values"]}
assert "PR-AA.001-c" not in {v["code"] for v in generated["xmlWbs"]["values"]}
generated_xml = ET.parse(args.root / "generated.xml").getroot()
assert text(generated_xml.find("p:WBSMasks", ns), "GenerateCodes") == "0"
wire_codes = [n.text for n in generated_xml.findall("p:Tasks/p:Task/p:WBS", ns)]
assert len(wire_codes) == 5 and "PR-AA.001-b" in wire_codes and "PR-AA.001-c" not in wire_codes
assert len(generated_xml.findall("p:OutlineCodes/p:OutlineCode/p:Values/p:Value", ns)) == 7
result = {"status": "PASS", "xml_definitions": 4, "masked_tables": 3, "outline_mask_levels": 5, "wbs_mask_levels": 3, "initial_selected_cells": 4, "wbs_codes": 4, "generated_dictionary_values": 5, "shared_table_preserved": True, "external_product_run": False}
if args.reports:
    import openpyxl
    from pypdf import PdfReader
    book = openpyxl.load_workbook(args.root / "fields.xlsx", data_only=True)
    rows = [tuple(c.value for c in row) for row in book["작업 사용자 필드와 코드"]]
    assert any(row[0] == "T" and row[4] == "AA.01/공정" for row in rows)
    assert any(row[0] == "B" and row[4] == "미정" for row in rows)
    pdf = PdfReader(args.root / "fields.pdf")
    content = re.sub(r"\s+", "", "".join(page.extract_text() for page in pdf.pages))
    assert "AA.01/공정" in content
    result["reports"] = {"pdf_pages": len(pdf.pages), "masked_code_text_preserved": True}
if args.reader:
    import jpype
    import mpxj
    jpype.startJVM("-Dlog4j2.statusLoggerLevel=OFF")
    try:
        from org.mpxj.reader import UniversalProjectReader
        proxy = UniversalProjectReader().getProjectReaderProxy(str((args.root / "export.xml").resolve()))
        reader = proxy.getProjectReader()
        reader.setIgnoreErrors(False)
        try:
            model = proxy.read()
        finally:
            proxy.close()
        assert not list(model.getIgnoredErrors())
        actual = {str(t.getName()): str(t.getWBS()) for t in model.getTasks() if str(t.getName()) in ("설계", "상세 설계", "설계 작업", "검토")}
        assert actual == {"설계": "PR-AA", "상세 설계": "PR-AA.001", "설계 작업": "PR-AA.001-a", "검토": "PR-AA.002"}
        fields = {str(f.getAlias()): f for f in model.getCustomFields()}
        checked = 0
        for alias, expected in (("공정 코드 1", [(1, 2, "."), (2, 2, "/"), (3, 0, "")]), ("공정 코드 2", [(1, 2, ".")]), ("공정 코드 3", [(1, 1, ".")])):
            actual = [(int(m.getLevel()), int(m.getLength()), str(m.getSeparator()) if m.getSeparator() is not None else "") for m in fields[alias].getMasks()]
            assert actual == expected, (alias, actual)
            assert fields[alias].getLookupTable().getAllLevelsRequired()
            checked += len(actual)
        assert not list(fields["공정 코드 0"].getMasks())
        result["reader"] = {"shared_table_limit": "MPXJ attaches the shared table/masks only to the last referencing field; original XML/native shared references checked independently", "strict_read": "PASS", "ignored_errors": 0, "wbs_values_checked": 4, "mask_levels_checked": checked, "limitation": "Enterprise hex selections, raw mask types and WBS generation flags are verified independently; no external-product execution"}
    finally:
        jpype.shutdownJVM()
print(json.dumps(result, ensure_ascii=False, indent=2))
