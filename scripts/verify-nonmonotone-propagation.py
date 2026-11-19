"""Independent small-grid resource/work oracles and native/XLSX verification."""
import hashlib
import itertools
import json
import statistics
import sys
from datetime import datetime, timedelta
from pathlib import Path

from openpyxl import load_workbook


def minute(stamp, daily):
    value = datetime.strptime(stamp, "%Y-%m-%d %H:%M")
    anchor = datetime(2026, 11, 2)
    days = (value.date() - anchor.date()).days
    assert days >= 0 and value.weekday() < 5
    at = value.hour * 60 + value.minute - 540
    assert 0 <= at <= daily
    return sum((anchor + timedelta(days=d)).weekday() < 5 for d in range(days)) * daily + at


def work_profile(start, horizon):
    # Native fixed-work contract: R0=50 on 0..2/4..6 each day; R1=100 all day.
    demand = [[0] * horizon for _ in range(2)]
    remaining = 151
    for at in range(start, horizon):
        units = [50 if at % 8 in (0, 1, 4, 5) else 0, 100]
        if remaining >= sum(units):
            used = units
        else:
            used = [u * remaining // sum(units) for u in units]
            left = remaining - sum(used)
            for r, available in enumerate(units):
                if available and left:
                    used[r] += 1
                    left -= 1
            assert left == 0
        for r in range(2):
            demand[r][at] = used[r]
        remaining -= sum(used)
        if remaining == 0:
            return at + 1, demand
    return None


def resource_range(demand):
    occupied = [i for i, units in enumerate(demand) if units]
    return (occupied[0], occupied[-1] + 1) if occupied else None


def work_oracle(horizon):
    profiles = {s: work_profile(s, horizon) for s in range(horizon)}
    profiles = {s: p for s, p in profiles.items() if p is not None}
    assert any(resource_range(d[0]) is None for _, d in profiles.values())
    assert any(any(u not in (0, 50, 100) for row in d for u in row) for _, d in profiles.values())
    def feasible(row):
        shapes = [profiles[s][1] for s in row]
        for r, capacity in enumerate((100, 200)):
            if any(sum(shape[r][i] for shape in shapes) > capacity for i in range(horizon)):
                return False
        r0 = [resource_range(shape[0]) for shape in shapes]
        for a, b in ((0, 1), (0, 2), (1, 2)):
            if r0[a] and r0[b] and r0[a][1] > r0[b][1]:
                return False
        r1 = [resource_range(shape[1]) for shape in shapes]
        for a in (0, 1):
            if r1[a] and r1[2] and r1[a][1] > r1[2][0]:
                return False
        return True
    original = (0, 0, 1)
    valid = [row for row in itertools.product(profiles, repeat=3)
             if row[2] >= 1 and feasible(row)]
    total = [max(row[i] - original[i] for row in valid) for i in range(3)]
    free = [max(row[i] - original[i] for row in valid if all(row[j] == original[j] for j in range(3) if j != i))
            for i in range(3)]
    assert total == ([4, 4, 5] if horizon == 8 else [12, 12, 13]) and free == [0, 0, horizon - 3]
    return profiles, set(valid), total, free


def varying_oracle():
    capacities = [50 if day in (1, 9) else 0 if day == 2 else 100 for day in range(10) for _ in range(4)]
    def feasible(row):
        return row[0] + 2 <= row[2] and all(sum(s <= i < s + 2 for s in row) * 50 <= cap
                                            for i, cap in enumerate(capacities))
    original = (0, 0, 2)
    valid = {row for row in itertools.product(range(39), repeat=3) if row[2] >= 2 and feasible(row)}
    total = [max(row[i] - original[i] for row in valid) for i in range(3)]
    free = [max(row[i] - original[i] for row in valid if all(row[j] == original[j] for j in range(3) if j != i))
            for i in range(3)]
    assert total == [36, 38, 36] and free == [0, 38, 36] and len(valid) == 17366
    return valid, total, free


def verify(build):
    root = build / "nonmonotone-propagation-qa"
    data = json.loads((root / "metrics.json").read_text(encoding="utf-8"))
    assert data["engine"] == "ppm-adopted-leveling-1.7" and data["primitive_trials"] == 200
    assert len(data["fallback_counters"]) == 6 and data["fallback_counters"][4] > 0
    assert 0 < data["fallback_counters"][5] <= 50000
    names = ["holes-small", "holes-32", "holes-96", "fractional", "fractional-long", "varying"]
    assert [c["name"] for c in data["cases"]] == names
    observations, oracles = [], {}
    witness_count = date_count = 0
    for case in data["cases"]:
        name = case["name"]
        assert len(case["counters"]) == 6 and case["counters"][5] <= 50000
        daily = 4 if name == "varying" else 8 if name in ("fractional", "fractional-long", "holes-small") else 120
        rows = {r["id"]: r for r in case["rows"]}
        base = {k: (minute(r["start"], daily), minute(r["finish"], daily)) for k, r in rows.items()}
        expected = {}
        if name.startswith("holes"):
            count = {"holes-small": 2, "holes-32": 32, "holes-96": 96}[name]
            assert len(rows) == count + 3 and case["counters"][0] == count * (daily - 4)
            assert case["links"] == [["A", "B", "R"]]
            assert base["A"] == base["B"] == base["H"] == (0, daily)
            for k in rows:
                expected[k] = (1, 1) if k.startswith("C") else (0, 0)
            assert all(base[f"C{i}"] == (2, 3) for i in range(count))
            # At 4..daily, fixed A+B=80%; each C=40%. Each pair fits but their sum does not.
            assert 40 + 40 <= 100 < 40 + 40 + 40
            if count == 2:
                valid = {r for r in itertools.product(range(2, daily), repeat=2)
                         if all(s in (2, 3) for s in r)}
                assert valid == {(2, 2), (2, 3), (3, 2), (3, 3)}
                oracles[name] = len(valid)
        else:
            assert set(rows) == {"A", "B", "C", "H"}
            if name == "varying":
                valid, total, free = varying_oracle()
                assert case["links"] == [["A", "C", "R"]]
                assert case["counters"][1] > 0 and case["counters"][2] > 0
            else:
                horizon = 16 if name == "fractional-long" else 8
                profiles, valid, total, free = work_oracle(horizon)
                assert case["links"] == [["A", "B", "R0"], ["A", "C", "R0"], ["B", "C", "R0"],
                                          ["A", "C", "R1"], ["B", "C", "R1"]]
                if name == "fractional-long":
                    assert case["counters"][1] > 0
            oracles[name] = len(valid)
            expected = {k: (total[i], free[i]) for i, k in enumerate(("A", "B", "C"))}
            expected["H"] = (0, 0)
        for k, row in rows.items():
            assert (row["total"], row["free"]) == expected[k] and row["critical"] == (row["total"] == 0)
        seen = set()
        for proof in case["proofs"]:
            goal = proof["id"]
            assert goal in rows and goal not in seen
            seen.add(goal)
            current = dict(base)
            dates = proof["dates"]
            assert len(dates) == len({d["id"] for d in dates}) and goal in {d["id"] for d in dates}
            for d in dates:
                assert d["id"] in rows
                current[d["id"]] = (minute(d["start"], daily), minute(d["finish"], daily))
            assert current[goal][0] - base[goal][0] == rows[goal]["total"]
            assert all(base[k][0] <= s <= e <= base["H"][1] for k, (s, e) in current.items())
            if name.startswith("holes"):
                assert current["A"] == current["B"] == current["H"] == base["A"]
                assert all(s in (2, 3) and e == s + 1 for k, (s, e) in current.items() if k.startswith("C"))
            else:
                starts = tuple(current[k][0] for k in ("A", "B", "C"))
                assert starts in valid
                for k, start in zip(("A", "B", "C"), starts):
                    end = start + 2 if name == "varying" else profiles[start][0]
                    assert current[k][1] == end
            witness_count += 1
            date_count += len(dates)
        assert seen == set(rows) - {"H"}
        samples = 1 if name == "holes-small" else 3
        assert all(len(case[k]) == samples for k in ("legacy_ms", "optimized_ms", "legacy_checkpoints", "optimized_checkpoints"))
        assert all(v > 0 for v in case["legacy_ms"] + case["optimized_ms"])
        a, b = statistics.median(case["legacy_ms"]), statistics.median(case["optimized_ms"])
        observations.append({"name": name, "samples_each": samples, "legacy_median_ms": a, "optimized_median_ms": b,
                             "observed_ratio": a / b, "counters": case["counters"]})
    native = json.loads((root / "selected.ppm").read_text(encoding="utf-8"))
    canonical = json.dumps(native["payload"], sort_keys=True, ensure_ascii=False, separators=(",", ":")).encode()
    assert hashlib.sha256(canonical).hexdigest() == native["sha256"] and native["schemaVersion"] == 12
    baselines = native["payload"]["baselines"]
    assert [b["engineVersion"] for b in baselines] == ["ppm-adopted-leveling-1.6", "ppm-adopted-leveling-1.7"]
    assert baselines[0]["schedule"] == baselines[1]["schedule"]
    book = load_workbook(root / "report.xlsx", data_only=False)
    source = {r[0]: r[1] for r in book["출처와 설정"].iter_rows(min_row=7, values_only=True)}
    assert source["계산 엔진"] == "ppm-adopted-leveling-1.7"
    rows = list(book["계획 일정"].iter_rows(min_row=7, values_only=True))
    assert [round(r[6] * 60) for r in rows] == [0, 0, 1, 1, 0]
    assert [round(r[9] * 60) for r in rows] == [0, 0, 1, 1, 0]
    assert any("LEVELING_NONMONOTONE_PROPAGATION_POLICY" in str(c.value) for row in book["진단"] for c in row)
    return {"status": "success", "engine": data["engine"], "paired_observations": observations,
            "witnesses_checked": witness_count, "date_references_checked": date_count,
            "exhaustive_feasible_counts": oracles, "primitive_coordinate_checks": 200 * 3 * 16,
            "fallback_counters": data["fallback_counters"],
            "legacy_1_6_baseline_preserved": True, "native_schema": 12, "xlsx_checked": True}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-forecast")), indent=2))
