"""Independent unit-chain bounds and reconstruction of every native feasible witness."""
import itertools
import json
import re
import sys
from datetime import datetime, timedelta
from pathlib import Path


def calendar_minute(stamp):
    date = datetime.strptime(stamp, "%Y-%m-%d %H:%M")
    anchor = datetime(2026, 11, 2)
    days = (date.date() - anchor.date()).days
    assert days >= 0 and date.weekday() < 5
    working_days = sum((anchor + timedelta(days=day)).weekday() < 5 for day in range(days))
    minute = date.hour * 60 + date.minute
    assert 540 <= minute <= 720 or 780 <= minute <= 1080
    return working_days * 480 + minute - 540 - (60 if minute >= 780 else 0)


def verify(build):
    data = json.loads((build / "scaled-repartition-float-qa/metrics.json").read_text(encoding="utf-8"))
    assert data["engine"] == "ppm-adopted-leveling-1.4"
    # Primitive exhaustive check of the bound argument on six unit tasks with two spare minutes.
    for fixed in (0, 3, 6):
        feasible = [delays for delays in itertools.product(range(3), repeat=6)
                    if all(delays[i] <= delays[i + 1] for i in range(5))
                    and all(delays[i] == 0 for i in range(fixed))]
        assert [max(d[i] for d in feasible) for i in range(6)] == [0 if i < fixed else 2 for i in range(6)]
    summaries = []
    configurations = [(65, 0, 0, True), (128, 2, 0, True), (65, 2, 0, False),
                      (256, 2, 192, True), (1000, 0, 0, True)]
    assert len(data["cases"]) == len(configurations)
    for case, (count, slack, fixed, logical) in zip(data["cases"], configurations):
        assert (case["tasks"], case["slack"], case["fixed_prefix"], case["logical_chain"]) == (count, slack, fixed, logical)
        rows = case["rows"]
        assert len(rows) == count + 1 and rows[-1]["id"] == "H"
        assert len({r["id"] for r in rows}) == len(rows)
        starts = [calendar_minute(r["start"]) for r in rows]
        finishes = [calendar_minute(r["finish"]) for r in rows]
        assert starts == list(range(count)) + [0] and finishes == list(range(1, count + 1)) + [count + slack]
        for i, row in enumerate(rows):
            # Unit-resource FIFO gives s[i+1] >= s[i]+1 and s[last]+1 <= count+slack.
            # Hence s[i] <= i+slack, attained by shifting the suffix together.
            total = 0 if i == count or i < fixed else slack
            free = slack if i == count - 1 and i >= fixed else 0
            assert (row["total"], row["free"], row["critical"]) == (total, free, total == 0)
        assert len(case["witnesses"]) == count
        assert [w["task"] for w in case["witnesses"]] == [r["id"] for r in rows[:-1]]
        for witness in case["witnesses"]:
            actual = {r["id"]: (s, f) for r, s, f in zip(rows, starts, finishes)}
            matches = re.findall(r"(T\d+)=(\d{4}-\d\d-\d\d \d\d:\d\d)~(\d{4}-\d\d-\d\d \d\d:\d\d); ", witness["dates"])
            assert matches and len(matches) == len({m[0] for m in matches})
            assert "".join(f"{task}={start}~{finish}; " for task, start, finish in matches) == witness["dates"]
            assert witness["task"] in {m[0] for m in matches}
            for task, start, finish in matches:
                assert task in actual
                actual[task] = calendar_minute(start), calendar_minute(finish)
            ordered = [actual[r["id"]] for r in rows[:-1]]
            assert all(finish == start + 1 and start >= i and finish <= count + slack
                       for i, (start, finish) in enumerate(ordered))
            assert all(ordered[i][1] <= ordered[i + 1][0] for i in range(count - 1))
            assert all(ordered[i] == (i, i + 1) for i in range(fixed))
            index = int(witness["task"][1:])
            assert actual[witness["task"]][0] - index == witness["total"] == rows[index]["total"]
        summaries.append({"tasks": count, "fixed": count if not slack else fixed,
                          "movable": 0 if not slack else count - fixed, "logical_chain": logical,
                          "witnesses_checked": count, "analysis_ms": case["analysis_ms"]})
    return {"status": "success", "engine": data["engine"], "small_exhaustive_oracle": True,
            "cases": summaries, "witnesses_checked": sum(c[0] for c in configurations)}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-forecast")), indent=2))
