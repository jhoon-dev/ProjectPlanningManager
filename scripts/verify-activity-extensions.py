"""Independent native/weighted-progress/endpoint/XLSX/PDF checks; not a P6 compatibility claim."""
from fractions import Fraction
import hashlib
import importlib.metadata
import json
from pathlib import Path
import re
import sys
import unicodedata

from openpyxl import load_workbook
from pypdf import PdfReader


def native(path):
    document = json.loads(path.read_text(encoding="utf-8"))
    assert document["schemaVersion"] == 19
    body = document["payload"]
    compact = json.dumps(body, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
    assert hashlib.sha256(compact).hexdigest() == document["sha256"]
    return body


def weighted(report):
    numerator = sum(Fraction(s["step"]["weight"] * s["basisPoints"]) for s in report["steps"])
    denominator = sum(s["step"]["weight"] for s in report["steps"])
    value = numerator / denominator
    return (value.numerator * 2 + value.denominator) // (2 * value.denominator)


def verify(build):
    root = build / "activity-extension-qa"
    p = native(root / "activities.ppm")
    assert len(p["tasks"]) == 3 and len(p["activities"]) == 3 and len(p["stepReports"]) == 2
    assert [weighted(r) for r in p["stepReports"]] == [2500, 8750]
    assert [r["steps"][0]["step"]["weight"] for r in p["stepReports"]] == [1, 7]
    assert p["costEntries"][-1]["physicalBasisPoints"] == 1000  # Separate manual source, not added to Steps.
    definitions = {a["kind"]: a for a in p["activities"]}
    assert definitions["steps"]["physicalFromSteps"]
    frozen = {a["kind"]: a for a in p["baselines"][0]["input"]["activities"]}
    assert frozen["steps"]["steps"][0]["weight"] == 1 and definitions["steps"]["steps"][0]["weight"] == 7
    assert len(p["baselines"][0]["schedule"]) == 3  # No support interval duplicates the executable work.
    assert p["assignments"] == []
    expected = json.loads((root / "expected.json").read_text(encoding="utf-8"))
    assert Fraction(expected["EV"], 100000) == Fraction(weighted(p["stepReports"][-1]), 10000)
    policy = native(root / "actual-dates.ppm")
    assert policy["progressPolicy"]["policy"] == "native-actual-dates-v1"
    source_b = next(t for t in policy["tasks"] if t["id"] == "B")
    assert source_b["predecessors"] == ["A", "D"]  # Persisted source remains complete.
    comparison = json.loads((build / "activity-policy-comparison.json").read_text(encoding="utf-8"))
    b = next(t for t in comparison["tasks"] if t["taskId"] == "B")
    assert b["retained"]["finish"] == "2026-11-04 14:00"
    assert b["overridden"]["finish"] == "2026-11-03 14:00"
    assert b["actualDates"]["finish"] == "2026-11-03 18:00"
    assert b["actualDates"]["start"] == "2026-11-02 14:00"
    historical = comparison["actualDatesHistoricalRelationships"]
    assert [(r["predecessorId"], r["taskId"], r["type"]) for r in historical] == [("A", "B", "FS")]
    book = load_workbook(root / "activities.xlsx", data_only=True)
    rows = [list(row) for sheet in book for row in sheet.iter_rows(values_only=True)]
    step_rows = [r for r in rows if len(r) >= 7 and r[1] in {"도면", "승인"}]
    assert len(step_rows) == 2
    assert step_rows[0][2:5] == [7, 1, 0.875] and step_rows[1][2:5] == [1, 0, 0.875]
    assert all(r[6] == "Steps" for r in step_rows)
    support = next(r for r in rows if r[0] == "현장 관리" and r[1] == "loe")
    assert support[2:6] == ["2026-11-02 09:00", "2026-11-04 14:00", 1200, 2]
    summary = next(r for r in rows if r[0] == "현장 요약" and r[1] == "wbs-summary")
    assert summary[2:6] == support[2:6]
    pdf = PdfReader(root / "activities.pdf")
    text = re.sub(r"\s+", "", unicodedata.normalize("NFKC", "".join(page.extract_text() for page in pdf.pages)))
    for label in ("Steps물리진척", "LOE·WBSSummary지원구간", "현장관리", "현장요약", "87.5%"):
        assert label in text, label
    return {"status": "success", "schema": 19, "native_checksums": True,
            "steps_frozen_weights": [1, 7], "physical_basis_points": [2500, 8750],
            "manual_physical_kept_separate": 1000, "EV": 87500,
            "work_tasks": 3, "support_intervals": 2, "support_duplicate_assignments": 0,
            "A_B_C_golden_finishes": True, "actual_start_fixed": True, "source_relationships_preserved": True,
            "xlsx_support_working_minutes": 1200, "pdf_pages": len(pdf.pages),
            "openpyxl": importlib.metadata.version("openpyxl"), "pypdf": importlib.metadata.version("pypdf"),
            "P6_roundtrip_run": False}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build")), indent=2))
