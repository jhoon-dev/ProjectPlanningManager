"""Read back the native UI projection without using the C++ witness/display implementation."""
import hashlib
import json
import sys
from pathlib import Path


def verify(build):
    root = build / "leveling-witness-qa"
    data = json.loads((root / "view.json").read_text(encoding="utf-8"))
    expected = [
        ("A", "2026-11-02 09:00", "2026-11-03 15:00", "2026-11-04 09:00", "2026-11-04 18:00"),
        ("B", "2026-11-02 13:00", "2026-11-02 17:00", "2026-11-03 14:00", "2026-11-03 18:00"),
        ("C", "2026-11-02 09:00", "2026-11-04 18:00", "2026-11-02 09:00", "2026-11-04 18:00"),
    ]
    for key in ("A", "B"):
        view = data[key]
        assert view["source"].startswith(key + " ·") and view["focus"] == "B"
        assert "총여유 9h / 자유여유 1h" in view["summary"] and "함께 이동 1작업" in view["summary"]
        assert [(row["id"], *row["cells"][1:5]) for row in view["rows"]] == expected
        assert [row["cells"][7] for row in view["rows"]] == ["함께 이동", "선택 작업 · 이동", "현재 유지"]
        assert all(row["cells"][5:7] == ["—", "—"] for row in view["rows"])
    assert "현재 A <원문>" in data["A"]["rows"][0]["cells"][0]
    assert "분할 A" in data["B"]["rows"][0]["cells"][0] and "현재 A" not in data["B"]["rows"][0]["cells"][0]
    forecast = {r["id"]: r["cells"] for r in data["forecast"]["rows"]}
    assert forecast["A"][1] == "2026-11-02 10:00" and forecast["A"][5] == "2026-11-02 09:00"
    assert forecast["D"][3] == "—" and forecast["D"][6] == "2026-11-02 10:00" and forecast["D"][7] == "완료 실적"
    assert "잔여 예측 / 상태일 2026-11-02 10:00" in data["forecast"]["source"]
    native = json.loads((root / "selected.ppm").read_text(encoding="utf-8"))
    canonical = json.dumps(native["payload"], sort_keys=True, ensure_ascii=False, separators=(",", ":")).encode()
    assert hashlib.sha256(canonical).hexdigest() == native["sha256"]
    assert native["schemaVersion"] == 9
    assert native["payload"]["tasks"][0]["name"] == "수정된 A"
    def check_ephemeral(value):
        if isinstance(value, dict):
            assert not {"levelingWitnesses", "resourceOrderLinks", "levelingFloatCalculated"} & value.keys()
            for child in value.values():
                check_ephemeral(child)
        elif isinstance(value, list):
            for child in value:
                check_ephemeral(child)
    check_ephemeral(native)
    images = ["A-total.png", "B-total.png", "narrow.png", "forecast.png"]
    for name in images:
        assert (root / name).read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
    return {"status": "success", "A_B_inputs_separate": True, "co_moving_dates_match_fixture": True,
            "total_free_distinguished": True, "actual_dates_preserved": True,
            "native_schema": 9, "evidence_not_persisted": True, "screenshots": images,
            "prepared_10000_model_ms": data["prepared_10000_model_ms"]}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1] if len(sys.argv) > 1 else "build-forecast")), indent=2))
