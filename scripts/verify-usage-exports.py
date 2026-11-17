"""Independent openpyxl/pypdf verification of timephased_usage_ui artifacts."""
import json
import re
import sys
import unicodedata
import zipfile
from decimal import Decimal
from pathlib import Path

from openpyxl import load_workbook
from pypdf import PdfReader


def verify(build):
    root = build / "usage-qa"
    with zipfile.ZipFile(root / "usage.xlsx") as archive:
        assert archive.testzip() is None
        assert not any("externalLink" in name or "vbaProject" in name for name in archive.namelist())
    book = load_workbook(root / "usage.xlsx", data_only=False)
    rows = list(book["시간축 공수와 원가"].iter_rows(min_row=7))
    assert len(rows) == 84, "six unique leaves x fourteen days"
    assert len({(row[0].value, row[2].value, row[4].value) for row in rows}) == 84

    def total(column):
        cells = [row[column] for row in rows if row[column].data_type == "n"]
        return sum((Decimal(str(cell.value)) for cell in cells), Decimal(0))

    for column, expected in {6: 16, 7: 4, 8: 212, 9: 16, 10: 4, 11: 212, 12: 12, 13: 2, 14: 74}.items():
        assert total(column) == expected, (column, total(column), expected)
    fixed = [row for row in rows if row[3].value == "작업 고정비"]
    assert len(fixed) == 42 and all(row[14].value == "고정비 제외" for row in fixed)
    monday = [row for row in rows if row[4].value == "2026-11-02"]
    assert sum(Decimal(str(row[8].value)) for row in monday) == 88
    actual = list(book["기간 미배분 실제원가"].iter_rows(min_row=7))
    assert len(actual) == 3 and sum(row[3].value for row in actual) == 80
    source = {row[0].value: row[1].value for row in book["Usage 출처와 정책"].iter_rows(min_row=7)}
    assert source["문서 ID"] == "usage-fixture" and source["상태일"] == "2026-11-02 18:00"
    assert "승인 계획" in source["기준선"] and source["저장 상태"] == "저장된 원본"
    assert source["기간"] == "2026-11-02 ~ 2026-11-15"
    assert not any(cell.data_type == "f" for sheet in book for row in sheet for cell in row)
    button = load_workbook(root / "button-usage.xlsx", data_only=False)
    weekly = list(button["시간축 공수와 원가"].iter_rows(min_row=7))
    assert len(weekly) == 12 and sum(row[8].value for row in weekly) == 212
    assert sum(row[14].value for row in weekly if row[14].data_type == "n") == 74
    reader = PdfReader(root / "usage.pdf")
    text = unicodedata.normalize("NFKC", "\n".join(page.extract_text() or "" for page in reader.pages))
    compact = re.sub(r"\s+", "", text)
    for label in ["시간축공수와원가", "기간미배분실제원가", "고정비제외", "Usage출처와정책", "usage-fixture"]:
        assert label in compact, label
    assert not any(ord(char) < 32 and char not in "\r\n\t" for char in text)
    return {"status": "success", "unique_period_rows": 84, "plan_labor_hours": 16,
            "plan_equipment_hours": 4, "plan_minor": 212, "baseline_minor": 212,
            "remaining_assignment_minor": 74, "unallocated_actual_minor": 80,
            "fixed_remaining_excluded": True, "ui_button_weekly_rows": len(weekly), "pdf_pages": len(reader.pages)}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-forecast")), indent=2))
