"""Independent native/XLSX/PDF checks for cross-split resource-window float."""
import hashlib
import json
import re
import sys
import zipfile
from pathlib import Path

from openpyxl import load_workbook
from pypdf import PdfReader


def verify(build):
    root = build / "segment-leveling-float-qa"
    native = json.loads((root / "selected.ppm").read_text(encoding="utf-8"))
    assert native["schemaVersion"] == 15
    payload = native["payload"]
    assert len(payload["levelingDecisions"]) == 1
    assert payload["levelingDecisions"][0]["afterPlan"] == payload["planLeveling"]
    canonical = json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
    assert hashlib.sha256(canonical).hexdigest() == native["sha256"]
    assert payload["tasks"][0]["breaks"] == [{"start": "2026-11-02 12:00", "finish": "2026-11-03 09:00"}]
    assert "resourceOrderLinks" not in payload
    with zipfile.ZipFile(root / "report.xlsx") as archive:
        assert archive.testzip() is None
        assert not any("externalLink" in n or "vbaProject" in n for n in archive.namelist())
    book = load_workbook(root / "report.xlsx", data_only=False)
    assert not any(c.data_type == "f" for sheet in book for row in sheet for c in row)
    rows = list(book["계획 일정"].iter_rows(min_row=7, values_only=True))
    assert [r[0] for r in rows] == [t["id"] for t in payload["tasks"]]
    assert [(r[4], r[5], r[6], r[7], r[9]) for r in rows] == [
        ("2026-11-02 09:00", "2026-11-03 15:00", 0, "평준화 임계", 0),
        ("2026-11-02 13:00", "2026-11-02 17:00", 1, "평준화 여유", 1),
        ("2026-11-02 09:00", "2026-11-02 18:00", 5, "평준화 여유", 5)]
    links = list(book["자원 순서 관계"].iter_rows(min_row=7, values_only=True))
    assert len(links) == 2
    assert all(r[2] == payload["resources"][0]["id"] for r in links)
    a, b = rows[0][0], rows[1][0]
    assert [(r[0], r[1], r[3], r[5], r[6]) for r in links] == [
        (a, b, 100, "2026-11-02 12:00", "2026-11-02 13:00"),
        (b, a, 100, "2026-11-02 17:00", "2026-11-03 09:00")]
    kpis = {r[0]: r[1] for r in book["대시보드"].iter_rows(min_row=7, values_only=True)}
    assert kpis["계획 임계 작업"] == 1 and kpis["현재 계획 예산"] == 720
    source = {r[0]: r[1] for r in book["출처와 설정"].iter_rows(min_row=7, values_only=True)}
    assert source["계산 엔진"] == "jobs-adopted-leveling-1.8"
    assert source["활성 일정 정책"].endswith("resource-repartition-float-v3")
    pages = {}
    for name in ("report", "print"):
        pdf = PdfReader(root / (name + ".pdf"), strict=True)
        raw = "\n".join(p.extract_text() or "" for p in pdf.pages)
        compact = re.sub(r"\s+", "", raw)
        assert "resource-repartition-float-v3" in compact and "평준화임계" in compact and "평준화여유" in compact
        assert not any(v in raw for v in ("STALE_FLOAT", "STALE_STATE", "LEVELING_FLOAT_UNAVAILABLE"))
        assert not any(ord(c) < 32 and c not in "\t\r\n" for c in raw)
        pages[name] = len(pdf.pages)
    return {"status": "success", "schema": 15, "breaks_preserved": True, "segment_handoffs": 2,
            "A_total_free_hours": [0, 0], "B_total_free_hours": [1, 1], "C_total_free_hours": [5, 5],
            "critical_tasks": 1, "plan_cost": 720, "pdf_pages": pages,
            "policy": "resource-repartition-float-v3", "engine": "jobs-adopted-leveling-1.8"}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-forecast")), indent=2))
