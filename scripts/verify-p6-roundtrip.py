"""Compare a returned P6 artifact. This tool never executes P6 or certifies its provenance."""
import argparse
import copy
import hashlib
import json
from decimal import Decimal, InvalidOperation
from pathlib import Path


def read_xer(path, encoding):
    data = path.read_bytes()
    if len(data) > 16 * 1024 * 1024:
        raise ValueError("XER exceeds 16 MiB")
    tables = {}
    current = None
    fields = None
    ended = False
    cells_seen = 0
    for index, line in enumerate(data.decode(encoding, errors="strict").splitlines()):
        if not line:
            continue
        cells = line.split("\t")
        cells_seen += len(cells)
        if len(cells) > 257 or cells_seen > 1000000 or index > 300000:
            raise ValueError("XER structure limit exceeded")
        if index == 0:
            if cells[0] != "ERMHDR":
                raise ValueError("Missing XER header")
            continue
        if ended:
            raise ValueError("Content after end marker")
        if cells[0] == "%T":
            if len(cells) != 2 or cells[1] in tables or len(tables) >= 128:
                raise ValueError("Invalid/duplicate table")
            current = tables[cells[1]] = []
            fields = None
        elif cells[0] == "%F":
            if current is None or fields is not None or len(set(cells[1:])) != len(cells)-1:
                raise ValueError("Invalid field list")
            fields = cells[1:]
        elif cells[0] == "%R":
            if not fields or len(cells)-1 > len(fields):
                raise ValueError("Invalid row width")
            current.append(dict(zip(fields, cells[1:]+[""]*(len(fields)-len(cells)+1))))
        elif cells == ["%E"]:
            ended = True
        else:
            raise ValueError("Unknown record")
    if not ended:
        raise ValueError("Missing end marker")
    return data, tables


def compare(expected, tasks):
    by_code = {}
    for task in tasks:
        code = task.get("task_code", "")
        if not code or code in by_code:
            raise ValueError("Missing/duplicate selected Activity Code")
        by_code[code] = task
    results = []
    incomplete = False
    different = False
    for wanted in expected:
        code = wanted["activityCode"]
        actual = by_code.pop(code, {})
        fields = {}
        for target, source in [("start", "early_start_date"), ("finish", "early_end_date"),
                               ("totalFloatMinutes", "total_float_hr_cnt"),
                               ("freeFloatMinutes", "free_float_hr_cnt")]:
            raw = actual.get(source, "")
            value = raw or None
            if raw and target.endswith("Minutes"):
                try:
                    value = Decimal(raw)*60
                    if not value.is_finite():
                        raise ValueError("Non-finite float")
                except InvalidOperation as error:
                    raise ValueError("Invalid numeric float") from error
            status = "unknown" if value is None else "equal" if value == wanted[target] else "different"
            incomplete |= status == "unknown"
            different |= status == "different"
            fields[target] = {"expected": wanted[target], "returned": str(value) if isinstance(value, Decimal) else value, "status": status}
        results.append({"taskId": wanted["taskId"], "activityCode": code, "fields": fields})
    if by_code:
        different = True
    return {"comparison": "incomplete" if incomplete else "different" if different else "equal",
            "tasks": results, "unexpectedActivityCodes": sorted(by_code),
            "P6ExecutionEvidence": "unverified; artifact comparison does not prove an application run"}


