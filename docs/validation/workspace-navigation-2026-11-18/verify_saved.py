"""Read-only whole-file preservation check for the UX-W07 native walkthrough."""
from pathlib import Path
import hashlib
import json

folder = Path(__file__).resolve().parent
source = (folder.parent / "meeting-print-2026-11-18/review.ppm").read_bytes()
saved = (folder / "review.ppm").read_bytes()
assert source == saved, "view navigation changed the saved project bytes"
before, after = json.loads(source), json.loads(saved)
assert before == after
assert before["schemaVersion"] == after["schemaVersion"]
p = after["payload"]
assert len(p["tasks"]) == 5 and len(p["progress"]) == 6
assert len(p["baselines"]) == 1 and len(p["levelingDecisions"]) == 3
assert p["statusAt"] == "2026-11-16 09:00"
assert p["remainingLeveling"]["reason"] == "주간 검토: 설치 기술자 중복 배정 해소안 채택"
assert p["levelingDecisions"][-1]["afterRemaining"] == p["remainingLeveling"]
for key in ("$baselineFinish", "$finishVariance", "$reportState"):
    assert key not in saved.decode("utf-8")
print("PASS: review.ppm is byte-identical to the UX-W06 source after native navigation")
print("PASS: entire payload, schema, 5 tasks, 6 report entries, baseline, status and 3 decisions preserved")
print("PASS: current adoption and reason preserved; transient presentation columns not persisted")
print("Saved SHA-256:", hashlib.sha256(saved).hexdigest())
