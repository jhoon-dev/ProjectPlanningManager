"""Independently verify task_classification_ui XLSX/PDF output.

Uses the existing optional openpyxl/pypdf QA dependencies.
Usage: python scripts/verify-classification-exports.py build-forecast
"""
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
    root = build / "classification-qa"
    with zipfile.ZipFile(root / "classification.xlsx") as archive:
        assert archive.testzip() is None
        assert not any("externalLink" in name or "vbaProject" in name for name in archive.namelist())
    book = load_workbook(root / "classification.xlsx", data_only=False)
    sheet = book["작업 사용자 필드와 코드"]
    rows = list(sheet.iter_rows(min_row=7))
    assert len(rows) == 12, "report must include all three WBS-scope tasks, independently of two visible grouped tasks"
    numbers = {row[1].value: row[4] for row in rows if row[2].value == "field:quantity"}
    assert numbers["기초 A"].value == 10 and numbers["구조 B"].value == 0
    assert Decimal(str(numbers["전기 C"].value)) == Decimal("-0.125001")
    assert all(cell.data_type == "n" for cell in numbers.values())
    assert any(row[4].value == "미정" for row in rows)
    assert any(row[4].value == "북측 › 1구역" for row in rows)
    assert not any(cell.data_type == "f" for page in book for row in page for cell in row)
    reader = PdfReader(root / "classification.pdf")
    text = unicodedata.normalize("NFKC", "\n".join(page.extract_text() or "" for page in reader.pages))
    compact = re.sub(r"\s+", "", text)
    for phrase in ("작업사용자필드와코드", "수량", "북측", "-0.125001"):
        assert phrase in compact, phrase
    assert not any(ord(ch) < 32 and ch not in "\n\r\t" for ch in text)
    return {"status": "success", "classification_rows": len(rows), "numeric_precision": 6,
            "unknown_distinct_from_zero": True, "pdf_pages": len(reader.pages), "scope_tasks": 3}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-forecast")), indent=2))
