"""Read-only checks of the native save/reforecast walkthrough files."""
from pathlib import Path
import hashlib
import json

folder = Path(__file__).resolve().parent
original = (folder.parent / "weekly-update-flow-2026-11-17/submitted.ppm").read_bytes()
before = (folder / "before-explicit-save.ppm").read_bytes()
calculated = (folder / "after-reforecast-before-save.ppm").read_bytes()
saved = (folder / "submitted.ppm").read_bytes()
assert original == before == calculated
a, b = json.loads(original), json.loads(saved)
assert a["schemaVersion"] == b["schemaVersion"]
a, b = a["payload"], b["payload"]
assert a["tasks"] == b["tasks"] and a["baselines"] == b["baselines"]
assert a["statusAt"] == b["statusAt"] == "2026-11-16 09:00"
assert b["progress"][:5] == a["progress"] and len(b["progress"]) == 6
assert b["levelingDecisions"] == a["levelingDecisions"]
entry = b["progress"][-1]
assert entry["taskId"] == "B" and entry["actualDurationMinutes"] == 960 and entry["remainingMinutes"] == 960
assert entry["previousEntryId"] == a["progress"][3]["id"]
assert not any(e["taskId"] in ("D", "E") for e in b["progress"])
assert (folder / "review.ppm").read_bytes() == saved
print("PASS: reporting and explicit reforecast leave on-disk input unchanged until file save")
print("PASS: explicit file save adds exactly one correction for B; no report from cancelled D/E")
print("PASS: schema, task inputs, baseline, status and existing progress/leveling history preserved")
print("Saved SHA-256:", hashlib.sha256(saved).hexdigest())
