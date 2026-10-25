import json
from pathlib import Path
from clientcore.config_store import ConfigStore
from clientcore.registry import FEATURE_REGISTRY

def test_atomic_config_roundtrip(tmp_path):
    path = tmp_path / "settings.json"
    store = ConfigStore(path)
    fid = FEATURE_REGISTRY.all()[0].id
    store.save({fid: {"enabled": False, "hotkey": "F2", "settings": {}}})
    got = store.load()
    assert got[fid]["hotkey"] == "F2"
    assert got[fid]["enabled"] is False

def test_bad_config_recovers(tmp_path):
    path = tmp_path / "settings.json"
    path.write_text("not json", encoding="utf-8")
    store = ConfigStore(path)
    assert store.load() == {}
    assert store.warnings
