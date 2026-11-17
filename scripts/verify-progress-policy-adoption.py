"""Independently read the adopted-policy native/XLSX/PDF fixtures."""
import hashlib
import json
import re
import sys
import unicodedata
import zipfile
from pathlib import Path

from openpyxl import load_workbook
from pypdf import PdfReader


def verify(build):
    root = build / "progress-policy-adoption-qa"
    native = json.loads((root / "adopted.ppm").read_text(encoding="utf-8"))
    assert native["schemaVersion"] == 13
    canonical = json.dumps(native["payload"], ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    assert hashlib.sha256(canonical).hexdigest() == native["sha256"]
    selection = native["payload"]["progressPolicy"]
    assert selection["policy"] == "native-progress-override-v1"
    assert selection["adoptedBy"] == "PM" and selection["reason"] == "잔여 예측 검토"
    assert selection["sourceRevision"] < native["payload"]["revision"]
    assert native["payload"]["tasks"][1]["predecessors"] == ["A"]
    books, pages = {}, {}
    for stem in ("schedule", "usage"):
        with zipfile.ZipFile(root / (stem + ".xlsx")) as archive:
            assert archive.testzip() is None
            assert not any("externalLink" in n or "vbaProject" in n for n in archive.namelist())
        book = load_workbook(root / (stem + ".xlsx"), data_only=False)
        assert not any(c.data_type == "f" for sheet in book for row in sheet for c in row)
        values = "\n".join(str(c.value) for sheet in book for row in sheet for c in row if c.value is not None)
        reader = PdfReader(root / (stem + ".pdf"), strict=True)
        raw = "\n".join(p.extract_text() or "" for p in reader.pages)
        compact = re.sub(r"\s+", "", unicodedata.normalize("NFKC", raw))
        for phrase in (selection["policy"], "PM", "잔여예측검토"):
            assert phrase in re.sub(r"\s+", "", values) and phrase in compact, (stem, phrase)
        assert "PROGRESS_OVERRIDE_PREVIEW" not in raw + values
        assert not any(ord(c) < 32 and c not in "\r\n\t" for c in raw)
        books[stem] = book
        pages[stem] = len(reader.pages)
    forecast = list(books["schedule"]["실적과 예측"].iter_rows(min_row=7, values_only=True))
    b = next(r for r in forecast if r[0] == "B")
    assert b[2] == "2026-11-02 14:00" and b[5] == "2026-11-03 14:00"
    assert b[9] == "2026-11-03 09:00" and b[10] == 0 and b[11] == 0
    plan = list(books["schedule"]["계획 일정"].iter_rows(min_row=7, values_only=True))
    b_plan = next(r for r in plan if r[0] == "B")
    assert b_plan[4] == "2026-11-04 09:00" and b_plan[5] == "2026-11-04 18:00"
    usage = list(books["usage"]["시간축 공수와 원가"].iter_rows(min_row=7, values_only=True))
    assigned = [r for r in usage if r[0] == "B" and r[2] == "R"]
    remaining = [(r[4], r[12], r[14]) for r in assigned if r[12]]
    assert remaining == [("2026-11-03", 4, 240)], remaining
    assert sum(r[8] for r in assigned) == 960
    print_reader = PdfReader(root / "print.pdf", strict=True)
    print_text = "\n".join(p.extract_text() or "" for p in print_reader.pages)
    assert "native-progress-override-v1" in print_text and "2026-11-03 14:00" in re.sub(r"\s+", " ", print_text)
    assert "STALE_START" not in print_text and "STALE_FINISH" not in print_text
    return {"status": "success", "native_schema": 13, "original_relation_preserved": True,
            "B_forecast_finish": b[5], "B_plan_finish": b_plan[5], "B_remaining_hours": 4,
            "B_remaining_cost": 240, "B_plan_cost": 960, "metadata_provenance": True,
            "xlsx_files": 2, "pdf_pages": {**pages, "print": len(print_reader.pages)}}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-forecast")), indent=2))
