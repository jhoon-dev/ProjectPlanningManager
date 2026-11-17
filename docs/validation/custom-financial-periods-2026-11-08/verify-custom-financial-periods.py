"""Independent native-ledger/Decimal oracle and real XLSX/PDF readback for CFP-01..06."""
import hashlib
import json
import math
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path
import sys

from openpyxl import load_workbook
from pypdf import PdfReader


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def close(a, b):
    return a is None and b is None or a is not None and b is not None and math.isclose(a, b, rel_tol=1e-11, abs_tol=1e-10)


def plus(values):
    values = list(values)
    if any(v is None for v in values):
        return None
    value = sum(values)
    return value if abs(value) <= 9_000_000_000_000 else None


def minus(a, b):
    return None if a is None or b is None else a - b


def ratio(a, b):
    return None if a is None or b is None or a < 0 or b <= 0 else a / b


def main():
    build = Path(sys.argv[1] if len(sys.argv) > 1 else "build-governance")
    folder = build / "custom-financial-periods-qa"
    before = json.loads((folder / "before.json").read_text(encoding="utf-8"))
    final = json.loads((folder / "final.json").read_text(encoding="utf-8"))
    envelope = json.loads((folder / "closed.ppm").read_text(encoding="utf-8"))
    check(envelope["schemaVersion"] == 30 and envelope["payload"] == final, "schema30/envelope identity")
    # QJson sorts keys, emits non-ASCII UTF8 and writes compact JSON for the envelope checksum.
    raw = json.dumps(final, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
    check(hashlib.sha256(raw).hexdigest() == envelope["sha256"], "native checksum")
    events = final["financialPeriodEvents"]
    first_id = events[0]["period"]["id"]
    closes = [e for e in events if e["period"]["id"] == first_id and e["action"] == "close"]
    check(len(closes) == 2, "two immutable close receipts")
    check(closes[0]["knowledgeRevision"] < closes[1]["knowledgeRevision"], "knowledge revision ordering")
    baselines = {b["id"]: b for b in final["baselines"]}
    numeric_cells = 0

    def oracle(period, revision, status):
        baseline = baselines[period["baselineId"]]
        plan = baseline["input"]
        budgets = {v["taskId"]: v["fixedMinor"] for v in plan["taskCosts"]}
        manual = {v["taskId"]: v for v in period["values"]}
        dates = {v["taskId"]: v for v in baseline["schedule"]}
        start, end = period["fromDate"] + " 00:00", min(period["toDate"] + " 23:59", status)
        result = {}
        for task in plan["tasks"]:
            tid = task["id"]
            bac = budgets[tid]

            def boundary(at, inclusive):
                entries = [e for e in final["costEntries"] if e["taskId"] == tid and e["recordedRevision"] <= revision
                           and (e["statusAt"] < at or inclusive and e["statusAt"] == at)]
                entry = max(enumerate(entries), key=lambda v: (v[1]["statusAt"], v[0]))[1] if entries else None
                ac = entry["actualMinor"] if entry else None
                physical = entry["physicalBasisPoints"] if entry else None
                ev = int((Decimal(bac) * physical / 10000).quantize(Decimal(1), rounding=ROUND_HALF_UP)) if physical is not None else None
                # Controlled baseline: two 8h civil workdays, uniform fixed cost, no resource rates.
                schedule = dates[tid]
                check(schedule["start"][:10] == "2026-10-26" and schedule["finish"][:10] == "2026-10-27", "controlled baseline calendar")
                day = at[:10]
                pv = 0 if day < "2026-10-26" or day == "2026-10-26" and at[11:] == "00:00" else bac if day > "2026-10-27" or day == "2026-10-27" and at[11:] >= "18:00" else None
                check(pv is not None, "oracle boundary outside controlled endpoints")
                return {"bac": bac, "pv": pv, "ev": ev, "ac": ac, "acReportId": entry["id"] if entry else "", "evReportId": entry["id"] if entry else "", "estimateSupported": True}

            a, b = boundary(start, False), boundary(end, True)
            entries = {}
            for entry in final["periodActuals"]:
                if entry["recordedRevision"] <= revision and entry["statusAt"] <= status:
                    key = (entry["taskId"], entry["resourceId"], entry["date"])
                    previous = entries.get(key)
                    if previous is None or entry["statusAt"] >= previous["statusAt"]:
                        entries[key] = entry
            dated = sum(v["actualMinor"] for v in entries.values() if v["taskId"] == tid and v["actualMinor"] is not None
                        and period["fromDate"] <= v["date"] <= period["toDate"])
            explicit = manual.get(tid, {})
            native_pv, native_ev, ac = minus(b["pv"], a["pv"]), minus(b["ev"], a["ev"]), minus(b["ac"], a["ac"])
            pv = explicit.get("budget") if explicit.get("budget") is not None else native_pv
            ev = explicit.get("earned") if explicit.get("earned") is not None else native_ev
            result[tid] = {"snapshot": {"taskId": tid, "opening": a, "closing": b, "dated": dated},
                           "numbers": [pv, ev, ac, native_pv, native_ev, dated, bac, b["pv"], b["ev"], b["ac"]]}
        return result

    for e in [e for e in events if e["action"] == "close"]:
        rows = oracle(e["period"], e["knowledgeRevision"], e["statusAt"])
        check({r["taskId"]: r for r in e["tasks"]} == {tid: row["snapshot"] for tid, row in rows.items()}, "frozen native snapshots differ from knowledge-cutoff ledger")

    def verify_report(file, period, revision, status):
        nonlocal numeric_cells
        tables = json.loads((folder / file).read_text(encoding="utf-8"))["tables"]
        rows = oracle(period, revision, status)
        baseline = baselines[period["baselineId"]]["input"]
        tasks = {t["id"]: t for t in baseline["tasks"]}
        nodes = {n["id"]: n for n in baseline["wbsNodes"]}

        def selected(table, row):
            if table == "financialPeriod":
                return list(rows)
            if table == "financialPeriodTasks":
                return [row[0]]
            out = []
            for tid, task in tasks.items():
                w = task["wbsId"]
                while w:
                    if w == row[0]:
                        out.append(tid)
                        break
                    w = nodes[w]["parentId"]
            return out

        for table in tables[:3]:
            for row in table["rows"]:
                ids = selected(table["id"], row)
                sums = [plus(rows[tid]["numbers"][i] for tid in ids) for i in range(10)]
                pv, ev, ac, native_pv, native_ev, dated, bac, cpv, cev, cac = sums
                etc = minus(bac, cev) if bac is not None and cev is not None else None
                eac = plus([cac, etc])
                expected = [pv, ev, ac, minus(ev, ac), minus(ev, pv), ratio(ev, ac), ratio(ev, pv), native_pv, native_ev,
                            dated, minus(ac, dated), bac, cpv, cev, cac, eac, etc]
                for col, value in enumerate(expected, 2):
                    check(close(row[col], value), f"{file}/{table['id']}/{row[0]}/{col}: {row[col]} != {value}")
                    numeric_cells += 1
        return tables

    period = closes[0]["period"]
    first_tables = verify_report("closed-report.json", period, closes[0]["knowledgeRevision"], closes[0]["statusAt"])
    last_tables = verify_report("reclosed-report.json", period, closes[1]["knowledgeRevision"], closes[1]["statusAt"])
    verify_report("open-report.json", period, events[0]["recordedRevision"], events[0]["statusAt"])
    verify_report("reopened-report.json", period, closes[1]["knowledgeRevision"], closes[1]["statusAt"])
    check(first_tables[0]["rows"][0][4] == 2500 and last_tables[0]["rows"][0][4] == 2600, "historical/reclosed AC oracle")
    check(before["costEntries"] == final["costEntries"][:len(before["costEntries"])], "preexisting cost facts modified")
    for key in ["baselines", "progress", "periodActuals", "stepReports"]:
        check(before.get(key, []) == final.get(key, []), f"{key} mutated")
    formulas = 0
    for filename, tables in [("closed.xlsx", first_tables), ("reclosed.xlsx", last_tables)]:
        workbook = load_workbook(folder / filename, data_only=False)
        for sheet in workbook:
            formulas += sum(cell.data_type == "f" for row in sheet for cell in row)
        for table, title in zip(tables[:3], ["사용자 기간 EVM", "사용자 기간 작업", "사용자 기간 기준선 WBS"]):
            sheet = workbook[title]
            values = list(sheet.values)
            header = next(i for i, row in enumerate(values) if list(row[:len(table["columns"])]) == table["columns"])
            for i, row in enumerate(table["rows"]):
                actual = values[header + 1 + i]
                for col in range(2, 19):
                    expected = row[col]
                    if expected is not None and col not in (7, 8):
                        expected /= 100
                    check(close(actual[col], expected), f"{filename}/{title}/{i}/{col}")
        workbook.close()
    check(formulas == 0, "executable spreadsheet formulas")
    ui_folders = [build / "custom-financial-periods-ui-qa"]
    native_ui = build / "cfp-native-qa/custom-financial-periods-ui-qa"
    if native_ui.exists():
        ui_folders.append(native_ui)
    ui_numeric_cells = 0
    for ui_folder in ui_folders:
        ui = load_workbook(ui_folder / "ui-report.xlsx", data_only=False)
        check("사용자 기간·마감 이력" in ui.sheetnames, "UI workbook history")
        for sheet in ui:
            formulas += sum(cell.data_type == "f" for row in sheet for cell in row)
        for table, title in zip(first_tables[:3], ["사용자 기간 EVM", "사용자 기간 작업", "사용자 기간 기준선 WBS"]):
            values = list(ui[title].values)
            header = next(i for i, row in enumerate(values) if list(row[:len(table["columns"])]) == table["columns"])
            for i, row in enumerate(table["rows"]):
                for col in range(2, 19):
                    expected = row[col]
                    if expected is not None and col not in (7, 8):
                        expected /= 100
                    check(close(values[header + 1 + i][col], expected), f"{ui_folder}/{title}/{i}/{col}")
                    ui_numeric_cells += 1
        ui.close()
    check(formulas == 0, "UI executable spreadsheet formulas")
    pages, text_modes = {}, {}
    for file in [folder / "closed.pdf"] + [f / "ui-report.pdf" for f in ui_folders]:
        pdf = PdfReader(file, strict=True)
        text = "\n".join(page.extract_text() for page in pdf.pages)
        key = file.relative_to(build).as_posix()
        check(len(pdf.pages) > 0, f"PDF pages/{file}")
        if file.parent == native_ui and not text.strip():
            # Qt's native Windows font engine may emit glyphs as vector paths.
            # This is structural validation, not a claim of searchable PDF text.
            for page in pdf.pages:
                content = page.get_contents().get_data()
                check(len(content) > 1000 and b" m\n" in content and b" c\n" in content,
                      "native outline page has no vector glyph content")
                check(len(page["/Resources"].get("/Font", {})) == 0, "unexpected native font/text extraction mismatch")
            text_modes[key] = "vector outlines; structural readback and separate PDFium visual QA"
        else:
            check("사용자" in text and "마감" in text and "25.00" in text, f"PDF text/{file}")
            check(not any(ord(c) < 32 and c not in "\n\r\t" for c in text), "invalid PDF controls")
            text_modes[key] = "searchable text verified"
        pages[key] = len(pdf.pages)
    print(json.dumps({"status": "passed", "policy": "custom-financial-period-v1", "schema": 30,
                      "independent_numeric_cells": numeric_cells, "close_receipts_checked": sum(e["action"] == "close" for e in events),
                      "historical_ac_minor": 2500, "reclosed_ac_minor": 2600, "xlsx_files": 2 + len(ui_folders), "ui_xlsx_numeric_cells": ui_numeric_cells, "xlsx_formulas": formulas,
                      "pdf_pages": pages, "pdf_text_modes": text_modes, "preexisting_facts_unchanged": True, "external_product_execution": "not run"}, indent=2))


if __name__ == "__main__":
    main()
