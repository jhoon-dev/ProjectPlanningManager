"""Independent readers for resource views, dated remaining work and formula fields."""
from decimal import Decimal, ROUND_HALF_UP, localcontext
from datetime import datetime, timedelta
import hashlib
import json
from pathlib import Path
import re
import sys
import unicodedata
import zipfile

from openpyxl import load_workbook
from pypdf import PdfReader


def native(root, name, schema):
    data = json.loads((root / name).read_text(encoding="utf-8"))
    assert data["schemaVersion"] == schema
    p = data["payload"]
    compact = json.dumps(p, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
    assert hashlib.sha256(compact).hexdigest() == data["sha256"]
    return p


def book(root, name):
    with zipfile.ZipFile(root / name) as z:
        assert z.testzip() is None
        assert not any("externalLink" in n or "vbaProject" in n for n in z.namelist())
    result = load_workbook(root / name, data_only=False)
    assert not any(c.data_type == "f" for sheet in result for row in sheet for c in row)
    return result


def rows(book, sheet):
    return list(book[sheet].iter_rows(min_row=7))


def execution_ledger(b):
    """Integrate exported civil-minute rates without using the inverse allocation formula."""
    result = {}
    for row in rows(b, "실행 배정 구간"):
        at = datetime.strptime(row[2].value, "%Y-%m-%d %H:%M")
        finish = datetime.strptime(row[3].value, "%Y-%m-%d %H:%M")
        rate = Decimal(str(row[4].value)) * 100
        assert rate == rate.to_integral_value() and 0 <= rate <= 100000
        while at < finish:
            minute = at.hour * 60 + at.minute
            assert at.weekday() < 5 and (540 <= minute < 720 or 780 <= minute < 1080)
            key = row[0].value, row[1].value, at.strftime("%Y-%m-%d")
            result[key] = result.get(key, 0) + int(rate)
            at += timedelta(minutes=1)
    return result


def resource_views(root):
    p = native(root, "resources.ppm", 21)
    assert p["savedResourceViews"][0]["settings"]["groupBy"] == ["field:region", "type"]
    assert p["resourceFields"][0]["name"] == "권역"
    assert p["baselines"][0]["input"]["resourceFields"][0]["name"] == "지역"
    b = book(root, "resources.xlsx")
    values = rows(b, "자원 그룹과 필터")
    data = {row[2].value: row for row in values}
    assert len(values) == len(data) == 5 and set(data) == {"R0", "R1", "R2", "R3", "R4"}
    assert [data[k][9].value for k in ("R0", "R1", "R2")] == [10, 2, 0]
    assert all(data[k][9].data_type == "n" for k in ("R0", "R1", "R2"))
    assert data["R0"][10].value == "(빈 문자열)" and data["R1"][10].value == "0"
    assert data["R2"][10].value == "미정"
    assert data["R3"][6].value == "미정" and data["R4"][6].value == 0
    assert data["R4"][6].data_type == "n"
    return {"status": "passed", "unique_resources": 5, "typed_numbers": True,
            "missing_empty_zero_distinct": True, "stable_view_and_frozen_dictionary": True}


def dated_remaining(root):
    p = native(root, "remaining.ppm", 22)
    plan = p["remainingWork"][0]
    assert plan["statusAt"] == "2026-11-02 18:00"
    assert plan["days"] == [{"date": "2026-11-03", "workUnitMinutes": 12000},
                            {"date": "2026-11-04", "workUnitMinutes": 0},
                            {"date": "2026-11-05", "workUnitMinutes": 60000}]
    assert p["periodActuals"][0]["workUnitMinutes"] == 6001
    assert p["progress"][0]["actualDurationMinutes"] == 480
    assert p["progress"][0]["remainingMinutes"] == 1440
    assert p["baselines"][0]["input"]["tasks"][0]["durationMinutes"] == 1920
    assert "remainingWork" not in p["baselines"][0]["input"]
    assert p["scenarios"][0]["remainingWork"][0]["days"][-1]["workUnitMinutes"] == 24000
    b = book(root, "remaining.xlsx")
    targets = rows(b, "날짜별 잔여 공수 입력")
    assert [row[4].value for row in targets] == [2, 0, 10]
    assert all(row[4].data_type == "n" for row in targets)
    u = book(root, "remaining-usage.xlsx")
    values = [row for row in rows(u, "시간축 공수와 원가") if row[2].value == "R"]
    data = {row[4].value: row for row in values}
    dates = ["2026-11-03", "2026-11-04", "2026-11-05"]
    assert [data[d][12].value for d in dates] == [2, 0, 10]
    assert [data[d][14].value for d in dates] == [240, 0, 1200]
    assert sum(row[6].value for row in values) == 32
    assert sum(row[8].value for row in values) == 3360
    assert sum(row[12].value for row in values) == 12
    assert sum(row[14].value for row in values) == 1440
    ledger = execution_ledger(b)
    for day in plan["days"]:
        assert ledger.get((plan["taskId"], plan["resourceId"], day["date"]), 0) == day["workUnitMinutes"]
    return {"status": "passed", "remaining_hours": [2, 0, 10], "remaining_cost": 1440,
            "original_plan_hours": 32, "original_plan_cost": 3360,
            "actual_progress_and_frozen_scenario_preserved": True}


def formula_fields(root):
    p = native(root, "formulas.ppm", 23)
    factor = Decimal(next(v["value"] for v in p["projectAttributes"] if v["key"] == "field:factor"))
    task_fields = {f["id"]: f for f in p["customFields"]}
    assert task_fields["hours"]["expression"] == "[durationHours]*[project:factor]+coalesce([field:extra],0{h})"
    assert all(not any(v["key"] == "field:hours" for v in t.get("attributes", [])) for t in p["tasks"])
    assert all(not any(v["key"] in ("field:weighted", "field:fte") for v in r.get("attributes", []))
               for r in p["resources"])
    assert p["remainingWork"][0]["days"][0]["workUnitMinutes"] == 6001
    assert p["savedResourceViews"][0]["settings"]["filters"][0]["key"] == "field:weighted"
    b = book(root, "formulas.xlsx")
    tasks = {(row[0].value, row[2].value): row for row in rows(b, "작업 사용자 필드와 코드")}
    for t in p["tasks"]:
        expected = Decimal(t["durationMinutes"]) / 60 * factor
        cell = tasks[t["id"], "field:hours"][4]
        assert Decimal(str(cell.value)) == expected and cell.data_type == "n"
        assert tasks[t["id"], "field:error"][4].value == "미정"
    project = {row[2].value: row for row in rows(b, "프로젝트 사용자 필드")}
    assert project["field:count"][4].value == len(p["tasks"]) * factor
    resources = {(row[0].value, row[2].value): row for row in rows(b, "자원 사용자 필드")}
    for r in p["resources"]:
        score = next((v["value"] for v in r.get("attributes", []) if v["key"] == "field:score"), None)
        cell = resources[r["id"], "field:weighted"][4]
        if score is None:
            assert cell.value == "미정"
        else:
            assert Decimal(str(cell.value)) == Decimal(score) * factor and cell.data_type == "n"
    assert resources["R1", "field:fte"][4].value == 0
    assert resources["R2", "field:fte"][4].value == "미정"
    diagnostics = rows(b, "계산식 미설정과 오류")
    assert sum(row[4].value == "계산 오류" for row in diagnostics) == 2
    assert sum(row[4].value == "미설정" for row in diagnostics) == 2
    assert execution_ledger(b)["T", "R0", "2026-11-03"] == 6001
    filtered = rows(book(root, "formula-resources.xlsx"), "자원 그룹과 필터")
    assert [row[1].value for row in filtered] == ["R1", "R0"]
    assert [row[8].value for row in filtered] == [4, 20]
    assert [row[9].value for row in filtered] == [0, 1]
    return {"status": "passed", "task_hours": [16, 4], "project_count": 4,
            "resource_scores": [20, 4], "numeric_results": True,
            "missing_and_errors_are_not_zero": True, "stored_values_are_inputs_only": True,
            "combined_schema": 23, "single_quantum_remaining_work": 6001}


def decimal_arithmetic(root):
    values = json.loads((root / "formula-arithmetic.json").read_text(encoding="utf-8"))
    assert len(values) == 400
    limit = Decimal("999999999.999999")
    errors = 0
    with localcontext() as context:
        context.prec = 80
        for case in values:
            a, b = Decimal(case["a"]), Decimal(case["b"])
            if case["op"] == "/" and not b:
                expected = None
            else:
                if case["op"] == "+": value = a + b
                elif case["op"] == "-": value = a - b
                elif case["op"] == "*": value = a * b
                elif case["op"] == "/": value = a / b
                else: raise AssertionError("unknown operator")
                value = value.quantize(Decimal("0.000001"), rounding=ROUND_HALF_UP)
                expected = int(value * 1000000) if abs(value) <= limit else None
            assert case["raw"] == expected, case
            if expected is None:
                errors += 1
                assert case["issue"]
            else:
                assert not case["issue"]
    return {"status": "passed", "cases": len(values), "expected_errors": errors,
            "oracle": "Python Decimal precision80 / half away from zero to six decimals"}


def pdfs(root):
    result = {}
    for name, phrases in {"resources.pdf": ["자원그룹과필터", "권역", "숙련도"],
                          "remaining.pdf": ["날짜별잔여공수입력", "2026-11-05"],
                          "formulas.pdf": ["계산식필드정의", "계산식미설정과오류", "0으로나눌수없습니다."]}.items():
        pdf = PdfReader(root / name, strict=True)
        text = unicodedata.normalize("NFKC", "\n".join(page.extract_text() or "" for page in pdf.pages))
        compressed = re.sub(r"\s+", "", text)
        for phrase in phrases: assert phrase in compressed, (name, phrase)
        assert not any(ord(c) < 32 and c not in "\n\r\t" for c in text)
        result[name] = len(pdf.pages)
    return result


def verify(build):
    root = build / "professional-fields-qa"
    return {"status": "passed", "resource_views": resource_views(root),
            "dated_remaining": dated_remaining(root), "formula_fields": formula_fields(root),
            "arithmetic": decimal_arithmetic(root), "pdf_pages": pdfs(root),
            "xlsx_contains_no_executable_formulas": True}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-governance")),
                     ensure_ascii=False, indent=2))
