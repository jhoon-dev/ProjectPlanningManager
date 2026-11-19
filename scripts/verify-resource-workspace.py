"""Independent native checksum, exact material arithmetic, workspace links and report readers."""
from fractions import Fraction
import hashlib
import importlib.metadata
import json
from pathlib import Path
import sys
import re
import unicodedata
from openpyxl import load_workbook
from pypdf import PdfReader


def native(path, schema, kind="PPM"):
    root = json.loads(path.read_text(encoding="utf-8"))
    assert root["schemaVersion"] == schema and root["format"] == kind
    payload = root["payload"]
    canonical = json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
    assert hashlib.sha256(canonical).hexdigest() == root["sha256"]
    return payload


def rows(path):
    book = load_workbook(path, data_only=True)
    result = [list(row) for sheet in book for row in sheet.iter_rows(values_only=True)]
    book.close()
    return result


def verify(build):
    resource = build / "resource-plan-qa"
    p = native(resource / "resource-plan.ppm", 20)
    plan = p["resourcePlan"]
    assert plan["materials"][0]["unitPriceMinor"] == 399
    assert plan["uses"][0]["quantityMilli"] == 1500
    assert len(plan["demands"]) == len(p["assignments"]) == 1
    assert plan["demands"][0]["resourceId"] == p["assignments"][0]["resourceId"]
    frozen = p["baselines"][0]["input"]["resourcePlan"]
    cost = Fraction(frozen["materials"][0]["unitPriceMinor"] * frozen["uses"][0]["quantityMilli"], 1000)
    rounded = (cost.numerator * 2 + cost.denominator) // (2 * cost.denominator)
    assert rounded == 299 and frozen["expenses"][0]["amountMinor"] + rounded == 400
    assert plan["materials"][0]["stockMilli"] == 2000
    rr = rows(resource / "resource-plan.xlsx")
    assert any(r[0] == "U" and r[1:6] == ["A", "M", "1.5", "start", 599] for r in rr)
    assert any(r[0] == "E" and r[1:6] == ["A", "운송", "운송비", 101, "uniform"] for r in rr)
    assert any(r[0] == "D" and r[1:5] == ["A", "설치조", "R", 100] for r in rr)
    integrated = build / "integrated-workspace-qa"
    workspace = native(integrated / "integrated.ppmworkspace", 2, "PPM-WORKSPACE")
    assert workspace["links"][0] == {"id": "L", "predecessorProjectId": "P", "predecessorTaskId": "A", "successorProjectId": "Q", "successorTaskId": "A", "type": "FS", "lagMinutes": 0}
    assert workspace["integrated"]["priority"] == "deadline-first"
    assert all(not Path(ref["path"]).is_absolute() for ref in workspace["projects"])
    result = json.loads((integrated / "integrated-workspace.json").read_text(encoding="utf-8"))
    assert result["contract"] == "ppm-integrated-workspace-v1" and result["generation"] == 42
    tasks = {(r["projectId"], r["taskId"]): r for r in result["tasks"]}
    assert set(tasks) == {("P", "A"), ("Q", "A")}
    assert tasks["P", "A"]["finish"] == "2026-11-02 18:00"
    assert tasks["Q", "A"]["start"] == "2026-11-03 09:00"
    assert tasks["Q", "A"]["originalStart"] == "2026-11-02 09:00"
    assert result["finish"] == "2026-11-03 18:00"
    assert all(d["peakUnits"] <= 100 and d["overloadMinutes"] == 0 for d in result["sharedLoad"])
    ir = rows(integrated / "integrated-workspace.xlsx")
    qrow = next(r for r in ir if r[0] == "Q" and r[2] == "A")
    assert qrow[4:9] == [1, "2026-11-02 09:00", "2026-11-02 18:00", "2026-11-03 09:00", "2026-11-03 18:00"]
    pages = {}
    for folder, name, terms in [(resource, "resource-plan", ["자재", "운송비", "설치조"]), (integrated, "integrated-workspace", ["통합 일정", "Project P", "Project Q", "2026-11-03 09:00"])]:
        reader = PdfReader(folder / (name + ".pdf"))
        assert len(reader.pages) > 0
        content = unicodedata.normalize("NFC", "\n".join(page.extract_text() or "" for page in reader.pages))
        compact = re.sub(r"\s+", "", content)
        assert all(re.sub(r"\s+", "", term) in compact for term in terms), (name, terms)
        assert all(float(page.mediabox.width) > 0 and float(page.mediabox.height) > 0 for page in reader.pages)
        pages[name] = len(reader.pages)
    return {"status": "passed", "nativeSchema": 20, "workspaceSchema": 2, "qualifiedTasks": len(tasks), "frozenBudget": 400, "currentMaterialCost": 599, "pdfPages": pages, "readers": {name: importlib.metadata.version(name) for name in ["openpyxl", "pypdf"]}, "limits": "Native contract and independent artifact readers; no installed P6/MSP or physical printer execution"}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-decisions")), ensure_ascii=False, indent=2))
