"""Independent JSON/ledger conservation checks; no application libraries required."""
import argparse
import copy
import hashlib
import json
from pathlib import Path


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()


def verify(directory):
    checks = 0

    def equal(actual, expected, path):
        nonlocal checks
        if isinstance(expected, dict):
            assert isinstance(actual, dict) and actual.keys() == expected.keys(), path
            for key in expected:
                equal(actual[key], expected[key], path + "/" + key)
        elif isinstance(expected, list):
            assert isinstance(actual, list) and len(actual) == len(expected), path
            for index, value in enumerate(expected):
                equal(actual[index], value, path + "/" + str(index))
        else:
            assert actual == expected, (path, actual, expected)
            checks += 1

    def read(name):
        return json.loads((directory / name).read_text(encoding="utf-8"))

    def project(name):
        envelope = read(name)
        assert envelope["schemaVersion"] == 33
        assert envelope["sha256"] == hashlib.sha256(canonical(envelope["payload"])).hexdigest()
        return envelope["payload"]

    def projection(before, links):
        task = {v["sourceId"]: v["targetId"] for v in links if v["kind"] == "task"}
        resource = {v["sourceId"]: v["targetId"] for v in links if v["kind"] == "resource"}
        original = copy.deepcopy(before)
        original.pop("ledgerIdMigrations", None)
        for key, mapping in (("tasks", task), ("resources", resource)):
            ids = {v["id"] for v in original[key]}
            original[key] = [v for v in original[key] if mapping.get(v["id"]) not in ids]

        def walk(value, path=()):
            # These are original external source namespaces, not current owner references.
            if "xerScheduleAdoptions" in path or ("relink" in path and path[-1:] == ("incoming",)):
                return value
            if isinstance(value, list):
                if path[-1:] in (("predecessors",), ("drivingPredecessors",), ("taskIds",)):
                    return [task.get(v, v) for v in value]
                return [walk(v, path + (str(i),)) for i, v in enumerate(value)]
            if not isinstance(value, dict):
                return value
            out = {}
            for key, item in value.items():
                if key in ("taskId", "startTaskId", "finishTaskId", "predecessorId"):
                    out[key] = task.get(item, item)
                elif key == "resourceId":
                    out[key] = resource.get(item, item)
                elif key == "id" and len(path) >= 2 and path[-2] in ("tasks", "resources"):
                    mapping = task if path[-2] == "tasks" else resource
                    out[key] = mapping.get(item, item)
                elif key in ("sourceId", "targetId") and "relink" in path:
                    mapping = task if value["kind"] == "task" else resource
                    out[key] = mapping.get(item, item) if key == "targetId" or "previousLinks" in path else item
                else:
                    out[key] = walk(item, path + (key,))
            return out

        expected = walk(original)
        expected["revision"] += 1
        return expected

    before = read("before.json")
    after = project("migrated.ppm")
    sequential = project("sequential.ppm")
    detailed = project("detailed.ppm")
    equal(after["ledgerIdMigrations"][0]["before"], before, "original-before")
    receipts = []
    for label, payload in (("first", after), ("sequential", sequential), ("detailed", detailed)):
        for event in payload["ledgerIdMigrations"]:
            assert "ledgerIdMigrations" not in event["before"]
            assert "ledgerIdMigrations" not in event["after"]
            assert event["policy"] == "explicit-ledger-identity-v1" and event["confirmed"]
            assert event["sourceDocumentId"] == event["before"]["documentId"]
            expected = projection(event["before"], event["links"])
            equal(event["after"], expected, label + "/projection")
            for key in ("progress", "costEntries", "periodActuals", "stepReports"):
                strip = lambda rows: [{k: v for k, v in row.items() if k not in ("taskId", "resourceId")} for row in rows]
                old, new = strip(event["before"].get(key, [])), strip(event["after"].get(key, []))
                equal(old, new, label + "/immutable-ledger/" + key)
            receipts.append({"case": label, "revision": event["recordedRevision"], "links": event["links"],
                             "original_sha256": hashlib.sha256(canonical(event["before"])).hexdigest(),
                             "result_sha256": hashlib.sha256(canonical(event["after"])).hexdigest()})
        last = payload["ledgerIdMigrations"][-1]
        root = {k: v for k, v in payload.items() if k != "ledgerIdMigrations"}
        expected = copy.deepcopy(last["after"])
        expected["documentId"], expected["derivedFrom"] = root["documentId"], root["derivedFrom"]
        if label == "detailed":
            assert "remainingLeveling" in expected and "remainingLeveling" not in root
            expected.pop("remainingLeveling")
            decision = root["levelingDecisions"][-1]
            assert decision["action"] == "invalidate" and decision["reason"] == "관측 원장 ID 이관"
            assert decision["recordedRevision"] == last["recordedRevision"]
            equal(root["levelingDecisions"][:-1], expected["levelingDecisions"], "leveling-prefix")
            expected["levelingDecisions"] = root["levelingDecisions"]
        equal(root, expected, label + "/current-root")

    # The first immutable migration remains byte-equivalent through ordinary edits,
    # copy/save, financial re-merge, and the second migration.
    equal(sequential["ledgerIdMigrations"][0], after["ledgerIdMigrations"][0], "prior-event")
    financial_sums = {}
    for key in ("actualMinor", "workUnitMinutes"):
        values = [row.get(key) for row in before["periodActuals"]]
        financial_sums[key] = {"known_total": sum(v for v in values if v is not None),
                               "unknown_rows": sum(v is None for v in values)}
    return {"status": "PASS", "leaf_checks": checks, "ledger_totals_preserved": financial_sums,
            "receipts": receipts, "archives": "XER schedule sources and raw relink incoming unchanged",
            "coverage": "all JSON fields, before/after replay, typed owner references, baseline/scenario/closed period/EVM/XER, metadata, leveling invalidation, sequential history"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("evidence", type=Path)
    parser.add_argument("--mpxj", action="store_true")
    args = parser.parse_args()
    result = verify(args.evidence)
    if args.mpxj:
        import importlib.metadata
        import jpype
        import mpxj  # noqa: F401
        assert importlib.metadata.version("mpxj") == "16.10.0"
        assert importlib.metadata.version("JPype1") == "1.6.0"
        jpype.startJVM()
        try:
            from org.mpxj.reader import UniversalProjectReader
            from org.mpxj import TimeUnit
            proxy = UniversalProjectReader().getProjectReaderProxy(str((args.evidence / "export.xer").resolve()))
            try:
                proxy.getProjectReader().setIgnoreErrors(False)
                model = proxy.read()
                assert not list(model.getIgnoredErrors())
                native = json.loads((args.evidence / "sequential.ppm").read_text(encoding="utf-8"))["payload"]
                leaves = [t for t in model.getTasks() if not t.getSummary()]
                duration = lambda t: round(float(t.getPlannedDuration().convertUnits(TimeUnit.MINUTES, model.getProjectProperties()).getDuration()))
                assert {str(t.getName()): duration(t) for t in leaves} == {t["name"]: t["durationMinutes"] for t in native["tasks"]}
                assert {str(r.getName()) for r in model.getResources()} == {r["name"] for r in native["resources"]}
                result["independent_reader"] = {"reader": "MPXJ16.10.0 strict", "ignored_errors": 0,
                                                "tasks": len(leaves), "planned_durations": "matched",
                                                "resource_names": "matched"}
            finally:
                proxy.close()
        finally:
            jpype.shutdownJVM()
    print(json.dumps(result, ensure_ascii=False, indent=2))
