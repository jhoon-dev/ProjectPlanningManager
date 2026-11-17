"""Read original MPP objects directly, without the bridge or its XML writer, and compare native projections."""
import hashlib
import json
from pathlib import Path
import sys
import jpype
import mpxj  # noqa: F401

ROOT = Path(__file__).resolve().parents[1]
evidence = Path(sys.argv[1])
fixtures = ROOT / "tests/fixtures/mpp-public"
checks = 0


def equal(a, b, label):
    global checks
    assert a == b, (label, a, b)
    checks += 1


jpype.startJVM("-Dlog4j2.statusLoggerLevel=OFF")
try:
    from java.io import File
    from org.mpxj.mpp import MPPReader
    from org.mpxj import TimeUnit
    cases = []
    for name in ("mpp9relations.mpp", "mpp12relations.mpp", "mpp14relations.mpp", "assigned-resources-mpp8-project2003.mpp"):
        source = fixtures / name
        model = MPPReader().read(File(str(source.resolve())))
        properties = model.getProjectProperties()
        native = json.loads((evidence / (name + ".json")).read_text(encoding="utf-8"))
        report = json.loads((evidence / (name + ".report.json")).read_text(encoding="utf-8"))
        equal(len(model.getIgnoredErrors()), 0, "reader errors")
        equal(report["source"]["sha256"], hashlib.sha256(source.read_bytes()).hexdigest(), "source SHA256")
        equal(report["currencyPolicy"]["financialValuesImported"], False, "financial policy")
        for key in ("progress", "costEntries", "baselines", "periodActuals", "stepReports"):
            equal(native.get(key, []), [], "no inferred " + key)
        equal(native.get("statusAt"), None, "no inferred status date")
        tasks = {str(t.getUniqueID()): t for t in model.getTasks() if not t.getSummary() and not t.getNull()}
        equal(len(native["tasks"]), len(tasks), "leaf count")
        for t in native["tasks"]:
            original = tasks[t["id"].removeprefix("xml-task-")]
            equal(t["name"], str(original.getName()), "task name")
            minutes = float(original.getDuration().convertUnits(TimeUnit.MINUTES, properties).getDuration())
            equal(t["durationMinutes"], round(minutes), "working-minute duration")
            expected = sorted("xml-task-" + str(link.getPredecessorTask().getUniqueID()) for link in original.getPredecessors())
            equal(sorted(t["predecessors"]), expected, "predecessor identities")
            rules = {r["predecessorId"]: (r["type"], r["lagMinutes"]) for r in t.get("dependencyRules", [])}
            for link in original.getPredecessors():
                key = "xml-task-" + str(link.getPredecessorTask().getUniqueID())
                expected_type = str(link.getType())
                lag = round(float(link.getLag().convertUnits(TimeUnit.MINUTES, properties).getDuration()))
                equal(rules.get(key, ("FS", 0)), (expected_type, lag), "dependency type/lag")
        resources = {str(r.getUniqueID()): r for r in model.getResources() if r.getName() is not None and str(r.getName()).strip()}
        equal(len(native["resources"]), len(resources), "resource count")
        for r in native["resources"]:
            original = resources[r["id"].removeprefix("xml-resource-")]
            equal(r["name"], str(original.getName()), "resource name")
            capacity = original.getMaxUnits()
            equal(r["maxUnits"], None if capacity is None else round(float(capacity)), "capacity hundredths")
        expected_assignments = []
        for a in model.getResourceAssignments():
            rid = str(a.getResourceUniqueID())
            if rid in resources:
                expected_assignments.append(("xml-task-" + str(a.getTaskUniqueID()), "xml-resource-" + rid, round(float(a.getUnits()))))
        equal(sorted((a["taskId"], a["resourceId"], a["units"]) for a in native["assignments"]), sorted(expected_assignments), "assignments")
        cases.append({"file": name, "format": report["reader"]["format"], "tasks": len(tasks), "resources": len(resources), "assignments": len(expected_assignments)})
    print(json.dumps({"status": "PASS", "checks": checks, "cases": cases,
                      "method": "Direct MPPReader objects compared with C++ native fields; bridge and MSPDIWriter are bypassed. This is a projection check using the same binary decoder, not independent Microsoft Project recalculation."}, indent=2))
finally:
    jpype.shutdownJVM()
