"""Build a deterministic synthetic 30-task pilot case; no real project data."""
from copy import deepcopy
from pathlib import Path
import hashlib
import json

folder = Path(__file__).resolve().parent
source = json.loads((folder.parent / "weekly-update-ux-2026-11-17/weekly-prepared.ppm").read_text(encoding="utf-8"))
old = source["payload"]
p = deepcopy(old)
p["documentId"] = "weekly-pm-pilot-30-20261118"
p["derivedFrom"] = old["documentId"]
p["revision"] = 2
p["name"] = "자동화 설비 6개 셀 구축 · 사용자 시험 합성 사례"
p.pop("remainingLeveling", None)
p["levelingDecisions"] = []
p["tasks"], p["resources"], p["assignments"], p["progress"] = [], [], [], []
schedule = []
packages = ["반입 컨베이어", "로봇 조립", "비전 검사", "전기 검사", "포장", "출하 컨베이어"]
for number, package in enumerate(packages, 1):
    prefix = f"C{number:02d}-"
    resource = deepcopy(old["resources"][0])
    resource["id"] = f"technician-{number}"
    resource["name"] = f"설치 기술자 {number}"
    p["resources"].append(resource)
    for original in old["tasks"]:
        task = deepcopy(original)
        task["id"] = prefix + original["id"]
        task["name"] = f"{number:02d} {package} · {original['name']}"
        task["predecessors"] = [prefix + key for key in original["predecessors"]]
        p["tasks"].append(task)
    for original in old["assignments"]:
        assignment = deepcopy(original)
        assignment["taskId"] = prefix + original["taskId"]
        assignment["resourceId"] = resource["id"]
        p["assignments"].append(assignment)
    # One completed design and one in-progress parts report per package.
    for original in old["progress"][:2]:
        entry = deepcopy(original)
        entry["id"] = prefix + original["id"]
        entry["taskId"] = prefix + original["taskId"]
        entry["sourceDocumentId"] = p["documentId"]
        entry["recordedRevision"] = 2
        entry["recordedAt"] = "2026-11-13T00:00:00Z"
        entry["reporter"] = f"합성 담당자 {number}"
        p["progress"].append(entry)
    for original in old["baselines"][0]["schedule"]:
        row = deepcopy(original)
        row["taskId"] = prefix + original["taskId"]
        row["drivingPredecessors"] = [prefix + key for key in original["drivingPredecessors"]]
        schedule.append(row)
baseline = deepcopy(old["baselines"][0])
baseline["id"] = "pilot-baseline-v1"
baseline["sourceDocumentId"] = p["documentId"]
baseline["inputRevision"] = 1
baseline["createdAt"] = "2026-11-09T00:00:00Z"
baseline["approvedBy"] = "합성 PM"
baseline["reason"] = "사용자 시험용 최초 계획 · 실제 승인 아님"
baseline["schedule"] = schedule
for key in baseline["input"]:
    baseline["input"][key] = deepcopy(p[key])
p["baselines"] = [baseline]

def compact(value):
    # This case contains strings, integers, booleans and null only; matches Qt compact JSON.
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")

source["payload"] = p
source["sha256"] = hashlib.sha256(compact(p)).hexdigest()
target = folder / "pilot-start.ppm"
target.write_text(json.dumps(source, ensure_ascii=False, sort_keys=True, indent=4) + "\n", encoding="utf-8", newline="\n")
assert len(p["tasks"]) == 30 and len(p["progress"]) == 12 and len(schedule) == 30
print("Generated synthetic pilot case: 30 tasks, 6 resources, 12 assignments, 12 prior reports, 1 baseline")
print("SHA-256:", hashlib.sha256(target.read_bytes()).hexdigest())
