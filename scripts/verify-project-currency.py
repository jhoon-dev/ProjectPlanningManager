"""Independent rational arithmetic and full JSON projection/conservation oracle."""
import copy
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import sys

MAX_MONEY = 9_000_000_000_000
DECIMALS = {"KRW": 0, "JPY": 0, "USD": 2, "EUR": 2}
MONEY = {"hourlyMinor", "fixedMinor", "unitPriceMinor", "amountMinor", "actualMinor", "regularMinor",
         "overtimeMinor", "laborMinor", "equipmentMinor", "materialMinor", "expenseMinor", "bcwp", "bcws",
         "openingEv", "openingPv", "budget", "earned", "bac", "pv", "ev", "ac", "dated"}


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()


def convert(amount, rate):
    if amount is None:
        return None
    value = Fraction(abs(amount) * rate["numerator"] * 10 ** DECIMALS[rate["targetCurrency"]],
                     rate["denominator"] * 10 ** DECIMALS[rate["fromCurrency"]])
    result, remainder = divmod(value.numerator, value.denominator)
    if rate["rounding"] == "exact-minor-v1":
        assert remainder == 0
    else:
        result += 2 * remainder >= value.denominator
    assert result <= MAX_MONEY
    return -result if amount < 0 else result


def stripped(p):
    p = copy.deepcopy(p)
    p.pop("currencyConversions", None)
    p.pop("ledgerIdMigrations", None)
    return p


def projection(before, rate):
    def walk(value, path=()):
        if path[:1] == ("xerScheduleAdoptions",):
            return copy.deepcopy(value)
        if "relink" in path and path[-1:] in (("incoming",), ("incomingConversion",), ("incomingCurrencySteps",)):
            return copy.deepcopy(value)
        if isinstance(value, list):
            return [walk(v, path + (str(i),)) for i, v in enumerate(value)]
        if not isinstance(value, dict):
            return value
        out = {}
        for key, item in value.items():
            if key in MONEY:
                assert item is None or type(item) is int, (path, key)
                out[key] = convert(item, rate)
            elif key == "currency":
                out[key] = rate["targetCurrency"]
            elif path[-1:] == ("previousConversion",) and key == "targetCurrency":
                assert item == rate["fromCurrency"] and value["numerator"] == value["denominator"] == 1
                out[key] = rate["targetCurrency"]
            else:
                out[key] = walk(item, path + (key,))
        if path[-1:] == ("relink",):
            out["incomingCurrencySteps"] = copy.deepcopy(value.get("incomingCurrencySteps", [])) + [rate]
        return out

    result = walk(stripped(before))
    result["revision"] += 1
    return result


def verify(directory):
    checks = 0

    def equal(a, b, path):
        nonlocal checks
        if isinstance(b, dict):
            assert isinstance(a, dict) and a.keys() == b.keys(), path
            for key in b:
                equal(a[key], b[key], path + "/" + key)
        elif isinstance(b, list):
            assert isinstance(a, list) and len(a) == len(b), path
            for i, v in enumerate(b):
                equal(a[i], v, path + "/" + str(i))
        else:
            assert type(a) is type(b) and a == b, (path, a, b)
            checks += 1

    def read(name):
        return json.loads((directory / name).read_text(encoding="utf-8-sig"))

    def project(name):
        envelope = read(name)
        assert envelope["schemaVersion"] == 34
        assert hashlib.sha256(canonical(envelope["payload"])).hexdigest() == envelope["sha256"]
        return envelope["payload"]

    before = read("before.json")
    converted = project("converted.ppm")
    intermediate = project("intermediate.ppm")
    sequential = project("sequential.ppm")
    leveling = project("leveling.ppm")
    equal(converted["currencyConversions"][0]["before"], stripped(before), "first-original")
    equal(converted["ledgerIdMigrations"], before["ledgerIdMigrations"], "original-identity-archive")
    equal(sequential["currencyConversions"][0], converted["currencyConversions"][0], "prior-currency-archive")
    events = 0
    for label, p in (("converted", converted), ("intermediate", intermediate), ("sequential", sequential), ("leveling", leveling)):
        for e in p["currencyConversions"]:
            assert "currencyConversions" not in e["before"] and "ledgerIdMigrations" not in e["before"]
            assert "currencyConversions" not in e["after"] and "ledgerIdMigrations" not in e["after"]
            assert e["policy"] == "project-currency-v1" and e["confirmed"]
            assert e["recordedRevision"] == e["before"]["revision"] + 1
            equal(e["after"], projection(e["before"], e["rate"]), label + "/pure-projection")
            events += 1
        # Interleaving an identity migration gives the latest pure root in its own receipt.
        last = max(p["currencyConversions"] + p.get("ledgerIdMigrations", []), key=lambda e: e["recordedRevision"])
        assert last["recordedRevision"] == p["revision"]
        expected = copy.deepcopy(last["after"])
        actual = stripped(p)
        expected["documentId"], expected["derivedFrom"] = actual["documentId"], actual["derivedFrom"]
        if "planLeveling" in expected or "remainingLeveling" in expected:
            expected.pop("planLeveling", None)
            expected.pop("remainingLeveling", None)
            equal(actual["levelingDecisions"][:-1], expected["levelingDecisions"], label + "/leveling-prefix")
            decision = actual["levelingDecisions"][-1]
            assert decision["action"] == "invalidate" and decision["reason"] == "프로젝트 전체 통화 전환"
            expected["levelingDecisions"] = actual["levelingDecisions"]
        equal(actual, expected, label + "/current-root")

    arithmetic = read("arithmetic.json")
    for row in arithmetic:
        rate = {"fromCurrency": row["from"], "targetCurrency": row["to"], "numerator": row["n"],
                "denominator": row["d"], "rounding": "half-away-from-zero-v1"}
        invalid = row["from"] == row["to"] and row["n"] != row["d"]
        try:
            result = convert(row["amount"], rate)
        except AssertionError:
            invalid = True
        if invalid:
            assert row.get("rejected") is True, row
        else:
            equal(row.get("result"), result, "rational-arithmetic")
    return {"status": "PASS", "arithmetic_cases": len(arithmetic), "leaf_checks": checks,
            "currency_events_checked": events, "supported_limit": len(leveling["currencyConversions"]),
            "method": "Python Fraction, independent typed-key projection, every JSON field compared; no application libraries",
            "preserved": "unrelated fields, IDs/provenance/work/dates, raw XER incoming and prior ID/currency/schedule archives",
            "rounding": "per-field integer minor units; signed ties away from zero; mismatch/overflow rejected by native contracts"}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1])), ensure_ascii=False, indent=2))
