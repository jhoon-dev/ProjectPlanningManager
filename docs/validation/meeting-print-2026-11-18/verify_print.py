"""Read-only independent PDF/preservation checks for the native UX-W06 walkthrough."""
from pathlib import Path
import hashlib
import re
from pypdf import PdfReader

folder = Path(__file__).resolve().parent
source = folder.parent / "outcome-summary-2026-11-17/review.ppm"
assert source.read_bytes() == (folder / "review.ppm").read_bytes()
print("PASS: project saved bytes unchanged by presets, preview and both PDF exports")
names = ["설계 검토", "부품 준비", "조립", "현장 설치 준비", "시운전"]
compact = lambda text: re.sub(r"\s+", "", text)
for filename, bounds in [("meeting-full.pdf", ("2026-11-09", "2026-11-24")),
                         ("meeting-next-week.pdf", ("2026-11-23", "2026-11-29"))]:
    path = folder / filename
    pdf = PdfReader(path)
    # Native preview reported 3 pages: one data page and two full provenance pages.
    assert len(pdf.pages) == 3
    texts = [compact(page.extract_text()) for page in pdf.pages]
    for i, (page, text) in enumerate(zip(pdf.pages, texts), 1):
        assert abs(float(page.mediabox.width) - 842) < 1
        assert abs(float(page.mediabox.height) - 595) < 1
        for term in ["상태일2026-11-1609:00", "기준선v1최초계획", "전체보고3/5",
                     "전체완료2026-11-2418:00", "기준선대비+5d(+40h)", f"{i}/3페이지"]:
            assert term in text, (filename, i, term)
        assert "STALE" not in text
    first = texts[0]
    for name in names:
        assert compact(name) in first, (filename, name)
    for term in ["현재완료", "기준선완료", "완료편차", "상태일보고", "미보고·미정", *bounds]:
        assert term in first, (filename, term)
    assert all("인쇄출처" in text for text in texts[1:])
    all_text = "".join(texts)
    for term in ["jobs-adopted-leveling-1.8", "UNREPORTED_REMAINING", "RESOURCE_DELAY",
                 "serial-no-split-v1", "설치기술자중복배정해소안채택", "전체프로젝트·화면필터제외"]:
        assert term in all_text, (filename, term)
    print(f"PASS: {filename}: 3 A4 landscape pages; 5 tasks on one data page; repeated facts and full provenance")
    print("SHA-256:", hashlib.sha256(path.read_bytes()).hexdigest())
print("PASS: next-week time axis keeps all 5 task rows; no source data mutation")
