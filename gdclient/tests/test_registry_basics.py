from clientcore.registry import FEATURE_REGISTRY, validate_configuration, VALID_HOTKEYS
from clientcore.runtime import FeatureRuntime

def test_catalog_has_342_unique_entries():
    features = FEATURE_REGISTRY.all()
    assert len(features) == 342
    assert len({f.id for f in features}) == 342

def test_every_feature_has_valid_metadata():
    for f in FEATURE_REGISTRY.all():
        assert f.id and f.name and f.category and f.description
        assert f.default_hotkey in VALID_HOTKEYS
        assert f.implementation_status in {"implemented", "experimental", "not-wired"}

def test_unknown_feature_cannot_be_queried_as_existing():
    assert FEATURE_REGISTRY.get("does.not.exist") is None

def test_not_wired_feature_cannot_activate():
    f = next(x for x in FEATURE_REGISTRY.all() if x.implementation_status == "not-wired")
    transition = FeatureRuntime().set_enabled(f.id, True)
    assert not transition.accepted
    assert "not implemented" in transition.message

def test_supported_external_feature_can_toggle():
    f = next(x for x in FEATURE_REGISTRY.all() if x.implementation_status == "implemented")
    transition = FeatureRuntime().set_enabled(f.id, True)
    assert transition.accepted and transition.enabled

def test_invalid_hotkey_rejected():
    f = FEATURE_REGISTRY.all()[0]
    result = validate_configuration(f.id, {"enabled": False, "hotkey": "NOT_A_KEY"})
    assert not result.ok

def test_setting_bounds_rejected():
    f = next(x for x in FEATURE_REGISTRY.all() if "multiplier" in x.settings_schema)
    result = validate_configuration(f.id, {"settings": {"multiplier": 1000}})
    assert not result.ok
