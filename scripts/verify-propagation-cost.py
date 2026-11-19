"""Independent primitive-grid oracles reused across engine versions; cost decisions are not feasibility proofs."""
import hashlib
import importlib.util
import itertools
import json
import statistics
import sys
from pathlib import Path

from openpyxl import load_workbook


def module(name):
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), Path(__file__).with_name(name + ".py"))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def verify(build):
    primitive = module("verify-nonmonotone-propagation")
    regular = module("verify-candidate-propagation").minute
    split_oracle = module("verify-repartition-float").oracle()
    root = build / "propagation-cost-qa"
    data = json.loads((root / "metrics.json").read_text(encoding="utf-8"))
    assert data["engine"] == "ppm-adopted-leveling-1.8"
    names = ["holes-small", "fractional", "fractional-long", "varying", "holes-96", "late-tiny", "adaptive-observed", "split"]
    assert [c["name"] for c in data["cases"]] == names
    observations, counts = [], {}
    proofs = references = 0
    for case in data["cases"]:
        name = case["name"]
        cost, pruning = case["counters"], case["pruning"]
        assert len(cost) == 7 and len(pruning) == 6 and 0 <= pruning[5] <= 50000
        if name in names[:4] or name == "late-tiny":
            assert cost[0] > 0 and cost[3] == pruning[5] == 0
        if name == "holes-96":
            assert pruning[0] == 11136
        if name == "adaptive-observed":
            assert cost[3] > 0 and cost[4] > 0
        daily = {"holes-small": 8, "holes-96": 120, "late-tiny": 12, "adaptive-observed": 12, "fractional": 8,
                 "fractional-long": 8, "varying": 4}.get(name)
        minute = regular if name == "split" else lambda stamp: primitive.minute(stamp, daily)
        rows = {r["id"]: r for r in case["rows"]}
        assert len(rows) == len(case["rows"])
        base = {k: (minute(r["start"]), minute(r["finish"])) for k, r in rows.items()}
        if name in ("holes-small", "holes-96", "late-tiny", "adaptive-observed"):
            count = {"holes-small": 2, "holes-96": 96, "late-tiny": 6, "adaptive-observed": 15}[name]
            assert len(rows) == count + 3
            expected = {k: (1, 1) if k.startswith("C") else (0, 0) for k in rows}
            expected_proofs = set(rows) - {"H"}
            if count < 20:
                feasible = list(itertools.product((2, 3), repeat=count))
                assert len(feasible) == 2 ** count
                counts[name] = len(feasible)
        elif name == "split":
            expected = {"A": (660, 660), "B": (540, 60), "C": (0, 0)}
            expected_proofs = {"A", "B"}
            counts[name] = split_oracle["feasible_pairs"]
        else:
            if name == "varying":
                valid, total, free = primitive.varying_oracle()
            else:
                profiles, valid, total, free = primitive.work_oracle(16 if name == "fractional-long" else 8)
            counts[name] = len(valid)
            expected = {k: (total[i], free[i]) for i, k in enumerate(("A", "B", "C"))}
            expected["H"] = (0, 0)
            expected_proofs = {"A", "B", "C"}
        for k, row in rows.items():
            assert (row["total"], row["free"]) == expected[k] and row["critical"] == (row["total"] == 0)
        seen = set()
        for proof in case["proofs"]:
            goal = proof["id"]
            assert goal in expected_proofs and goal not in seen
            seen.add(goal)
            current = dict(base)
            dates = proof["dates"]
            assert len(dates) == len({d["id"] for d in dates}) and goal in {d["id"] for d in dates}
            for d in dates:
                assert d["id"] in current
                current[d["id"]] = (minute(d["start"]), minute(d["finish"]))
            delta = current[goal][0] - base[goal][0]
            if name == "split" and goal == "A" and current[goal][0] >= 480:
                delta -= 300
            assert delta == rows[goal]["total"]
            end = 1440 if name == "split" else base["H"][1]
            assert all(base[k][0] <= s <= e <= end for k, (s, e) in current.items())
            if name in ("holes-small", "holes-96", "late-tiny", "adaptive-observed"):
                assert current["A"] == current["B"] == current["H"] == (0, daily)
                assert all(s in (2, 3) and e == s + 1 for k, (s, e) in current.items() if k.startswith("C"))
            elif name == "split":
                aa = list(range(180)) + list(range(480, 1440))
                start = current["A"][0]
                assert start in aa
                occupied = aa[aa.index(start):aa.index(start) + 480]
                assert len(occupied) == 480 and current["A"][1] == occupied[-1] + 1
                bs, be = current["B"]
                assert be == bs + 240 and be <= current["A"][1]
                assert not set(occupied).intersection(range(bs, be)) and current["C"] == (0, 1440)
            else:
                starts = tuple(current[k][0] for k in ("A", "B", "C"))
                assert starts in valid
                for k, start in zip(("A", "B", "C"), starts):
                    assert current[k][1] == (start + 2 if name == "varying" else profiles[start][0])
            proofs += 1
            references += len(dates)
        assert seen == expected_proofs
        n = 5 if name in names[:4] or name == "late-tiny" else 3
        assert all(len(case[k]) == n for k in ("legacy_ms", "optimized_ms", "legacy_checkpoints", "optimized_checkpoints"))
        assert all(t > 0 for t in case["legacy_ms"] + case["optimized_ms"])
        a, b = statistics.median(case["legacy_ms"]), statistics.median(case["optimized_ms"])
        observations.append({"name": name, "samples_each": n, "legacy_median_ms": a, "optimized_median_ms": b,
                             "observed_ratio": a / b, "counters": cost, "pruning": pruning})
    native = json.loads((root / "selected.ppm").read_text(encoding="utf-8"))
    canonical = json.dumps(native["payload"], sort_keys=True, ensure_ascii=False, separators=(",", ":")).encode()
    assert hashlib.sha256(canonical).hexdigest() == native["sha256"] and native["schemaVersion"] == 12
    baselines = native["payload"]["baselines"]
    assert [b["engineVersion"] for b in baselines] == ["ppm-adopted-leveling-1.7", "ppm-adopted-leveling-1.8"]
    assert baselines[0]["schedule"] == baselines[1]["schedule"]
    book = load_workbook(root / "report.xlsx", data_only=False)
    source = {r[0]: r[1] for r in book["출처와 설정"].iter_rows(min_row=7, values_only=True)}
    assert source["계산 엔진"] == "ppm-adopted-leveling-1.8"
    rows = list(book["계획 일정"].iter_rows(min_row=7, values_only=True))
    assert [round(r[6] * 60) for r in rows] == [0, 0, 1, 1, 0]
    assert [round(r[9] * 60) for r in rows] == [0, 0, 1, 1, 0]
    assert any("LEVELING_PROPAGATION_COST_POLICY" in str(c.value) for row in book["진단"] for c in row)
    return {"status": "success", "engine": data["engine"], "paired_observations": observations,
            "witnesses_checked": proofs, "date_references_checked": references, "exhaustive_feasible_counts": counts,
            "legacy_1_7_baseline_preserved": True, "native_schema": 12, "xlsx_checked": True}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-forecast")), indent=2))
