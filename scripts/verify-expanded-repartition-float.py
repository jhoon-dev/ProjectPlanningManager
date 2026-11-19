"""Independent unit-chain/finish-window bounds, full native witnesses, archives and reports."""
import hashlib
import itertools
import json
import re
import sys
import zipfile
from datetime import datetime, timedelta
from pathlib import Path

from openpyxl import load_workbook
from pypdf import PdfReader


def minute(stamp):
    date = datetime.strptime(stamp, "%Y-%m-%d %H:%M")
    anchor = datetime(2026, 11, 2)
    days = (date.date() - anchor.date()).days
    assert days >= 0 and date.weekday() < 5
    workdays = sum((anchor + timedelta(days=d)).weekday() < 5 for d in range(days))
    at = date.hour * 60 + date.minute
    assert 540 <= at <= 720 or 780 <= at <= 1080
    return workdays * 480 + at - 540 - (60 if at >= 780 else 0)


def verify(build):
    root = build / "expanded-repartition-float-qa"
    data = json.loads((root / "metrics.json").read_text(encoding="utf-8"))
    assert data["engine"] == "ppm-adopted-leveling-1.5"
    # Primitive exhaustive oracle for four unit tasks: a dense resource chain and
    # independent three-minute windows on the same 100% resource.
    for separate in (False, True):
        valid = []
        for delays in itertools.product(range(3), repeat=4):
            starts = [i * (3 if separate else 1) + delays[i] for i in range(4)]
            if all(starts[i] + 1 <= starts[i + 1] for i in range(3)):
                valid.append(delays)
        assert len(valid) == (81 if separate else 15)
        assert [max(d[i] for d in valid) for i in range(4)] == [2] * 4
    configurations = [(129, False), (512, False), (1000, True)]
    assert len(data["cases"]) == len(configurations)
    cases, references, witnesses = [], 0, 0
    for case, (count, separate) in zip(data["cases"], configurations):
        assert (case["movable"], case["separate"]) == (count, separate)
        step = 3 if separate else 1
        rows = {r["id"]: r for r in case["rows"]}
        assert len(rows) == len(case["rows"]) == count * (2 if separate else 1) + 1
        base = {name: (minute(r["start"]), minute(r["finish"]), r["start"], r["finish"]) for name, r in rows.items()}
        end = (count - 1) * step + 3
        for name, row in rows.items():
            moving = name.startswith("T")
            total = 2 if moving else 0
            free = 2 if moving and (separate or name == f"T{count - 1}") else 0
            assert (row["total"], row["free"], row["critical"]) == (total, free, not moving)
            if moving:
                index = int(name[1:])
                assert base[name][:2] == (index * step, index * step + 1)
            elif name == "H":
                assert base[name][:2] == (0, end)
            else:
                assert separate and base[name][:2] == (int(name[1:]) * 3 + 3,) * 2
        records = case["date_records"]
        decoded = [(r["id"], (minute(r["start"]), minute(r["finish"]), r["start"], r["finish"])) for r in records]
        assert all(name in rows for name, _ in decoded)
        proofs = case["proofs"]
        assert len(proofs) == count * (2 if separate else 1)
        assert {p["id"] for p in proofs} == set(rows) - {"H"}
        for proof in proofs:
            current = dict(base)
            selected = [decoded[i] for i in proof["dates"]]
            assert len(selected) == len({name for name, _ in selected}) and proof["id"] in {name for name, _ in selected}
            for name, dates in selected:
                current[name] = dates
            tasks = [current[f"T{i}"] for i in range(count)]
            # Each task occupies exactly one working minute; distinct starts certify
            # the full 100% capacity sum, including all omitted/current tasks.
            assert len({t[0] for t in tasks}) == count
            assert all(i * step <= t[0] <= i * step + 2 and t[1] == t[0] + 1 for i, t in enumerate(tasks))
            assert all(tasks[i][3] <= tasks[i + 1][2] for i in range(count - 1))
            assert all(t[3] <= base["H"][3] for t in tasks)
            if separate:
                assert all(current[f"M{i}"] == base[f"M{i}"] and tasks[i][3] <= base[f"M{i}"][2] for i in range(count))
            assert current[proof["id"]][0] - base[proof["id"]][0] == rows[proof["id"]]["total"]
            witnesses += 1
            references += len(selected)
        cases.append({"movable": count, "search_units": count if separate else 1,
                      "largest_unit": 1 if separate else count, "witnesses": len(proofs), "analysis_ms": case["analysis_ms"]})
    native = json.loads((root / "selected.ppm").read_text(encoding="utf-8"))
    canonical = json.dumps(native["payload"], sort_keys=True, ensure_ascii=False, separators=(",", ":")).encode()
    assert native["sha256"] == hashlib.sha256(canonical).hexdigest() and native["schemaVersion"] == 12
    baselines = native["payload"]["baselines"]
    assert [b["engineVersion"] for b in baselines] == ["ppm-adopted-leveling-1.4", "ppm-adopted-leveling-1.5"]
    assert all(r["totalFloatMinutes"] == 0 and not r["critical"] for r in baselines[0]["schedule"])
    assert [r["totalFloatMinutes"] for r in baselines[1]["schedule"]] == [2] * 129 + [0]
    assert "levelingWitnesses" not in json.dumps(native)
    with zipfile.ZipFile(root / "report.xlsx") as archive:
        assert archive.testzip() is None
    book = load_workbook(root / "report.xlsx", data_only=False)
    assert all(len(c.value) < 32767 for sheet in book for row in sheet for c in row if isinstance(c.value, str))
    plan = list(book["계획 일정"].iter_rows(min_row=7, values_only=True))
    assert [r[0] for r in plan] == [f"T{i}" for i in range(129)] + ["H"]
    assert [round(r[6] * 60) for r in plan] == [2] * 129 + [0]
    assert [round(r[9] * 60) for r in plan] == [0] * 128 + [2, 0]
    provenance = {r[0]: r[1] for r in book["출처와 설정"].iter_rows(min_row=7, values_only=True)}
    assert provenance["계산 엔진"] == "ppm-adopted-leveling-1.5"
    diagnostics = "\n".join(str(c.value) for row in book["진단"] for c in row if c.value is not None)
    assert "LEVELING_REPARTITION_COMPONENT_POLICY" in diagnostics and "LEVELING_REPARTITION_WITNESS_PREVIEW" in diagnostics
    assert "109개 날짜 생략" in diagnostics and "총여유 근거 창" in diagnostics
    pdf = PdfReader(root / "report.pdf", strict=True)
    text = re.sub(r"\s+", "", "\n".join(page.extract_text() or "" for page in pdf.pages))
    assert "ppm-adopted-leveling-1.5" in text and "109개날짜생략" in text
    assert "LEVELING_FLOAT_UNAVAILABLE" not in text
    assert (root / "witness-129.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
    return {"status": "success", "engine": data["engine"], "small_exhaustive_oracle": True,
            "cases": cases, "witnesses_checked": witnesses, "date_references_checked": references,
            "legacy_1_4_preserved": True, "native_schema": 12, "xlsx_preview_explicit": True, "pdf_pages": len(pdf.pages)}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-forecast")), indent=2))
