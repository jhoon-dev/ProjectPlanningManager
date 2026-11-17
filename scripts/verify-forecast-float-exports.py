"""Independently read the exports produced by forecast_float_ui.

Development-only dependencies match verify-report-fixtures.py: openpyxl/pypdf.
Usage: python scripts/verify-forecast-float-exports.py build-forecast
"""
import json
import re
import sys
import unicodedata
import zipfile
from pathlib import Path

from openpyxl import load_workbook
from pypdf import PdfReader


def verify(build: Path):
    root = build / "forecast-float-qa"
    with zipfile.ZipFile(root / "forecast.xlsx") as archive:
        assert archive.testzip() is None, "XLSX ZIP checksum failure"
        assert not any("externalLink" in name or "vbaProject" in name for name in archive.namelist())
    book = load_workbook(root / "forecast.xlsx", data_only=False)
    sheet = book["실적과 예측"]
    assert sheet["K7"].value == sheet["L7"].value == "미정", "completed actual has numeric future float"
    for address in ("K8", "L8", "K9", "L9"):
        assert sheet[address].value == 0 and sheet[address].data_type == "n", "critical float is not numeric zero"
    for address in ("K10", "L10"):
        assert sheet[address].value == 8 and sheet[address].data_type == "n", "float hours are incorrect"
    assert sheet["M7"].value == "실제 완료"
    assert sheet["M8"].value == sheet["M9"].value == "예측 임계"
    assert sheet["M10"].value == "예측 여유"
    assert not any(cell.data_type == "f" for page in book for row in page for cell in row)
    reader = PdfReader(root / "forecast.pdf")
    text = unicodedata.normalize("NFKC", "\n".join(page.extract_text() or "" for page in reader.pages))
    compact = re.sub(r"\s+", "", text)
    for phrase in ("잔여총여유", "잔여자유여유", "예측임계", "예측여유", "실제완료"):
        assert phrase in compact, f"PDF label missing: {phrase}"
    assert not any(ord(ch) < 32 and ch not in "\n\r\t" for ch in text), "PDF contains control characters"
    return {"status": "success", "xlsx_numeric_hours": 8, "completed_float": "unknown",
            "pdf_pages": len(reader.pages), "pdf_policy_labels_verified": True}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-forecast")), indent=2))
