"""Independent XML/TSV + pinned MPXJ checks. No claim of MSP/P6 execution."""
from decimal import Decimal
import importlib.metadata
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


def tables(path):
    result = {}
    fields = None
    ended = False
    for index, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines()):
        cells = line.split("\t")
        if index == 0:
            assert cells[0] == "ERMHDR"
        elif cells[0] == "%T":
            assert not ended and cells[1] not in result
            rows = result[cells[1]] = []
            fields = None
        elif cells[0] == "%F":
            fields = cells[1:]
            assert len(fields) == len(set(fields))
        elif cells[0] == "%R":
            assert fields and len(cells) == len(fields) + 1
            rows.append(dict(zip(fields, cells[1:])))
        else:
            assert cells == ["%E"] and not ended
            ended = True
    assert ended
    return result


def raw_checks(root):
    ns = {"p": "http://schemas.microsoft.com/project"}
    xml = ET.parse(root / "mapped.xml").getroot()
    definitions = {}
    for node in xml.findall("p:ExtendedAttributes/p:ExtendedAttribute", ns):
        identifier = node.findtext("p:FieldID", namespaces=ns)
        assert identifier not in definitions
        definitions[identifier] = node.findtext("p:Alias", namespaces=ns)
    assert len(definitions) == 9
    assert xml.find(".//p:Formula", ns) is None
    leaves = [t for t in xml.findall("p:Tasks/p:Task", ns) if t.findtext("p:UID", namespaces=ns) != "0"]
    def values(node):
        return {definitions[item.findtext("p:FieldID", namespaces=ns)]:
                item.findtext("p:Value", default="", namespaces=ns)
                for item in node.findall("p:ExtendedAttribute", ns)}
    summary = next(t for t in xml.findall("p:Tasks/p:Task", ns) if t.findtext("p:UID", namespaces=ns) == "0")
    assert summary.findtext("p:b608000", namespaces=ns) == "123.000001"
    a, b = map(values, leaves)
    assert a == {"설명": "한글 & <상세>", "점수": "-2.123456", "검토일": "2026-11-06T00:00:00",
                 "지역": "서울", "환산 점수": "-4.246912"}
    assert b == {"설명": "", "점수": "0", "환산 점수": "0"}
    assert values(xml.find("p:Resources/p:Resource", ns)) == {
        "자원 점수": "0", "자원 날짜": "2026-11-09T00:00:00"}
    for name in ["mapped.xer", "combined.xer"]:
        data = tables(root / name)
        udf = {r["udf_type_id"]: r for r in data["UDFTYPE"]}
        assert len(udf) == len(data["UDFTYPE"])
        assert len({r["udf_type_name"] for r in data["UDFTYPE"]}) == len(udf)
        projects = {r["proj_id"] for r in data["PROJECT"]}
        tasks = {r["task_id"]: r["proj_id"] for r in data["TASK"]}
        resources = {r["rsrc_id"] for r in data["RSRC"]}
        resource_projects = {}
        for assignment in data["TASKRSRC"]:
            resource_projects.setdefault(assignment["rsrc_id"], set()).add(assignment["proj_id"])
        seen = set()
        for row in data["UDFVALUE"]:
            definition = udf[row["udf_type_id"]]
            key = row["udf_type_id"], row["proj_id"], row["fk_id"]
            assert key not in seen and row["proj_id"] in projects
            seen.add(key)
            scope = definition["table_name"]
            if scope == "TASK":
                assert tasks[row["fk_id"]] == row["proj_id"]
            elif scope == "RSRC":
                assert row["fk_id"] in resources and row["proj_id"] in resource_projects[row["fk_id"]]
            else:
                assert scope == "PROJECT" and row["fk_id"] == row["proj_id"]
            if definition["logical_data_type"] == "FT_FLOAT":
                number = Decimal(row["udf_number"])
                assert number.is_finite() and number.as_tuple().exponent >= -6
        if name == "mapped.xer":
            assert len(udf) == 9
            assert any(r["udf_text"] == "" and udf[r["udf_type_id"]]["udf_type_label"] == "설명"
                       for r in data["UDFVALUE"])
        else:
            assert len(projects) == 2 and len(udf) == 18
    oracle = json.loads((root / "dated-float-oracle.json").read_text(encoding="utf-8-sig"))
    assert [r["workUnitMinutes"] for r in oracle] == [100, 200, 300, 400]
    assert [r["totalA"] for r in oracle] == [3, 2, 1, 0]
    assert all(r["freeA"] == 0 and r["totalB"] == r["freeB"] == 6 and r["witnesses"] >= 2 for r in oracle)
    assert all(r["policy"].endswith("/resource-dated-float-v1") for r in oracle)
    return {"xml_definitions": 9, "xer_definitions": 9, "combined_projects": 2,
            "typed_precision": "6 decimal places", "dated_exhaustive_goal_cases": 4}


