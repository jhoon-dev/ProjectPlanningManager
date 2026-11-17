"""Independent exact-arithmetic and source-preservation oracle for XER source conversion."""
import argparse
import hashlib
import json
from decimal import Decimal
from fractions import Fraction
from pathlib import Path


def verify(root):
    def read(name):
        return json.loads((root / name).read_text(encoding="utf-8"))

    cases = read("numeric-cases.json")
    assert len(cases) == 760, len(cases)
    for case in cases:
        from_scale = 100 if case["from"] in ("USD", "EUR") else 1
        to_scale = 100 if case["to"] in ("USD", "EUR") else 1
        exact = Fraction(case["amount"] * case["n"] * to_scale, case["d"] * from_scale)
        rounded = (exact.numerator * 2 + exact.denominator) // (exact.denominator * 2)
        expected = rounded if rounded <= 9_000_000_000_000 else None
        assert case["result"] == expected, (case, expected)

    original, foreign = read("source.json"), read("foreign.json")
    assert original["currency"] == "USD" and foreign["currency"] == "EUR"
    assert original["sourceSha256"] == foreign["sourceSha256"]
    for before, after in zip(original["resources"], foreign["resources"], strict=True):
        assert after["type"] == {"labor": "equipment", "equipment": "labor"}[before["type"]]
    checks = len(cases)
    for section, fields in {
        "tasks": ["actualMinor"],
        "assignments": ["regularWork", "overtimeWork", "regularMinor", "overtimeMinor"],
        "periods": ["work", "actualMinor", "materialMinor", "expenseMinor", "bcwp", "bcws"],
    }.items():
        for before, after in zip(original[section], foreign[section], strict=True):
            for field in fields:
                expected = None if before[field] is None else before[field] * 2
                assert after[field] == expected, (section, field, before[field], after[field])
                checks += 1
    for before, after in zip(original["periods"], foreign["periods"], strict=True):
        for field, target in [("laborWork", "equipmentWork"), ("equipmentWork", "laborWork"),
                              ("laborMinor", "equipmentMinor"), ("equipmentMinor", "laborMinor")]:
            expected = None if before[field] is None else before[field] * 2
            assert after[target] == expected
            checks += 1

    before, converted = read("before.json"), read("converted.ppm")["payload"]
    for key in before:
        if key not in ("revision", "xerFinancialRevisions"):
            assert converted[key] == before[key], key
            checks += 1
    audit = converted["xerFinancialRevisions"][-1]["relink"]
    assert audit["incoming"] == foreign
    assert audit["incomingConversion"]["numerator"] == 1
    assert audit["incomingConversion"]["denominator"] == 2
    assert converted["xerFinancials"] == original

    tables, fields, table = {}, [], ""
    for line in (root / "export.xer").read_text(encoding="utf-8-sig").splitlines():
        cells = line.split("\t")
        if cells[0] == "ERMHDR":
            assert cells[-1] == "USD"
        elif cells[0] == "%T":
            table = cells[1]
        elif cells[0] == "%F":
            fields = cells[1:]
        elif cells[0] == "%R":
            tables.setdefault(table, []).append(dict(zip(fields, cells[1:], strict=True)))

    expected = sorted(tuple(None if a[k] is None else Decimal(a[k]) / divisor
                            for k, divisor in [("regularWork", 6000), ("overtimeWork", 6000),
                                               ("regularMinor", 100), ("overtimeMinor", 100)])
                      for a in original["assignments"])
    actual = sorted(tuple(Decimal(a[k]) if a.get(k) else None
                          for k in ["act_reg_qty", "act_ot_qty", "act_reg_cost", "act_ot_cost"])
                    for a in tables["TASKRSRC"])
    assert actual == expected, (actual, expected)
    checks += len(expected) * 4
    return {"status": "success", "numeric_cases": len(cases), "checks": checks,
            "converted_sha256": hashlib.sha256((root / "converted.ppm").read_bytes()).hexdigest()}


def verify_mpxj(root):
    import importlib.metadata
    import jpype
    import mpxj  # noqa: F401; registers the bundled Java classpath.

    assert importlib.metadata.version("mpxj") == "16.10.0"
    assert importlib.metadata.version("JPype1") == "1.6.0"
    jpype.startJVM()
    try:
        from org.mpxj.reader import UniversalProjectReader
        from org.mpxj import TimeUnit

        proxy = UniversalProjectReader().getProjectReaderProxy(str((root / "export.xer").resolve()))
        try:
            proxy.getProjectReader().setIgnoreErrors(False)
            model = proxy.read()
            assert not list(model.getIgnoredErrors())
            assignments = list(model.getResourceAssignments())
            work = sum(Decimal(str(a.getActualWork().convertUnits(TimeUnit.HOURS, model.getProjectProperties())
                                   .getDuration())) for a in assignments)
            cost = sum(Decimal(str(a.getActualCost() or 0)) for a in assignments)
            rounded_cost = cost.quantize(Decimal(".01"))
            assert abs(cost - rounded_cost) <= Decimal(".00000001")
            cost = rounded_cost
            assert work == Decimal("3.75") and cost == Decimal("110.46"), (work, cost)
            source = json.loads((root / "source.json").read_text(encoding="utf-8"))
            target = [t for t in model.getTasks() if not t.getSummary() and str(t.getName()) == source["tasks"][0]["name"]]
            assert len(target) == 1
            task_cost = Decimal(str(target[0].getActualCost()))
            assert abs(task_cost - Decimal("122.8")) <= Decimal(".00000001")
            return {"reader": "MPXJ16.10.0 strict", "actual_work_hours": str(work), "resource_cost": str(cost)}
        finally:
            proxy.close()
    finally:
        jpype.shutdownJVM()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("evidence", type=Path)
    parser.add_argument("--mpxj", action="store_true")
    args = parser.parse_args()
    result = verify(args.evidence)
    if args.mpxj:
        result["independent_reader"] = verify_mpxj(args.evidence)
    print(json.dumps(result, ensure_ascii=False, indent=2))
