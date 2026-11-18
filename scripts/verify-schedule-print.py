"""Independent PDF checks for schedule-print-v1 and QPrinter PDF device fixtures."""
import json
import re
import sys
import unicodedata
from pathlib import Path

from pypdf import PdfReader


def compact(text):
    return re.sub(r"\s+", "", unicodedata.normalize("NFKC", text))


def verify(build):
    root = build / "schedule-print-qa"
    proof = json.loads((root / "layout.json").read_text(encoding="utf-8"))
    readers = {}
    texts = {}
    for stem, count in (("schedule", proof["pages"]), ("printer", proof["pages"]),
                        ("range", 4), ("portrait", proof["portrait_pages"]),
                        ("tiled", proof["tiled_pages"]), ("selected", proof["selected_pages"])):
        reader = PdfReader(root / (stem + ".pdf"), strict=True)
        assert len(reader.pages) == count, (stem, len(reader.pages), count)
        page_text = []
        raw_text = []
        for page in reader.pages:
            width, height = float(page.mediabox.width), float(page.mediabox.height)
            expected = (842, 1191) if stem == "portrait" else (842, 595)
            assert abs(width - expected[0]) < 2 and abs(height - expected[1]) < 2, (stem, width, height)
            raw = page.extract_text() or ""
            raw_text.append(raw)
            assert not any(ord(c) < 32 and c not in "\n\r\t" for c in raw), stem
            text = compact(raw)
            assert "PPM" in text and "revision" in text and "페이지" in text, stem
            page_text.append(text)
        whole = "\n".join(page_text)
        if stem != "range":
            for phrase in ("END_LONG_NAME", "END_LONG_CELL", "END_HEADER", proof["document_id"],
                           "schedule-print-contract", "2026-11-03T03:00:00Z", "인쇄기준선"):
                assert phrase in whole, (stem, phrase)
            assert set(map(int, re.findall(r"행_(\d+)", "\n".join(raw_text)))) == set(range(1, 80)), stem
            assert all(value not in whole for value in ("STALE_START", "STALE_FINISH", "STALE_FLOAT", "STALE_STATE")), stem
        readers[stem] = reader
        texts[stem] = page_text
    assert texts["schedule"] == texts["printer"], "PDF and 300dpi printer device changed page content/order"
    assert texts["range"] == [texts["schedule"][2], texts["schedule"][2],
                              texts["schedule"][1], texts["schedule"][1]], "range/reverse/uncollated copies mismatch"
    for phrase in ("2026-11-0209:00", "2026-11-0309:00", "실제완료", "상태일"):
        assert phrase in "".join(texts["schedule"]), phrase
    assert "평준화임계" in "".join(texts["selected"]) and "자원순서" in "".join(texts["selected"])
    assert "2026-11-15~2026-11-15" in "".join(texts["tiled"]), "partial timeline page lost"
    colors = []

    def visit(operator, operands, _cm, _tm):
        if operator in (b"rg", b"RG", b"scn", b"SCN") and len(operands) == 3:
            try:
                colors.append(tuple(map(float, operands)))
            except (ValueError, TypeError):
                pass
        if operator in (b"g", b"G") and len(operands) == 1:
            colors.append((float(operands[0]),) * 3)

    for page in readers["schedule"].pages:
        page.extract_text(visitor_operand_before=visit)
    # Completion fill, critical outline, delay marker and baseline line use
    # the shared Gantt palette in both screen and print output.
    for rgb in ((81, 179, 132), (158, 63, 57), (216, 132, 22), (119, 119, 119)):
        assert any(all(abs(a - b / 255) < .01 for a, b in zip(color, rgb)) for color in colors), rgb
    return {"status": "success", "pdf_files": 6,
            "pages": {key: len(value.pages) for key, value in readers.items()},
            "tasks": 80, "long_name_cell_header_preserved": True,
            "pdf_printer_same_content": True, "range_reverse_copies": [3, 3, 2, 2],
            "actual_critical_baseline_colors": True, "selected_resource_order_float": True,
            "physical_printer_run": False}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-forecast")), indent=2))