def mpxj_checks(root):
    assert importlib.metadata.version("mpxj") == "16.10.0"
    assert importlib.metadata.version("JPype1") == "1.6.0"
    import jpype
    import mpxj  # Registers independent jars.
    jpype.startJVM()
    from java.lang import System
    from org.mpxj.reader import UniversalProjectReader
    results = []
    try:
        for name, project_id in [("mapped.xml", None), ("mapped.xer", None),
                                 ("combined.xer", 1), ("combined.xer", 2)]:
            proxy = UniversalProjectReader().getProjectReaderProxy(str((root / name).resolve()))
            try:
                reader = proxy.getProjectReader()
                if hasattr(reader, "setIgnoreErrors"):
                    reader.setIgnoreErrors(False)
                if project_id is not None:
                    reader.setProjectID(project_id)
                model = proxy.read()
                assert not list(model.getIgnoredErrors())
                leaves = [t for t in model.getTasks() if not t.getSummary()]
                a, b = leaves
                def get(obj, scope, alias):
                    fields = [f for f in model.getCustomFields() if str(f.getAlias()) == alias
                              and str(f.getFieldType().getFieldTypeClass()) == scope]
                    # A combined reader retains global definitions for both projects.
                    values = [obj.get(f.getFieldType()) for f in fields]
                    populated = [v for v in values if v is not None]
                    assert len(populated) <= 1, (name, alias, values)
                    return populated[0] if populated else None
                assert str(get(a, "TASK", "설명")) == "한글 & <상세>"
                assert Decimal(str(get(a, "TASK", "점수"))) == Decimal("-2.123456")
                assert Decimal(str(get(a, "TASK", "환산 점수"))) == Decimal("-4.246912")
                assert Decimal(str(get(b, "TASK", "점수"))) == 0
                assert str(get(a, "TASK", "검토일")) == "2026-11-06T00:00"
                assert get(b, "TASK", "검토일") is None and get(a, "TASK", "오류 결과") is None
                assert str(get(a, "TASK", "지역")) == "서울"
                empty = get(b, "TASK", "설명")
                assert (str(empty) == "") if name.endswith("xml") else empty is None
                used_resources = {int(a.getResource().getUniqueID()) for a in model.getResourceAssignments()}
                resources = [r for r in model.getResources() if str(r.getName()) == "설계 인력"
                             and int(r.getUniqueID()) in used_resources]
                populated = [r for r in resources if get(r, "RESOURCE", "자원 점수") is not None]
                assert len(populated) == 1
                for r in populated:
                    assert Decimal(str(get(r, "RESOURCE", "자원 점수"))) == 0
                    assert str(get(r, "RESOURCE", "자원 날짜")) == "2026-11-09T00:00"
                if not name.endswith("xml"):
                    assert Decimal(str(get(model.getProjectProperties(), "PROJECT", "프로젝트 점수"))) == Decimal("123.000001")
                results.append({"file": name, "project_id": project_id, "status": "passed"})
            finally:
                proxy.close()
        return {"mpxj": "16.10.0", "jpype": "1.6.0", "java": str(System.getProperty("java.version")),
                "files": results, "xer_empty_text_reader_result": "null; explicitly disclosed in conversion diagnostics"}
    finally:
        jpype.shutdownJVM()


if __name__ == "__main__":
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "build-governance") / "dated-udf-qa"
    print(json.dumps({"status": "passed", "raw": raw_checks(root), "independent_reader": mpxj_checks(root)},
                     ensure_ascii=False, indent=2))
