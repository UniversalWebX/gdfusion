
from __future__ import annotations
import json, re
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping

ROOT = Path(__file__).resolve().parents[1]
CATALOG_PATH = ROOT / "data" / "features.json"
VALID_HOTKEYS = frozenset({"NONE", "TAB", "ESC", "SPACE", "ENTER", "LEFT", "RIGHT", "UP", "DOWN", *[f"F{i}" for i in range(1, 25)], *[f"NUMPAD{i}" for i in range(10)]})
VALID_STATUSES = frozenset({"implemented", "experimental", "not-wired"})
VALID_ACTIVATION = frozenset({"external", "native-hook", "game-hook-required"})

@dataclass(frozen=True)
class FeatureSpec:
    id: str
    name: str
    category: str
    description: str
    implementation_status: str
    activation_type: str
    risk_level: str
    default_enabled: bool
    default_hotkey: str
    settings_schema: dict[str, dict[str, Any]]
    compatibility: tuple[str, ...]
    safe_mode_recommended: bool
    requires_game_focus: bool
    notes: str

    @property
    def activation_ready(self) -> bool:
        return self.implementation_status in {"implemented", "experimental"}

    @property
    def stable(self) -> bool:
        return self.implementation_status == "implemented"

    def defaults(self) -> dict[str, Any]:
        return {key: descriptor.get("default") for key, descriptor in self.settings_schema.items()}

@dataclass(frozen=True)
class ValidationResult:
    ok: bool
    normalized: dict[str, Any]
    errors: tuple[str, ...]
    warnings: tuple[str, ...]

