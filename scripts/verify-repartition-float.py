"""Independent minute-grid resource oracle and native/XLSX/PDF checks."""
import hashlib
import json
import re
import sys
import zipfile
from pathlib import Path

from openpyxl import load_workbook
from pypdf import PdfReader


def oracle(closed_wednesday=False):
    # Three working days, represented as primitive civil-minute bit sets.
    normal = [day * 1440 + minute for day in range(3)
              for minute in list(range(540, 720)) + list(range(780, 1080))]
    split = [t for t in normal if not 720 <= t < 1440 + 540]
    closed = sum(1 << t for t in normal if t >= 2880) if closed_wednesday else 0

    def candidates(calendar, duration, first):
        rows = []
        for start in range(first, len(calendar) - duration + 1):
            minutes = calendar[start:start + duration]
            mask = sum(1 << t for t in minutes)
            if not mask & closed:
                rows.append((start - first, minutes[-1] + 1, mask))
        return rows

    aa, bb = candidates(split, 480, 0), candidates(normal, 240, 180)
    early_a, early_b = aa[0], bb[0]
    totals, free, count = [0, 0], [0, 0], 0
    for a in aa:
        for b in bb:
            # Original interleaved B/A retain their final resource-return order.
            if b[1] <= a[1] and not a[2] & b[2]:
                totals[0], totals[1] = max(totals[0], a[0]), max(totals[1], b[0])
                count += 1
                if b == early_b:
                    free[0] = max(free[0], a[0])
                if a == early_a:
                    free[1] = max(free[1], b[0])
    expected = ([180, 60], [180, 60]) if closed_wednesday else ([660, 540], [660, 60])
    assert (totals, free) == expected
    return {"total_minutes": totals, "free_minutes": free, "feasible_pairs": count}


def verify(build):
    root = build / "repartition-float-qa"
    native = json.loads((root / "selected.ppm").read_text(encoding="utf-8"))
    payload = native["payload"]
    assert native["schemaVersion"] == 15
    assert len(payload["levelingDecisions"]) == 1
    assert payload["levelingDecisions"][0]["afterPlan"] == payload["planLeveling"]
    canonical = json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
    assert hashlib.sha256(canonical).hexdigest() == native["sha256"]
    assert payload["tasks"][0]["breaks"] == [{"start": "2026-11-02 12:00", "finish": "2026-11-03 09:00"}]
    assert "resourceOrderLinks" not in payload and "repartitionWitnesses" not in payload
    with zipfile.ZipFile(root / "report.xlsx") as archive:
        assert archive.testzip() is None
        assert not any("externalLink" in name or "vbaProject" in name for name in archive.namelist())
    book = load_workbook(root / "report.xlsx", data_only=False)
    assert not any(c.data_type == "f" for sheet in book for row in sheet for c in row)
    rows = list(book["계획 일정"].iter_rows(min_row=7, values_only=True))
    assert [r[0] for r in rows] == [t["id"] for t in payload["tasks"]]
    assert [(r[4], r[5], r[6], r[7], r[9]) for r in rows] == [
        ("2026-11-02 09:00", "2026-11-03 15:00", 11, "평준화 여유", 11),
        ("2026-11-02 13:00", "2026-11-02 17:00", 9, "평준화 여유", 1),
        ("2026-11-02 09:00", "2026-11-04 18:00", 0, "평준화 임계", 0)]
    native_oracle = oracle()
    assert [r[6] * 60 for r in rows[:2]] == native_oracle["total_minutes"]
    assert [r[9] * 60 for r in rows[:2]] == native_oracle["free_minutes"]
    source = {r[0]: r[1] for r in book["출처와 설정"].iter_rows(min_row=7, values_only=True)}
    assert source["계산 엔진"] == "ppm-adopted-leveling-1.8"
    assert source["활성 일정 정책"].endswith("resource-repartition-float-v3")
    assert "구간 재생성" in source["평준화 후 여유"]
    kpis = {r[0]: r[1] for r in book["대시보드"].iter_rows(min_row=7, values_only=True)}
    assert kpis["현재 계획 예산"] == 720 and kpis["계획 임계 작업"] == 1
    diagnostics = "\n".join(str(c.value) for row in book["진단"] for c in row if c.value is not None)
    assert "LEVELING_REPARTITION_WITNESS" in diagnostics
    assert rows[0][0] + "=2026-11-04 09:00~2026-11-04 18:00" in diagnostics
    assert rows[1][0] + "=2026-11-03 14:00~2026-11-03 18:00" in diagnostics
    pages = {}
    for stem in ("report", "print"):
        pdf = PdfReader(root / (stem + ".pdf"), strict=True)
        raw = "\n".join(page.extract_text() or "" for page in pdf.pages)
        compact = re.sub(r"\s+", "", raw)
        assert "resource-repartition-float-v3" in compact and "평준화임계" in compact and "평준화여유" in compact
        assert not any(value in raw for value in ("STALE_FLOAT", "STALE_STATE", "LEVELING_FLOAT_UNAVAILABLE"))
        assert not any(ord(c) < 32 and c not in "\t\r\n" for c in raw)
        pages[stem] = len(pdf.pages)
    return {"status": "success", "schema": 15, "engine": "ppm-adopted-leveling-1.8",
            "independent_minute_grid": native_oracle, "closed_wednesday_oracle": oracle(True),
            "native_breaks_preserved": True, "witnesses_verified": True, "plan_cost": 720, "pdf_pages": pages}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-forecast")), indent=2))
