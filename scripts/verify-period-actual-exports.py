"""Verify dated actual inputs, corrections and reconciliation with independent readers."""
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
    root = build / "period-actual-qa"
    with zipfile.ZipFile(root / "actuals.xlsx") as archive:
        assert archive.testzip() is None
        assert not any("externalLink" in name or "vbaProject" in name for name in archive.namelist())
    book = load_workbook(root / "actuals.xlsx", data_only=False)
    detail = list(book["기간별 보고 실제"].iter_rows(min_row=7, values_only=True))
    assert len(detail) == 3
    latest = {(row[3], row[0]): row for row in detail}
    assert latest[("R", "2026-11-02")][6] == 6 and latest[("R", "2026-11-02")][7] == 25
    assert latest[("R", "2026-11-03")][6:8] == (0, 0)
    assert latest[("E", "2026-11-03")][6] == 1 and latest[("E", "2026-11-03")][7] == "미정"
    labor = sum(Decimal(str(row[6])) for row in detail if row[5] == "인시")
    equipment = sum(Decimal(str(row[6])) for row in detail if row[5] == "장비시")
    assert labor == 6 and equipment == 1
    balances = {row[0]: row for row in book["누적 원가 배분 대사"].iter_rows(min_row=7, values_only=True)}
    assert balances["A"][3:7] == (80, 25, 55, 2)
    assert all(row[3] == row[4] + row[5] for row in balances.values())
    history = list(book["기간 실적 정정 이력"].iter_rows(min_row=7, values_only=True))
    assert len(history) == 4 and history[0][7:9] == (8, 30) and history[-1][7:9] == (6, 25)
    assert history[-1][1] == history[0][0] and history[-1][0] != history[0][0]
    assert all(row[10] == "현장 담당자" and row[11] and row[13] == "usage-fixture" for row in history)
    assert all(row[15] for row in history if row[8] != "미정")
    assert not any(cell.data_type == "f" for sheet in book for row in sheet for cell in row)
    reader = PdfReader(root / "actuals.pdf")
    text = unicodedata.normalize("NFKC", "\n".join(page.extract_text() or "" for page in reader.pages))
    compact = re.sub(r"\s+", "", text)
    for phrase in ("기간별보고실제", "누적원가배분대사", "기간실적정정이력", "현장담당자", "usage-fixture"):
        assert phrase in compact, phrase
    assert not any(ord(char) < 32 and char not in "\r\n\t" for char in text)
    return {"status": "success", "latest_rows": len(detail), "history_rows": len(history),
            "labor_hours": 6, "equipment_hours": 1, "cumulative_ac": 80,
            "allocated_actual": 25, "unallocated_actual": 55, "zero_distinct_from_unreported": True,
            "correction_preserved_original": True, "pdf_pages": len(reader.pages)}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-forecast")), indent=2))
