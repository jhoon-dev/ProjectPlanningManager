"""Read-only preservation checks for the native UX-W05 walkthrough."""
from pathlib import Path
import hashlib
import json

folder = Path(__file__).resolve().parent
source = (folder.parent / "save-reforecast-flow-2026-11-17/submitted.ppm").read_bytes()
before = (folder / "before-explicit-save.ppm").read_bytes()
saved = (folder / "review.ppm").read_bytes()
assert before == source
a, b = json.loads(source), json.loads(saved)
assert a["schemaVersion"] == b["schemaVersion"]
a, b = a["payload"], b["payload"]
changed = {k for k in a.keys() | b.keys() if a.get(k) != b.get(k)}
assert changed == {"revision", "remainingLeveling", "levelingDecisions"}, changed
assert b["revision"] == a["revision"] + 1
assert b["statusAt"] == "2026-11-16 09:00"
assert len(b["progress"]) == 6 and len(b["baselines"]) == 1 and len(b["tasks"]) == 5
assert b["levelingDecisions"][:-1] == a["levelingDecisions"]
entry = b["levelingDecisions"][-1]
assert entry["action"] == "adopt" and entry["beforeRemaining"] is None
assert entry["afterRemaining"] == b["remainingLeveling"]
assert entry["previousEntryId"] == a["levelingDecisions"][-1]["id"]
selection = b["remainingLeveling"]
assert selection["priority"] == "task-order" and selection["horizonWorkDays"] == 260
assert selection["reason"] == "주간 검토: 설치 기술자 중복 배정 해소안 채택"
assert selection["sourceRevision"] == a["revision"]
print("PASS: browsing and adoption leave the explicit saved file unchanged until Ctrl+S")
print("PASS: explicit save adds only remaining adoption, one decision and revision")
print("PASS: schema, tasks, 6 progress entries, baseline, assignments, status and prior decisions preserved")
print("PASS: new decision links prior history and records selection/reason/source revision")
print("Saved SHA-256:", hashlib.sha256(saved).hexdigest())
