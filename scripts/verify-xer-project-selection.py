"""Independent TSV + pinned MPXJ checks for the selected XER subset, not P6 parity."""
import hashlib
import importlib.metadata
import platform
import json
import sys
from decimal import Decimal
from pathlib import Path


def tsv(path):
    tables = {}
    fields = None
    current = None
    ended = False
    for index, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines()):
        cells = line.split("\t")
        if index == 0:
            assert cells[:2] == ["ERMHDR", "20.12"]
        elif cells[0] == "%T":
            assert not ended and cells[1] not in tables
            current = tables[cells[1]] = []
            fields = None
        elif cells[0] == "%F":
            assert fields is None
            fields = cells[1:]
            assert len(set(fields)) == len(fields)
        elif cells[0] == "%R":
            assert fields and len(cells) == len(fields) + 1
            current.append(dict(zip(fields, cells[1:])))
        else:
            assert cells == ["%E"] and not ended
            ended = True
    assert ended
    return tables


def verify(root):
    root = root / "xer-selection-qa"
    source_path = root / "original.xer"
    source = source_path.read_bytes()
    raw = tsv(source_path)
    expected_ids = {"100": {"20", "30", "40", "50", "60", "70"},
                    "200": {"1020", "1030", "1040", "1050", "1060", "1070"}}
    expected_calendars = {"100": {"1"}, "200": {"1", "502"}}
    expected_resources = {"100": {"99"}, "200": {"99", "299"}}
    assert {p["proj_id"] for p in raw["PROJECT"]} == set(expected_ids)
    exports = {}
    for label, pid in [("a", "100"), ("b", "200")]:
        report = json.loads((root / f"selected-{label}.json").read_text(encoding="utf-8"))
        selected = report["projectSelection"]
        assert selected["sourceProjectCount"] == 2 and selected["selectedProjectId"] == pid
        assert selected["excludedProjectCount"] == 1 and selected["externalRelations"] == "reject-incoming-and-outgoing"
        assert report["source"]["sha256"] == hashlib.sha256(source).hexdigest()
        assert report["source"]["projectId"] == pid and report["compatibility"]["externalRoundTrip"] == "unverified"
        for kind, expected in [("task", expected_ids[pid]), ("calendar", expected_calendars[pid]),
                               ("resource", expected_resources[pid])]:
            assert {m["xerId"] for m in report["mapping"] if m["kind"] == kind} == expected
        counts = {r["table"]: r for r in selected["tables"]}
        for name, row in counts.items():
            if name not in raw:
                assert name in {"ACTVTYPE", "ACTVCODE", "TASKACTV", "UDFTYPE", "UDFVALUE",
                                "FINDATES", "PROJCOST", "TASKFIN", "TRSRCFIN"} and row["inputRows"] == 0
            assert row["inputRows"] == len(raw.get(name, []))
            assert row["inputRows"] == row["selectedRows"] + row["excludedRows"]
        assert counts["TASK"]["selectedRows"] == 6 and counts["TASK"]["excludedRows"] == 6
        assert counts["RSRC"]["selectedRows"] == len(expected_resources[pid])
        assert counts["CALENDAR"]["selectedRows"] == len(expected_calendars[pid])
        exported = tsv(root / f"selected-{label}.xer")
        original_tasks = [t for t in raw["TASK"] if t["proj_id"] == pid]
        assert [t["task_name"] for t in exported["TASK"]] == [t["task_name"] for t in original_tasks]
        assert [Decimal(t["target_drtn_hr_cnt"]) for t in exported["TASK"]] == [Decimal(t["target_drtn_hr_cnt"]) for t in original_tasks]
        assert len(exported["PROJECT"]) == 1 and len(exported["TASKPRED"]) == 4
        exports[pid] = (label, original_tasks)

    assert importlib.metadata.version("mpxj") == "16.10.0"
    assert importlib.metadata.version("JPype1") == "1.6.0"
    import jpype
    import mpxj  # Registers the pinned jars, used only by this verifier.
    jpype.startJVM()
    try:
        from java.lang import System
        java_version = str(System.getProperty("java.version"))
        from java.io import FileInputStream
        from java.nio.charset import Charset
        from org.mpxj.primavera import PrimaveraXERFileReader
        from org.mpxj.reader import UniversalProjectReader
        from org.mpxj import TimeUnit
        reader = PrimaveraXERFileReader()
        reader.setCharset(Charset.forName("UTF-8"))
        stream = FileInputStream(str(source_path.resolve()))
        try:
            # listProjects does not use UniversalProjectReader's BOM preprocessing.
            if source.startswith(b"\xef\xbb\xbf"):
                assert stream.skip(3) == 3
            inventory = {str(e.getKey()): str(e.getValue()) for e in reader.listProjects(stream).entrySet()}
        finally:
            stream.close()
        assert inventory == {"100": "SITE-01", "200": "SITE-02"}
        for pid, (label, tasks) in exports.items():
            proxy = UniversalProjectReader().getProjectReaderProxy(str(source_path.resolve()))
            try:
                reader = proxy.getProjectReader()
                reader.setIgnoreErrors(False)
                reader.setProjectID(int(pid))
                project = proxy.read()
                assert not list(project.getIgnoredErrors())
                leaves = [t for t in project.getTasks() if not t.getSummary()]
                assert {str(t.getUniqueID()) for t in leaves} == expected_ids[pid]
                assert [str(t.getName()) for t in leaves] == [t["task_name"] for t in tasks]
                assert sum(len(list(t.getPredecessors())) for t in leaves) == 4
                assert len(list(project.getResourceAssignments())) == 2
            finally:
                proxy.close()
            proxy = UniversalProjectReader().getProjectReaderProxy(str((root / f"selected-{label}.xer").resolve()))
            try:
                proxy.getProjectReader().setIgnoreErrors(False)
                project = proxy.read()
                assert not list(project.getIgnoredErrors())
                leaves = [t for t in project.getTasks() if not t.getSummary()]
                minutes = [round(float(t.getDuration().convertUnits(TimeUnit.MINUTES, project.getProjectProperties()).getDuration())) for t in leaves]
                assert minutes == [int(Decimal(t["target_drtn_hr_cnt"]) * 60) for t in tasks]
                assert [str(t.getName()) for t in leaves] == [t["task_name"] for t in tasks]
                assert sum(len(list(t.getPredecessors())) for t in leaves) == 4
                assert len(list(project.getResources())) == len(expected_resources[pid])
                assert len(list(project.getResourceAssignments())) == 2
            finally:
                proxy.close()
    finally:
        jpype.shutdownJVM()
    return {"status": "success", "projects": 2, "selected_tasks_each": 6,
            "A_resources": 1, "B_resources": 2, "A_calendars": 1, "B_calendars": 2,
            "input_sha256": hashlib.sha256(source).hexdigest(), "selection_manifests_verified": True,
            "mpxj": "16.10.0", "jpype": "1.6.0", "java": java_version,
            "python": platform.python_version(), "mpxj_source_list_and_selected_identity": True,
            "mpxj_projected_exports_strict": True, "raw_actual_duration_parity_claimed": False,
            "P6_roundtrip_run": False}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-forecast")), indent=2))
