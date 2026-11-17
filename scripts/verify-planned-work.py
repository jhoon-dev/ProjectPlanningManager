"""Independent native/target/XLSX/PDF verification for planned-work conversion."""
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
    root = build / "planned-work-qa"
    native = json.loads((root / "work.ppm").read_text(encoding="utf-8"))
    assert native["schemaVersion"] == 17
    p = native["payload"]
    compact = json.dumps(p, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
    assert hashlib.sha256(compact).hexdigest() == native["sha256"]
    a = p["assignments"][0]
    targets = json.loads((root / "targets.json").read_text(encoding="utf-8"))
    assert [t["workUnitMinutes"] for t in targets] == [12000, 0, 60000]
    for t in targets:
        # Independent minute sum over stored task-relative rates for this standard calendar fixture.
        total = 0
        for minute in range(t["from"], t["to"]):
            rates = [v["units"] for v in a["periods"] if v["fromMinute"] <= minute < v["toMinute"]]
            assert len(rates) <= 1
            total += rates[0] if rates else a["units"]
        assert total == t["workUnitMinutes"]
    assert not p["baselines"][0]["input"]["assignments"][0].get("periods")
    assert len(p["periodActuals"]) == 1
    fact = p["periodActuals"][0]
    assert (fact["taskId"], fact["resourceId"], fact["date"], fact["workUnitMinutes"]) == (
        "T", "R", "2026-11-02", 6001)
    assert fact["actualMinor"] is None and fact["recordedRevision"] < p["revision"]
    books = {}
    for name in ("work", "usage"):
        with zipfile.ZipFile(root / f"{name}.xlsx") as z:
            assert z.testzip() is None
            assert not any("externalLink" in n or "vbaProject" in n for n in z.namelist())
        book = load_workbook(root / f"{name}.xlsx", data_only=False)
        assert not any(c.data_type == "f" for sheet in book for row in sheet for c in row)
        books[name] = book
    rows = list(books["work"]["기간별 계획 배정 정의"].iter_rows(min_row=7))
    assert [[c.value for c in row[4:7]] for row in rows] == [[0, 480, .25], [480, 960, 0], [960, 1440, 1.25]]
    assert all(row[6].data_type == "n" and "%" in row[6].number_format for row in rows)
    demand = list(books["work"]["기간별 자원 수요"].iter_rows(min_row=7))
    # The resource report explicitly shows nonzero remaining demand when statusAt exists.
    # This fixture is unstarted at Nov 3 close: it replays the three-day curve from Nov 4.
    assert [row[2].value for row in demand] == ["2026-11-04", "2026-11-06"]
    assert [row[4].value for row in demand] == [2, 10]
    rows = list(books["usage"]["시간축 공수와 원가"].iter_rows(min_row=7))
    rows = [row for row in rows if row[2].value == "R"]
    assert [row[6].value for row in rows[:3]] == [2, 0, 10]
    assert [row[8].value for row in rows[:3]] == [120, 0, 1200]
    assert sum(row[8].value for row in rows) == 1320
    pdf = PdfReader(root / "work.pdf", strict=True)
    text = unicodedata.normalize("NFKC", "\n".join(page.extract_text() or "" for page in pdf.pages))
    compressed = re.sub(r"\s+", "", text)
    for phrase in ("기간별계획배정정의", "25.0%", "125.0%", "1440"):
        assert phrase in compressed, phrase
    assert not any(ord(c) < 32 and c not in "\n\r\t" for c in text)
    return {"status": "success", "schema": 17, "integer_targets": [12000, 0, 60000],
            "day_work_hours": [2, 0, 10], "day_cost": [120, 0, 1200], "total_cost": 1320,
            "baseline_preserved": True, "actual_ledger_preserved": True, "numeric_percent": True,
            "report_nonzero_remaining_hours": [2, 10],
            "pdf_pages": len(pdf.pages), "no_formulas": True}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-governance")),
                     ensure_ascii=False, indent=2))