def self_test(kit, mpxj_reopen=False):
    expected = json.loads((kit/"expected-plan.json").read_text(encoding="utf-8"))
    assert expected["inputSha256"] == hashlib.sha256((kit/"plan-input.xer").read_bytes()).hexdigest()
    rows = [{"task_code": e["activityCode"], "early_start_date": e["start"], "early_end_date": e["finish"],
             "total_float_hr_cnt": str(Decimal(e["totalFloatMinutes"])/60),
             "free_float_hr_cnt": str(Decimal(e["freeFloatMinutes"])/60)} for e in expected["expected"]]
    assert compare(expected["expected"], rows)["comparison"] == "equal"
    changed = copy.deepcopy(rows)
    changed[0]["early_end_date"] = "2099-01-01 09:00"
    assert compare(expected["expected"], changed)["comparison"] == "different"
    unknown = copy.deepcopy(rows)
    unknown[0]["total_float_hr_cnt"] = ""
    result = compare(expected["expected"], unknown)
    assert result["comparison"] == "incomplete" and result["tasks"][0]["fields"]["totalFloatMinutes"]["returned"] is None
    for malformed in [rows+[rows[0]], [dict(r, total_float_hr_cnt="NaN") for r in rows]]:
        try:
            compare(expected["expected"], malformed)
        except ValueError:
            pass
        else:
            raise AssertionError("Malformed returned rows accepted")
    native = json.loads((kit/"expected-native-comparison.json").read_text(encoding="utf-8"))
    tasks = {r["taskId"]: r for r in native["tasks"]}
    assert tasks["B"]["overridden"]["finish"] == "2026-11-03 14:00"
    assert tasks["C"]["finishDeltaMinutes"] == -480
    assert tasks["E"]["overridden"]["critical"] is None
    assert native["adopted"] is False and native["P6RoundTrip"] == "not-run"
    _, original_tables = read_xer(kit/"plan-input.xer", "utf-8-sig")
    assert compare(expected["expected"], original_tables["TASK"])["comparison"] == "incomplete"
    independent = "not-run"
    if mpxj_reopen:
        import importlib.metadata
        assert importlib.metadata.version("mpxj") == "16.10.0"
        assert importlib.metadata.version("JPype1") == "1.6.0"
        import jpype
        import mpxj
        jpype.startJVM()
        try:
            from org.mpxj.reader import UniversalProjectReader
            from org.mpxj import TimeUnit
            proxy = UniversalProjectReader().getProjectReaderProxy(str((kit/"plan-input.xer").resolve()))
            try:
                proxy.getProjectReader().setIgnoreErrors(False)
                project = proxy.read()
                assert not list(project.getIgnoredErrors())
                leaves = [t for t in project.getTasks() if not t.getSummary()]
                assert [str(t.getName()) for t in leaves] == [t["task_name"] for t in original_tables["TASK"]]
                assert [round(float(t.getDuration().convertUnits(TimeUnit.MINUTES, project.getProjectProperties()).getDuration())) for t in leaves] == [960,480,480,480,480]
                assert sum(len(list(t.getPredecessors())) for t in leaves) == 2
                assert len(list(project.getResourceAssignments())) == 0
            finally:
                proxy.close()
        finally:
            jpype.shutdownJVM()
        independent = "MPXJ 16.10.0 strict; plan subset only"
    return {"status": "success", "comparatorSelfTests": 6, "independentPlanRead": independent, "nativeSmallCaseVerified": True,
            "P6ActualRoundTrip": "not-run", "reason": "P6 not installed; user confirmed",
            "syntheticSelfTestIsP6Evidence": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kit", type=Path, default=Path("build-forecast/progress-policy-qa/p6-kit"))
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--self-test", action="store_true")
    mode.add_argument("--returned", type=Path)
    parser.add_argument("--project-id")
    parser.add_argument("--mpxj-reopen", action="store_true", help="Use the pinned independent reader in self-test mode only")
    parser.add_argument("--encoding", choices=["utf-8-sig", "cp1252"], default="utf-8-sig")
    args = parser.parse_args()
    if args.self_test:
        result = self_test(args.kit, args.mpxj_reopen)
    else:
        expected = json.loads((args.kit/"expected-plan.json").read_text(encoding="utf-8"))
        assert expected["inputSha256"] == hashlib.sha256((args.kit/"plan-input.xer").read_bytes()).hexdigest()
        data, tables = read_xer(args.returned, args.encoding)
        projects = {p["proj_id"] for p in tables.get("PROJECT", [])}
        selected = args.project_id or (next(iter(projects)) if len(projects) == 1 else None)
        if selected not in projects:
            raise ValueError("Select exactly one returned project with --project-id")
        result = compare(expected["expected"], [t for t in tables.get("TASK", []) if t.get("proj_id") == selected])
        result.update({"returnedSha256": hashlib.sha256(data).hexdigest(), "selectedProjectId": selected,
                       "inputSha256": expected["inputSha256"], "P6ActualRoundTrip": "unverified"})
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
