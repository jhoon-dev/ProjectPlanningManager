"""Independent resource/finish-window witnesses, a nonmonotone capacity oracle, and paired observations."""
import hashlib
import itertools
import json
import statistics
import sys
from datetime import datetime, timedelta
from pathlib import Path

from openpyxl import load_workbook


def minute(stamp, short=False):
    date = datetime.strptime(stamp, "%Y-%m-%d %H:%M")
    anchor = datetime(2026, 11, 2)
    days = (date.date() - anchor.date()).days
    assert days >= 0 and date.weekday() < 5
    workdays = sum((anchor + timedelta(days=d)).weekday() < 5 for d in range(days))
    at = date.hour * 60 + date.minute
    if short:
        assert 540 <= at <= 544
        return workdays * 4 + at - 540
    assert 540 <= at <= 720 or 780 <= at <= 1080
    return workdays * 480 + at - 540 - (60 if at >= 780 else 0)


def reconstructed(case, short=False):
    rows = {r["id"]: r for r in case["rows"]}
    assert len(rows) == len(case["rows"])
    base = {name: (minute(r["start"], short), minute(r["finish"], short), r["start"], r["finish"]) for name, r in rows.items()}
    records = [(r["id"], (minute(r["start"], short), minute(r["finish"], short), r["start"], r["finish"])) for r in case["date_records"]]
    seen = set()
    for proof in case["proofs"]:
        assert proof["id"] in rows and proof["id"] not in seen
        seen.add(proof["id"])
        current = dict(base)
        dates = [records[i] for i in proof["dates"]]
        assert len(dates) == len({d[0] for d in dates}) and proof["id"] in {d[0] for d in dates}
        for name, value in dates:
            assert name in current
            current[name] = value
        assert current[proof["id"]][0] - base[proof["id"]][0] == rows[proof["id"]]["total"]
        yield rows, base, proof, current, len(dates)
    assert seen == set(rows) - {"H"}


def tiny_oracle():
    # Ten four-minute weekdays. Wednesday is closed; Tuesdays/last Friday have one 50% slot.
    dates = []
    day = datetime(2026, 11, 2)
    while len(dates) < 10:
        if day.weekday() < 5:
            dates.append(day.strftime("%Y-%m-%d"))
        day += timedelta(days=1)
    capacities = [({"2026-11-03": 1, "2026-11-04": 0, "2026-11-13": 1}.get(d, 2)) for d in dates for _ in range(4)]
    masks = [(3 << i) for i in range(39)]
    closed = sum(1 << i for i, cap in enumerate(capacities) if cap == 0)
    single = sum(1 << i for i, cap in enumerate(capacities) if cap == 1)
    candidates = [i for i in range(39) if not masks[i] & closed]
    def feasible(a, b, c):
        aa, bb, cc = masks[a], masks[b], masks[c]
        return a + 2 <= c and not (aa & bb & cc) and not (((aa & bb) | (aa & cc) | (bb & cc)) & single)
    valid = [(a, b, c) for a, b, c in itertools.product(candidates, candidates, candidates)
             if c >= 2 and feasible(a, b, c)]
    original = (0, 0, 2)
    total = [max(row[i] - original[i] for row in valid) for i in range(3)]
    free = [max(row[i] - original[i] for row in valid if all(row[j] == original[j] for j in range(3) if j != i)) for i in range(3)]
    assert total == [36, 38, 36] and free == [0, 38, 36]
    assert not feasible(36, 38, 38)  # Individual late starts cannot be applied as one common late schedule.
    return total, free, capacities, len(valid)


