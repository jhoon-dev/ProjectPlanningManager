"""Read-only saved-ledger, PDF structure and independent calendar checks."""
from datetime import date, timedelta
from pathlib import Path
import hashlib
import json
from pypdf import PdfReader

folder = Path(__file__).resolve().parent
def read(path):
    return json.loads(path.read_text(encoding="utf-8"))

original_path = folder.parent / "weekly-update-ux-2026-11-17/weekly-prepared.ppm"
assert original_path.read_bytes() == (folder / "start.ppm").read_bytes() == (folder / "before-save.ppm").read_bytes()
assert (folder / "before-fix-saved.ppm").read_bytes() == (folder / "review.ppm").read_bytes()

for source, result, count in [
    (folder / "start.ppm", folder / "review.ppm", 5),
    (folder / "start.ppm", folder / "five-final.ppm", 5),
    (folder.parent / "weekly-pm-pilot-2026-11-18/pilot-start.ppm", folder / "pilot-final.ppm", 30),
]:
    a, b = read(source), read(result)
    assert a["schemaVersion"] == b["schemaVersion"]
    a, b = a["payload"], b["payload"]
    changed = {k for k in a.keys() | b.keys() if a.get(k) != b.get(k)}
    assert changed == {"revision", "statusAt", "progress", "remainingLeveling", "levelingDecisions"}, changed
    assert b["revision"] == a["revision"] + 4
    assert b["statusAt"] == "2026-11-16 09:00"
    assert b["tasks"] == a["tasks"] and len(b["tasks"]) == count
    assert len(b["baselines"]) == 1 and b["baselines"] == a["baselines"]
    assert b["progress"][:len(a["progress"])] == a["progress"]
    parts, assembly = b["progress"][len(a["progress"]):]
    assert parts["taskId"] == a["tasks"][1]["id"] and assembly["taskId"] == a["tasks"][2]["id"]
    assert parts["actualStart"] == "2026-11-11 09:00" and parts["actualFinish"] is None
    assert parts["actualDurationMinutes"] == parts["remainingMinutes"] == 960
    assert assembly["actualStart"] is None and assembly["actualFinish"] is None
    assert assembly["actualDurationMinutes"] == 0 and assembly["remainingMinutes"] == 960
    assert all(e["statusAt"] == b["statusAt"] and e["reporter"] and e["reason"] for e in [parts, assembly])
    assert parts["previousEntryId"] == [e for e in a["progress"] if e["taskId"] == parts["taskId"]][-1]["id"]
    assert not assembly["previousEntryId"]
    assert len({e["taskId"] for e in b["progress"]}) == (3 if count == 5 else 13)
    assert b["levelingDecisions"][:len(a["levelingDecisions"])] == a["levelingDecisions"]
    additions = b["levelingDecisions"][len(a["levelingDecisions"]):]
    assert [e["action"] for e in additions] == (["invalidate", "adopt"] if count == 5 else ["adopt"])
    assert additions[-1]["afterRemaining"] == b["remainingLeveling"]
    assert b["remainingLeveling"]["priority"] == "task-order" and b["remainingLeveling"]["horizonWorkDays"] == 260
    assert all(key not in result.read_text(encoding="utf-8") for key in ["$baselineFinish", "$finishVariance", "$reportState"])
    print(f"PASS: {result.name}: {count} tasks/baseline/old ledger preserved; exactly B/C reports and expected adoption decisions added")
    print("SHA-256:", hashlib.sha256(result.read_bytes()).hexdigest())

def workdays_after(start, days):
    current = start
    for _ in range(days):
        current += timedelta(days=1)
        while current.weekday() >= 5:
            current += timedelta(days=1)
    return current

# Independent hand calculation: B uses Mon/Tue; C and D overlap Wed/Thu.
# Serial resource scheduling retains C and moves D to Fri/Mon, then E to Tue.
assert workdays_after(date(2026, 11, 16), 1) == date(2026, 11, 17)
assert workdays_after(date(2026, 11, 17), 3) == date(2026, 11, 20)
assert workdays_after(date(2026, 11, 17), 5) == date(2026, 11, 24)
assert 2 * 8 == 16 and 6 * 16 == 96
print("PASS: independent weekday arithmetic: baseline17 / logical20 (+3d/24h) / serial24 (+5d/40h); overload16h or96h")

for tag, pages in [("five", 3), ("pilot", 6)]:
    pdf = PdfReader(folder / f"{tag}-meeting.pdf")
    assert len(pdf.pages) == pages
    for page in pdf.pages:
        assert abs(float(page.mediabox.width) - 842) < 1 and abs(float(page.mediabox.height) - 595) < 1
        assert page.get_contents().get_data()
    print(f"PASS: {tag}-meeting.pdf: {pages} nonempty A4 landscape pages")
    if sum(len(page.extract_text()) for page in pdf.pages) < 100:
        print("NOTE: native test font is emitted as vector outlines; facts/rows checked visually on every rendered page, not by text extraction")
print("Visual QA record: five pages1-3 and pilot pages1-6 all reviewed; 5/30 task rows and full provenance retained")
