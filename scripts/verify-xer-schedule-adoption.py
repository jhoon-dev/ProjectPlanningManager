"""Independent ID/structure/working-minute oracle for XER schedule adoption evidence."""
import argparse
import bisect
from datetime import datetime, timedelta
import hashlib
import json
from pathlib import Path


def verify(root):
    def read(name):
        return json.loads((root / name).read_text(encoding="utf-8-sig"))

    before = read("before.json")
    envelope = read("adopted.ppm")
    after = envelope["payload"]
    assert envelope["schemaVersion"] == 32
    canonical = json.dumps(after, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
    assert hashlib.sha256(canonical).hexdigest() == envelope["sha256"]
    event = after["xerScheduleAdoptions"][-1]
    source = event["source"]["input"]
    assert source == read("source-plan.json")
    assert event["source"]["sourceSha256"] == hashlib.sha256((root / "source.xer").read_bytes()).hexdigest()
    assert all(event[k] for k in ("confirmed", "schedule", "wbs", "calendars"))
    changed = {"tasks", "startDate", "calendar", "holidays", "calculationProfile", "wbsNodes", "calendars",
               "planLeveling", "remainingLeveling", "levelingDecisions", "revision", "xerScheduleAdoptions"}
    checks = 0
    for key in before:
        if key not in changed:
            assert before[key] == after[key], key
            checks += 1
    assert after["startDate"] == source["startDate"] and after["holidays"] == source["holidays"]
    tasks = {t["id"]: t for t in after["tasks"]}
    task_map = {l["sourceId"]: ("xsa-" + event["id"] + "-task-" + l["sourceId"] if l["create"] else l["targetId"])
                for l in event["links"]}
    selected = set(task_map.values()) - {""}
    for old in before["tasks"]:
        if old["id"] not in selected:
            assert old == tasks[old["id"]]
            checks += 1
    def identifier(kind, value):
        return "xsa-" + event["id"] + "-" + kind + "-" + value if value else ""
    for kind, field in [("calendar", "calendars"), ("wbs", "wbsNodes")]:
        actual = {v["id"]: v for v in after[field]}
        for old in before[field]:
            assert actual[old["id"]] == old
            checks += 1
        for incoming in source[field]:
            expected = dict(incoming)
            expected["id"] = identifier(kind, incoming["id"])
            if kind == "wbs":
                expected["parentId"] = identifier(kind, incoming["parentId"])
            assert actual[expected["id"]] == expected
            checks += 1
    for incoming in source["tasks"]:
        target = task_map[incoming["id"]]
        if not target:
            continue
        t = tasks[target]
        for field in ("name", "durationMinutes", "constraintType", "constraintAt", "deadlineAt", "taskType",
                      "workUnitMinutes", "workDriven", "breaks"):
            assert t.get(field) == incoming.get(field), (field, t.get(field), incoming.get(field))
            checks += 1
        assert t["predecessors"] == [task_map[x] for x in incoming["predecessors"]]
        edges = [dict(e, predecessorId=task_map[e["predecessorId"]]) for e in incoming["dependencyRules"]]
        assert t["dependencyRules"] == edges
        assert t["wbsId"] == identifier("wbs", incoming["wbsId"])
        assert t["calendarId"] == identifier("calendar", incoming["calendarId"])
        checks += 4

    # Enumerate civil working minutes independently of Qt/native calendar code.
    start = datetime.fromisoformat(after["startDate"])
    calendars = {c["id"]: c for c in after["calendars"]}
    default = {"weekdays": [1, 2, 3, 4, 5], "periods": [{"begin": 540, "end": 720}, {"begin": 780, "end": 1080}],
               "holidays": after["holidays"]}
    resources = {r["id"]: r for r in after["resources"]}
    assignments = {}
    for a in after["assignments"]:
        assignments.setdefault(a["taskId"], []).append(a)
    def available(calendar, stamp):
        minute = stamp.hour * 60 + stamp.minute
        return (stamp.isoweekday() in calendar["weekdays"] and stamp.date().isoformat() not in calendar["holidays"]
                and any(p["begin"] <= minute < p["end"] for p in calendar["periods"]))
    clocks = {}
    for task_id, task in tasks.items():
        assert task.get("taskType", "fixed-duration") == "fixed-duration"
        assert task.get("constraintType", "ASAP") == "ASAP", task
        calendar = calendars.get(task.get("calendarId"), default)
        resource_calendars = [calendars.get(resources[a["resourceId"]].get("calendarId"), default)
                              for a in assignments.get(task_id, []) if a["units"] > 0]
        clocks[task_id] = [start + timedelta(minutes=n) for n in range(35 * 1440)
                           if available(calendar, start + timedelta(minutes=n)) and
                           all(available(c, start + timedelta(minutes=n)) for c in resource_calendars)]
    dates = {}
    def solve(task_id):
        if task_id in dates:
            return dates[task_id]
        t, minutes = tasks[task_id], clocks[task_id]
        offset, duration = 0, t["durationMinutes"]
        rules = {e["predecessorId"]: e for e in t["dependencyRules"]}
        for pred in t["predecessors"]:
            ps, pf = solve(pred)
            rule = rules.get(pred, {"type": "FS", "lagMinutes": 0})
            anchor = pf if rule["type"][0] == "F" else ps
            bound = bisect.bisect_left(minutes, anchor) + rule["lagMinutes"]
            if rule["type"][1] == "F":
                bound -= duration
            offset = max(offset, bound)
        first = minutes[offset]
        finish = minutes[offset + duration - 1] + timedelta(minutes=1) if duration else first
        dates[task_id] = first, finish
        return dates[task_id]
    observed = {t["id"]: t for t in read("plan-dates.json")}
    for task_id in tasks:
        first, finish = solve(task_id)
        assert observed[task_id]["start"] == first.strftime("%Y-%m-%d %H:%M"), (task_id, observed[task_id], first)
        assert observed[task_id]["finish"] == finish.strftime("%Y-%m-%d %H:%M"), (task_id, observed[task_id], finish)
        checks += 2
    return {"status": "success", "checks": checks, "tasks": len(tasks), "independent_working_minute_dates": len(dates) * 2,
            "schema": 32, "source_sha256": event["source"]["sourceSha256"],
            "native_facts_unchanged": True, "adopted_sha256": hashlib.sha256((root / "adopted.ppm").read_bytes()).hexdigest()}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("evidence", type=Path)
    parser.add_argument("--mpxj", action="store_true")
    args = parser.parse_args()
    result = verify(args.evidence)
    if args.mpxj:
        import importlib.metadata
        import jpype
        import mpxj  # noqa: F401; registers bundled Java classpath.
        assert importlib.metadata.version("mpxj") == "16.10.0"
        assert importlib.metadata.version("JPype1") == "1.6.0"
        jpype.startJVM()
        try:
            from org.mpxj.reader import UniversalProjectReader
            from org.mpxj import TimeUnit
            from java.time import DayOfWeek
            model_path = str((args.evidence / "export.xer").resolve())
            proxy = UniversalProjectReader().getProjectReaderProxy(model_path)
            try:
                proxy.getProjectReader().setIgnoreErrors(False)
                model = proxy.read()
                assert not list(model.getIgnoredErrors())
                after = json.loads((args.evidence / "adopted.ppm").read_text(encoding="utf-8"))["payload"]
                leaves = [t for t in model.getTasks() if not t.getSummary()]
                actual = {str(t.getName()): round(float(t.getPlannedDuration().convertUnits(TimeUnit.MINUTES, model.getProjectProperties()).getDuration())) for t in leaves}
                assert actual == {t["name"]: t["durationMinutes"] for t in after["tasks"]}
                summaries = {str(t.getName()): t for t in model.getTasks() if t.getSummary()}
                assert {w["name"] for w in after["wbsNodes"]} <= summaries.keys()
                wbs = {w["id"]: w for w in after["wbsNodes"]}
                for w in wbs.values():
                    if w["parentId"]:
                        assert str(summaries[w["name"]].getParentTask().getName()) == wbs[w["parentId"]]["name"]
                def hours(c):
                    found = []
                    for day in range(1, 8):
                        periods = c.getCalendarHours(DayOfWeek.of(day))
                        spans = []
                        if periods is not None:
                            for period in periods:
                                start = period.getStart().toSecondOfDay() // 60
                                spans.append((start, start + period.getDurationAsMilliseconds() // 60000))
                        found.append(tuple(spans))
                    return tuple(found)
                calendars = list(model.getCalendars())
                def holidays(c):
                    days = []
                    for e in c.getCalendarExceptions():
                        first, last = str(e.getFromDate())[:10], str(e.getToDate())[:10]
                        assert first == last
                        days.append(first)
                    return tuple(sorted(days))
                observed = {(str(c.getName()), hours(c), holidays(c)) for c in calendars}
                for c in after["calendars"]:
                    expected = tuple(tuple((p["begin"], p["end"]) for p in c["periods"]) if d in c["weekdays"] else () for d in range(1, 8))
                    assert (c["name"], expected, tuple(sorted(c["holidays"]))) in observed, c["name"]
                result["independent_reader"] = {"reader": "MPXJ16.10.0 strict", "tasks": len(leaves),
                    "planned_durations": "matched", "wbs_parents": "matched", "calendar_working_hours_and_holidays": "matched",
                    "current_duration_null_tasks": [str(t.getName()) for t in leaves if t.getDuration() is None],
                    "current_duration_semantics": "Reader actual/remaining/calendar duration is separate from preserved planned duration."}
            finally:
                proxy.close()
        finally:
            jpype.shutdownJVM()
    print(json.dumps(result, ensure_ascii=False, indent=2))
