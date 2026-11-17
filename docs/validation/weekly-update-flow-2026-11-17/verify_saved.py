"""Read-only checks of files saved through the native weekly-update walkthrough."""
import hashlib
import json
from pathlib import Path

folder = Path(__file__).resolve().parent
original = json.loads((folder.parent / "weekly-update-ux-2026-11-17" / "weekly-prepared.ppm").read_text(encoding="utf-8"))
saved = json.loads((folder / "submitted.ppm").read_text(encoding="utf-8"))
a, b = original["payload"], saved["payload"]
assert original["schemaVersion"] == saved["schemaVersion"]
assert a["tasks"] == b["tasks"]
assert a["baselines"] == b["baselines"]
assert b["statusAt"] == "2026-11-16 09:00"
assert b["progress"][:3] == a["progress"]
assert len(b["progress"]) == 5
assert [e["taskId"] for e in b["progress"][3:]] == ["B", "C"]
parts, assembly = b["progress"][3:]
assert parts["actualDurationMinutes"] == 960 and parts["remainingMinutes"] == 480
assert parts["previousEntryId"] == a["progress"][-1]["id"]
assert assembly["actualDurationMinutes"] == 0 and assembly["remainingMinutes"] == 960
assert assembly["actualStart"] is None and assembly["actualFinish"] is None
assert all(e["statusAt"] == b["statusAt"] for e in (parts, assembly))
# Existing domain contract: changing status invalidates the adopted remaining leveling,
# while retaining its decision and appending the invalidation to the immutable ledger.
assert b["levelingDecisions"][:len(a["levelingDecisions"])] == a["levelingDecisions"]
assert len(b["levelingDecisions"]) == len(a["levelingDecisions"]) + 1
assert b["levelingDecisions"][-1]["action"] == "invalidate"
assert "remainingLeveling" not in b
submitted = (folder / "submitted.ppm").read_bytes()
assert submitted == (folder / "after-cancel.ppm").read_bytes() == (folder / "review.ppm").read_bytes()
print("PASS: schema, tasks, baseline and original 3 progress entries preserved")
print("PASS: exactly 2 new reports (B 2d/1d, C explicit not-started 0d/2d); D/E remain unreported")
print("PASS: status change retains adoption history and appends its expected invalidation")
print("PASS: reopen, browse, cancel, save and calculate leave submitted file bytes unchanged")
print("SHA-256:", hashlib.sha256(submitted).hexdigest())
