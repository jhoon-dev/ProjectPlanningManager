"""Independent native/pypdf checks, including PDF-versus-printer physical geometry."""
import hashlib
import json
import math
from pathlib import Path
import re
import sys
import unicodedata

from pypdf import PdfReader


def compact(value):
    return re.sub(r"\s+", "", unicodedata.normalize("NFKC", value))


def geometry(page):
    color, stack, points, boxes, fonts = None, [], [], [], []

    def operand(op, args, cm, _tm):
        nonlocal color, points
        if op == b"q":
            stack.append(color)
        elif op == b"Q" and stack:
            color = stack.pop()
        elif op in (b"scn", b"rg") and len(args) == 3:
            color = tuple(float(x) for x in args)
        elif op in (b"m", b"l"):
            if op == b"m":
                points = []
            x, y = map(float, args[:2])
            a, b, c, d, e, f = map(float, cm)
            points.append((a*x+c*y+e, b*x+d*y+f))
        elif op in (b"f", b"f*") and points and color:
            if abs(color[0] - 208/255) < .002 and abs(color[1] - 239/255) < .002 and color[2] > .99:
                xs, ys = zip(*points)
                boxes.append((max(xs)-min(xs), max(ys)-min(ys)))

    def text(value, cm, _tm, _font, size):
        if "네트워크" in value or "기초" in value:
            fonts.append((compact(value), float(size)*math.hypot(float(cm[0]), float(cm[1]))))

    page.extract_text(visitor_operand_before=operand, visitor_text=text)
    assert boxes, "missing vector blue card"
    return boxes[0], fonts


def verify(build):
    root = build / "print-network-qa"
    native = json.loads((root / "presentation.ppm").read_text(encoding="utf-8"))
    p = native["payload"]
    assert native["schemaVersion"] == 18
    digest = hashlib.sha256(json.dumps(p, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    assert digest == native["sha256"]
    prefs = p["printPreferences"]
    assert prefs["paper"] == 1 and prefs["margin"] == 12 and prefs["relations"] and prefs["wbs"]
    assert p["networkPositions"] == [{"taskId": "TA", "x": 20, "y": 40}, {"taskId": "TB", "x": 800, "y": 50}]
    assert "printPreferences" not in p["baselines"][0]["input"]
    assert "networkPositions" not in p["baselines"][0]["input"]
    meta = json.loads((root / "expected.json").read_text())
    pdfs = {name: PdfReader(root / (name + ".pdf"), strict=True)
            for name in ("schedule", "network-fit", "network-tiles", "network-printer", "network-notes")}
    assert len(pdfs["schedule"].pages) == meta["schedule_pages"]
    assert len(pdfs["network-fit"].pages) == 1
    assert len(pdfs["network-tiles"].pages) == len(pdfs["network-printer"].pages) == meta["network_tiles"]
    assert len(pdfs["network-notes"].pages) > meta["network_tiles"]
    text = {name: compact("\n".join(page.extract_text() or "" for page in pdf.pages)) for name, pdf in pdfs.items()}
    for phrase in ("WBS", "시설", "표시2작업", "관계참조", "FS60m"):
        assert phrase in text["schedule"], phrase
    for phrase in ("TA", "TB", "기초설계", "상세설계", "FS+60m"):
        assert phrase in text["network-tiles"], phrase
    assert "전체작업명/ID" in text["network-notes"]
    assert "x"*1000 in text["network-notes"]
    box, fonts = geometry(pdfs["network-tiles"].pages[0])
    printer_box, printer_fonts = geometry(pdfs["network-printer"].pages[0])
    assert all(abs(a-b) < .2 for a, b in zip(box, printer_box)), (box, printer_box)
    assert abs(box[0]-225) < .2 and abs(box[1]-112.5) < .2, box
    assert fonts and printer_fonts
    assert abs(fonts[0][1]-printer_fonts[0][1]) < .2, (fonts, printer_fonts)
    first_fonts, second_fonts = dict(fonts), dict(printer_fonts)
    assert any("기초" in key for key in first_fonts)
    for key in first_fonts.keys() & second_fonts.keys():
        assert abs(first_fonts[key]-second_fonts[key]) < .2, (key, first_fonts[key], second_fonts[key])
    assert not (root / "cancelled.pdf").exists()
    return {"status": "success", "schema": 18, "positions": 2, "schedule_pages": meta["schedule_pages"],
            "scoped_wbs": True, "relations": 1, "network_tiles": meta["network_tiles"],
            "notes_pages": len(pdfs["network-notes"].pages)-meta["network_tiles"],
            "pdf_printer_card_points": list(box), "pdf_printer_font_points": fonts[0][1],
            "physical_geometry_matches": True, "cancel_no_file": True}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build")), ensure_ascii=False, indent=2))
