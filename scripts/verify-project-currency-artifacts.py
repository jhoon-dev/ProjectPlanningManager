"""Read actual currency-conversion exports with independent report/Java readers."""
import argparse
from decimal import Decimal
import importlib.metadata
import json
from pathlib import Path


def reports(root):
    from openpyxl import load_workbook
    from pypdf import PdfReader

    native = json.loads((root / "converted.ppm").read_text(encoding="utf-8"))["payload"]
    event = native["currencyConversions"][0]
    workbook = load_workbook(root / "report.xlsx", data_only=True)
    history = list(workbook["프로젝트 전체 통화 전환 이력"].values)
    assert history[6] == (event["id"], "USD → KRW", "200 / 1", event["rate"]["rounding"],
                          event["rate"]["effectiveDate"] + " / " + event["rate"]["reference"],
                          event["reporter"] + " / " + event["reason"], event["recordedAt"],
                          event["sourceDocumentId"] + " / " + str(event["recordedRevision"]))
    costs = {r[0]: r for r in list(workbook["작업별 원가"].values)[6:]}
    observed = {}
    for row in sorted(native["costEntries"], key=lambda r: (r["statusAt"], r["recordedRevision"])):
        observed[row["taskId"]] = row["actualMinor"]
    for task in native["tasks"]:
        expected = observed.get(task["id"])
        assert costs[task["id"]][6] == (expected if expected is not None else "미정")
    material = list(workbook["자재 재고·소모"].values)[6:]
    assert {r[0]: r[6] for r in material} == {m["id"]: m["unitPriceMinor"] for m in native["resourcePlan"]["materials"]}
    pdf = PdfReader(root / "report.pdf")
    assert len(pdf.pages) > 0 and all(p.mediabox.width > 0 and p.mediabox.height > 0 for p in pdf.pages)
    text = "".join("\n".join(p.extract_text() for p in pdf.pages).split())
    for value in ("프로젝트 전체 통화 전환 이력", "USD → KRW", "200 / 1", "통화 KRW",
                  event["rate"]["reference"], event["reporter"], event["reason"],
                  str(observed["observed-task"])):
        assert "".join(value.split()) in text, value
    return {"status": "PASS", "xlsx_sheets": len(workbook.sheetnames), "pdf_pages": len(pdf.pages),
            "cost_rows_checked": len(costs), "material_prices_checked": len(material),
            "currency": "KRW", "history_metadata": "matched original native receipt",
            "known_zero_unknown": "preserved in workbook cost rows",
            "readers": {"openpyxl": importlib.metadata.version("openpyxl"),
                        "pypdf": importlib.metadata.version("pypdf")}}


def exchange(root):
    import jpype
    import mpxj  # noqa: F401: register Java classpath

    assert importlib.metadata.version("mpxj") == "16.10.0"
    jpype.startJVM("-Dlog4j2.statusLoggerLevel=OFF")
    result = []
    try:
        from org.mpxj.reader import UniversalProjectReader
        from org.mpxj import TimeUnit
        for stem, filename in (("target-krw", "converted.ppm"), ("export", "sequential.ppm")):
            native = json.loads((root / filename).read_text(encoding="utf-8"))["payload"]
            for suffix in ("xer", "xml"):
                path = root / f"{stem}.{suffix}"
                proxy = UniversalProjectReader().getProjectReaderProxy(str(path.resolve()))
                try:
                    reader = proxy.getProjectReader()
                    if hasattr(reader, "setIgnoreErrors"):
                        reader.setIgnoreErrors(False)
                    model = proxy.read()
                    assert not list(model.getIgnoredErrors())
                    props = model.getProjectProperties()
                    currency = str(props.getCurrencySymbol() if suffix == "xer" else props.getCurrencyCode())
                    digits = 0 if native["currency"] in ("KRW", "JPY") else 2
                    assert currency == native["currency"] and props.getCurrencyDigits() == digits
                    names = {str(t.getName()): t for t in model.getTasks() if not t.getSummary()}
                    assert {t["name"] for t in native["tasks"]} <= names.keys()
                    ac_checked = 0
                    if suffix == "xer":
                        assert set(names) == {t["name"] for t in native["tasks"]}
                        for task in native["tasks"]:
                            minutes = names[task["name"]].getPlannedDuration().convertUnits(TimeUnit.MINUTES, props)
                            assert round(float(minutes.getDuration())) == task["durationMinutes"]
                        observed = {r["taskId"]: r["actualMinor"] for r in native["costEntries"]}
                        for task in native["tasks"]:
                            value = names[task["name"]].getActualCost()
                            expected = observed.get(task["id"])
                            if expected is None:
                                assert value is None
                            else:
                                assert Decimal(str(value)) * 10 ** digits == expected
                            ac_checked += 1
                        assert {str(r.getName()) for r in model.getResources()} == {r["name"] for r in native["resources"]}
                    result.append({"file": path.name, "currency": currency, "digits": digits,
                                   "ignored_errors": 0, "native_task_names": len(native["tasks"]),
                                   "cumulative_ac_rows": ac_checked})
                finally:
                    proxy.close()
    finally:
        jpype.shutdownJVM()
    return {"status": "PASS", "reader": "MPXJ16.10.0 strict", "files": result,
            "scope": "XML currency precision/task names; XER additionally planned durations/resources/cumulative AC"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--mpxj", action="store_true")
    args = parser.parse_args()
    print(json.dumps(exchange(args.root) if args.mpxj else reports(args.root), ensure_ascii=False, indent=2))
