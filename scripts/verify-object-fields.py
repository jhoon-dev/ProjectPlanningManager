"""Read project/resource field native, XLSX and PDF artifacts independently of Qt."""
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re
import sys
import unicodedata
import zipfile

from openpyxl import load_workbook
from pypdf import PdfReader


def verify(build):
    root = build / "object-fields-qa"
    native = json.loads((root / "objects.ppm").read_text(encoding="utf-8"))
    assert native["schemaVersion"] == 16
    p = native["payload"]
    compact = json.dumps(p, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
    assert hashlib.sha256(compact).hexdigest() == native["sha256"]
    assert len(p["projectFields"]) == len(p["resourceFields"]) == len(p["customFields"]) == 4
    project_values = {a["key"]: a["value"] for a in p["projectAttributes"]}
    assert project_values == {"field:label": "=2+3 발주처", "field:amount": "0",
                              "field:due": "2028-02-29", "field:phase": "review"}
    resources = {r["name"]: r for r in p["resources"]}
    resource_values = {name: {a["key"]: a["value"] for a in r["attributes"]} for name, r in resources.items()}
    assert resource_values["배정 인력"]["field:amount"] == "12.500001"
    assert resource_values["미배정 장비"]["field:label"] == ""
    assert resource_values["미배정 장비"]["field:amount"] == "0"
    assert "field:due" not in resource_values["미배정 장비"]
    assert p["tasks"][0]["attributes"] == [{"key": "field:label", "value": "작업 담당"}]
    frozen = p["baselines"][0]["input"]
    assert frozen["resourceFields"][3]["options"][0]["name"] == "검토"
    assert p["resourceFields"][3]["options"][0]["name"] == "검토 대기"
    assert p["scenarios"][0]["input"]["resourceFields"] == frozen["resourceFields"]
    books = {}
    for name in ("objects", "wbs"):
        with zipfile.ZipFile(root / (name + ".xlsx")) as archive:
            assert archive.testzip() is None
            assert not any("externalLink" in path or "vbaProject" in path for path in archive.namelist())
        book = load_workbook(root / (name + ".xlsx"), data_only=False)
        assert not any(cell.data_type == "f" for sheet in book for row in sheet for cell in row)
        books[name] = book

    def rows(book, sheet):
        return list(book[sheet].iter_rows(min_row=7))

    project_rows = rows(books["objects"], "프로젝트 사용자 필드")
    resource_rows = rows(books["objects"], "자원 사용자 필드")
    assert len(project_rows) == 4 and len(resource_rows) == 8
    by_key = {r[2].value: r[4] for r in project_rows}
    assert by_key["field:label"].value == "=2+3 발주처" and by_key["field:label"].data_type == "s"
    assert by_key["field:amount"].value == 0 and by_key["field:amount"].data_type == "n"
    actual = {(r[1].value, r[2].value): r[4] for r in resource_rows}
    assert Decimal(str(actual["배정 인력", "field:amount"].value)) == Decimal("12.500001")
    assert actual["배정 인력", "field:amount"].data_type == "n"
    assert actual["미배정 장비", "field:label"].value == "(빈 문자열)"
    assert actual["미배정 장비", "field:amount"].value == 0
    assert actual["미배정 장비", "field:due"].value == "미정"
    assert actual["배정 인력", "field:phase"].value == "검토 대기"
    scoped = rows(books["wbs"], "자원 사용자 필드")
    assert len(scoped) == 4 and {r[1].value for r in scoped} == {"배정 인력"}
    assert len(rows(books["wbs"], "프로젝트 사용자 필드")) == 4
    kpis = {r[0].value: r[1].value for r in rows(books["objects"], "대시보드")}
    assert kpis["선택 작업"] == 2 and kpis["현재 계획 예산"] == 90
    schedule = rows(books["objects"], "계획 일정")
    assert [r[5].value for r in schedule] == ["2026-11-02 18:00", "2026-11-03 14:00"]
    pdf = PdfReader(root / "objects.pdf", strict=True)
    text = unicodedata.normalize("NFKC", "\n".join(page.extract_text() or "" for page in pdf.pages))
    compact = re.sub(r"\s+", "", text)
    for phrase in ("프로젝트사용자필드", "자원사용자필드", "12.500001", "검토대기"):
        assert phrase in compact, phrase
    assert not any(ord(c) < 32 and c not in "\n\r\t" for c in text)
    return {"status": "success", "schema": 16, "project_rows": 4, "resource_rows": 8,
            "wbs_resource_rows": 4, "scope_isolation": True, "precision": 6,
            "empty_missing_zero": True, "immutable_snapshots": True, "no_formulas": True,
            "unchanged_plan_cost": 90, "pdf_pages": len(pdf.pages)}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build")), ensure_ascii=False, indent=2))
