"""Read combined-policy native, workbook and PDF outputs with independent readers."""
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
    root = build / "progress-leveling-qa"
    native = json.loads((root / "combined.ppm").read_text(encoding="utf-8"))
    assert native["schemaVersion"] == 15
    p = native["payload"]
    assert p["levelingDecisions"][-1]["afterRemaining"] == p["remainingLeveling"]
    canonical = json.dumps(p, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    assert hashlib.sha256(canonical).hexdigest() == native["sha256"]
    assert p["progressPolicy"]["policy"] == "native-progress-override-v1"
    assert p["remainingLeveling"]["reason"] == "자원 중복 배정 해소"
    assert p["tasks"][1]["predecessors"] == ["A"]
    books, pages = {}, {}
    for stem in ("schedule", "usage"):
        with zipfile.ZipFile(root / (stem + ".xlsx")) as archive:
            assert archive.testzip() is None
            assert not any("externalLink" in n or "vbaProject" in n for n in archive.namelist())
        book = load_workbook(root / (stem + ".xlsx"), data_only=False)
        assert not any(c.data_type == "f" for sheet in book for row in sheet for c in row)
        text = "\n".join(str(c.value) for sheet in book for row in sheet for c in row if c.value is not None)
        pdf = PdfReader(root / (stem + ".pdf"), strict=True)
        raw = "\n".join(page.extract_text() or "" for page in pdf.pages)
        compact = re.sub(r"\s+", "", unicodedata.normalize("NFKC", raw))
        for phrase in ("native-progress-override-v1", "자원중복배정해소", "진행중수신관계검토"):
            assert phrase in re.sub(r"\s+", "", text) and phrase in compact, (stem, phrase)
        assert "PROGRESS_OVERRIDE_PREVIEW" not in raw + text
        assert not any(ord(c) < 32 and c not in "\r\n\t" for c in raw)
        books[stem], pages[stem] = book, len(pdf.pages)
    rows = list(books["schedule"]["실적과 예측"].iter_rows(min_row=7, values_only=True))
    b = next(r for r in rows if r[0] == "B")
    d = next(r for r in rows if r[0] == "D")
    assert (b[2], b[5], b[9]) == ("2026-11-02 14:00", "2026-11-03 14:00", "2026-11-03 09:00")
    assert (d[5], d[9]) == ("2026-11-04 14:00", "2026-11-03 14:00")
    assert b[10] == b[11] == d[10] == d[11] == 0 and b[12] == d[12] == "평준화 임계"
    source = {r[0]: r[1] for r in books["schedule"]["출처와 설정"].iter_rows(min_row=7, values_only=True)}
    assert "adopted-leveling-v1/progress-leveling-v1/native-progress-override-v1" in source["활성 일정 정책"]
    detail = list(books["usage"]["시간축 공수와 원가"].iter_rows(min_row=7, values_only=True))
    allocated = [r for r in detail if r[2] == "R"]
    assert [(r[0], r[4], r[12], r[14]) for r in allocated if r[12]] == [
        ("B", "2026-11-03", 4, 240), ("D", "2026-11-03", 4, 240), ("D", "2026-11-04", 4, 480)]
    assert sum(r[12] for r in allocated) == 12 and sum(r[14] for r in allocated) == 960
    assert sum(r[8] for r in allocated) == 1440
    printed = PdfReader(root / "print.pdf", strict=True)
    text = "\n".join(p.extract_text() or "" for p in printed.pages)
    assert "progress-leveling-v1" in re.sub(r"\s+", "", text)
    assert "2026-11-04 14:00" in re.sub(r"\s+", " ", text)
    assert "STALE_START" not in text and "STALE_FINISH" not in text
    return {"status": "success", "schema": 15, "original_relation_preserved": True,
            "B_finish": b[5], "D_remaining_start": d[9], "D_finish": d[5],
            "remaining_hours": 12, "remaining_cost": 960, "plan_cost": 1440,
            "resource_order_float": True, "both_adoption_provenances": True,
            "xlsx_files": 2, "pdf_pages": {**pages, "print": len(printed.pages)}}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-forecast")), indent=2))