class FeatureRegistry:
    def __init__(self, features: list[dict[str, Any]]):
        by_id: dict[str, FeatureSpec] = {}
        by_category: dict[str, list[str]] = {}
        for raw in features:
            feature = FeatureSpec(
                id=str(raw["id"]), name=str(raw["name"]), category=str(raw["category"]),
                description=str(raw.get("description", "")),
                implementation_status=str(raw["implementation_status"]),
                activation_type=str(raw["activation_type"]), risk_level=str(raw.get("risk_level", "low")),
                default_enabled=bool(raw.get("default_enabled", False)),
                default_hotkey=str(raw.get("default_hotkey", "NONE")).upper(),
                settings_schema=dict(raw.get("settings_schema", {})),
                compatibility=tuple(raw.get("compatibility", [])),
                safe_mode_recommended=bool(raw.get("safe_mode_recommended", False)),
                requires_game_focus=bool(raw.get("requires_game_focus", False)), notes=str(raw.get("notes", "")),
            )
            if feature.id in by_id:
                raise ValueError(f"Duplicate feature ID: {feature.id}")
            if feature.implementation_status not in VALID_STATUSES:
                raise ValueError(f"Invalid implementation status for {feature.id}")
            if feature.activation_type not in VALID_ACTIVATION:
                raise ValueError(f"Invalid activation type for {feature.id}")
            if feature.default_hotkey not in VALID_HOTKEYS:
                raise ValueError(f"Invalid default hotkey for {feature.id}")
            self._validate_schema(feature)
            by_id[feature.id] = feature
            by_category.setdefault(feature.category, []).append(feature.id)
        self._by_id = by_id
        self._by_category = {k: tuple(v) for k, v in by_category.items()}

    @staticmethod
    def _validate_schema(feature: FeatureSpec) -> None:
        for key, s in feature.settings_schema.items():
            if s.get("type") not in {"boolean", "integer", "number", "string", "choice"}:
                raise ValueError(f"Unsupported setting type {s.get('type')!r} in {feature.id}.{key}")
            if "default" not in s:
                raise ValueError(f"Missing default for {feature.id}.{key}")
            if s["type"] in {"integer", "number"}:
                if "minimum" in s and "maximum" in s and s["minimum"] > s["maximum"]:
                    raise ValueError(f"Invalid bounds for {feature.id}.{key}")
            if s["type"] == "choice" and s.get("choices") and s["default"] not in s["choices"]:
                raise ValueError(f"Default is not a valid choice for {feature.id}.{key}")

    def get(self, feature_id: str) -> FeatureSpec | None:
        return self._by_id.get(feature_id)

    def require(self, feature_id: str) -> FeatureSpec:
        try:
            return self._by_id[feature_id]
        except KeyError as exc:
            raise KeyError(f"Unknown feature ID: {feature_id}") from exc

    def all(self) -> tuple[FeatureSpec, ...]:
        return tuple(self._by_id.values())

    def categories(self) -> tuple[str, ...]:
        return tuple(self._by_category.keys())

    def in_category(self, category: str) -> tuple[FeatureSpec, ...]:
        return tuple(self._by_id[x] for x in self._by_category.get(category, ()))

    def search(self, query: str, *, category: str | None = None, status: str | None = None) -> tuple[FeatureSpec, ...]:
        q = query.casefold().strip()
        result = []
        for feature in self._by_id.values():
            if category and feature.category != category:
                continue
            if status and feature.implementation_status != status:
                continue
            haystack = f"{feature.id} {feature.name} {feature.category} {feature.description}".casefold()
            if not q or q in haystack:
                result.append(feature)
        return tuple(result)

    def validate_configuration(self, feature_id: str, supplied: Mapping[str, Any] | None) -> ValidationResult:
        feature = self.require(feature_id)
        data = dict(supplied or {})
        errors: list[str] = []
        warnings: list[str] = []
        allowed = {"enabled", "hotkey", "settings"}
        extras = set(data) - allowed
        if extras:
            errors.append("Unknown configuration keys: " + ", ".join(sorted(map(str, extras))))
        enabled = data.get("enabled", feature.default_enabled)
        if not isinstance(enabled, bool):
            errors.append("enabled must be a boolean")
            enabled = feature.default_enabled
        hotkey = str(data.get("hotkey", feature.default_hotkey)).upper()
        if hotkey not in VALID_HOTKEYS:
            errors.append(f"Invalid hotkey: {hotkey}")
            hotkey = feature.default_hotkey
        provided_settings = data.get("settings", {})
        if not isinstance(provided_settings, Mapping):
            errors.append("settings must be an object")
            provided_settings = {}
        unknown_settings = set(provided_settings) - set(feature.settings_schema)
        if unknown_settings:
            errors.append("Unknown settings: " + ", ".join(sorted(map(str, unknown_settings))))
        normalized_settings: dict[str, Any] = {}
        for key, schema in feature.settings_schema.items():
            value = provided_settings.get(key, schema["default"])
            value_type = schema["type"]
            try:
                if value_type == "boolean":
                    if not isinstance(value, bool): raise ValueError("must be boolean")
                elif value_type == "integer":
                    if isinstance(value, bool) or not isinstance(value, int): raise ValueError("must be an integer")
                    if value < schema.get("minimum", value) or value > schema.get("maximum", value): raise ValueError("outside allowed range")
                elif value_type == "number":
                    if isinstance(value, bool) or not isinstance(value, (int, float)): raise ValueError("must be numeric")
                    if value < schema.get("minimum", value) or value > schema.get("maximum", value): raise ValueError("outside allowed range")
                    value = float(value)
                elif value_type == "string":
                    if not isinstance(value, str): raise ValueError("must be text")
                    pattern = schema.get("pattern")
                    if pattern and not re.fullmatch(pattern, value): raise ValueError("does not match required format")
                elif value_type == "choice":
                    if value not in schema.get("choices", []): raise ValueError("not one of the allowed choices")
                normalized_settings[key] = value
            except (ValueError, TypeError) as exc:
                errors.append(f"{key}: {exc}")
                normalized_settings[key] = schema["default"]
        if enabled and not feature.activation_ready:
            warnings.append("This feature is catalogued but not wired to a game hook; it cannot be activated by this build.")
        elif enabled and feature.implementation_status == "experimental":
            warnings.append("This feature is experimental and may not work on this exact game build.")
        if enabled and feature.safe_mode_recommended:
            warnings.append("This feature can affect gameplay results; use safe mode and local practice testing.")
        normalized = {"enabled": enabled, "hotkey": hotkey, "settings": normalized_settings}
        return ValidationResult(not errors, normalized, tuple(errors), tuple(warnings))

_catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
if _catalog.get("schema_version") != 1:
    raise RuntimeError("Unsupported feature catalog schema version")
FEATURE_REGISTRY = FeatureRegistry(_catalog["features"])

def validate_configuration(feature_id: str, supplied: Mapping[str, Any] | None) -> ValidationResult:
    return FEATURE_REGISTRY.validate_configuration(feature_id, supplied)