def verify(build):
    root = build / "candidate-propagation-qa"
    data = json.loads((root / "metrics.json").read_text(encoding="utf-8"))
    assert data["engine"] == "ppm-adopted-leveling-1.6"
    observations = []
    proof_count = date_count = 0
    for case, name, count, separate in zip(data["cases"], ["resource-60", "coupled-256", "separate-1000", "resource-750"], [60, 256, 1000, 750], [False, False, True, False]):
        assert case["name"] == name
        assert case["legacy_available"] == (name != "resource-750")
        samples = 1 if name == "resource-750" else 3
        assert len(case["legacy_ms"]) == len(case["optimized_ms"]) == samples
        assert all(v > 0 for v in case["legacy_ms"] + case["optimized_ms"])
        before, after = statistics.median(case["legacy_ms"]), statistics.median(case["optimized_ms"])
        counters = case["counters"]
        assert len(counters) == 6 and counters[3] == 1 and counters[4] == count - 1
        assert counters[1] == count * (4 if separate else 3)
        if name.startswith("resource-"):
            assert counters[0] == count * (count - 1) // 2
        for rows, base, proof, current, references in reconstructed(case):
            assert len(rows) == count * (2 if separate else 1) + 1
            tasks = [current[f"T{i}"] for i in range(count)]
            step = 3 if separate else 1
            assert len({r[0] for r in tasks}) == count
            assert all(i * step <= row[0] <= i * step + 2 and row[1] == row[0] + 1 for i, row in enumerate(tasks))
            assert all(tasks[i][3] <= tasks[i + 1][2] for i in range(count - 1))
            assert all(row[3] <= base["H"][3] for row in tasks)
            for i in range(count):
                row = rows[f"T{i}"]
                assert (row["total"], row["free"], row["critical"]) == (2, 2 if separate or i == count - 1 else 0, False)
                if separate:
                    assert current[f"M{i}"] == base[f"M{i}"] and tasks[i][3] <= base[f"M{i}"][2]
            proof_count += 1
            date_count += references
        observations.append({"name": name, "samples_each": samples, "legacy_available": case["legacy_available"],
                             "legacy_median_ms": before, "optimized_median_ms": after,
                             "observed_ratio": before / after if case["legacy_available"] else None, "counters": counters})
    totals, free, capacities, valid_count = tiny_oracle()
    tiny = data["nonmonotone"]
    assert tiny["counters"][2] > 0
    for rows, base, proof, current, references in reconstructed(tiny, True):
        for i, name in enumerate(("A", "B", "C")):
            assert (rows[name]["total"], rows[name]["free"]) == (totals[i], free[i])
        tasks = [current[name] for name in ("A", "B", "C")]
        assert all(row[1] == row[0] + 2 and row[0] >= base[name][0] for name, row in zip(("A", "B", "C"), tasks))
        assert tasks[0][3] <= tasks[2][2]
        assert all(sum(start <= i < finish for start, finish, *_ in tasks) <= cap for i, cap in enumerate(capacities))
        proof_count += 1
        date_count += references
    native = json.loads((root / "selected.ppm").read_text(encoding="utf-8"))
    canonical = json.dumps(native["payload"], sort_keys=True, ensure_ascii=False, separators=(",", ":")).encode()
    assert native["sha256"] == hashlib.sha256(canonical).hexdigest() and native["schemaVersion"] == 12
    baselines = native["payload"]["baselines"]
    assert [b["engineVersion"] for b in baselines] == ["ppm-adopted-leveling-1.5", "ppm-adopted-leveling-1.6"]
    assert baselines[0]["schedule"] == baselines[1]["schedule"]
    assert [r["totalFloatMinutes"] for r in baselines[1]["schedule"]] == [2] * 60 + [0]
    book = load_workbook(root / "report.xlsx", data_only=False)
    source = {r[0]: r[1] for r in book["출처와 설정"].iter_rows(min_row=7, values_only=True)}
    assert source["계산 엔진"] == "ppm-adopted-leveling-1.6"
    rows = list(book["계획 일정"].iter_rows(min_row=7, values_only=True))
    assert [round(r[6] * 60) for r in rows] == totals + [0]
    assert [round(r[9] * 60) for r in rows] == free + [0]
    assert any("LEVELING_REPARTITION_PROPAGATION_POLICY" in str(c.value) for row in book["진단"] for c in row)
    return {"status": "success", "engine": data["engine"], "paired_observations": observations,
            "witnesses_checked": proof_count, "date_references_checked": date_count,
            "tiny_exhaustive_feasible": valid_count, "incompatible_individual_late_schedule_detected": True,
            "legacy_1_5_baseline_preserved": True, "native_schema": 12, "xlsx_checked": True}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-forecast")), indent=2))
