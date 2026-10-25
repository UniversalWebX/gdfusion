import json
from pathlib import Path
import pytest

from clientcore.profiles import export_profile, import_profile


def test_ini_profile_export_import_roundtrip(tmp_path: Path):
    settings = tmp_path / "settings.ini"
    settings.write_text("[UI]\nShowHUD=1\nAccent=2\n[Physics]\nTPSPreference=480\n[Keybinds]\nMenu=9\n", encoding="utf-8")
    profile = tmp_path / "profiles" / "practice.json"
    export_profile(settings, profile)
    doc = json.loads(profile.read_text(encoding="utf-8"))
    assert doc["schema_version"] == 1
    assert doc["settings"]["Physics"]["TPSPreference"] == "480"
    assert doc["settings"]["Keybinds"]["Menu"] == "9"

    settings.write_text("[UI]\nShowHUD=0\nOpacity=200\n[Custom]\nKeep=1\n", encoding="utf-8")
    import_profile(settings, profile)
    raw = settings.read_bytes()
    assert raw.startswith(b"\xff\xfe")
    loaded = raw[2:].decode("utf-16-le")
    assert "ShowHUD = 1" in loaded
    assert "Opacity = 200" in loaded
    assert "Keep = 1" in loaded


def test_profile_rejects_unknown_section_without_mutating_settings(tmp_path: Path):
    settings = tmp_path / "settings.ini"
    settings.write_text("[UI]\nShowHUD=1\n", encoding="utf-8")
    before = settings.read_bytes()
    profile = tmp_path / "bad.json"
    profile.write_text(json.dumps({"schema_version": 1, "application": "GDClient", "settings": {"Arbitrary": {"x": "y"}}}))
    with pytest.raises(ValueError, match="unsupported settings section"):
        import_profile(settings, profile)
    assert settings.read_bytes() == before


def test_profile_rejects_wrong_schema_and_multiline_values(tmp_path: Path):
    settings = tmp_path / "settings.ini"
    profile = tmp_path / "bad.json"
    for doc, message in [
        ({"schema_version": 99, "application": "GDClient", "settings": {}}, "schema"),
        ({"schema_version": 1, "application": "GDClient", "settings": {"UI": {"ShowHUD": "1\n[bad]"}}}, "invalid/oversized"),
    ]:
        profile.write_text(json.dumps(doc), encoding="utf-8")
        with pytest.raises(ValueError, match=message):
            import_profile(settings, profile)
