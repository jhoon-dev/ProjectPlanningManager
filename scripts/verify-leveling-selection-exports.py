"""Read native adoption fixtures with independent XLSX/PDF readers."""
import json
import re
import sys
import unicodedata
import zipfile
from pathlib import Path

from openpyxl import load_workbook
from pypdf import PdfReader


def verify(build):
    root = build / "leveling-selection-qa"
    books = {}
    pages = {}
    for stem in ("schedule", "usage"):
        with zipfile.ZipFile(root / (stem + ".xlsx")) as archive:
            assert archive.testzip() is None
            assert not any("externalLink" in name or "vbaProject" in name for name in archive.namelist())
        book = load_workbook(root / (stem + ".xlsx"), data_only=False)
        assert not any(cell.data_type == "f" for sheet in book for row in sheet for cell in row)
        source_name = "출처와 설정" if stem == "schedule" else "Usage 출처와 정책"
        source = {row[0]: row[1] for row in book[source_name].iter_rows(min_row=7, values_only=True)}
        adopted = source["평준화 채택"]
        for phrase in ("계획", "serial-no-split-v1", "task-order", "260", "공유 작업자 과부하 해소", "자원"):
            assert phrase in adopted, phrase
        reader = PdfReader(root / (stem + ".pdf"), strict=True)
        raw = "\n".join(page.extract_text() or "" for page in reader.pages)
        compact = re.sub(r"\s+", "", unicodedata.normalize("NFKC", raw))
        if stem == "schedule":
            assert "RESOURCE_NOT_CHECKED" not in raw, "selected report retained unvalidated-resource advisory"
            assert not any(cell.value == "RESOURCE_NOT_CHECKED" for sheet in book for row in sheet for cell in row)
        for phrase in ("평준화채택", "serial-no-split-v1", "task-order", "공유작업자과부하해소", "자원"):
            assert phrase in compact, (stem, phrase)
        assert not any(ord(char) < 32 and char not in "\r\n\t" for char in raw)
        books[stem] = book
        pages[stem] = len(reader.pages)
    rows = list(books["schedule"]["계획 일정"].iter_rows(min_row=7, values_only=True))
    assert len(rows) == 3
    assert [row[4] for row in rows] == ["2026-11-02 09:00", "2026-11-03 09:00", "2026-11-04 09:00"]
    assert all(row[6] == 0 and row[7] == "평준화 임계" for row in rows)
    kpis = {row[0]: row[1] for row in books["schedule"]["대시보드"].iter_rows(min_row=7, values_only=True)}
    assert kpis["계획 임계 작업"] == 3 and kpis["현재 계획 예산"] == 40
    detail = list(books["usage"]["시간축 공수와 원가"].iter_rows(min_row=7, values_only=True))
    assert sum(row[8] for row in detail) == 40
    assert sum(row[11] for row in detail) == 24  # Frozen logical baseline: all 3 at rate 1.
    assigned = [row for row in detail if row[2]]
    assert [(row[4], row[6], row[8]) for row in assigned if row[6]] == [
        ("2026-11-02", 8, 8), ("2026-11-03", 8, 16), ("2026-11-04", 8, 16)]
    return {"status": "success", "schedule_dates": "2026-11-02/03/04", "plan_cost": 40,
            "frozen_baseline_cost": 24, "resource_order_float": True,
            "policy_reason_provenance": True, "no_unvalidated_resource_advisory": True,
            "xlsx_files": 2, "pdf_pages": pages}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-forecast")), indent=2))
