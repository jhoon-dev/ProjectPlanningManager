"""Independent schema/hash, triangular quantile and automation receipt checks; stdlib only."""
import hashlib
import json
import math
from pathlib import Path
import sys


def verify(build):
    root = build / "governance-qa"
    envelope = json.loads((root / "governance.ppmworkspace").read_text(encoding="utf-8"))
    assert envelope["format"] == "PPM-WORKSPACE" and envelope["schemaVersion"] == 3
    body = envelope["payload"]
    canonical = json.dumps(body, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
    assert hashlib.sha256(canonical).hexdigest() == envelope["sha256"]
    g = body["governance"]
    assert len(g["members"]) == 4 and g["grants"][0]["role"] == "owner"
    assert g["events"][0]["kind"] == "request" and g["events"][1]["kind"] == "approve"
    assert g["events"][1]["targetId"] == g["events"][0]["id"]
    assert g["events"][0]["projectRevision"] == g["events"][1]["projectRevision"] == 5
    assert {p["currency"] for p in g["portfolio"]} == {"KRW", "USD"}
    assert g["risks"][0]["optimistic"] == 480 and g["risks"][0]["probability"] == 10000
    result = json.loads((root / "risk-result.json").read_text(encoding="utf-8"))
    assert result["seed"] == 12345 and result["iterations"] == 2000
    # Independent Monday-Friday 09-12/13-18 calendar, starting Mon Nov 2.
    from datetime import datetime, timedelta
    def offset(stamp):
        end = datetime.strptime(stamp, "%Y-%m-%d %H:%M")
        start = datetime(2026, 11, 2)
        total = 0
        while start.date() <= end.date():
            if start.weekday() < 5:
                for a, b in [(9, 12), (13, 18)]:
                    left = start.replace(hour=a)
                    right = min(start.replace(hour=b), end)
                    total += max(0, int((right-left).total_seconds() // 60))
            start += timedelta(days=1)
        return total
    expected = []
    observed = []
    for percent in [10, 50, 80, 90]:
        u = percent / 100
        quantile = 480 + (480 + math.sqrt(u*960*480) if u < .5 else 1440-math.sqrt((1-u)*960*480))
        actual = offset(result[f"P{percent}"])
        assert abs(actual - quantile) < 50, (percent, actual, quantile)
        expected.append(round(quantile, 3))
        observed.append(actual)
    assert observed == sorted(observed)
    assert 800 < result["deadlineHits"] < 1200 and result["medianDriftMinutes"] < 70
    receipt = json.loads((root / "receipt.json").read_text(encoding="utf-8"))
    assert receipt["format"] == "PPM-AUTOMATION-RECEIPT-1" and receipt["state"] == "applied"
    assert receipt["resultRevision"] == receipt["sourceRevision"] + 1
    assert len(receipt["requestSha256"]) == 64 and len(receipt["operations"]) == 2
    record = {"status": "passed", "checks": ["workspace3 SHA256", "qualified role/membership", "immutable approval revision", "currency separation", "triangular analytical quantiles", "independent calendar", "deadline probability", "median stability", "atomic receipt"], "expectedMinutes": expected, "observedMinutes": observed}
    (root / "independent-verification.json").write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(record, ensure_ascii=False))


if __name__ == "__main__":
    verify(Path(sys.argv[1] if len(sys.argv)>1 else "build").resolve())
