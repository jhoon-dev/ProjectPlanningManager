"""Independent TSV/native manifest and pinned MPXJ checks; does not claim P6 round trips."""
import hashlib
import importlib.metadata
import json
from pathlib import Path
import platform
import sys


def tsv(path):
    tables = {}
    ended = False
    fields = None
    for index, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines()):
        cells = line.split("\t")
        if index == 0:
            assert cells[:2] == ["ERMHDR", "20.12"]
        elif cells[0] == "%T":
            assert not ended and cells[1] not in tables
            rows = tables[cells[1]] = []
            fields = None
        elif cells[0] == "%F":
            assert fields is None and len(cells[1:]) == len(set(cells[1:]))
            fields = cells[1:]
        elif cells[0] == "%R":
            assert fields and len(cells) == len(fields) + 1
            rows.append(dict(zip(fields, cells[1:])))
        else:
            assert cells == ["%E"] and not ended
            ended = True
    assert ended
    return tables


def verify(root):
    path = root / "workspace.xer"
    report = json.loads((root / "conversion.json").read_text(encoding="utf-8"))
    assert report["contract"] == "ppm-xer-workspace-v1"
    assert report["xerSha256"] == hashlib.sha256(path.read_bytes()).hexdigest()
    tables = tsv(path)
    primary = {"CURRTYPE": "curr_id", "SCHEDOPTIONS": "schedoptions_id", "PROJECT": "proj_id", "CALENDAR": "clndr_id", "PROJWBS": "wbs_id", "TASK": "task_id",
               "RSRC": "rsrc_id", "RSRCRATE": "rsrc_rate_id", "TASKRSRC": "taskrsrc_id",
               "TASKPRED": "task_pred_id", "ACTVTYPE": "actv_code_type_id", "ACTVCODE": "actv_code_id"}
    identities = {}
    for name, key in primary.items():
        rows = tables[name]
        ids = {row[key] for row in rows}
        assert len(ids) == len(rows), (name, "duplicate IDs")
        identities[key] = ids
    aliases = {"parent_wbs_id": "wbs_id", "base_clndr_id": "clndr_id", "pred_task_id": "task_id",
               "pred_proj_id": "proj_id", "parent_actv_code_id": "actv_code_id"}
    for rows in tables.values():
        for row in rows:
            for key, value in row.items():
                target = aliases.get(key, key)
                if value and target in identities:
                    assert value in identities[target], (key, value)
    projects = identities["proj_id"]
    assert projects == {"1", "2"}
    assert len(tables["TASK"]) == 12 and len(tables["ACTVTYPE"]) == 4 and len(tables["ACTVCODE"]) == 8
    assert len(tables["TASKACTV"]) == 6
    owners = {r["task_id"]: r["proj_id"] for r in tables["TASK"]}
    types = {r["actv_code_type_id"]: r["proj_id"] for r in tables["ACTVTYPE"]}
    codes = {r["actv_code_id"]: r for r in tables["ACTVCODE"]}
    for row in tables["TASKACTV"]:
        assert owners[row["task_id"]] == row["proj_id"] == types[row["actv_code_type_id"]]
        assert codes[row["actv_code_id"]]["actv_code_type_id"] == row["actv_code_type_id"]
    for row in tables["ACTVCODE"]:
        if row["parent_actv_code_id"]:
            assert codes[row["parent_actv_code_id"]]["actv_code_type_id"] == row["actv_code_type_id"]
    for row in tables["TASKPRED"]:
        assert owners[row["task_id"]] == row["proj_id"] == row["pred_proj_id"] == owners[row["pred_task_id"]]
    native_ids = set()
    for entry in report["projects"]:
        native = root / entry["nativeFile"]
        assert hashlib.sha256(native.read_bytes()).hexdigest() == entry["nativeSha256"]
        document = json.loads(native.read_text(encoding="utf-8"))
        assert 10 <= document["schemaVersion"] <= 18  # Writer uses the oldest schema needed by the input.
        payload = document["payload"]
        assert payload["documentId"] == entry["documentId"]
        native_ids.add(payload["documentId"])
        conversion = json.loads((root / entry["conversion"]).read_text(encoding="utf-8"))
        pid = conversion["combinedProjectId"]
        task_mapping = {m["ppmId"]: m["xerId"] for m in conversion["combinedMapping"] if m["kind"] == "task"}
        assert len(task_mapping) == len(payload["tasks"])
        assert all(owners[uid] == pid for uid in task_mapping.values())
        local = tsv(native.parent / "project.xer")
        assert len(local["PROJECT"]) == 1 and len(local["TASKACTV"]) == 3
    assert len(native_ids) == 2
    workspace = json.loads((root / "workspace.ppmworkspace").read_text(encoding="utf-8"))["payload"]
    assert {p["documentId"] for p in workspace["projects"]} == native_ids
    assert all(not Path(p["path"]).is_absolute() for p in workspace["projects"])
    assert len(workspace["resources"]) == 1 and len(workspace["bindings"]) == 2
    assert importlib.metadata.version("mpxj") == "16.10.0"
    assert importlib.metadata.version("JPype1") == "1.6.0"
    import jpype
    import mpxj  # Registers the pinned jars for the independent reader.
    jpype.startJVM()
    try:
        from java.lang import System
        from org.mpxj.reader import UniversalProjectReader
        from org.mpxj import TimeUnit
        java = str(System.getProperty("java.version"))
        for pid in sorted(projects):
            proxy = UniversalProjectReader().getProjectReaderProxy(str(path.resolve()))
            try:
                reader = proxy.getProjectReader()
                reader.setIgnoreErrors(False)
                reader.setProjectID(int(pid))
                model = proxy.read()
                assert not list(model.getIgnoredErrors())
                leaves = [task for task in model.getTasks() if not task.getSummary()]
                expected = [row for row in tables["TASK"] if row["proj_id"] == pid]
                assert [str(task.getName()) for task in leaves] == [row["task_name"] for row in expected]
                assert [round(float(task.getDuration().convertUnits(TimeUnit.MINUTES, model.getProjectProperties()).getDuration())) for task in leaves] == [480, 120, 60, 120, 60, 0]
                assert sum(len(list(task.getPredecessors())) for task in leaves) == 4
                # MPXJ retains the whole file's code catalog; select by the explicit scope owner.
                catalog = list(model.getActivityCodes())
                assert len(catalog) == 4
                activity_codes = [code for code in catalog if str(code.getScopeProjectUniqueID()) == pid]
                assert len(activity_codes) == 2
                values = [value for code in activity_codes for value in code.getValues()]
                assert len(values) == 4 and sum(value.getParentValue() is not None for value in values) == 1
                assert [len(list(task.getActivityCodeValues())) for task in leaves[:2]] == [2, 1]
                assert len(list(model.getResourceAssignments())) == 2
            finally:
                proxy.close()
    finally:
        jpype.shutdownJVM()
    return {"status": "success", "projects": 2, "tasks": 12, "code_types": 4, "code_values": 8,
            "code_assignments": 6, "native_shared_bindings": 2, "all_ids_and_references": True,
            "native_hashes_and_relative_paths": True, "combined_task_mapping": True,
            "mpxj": "16.10.0", "jpype": "1.6.0", "java": java, "python": platform.python_version(),
            "P6_roundtrip_run": False}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1])), indent=2))
