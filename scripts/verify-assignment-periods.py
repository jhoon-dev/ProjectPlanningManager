"""Independently verify native contours and numeric XLSX/PDF projections."""
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
    root = build / "assignment-periods-qa"
    native = json.loads((root / "periods.ppm").read_text(encoding="utf-8"))
    assert native["schemaVersion"] == 17
    p = native["payload"]
    compact = json.dumps(p, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
    assert hashlib.sha256(compact).hexdigest() == native["sha256"]
    expected = [{"fromMinute": 0, "toMinute": 480, "units": 150},
                {"fromMinute": 480, "toMinute": 960, "units": 100},
                {"fromMinute": 960, "toMinute": 1440, "units": 50}]
    assert p["assignments"][0]["periods"] == expected
    assert p["baselines"][0]["input"]["assignments"][0]["periods"] == expected
    assert p["baselines"][0]["engineVersion"] == "ppm-effort-periods-1.0"
    assert p["scenarios"][0]["input"]["assignments"][0]["periods"] == expected
    books = {}
    for name in ("periods", "usage"):
        with zipfile.ZipFile(root / (name + ".xlsx")) as z:
            assert z.testzip() is None
            assert not any("externalLink" in x or "vbaProject" in x for x in z.namelist())
        book = load_workbook(root / (name + ".xlsx"), data_only=False)
        assert not any(c.data_type == "f" for s in book for row in s for c in row)
        books[name] = book
    rows = list(books["periods"]["기간별 계획 배정 정의"].iter_rows(min_row=7))
    assert len(rows) == 3
    assert [[c.value for c in row[4:7]] for row in rows] == [[0, 480, 1.5], [480, 960, 1], [960, 1440, .5]]
    assert all(row[6].data_type == "n" and "%" in row[6].number_format for row in rows)
    demand = list(books["periods"]["기간별 자원 수요"].iter_rows(min_row=7))
    assert [row[4].value for row in demand] == [12, 8, 4]
    usage = list(books["usage"]["시간축 공수와 원가"].iter_rows(min_row=7))
    usage = [row for row in usage if row[2].value == "R"]
    assert [row[6].value for row in usage[:3]] == [12, 8, 4]
    assert [row[8].value for row in usage[:3]] == [720, 960, 480]
    assert sum(row[8].value for row in usage) == 2160
    pdf = PdfReader(root / "periods.pdf", strict=True)
    text = unicodedata.normalize("NFKC", "\n".join(page.extract_text() or "" for page in pdf.pages))
    compressed = re.sub(r"\s+", "", text)
    for phrase in ("기간별계획배정정의", "150.0%", "50.0%", "1440"):
        assert phrase in compressed, phrase
    assert not any(ord(c) < 32 and c not in "\n\r\t" for c in text)
    return {"status": "success", "schema": 17, "contour_rows": 3,
            "immutable_baseline_scenario": True, "numeric_percent": True,
            "day_work_hours": [12, 8, 4], "day_cost": [720, 960, 480],
            "total_cost": 2160, "pdf_pages": len(pdf.pages), "no_formulas": True}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build")), ensure_ascii=False, indent=2))
