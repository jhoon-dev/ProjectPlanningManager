"""Independently read the native artifact from leveling_decision_contract."""
import hashlib
import json
from pathlib import Path
import sys


def verify(build_dir):
    path = Path(build_dir) / "leveling-decision-qa" / "source.ppm"
    envelope = json.loads(path.read_text(encoding="utf-8"))
    assert envelope["format"] == "JOBS" and envelope["schemaVersion"] == 15
    payload = envelope["payload"]
    compact = json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
    assert hashlib.sha256(compact).hexdigest() == envelope["sha256"], "payload checksum"
    entries = payload["levelingDecisions"]
    expected = ["adopt", "undo", "redo", "adopt", "invalidate", "undo", "invalidate", "undo", "redo", "undo",
                "release", "adopt", "invalidate", "adopt", "release"]
    assert [e["action"] for e in entries] == expected, "independent action sequence"
    ids = set()
    previous = None
    for e in entries:
        assert e["id"] not in ids
        ids.add(e["id"])
        assert e["previousEntryId"] == (previous["id"] if previous else "")
        assert e["recordedRevision"] == e["sourceRevision"] + 1 <= payload["revision"]
        assert e["reason"].strip() and e["recordedAt"].endswith("Z")
        if previous:
            assert e["sourceRevision"] >= previous["recordedRevision"]
            assert e["beforePlan"] == previous["afterPlan"]
            assert e["beforeRemaining"] == previous["afterRemaining"]
        previous = e
    assert entries[0]["afterPlan"]["priority"] == "task-order"
    assert entries[3]["afterPlan"]["priority"] == "longest-first"
    assert entries[12]["beforePlan"] and entries[12]["afterPlan"] is None
    assert entries[13]["afterRemaining"] and entries[13]["afterStatusAt"] == "2026-11-02 18:00"
    assert entries[-1]["sourceDocumentId"] == payload["documentId"]
    assert entries[0]["sourceDocumentId"] != payload["documentId"], "copy lost ancestor provenance"
    assert entries[-1]["afterPlan"] is None and entries[-1]["afterRemaining"] is None
    assert payload.get("planLeveling") is None and payload.get("remainingLeveling") is None
    assert len(payload["baselines"]) == 1 and len(payload["costEntries"]) == 1
    return {"schema": 15, "decisions": len(entries), "checksum": "passed", "sequence": "passed",
            "links_revisions_settings": "passed", "plan_remaining_copy_archives": "passed"}


if __name__ == "__main__":
    print(json.dumps(verify(sys.argv[1] if len(sys.argv) > 1 else "build"), ensure_ascii=False, indent=2))
