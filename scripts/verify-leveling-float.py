"""Independently inspect fixed-resource-order float and critical-path exports."""
import json
import re
import sys
import zipfile
from pathlib import Path

from openpyxl import load_workbook
from pypdf import PdfReader


def verify(build):
    root = build / "leveling-float-qa"
    with zipfile.ZipFile(root / "report.xlsx") as archive:
        assert archive.testzip() is None
        assert not any("externalLink" in n or "vbaProject" in n for n in archive.namelist())
    book = load_workbook(root / "report.xlsx", data_only=False)
    assert not any(c.data_type == "f" for sheet in book for row in sheet for c in row)
    rows = list(book["계획 일정"].iter_rows(min_row=7, values_only=True))
    assert len(rows) == 3
    assert [(r[4], r[5], r[6], r[7], r[9]) for r in rows] == [
        ("2026-11-02 09:00", "2026-11-02 18:00", 0, "평준화 임계", 0),
        ("2026-11-03 09:00", "2026-11-03 18:00", 0, "평준화 임계", 0),
        ("2026-11-02 09:00", "2026-11-02 18:00", 8, "평준화 여유", 8)]
    links = list(book["자원 순서 관계"].iter_rows(min_row=7, values_only=True))
    assert len(links) == 1 and links[0][0] == rows[0][0] and links[0][1] == rows[1][0]
    assert links[0][3] == 100 and links[0][4] == "driving"
    kpis = {r[0]: r[1] for r in book["대시보드"].iter_rows(min_row=7, values_only=True)}
    assert kpis["계획 임계 작업"] == 2 and kpis["현재 계획 예산"] == 960
    source = {r[0]: r[1] for r in book["출처와 설정"].iter_rows(min_row=7, values_only=True)}
    assert source["계산 엔진"] == "jobs-adopted-leveling-1.8"
    assert "resource-order-float-v1" in source["활성 일정 정책"]
    pages = {}
    for name in ("report", "print"):
        pdf = PdfReader(root / (name + ".pdf"), strict=True)
        text = "\n".join(p.extract_text() or "" for p in pdf.pages)
        compact = re.sub(r"\s+", "", text)
        assert "resource-order-float-v1" in compact and "평준화임계" in compact and "평준화여유" in compact
        assert "STALE_FLOAT" not in text and "STALE_STATE" not in text
        assert not any(ord(c) < 32 and c not in "\t\r\n" for c in text)
        pages[name] = len(pdf.pages)
    return {"status": "success", "A_total_free_hours": [0, 0], "B_total_free_hours": [0, 0],
            "C_total_free_hours": [8, 8], "critical_tasks": 2, "resource_driving_links": 1,
            "plan_cost": 960, "dates_preserved": True, "engine": "jobs-adopted-leveling-1.8", "pdf_pages": pages}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-forecast")), indent=2))
