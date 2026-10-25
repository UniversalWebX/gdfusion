
from __future__ import annotations
from dataclasses import dataclass
from threading import RLock
from typing import Any
from .registry import FEATURE_REGISTRY, FeatureSpec, validate_configuration

@dataclass(frozen=True)
class Transition:
    feature_id: str
    enabled: bool
    accepted: bool
    message: str
    warnings: tuple[str, ...] = ()

class FeatureRuntime:
    """Centralized feature state manager. It refuses to activate catalog-only hooks."""
    def __init__(self):
        self._lock = RLock()
        self._states: dict[str, dict[str, Any]] = {}
        self._listeners = []

    def add_listener(self, callback):
        with self._lock: self._listeners.append(callback)

    def state(self, feature_id: str) -> dict[str, Any]:
        spec = FEATURE_REGISTRY.require(feature_id)
        with self._lock:
            return dict(self._states.get(feature_id, {"enabled": spec.default_enabled, "hotkey": spec.default_hotkey, "settings": spec.defaults()}))

    def set_enabled(self, feature_id: str, enabled: bool) -> Transition:
        spec = FEATURE_REGISTRY.require(feature_id)
        if not isinstance(enabled, bool):
            return Transition(feature_id, False, False, "Enabled state must be boolean.")
        with self._lock:
            current = self.state(feature_id)
            if enabled and not spec.activation_ready:
                return Transition(feature_id, False, False, "Feature is catalogued but its game integration is not implemented.")
            result = validate_configuration(feature_id, {**current, "enabled": enabled})
            if not result.ok:
                return Transition(feature_id, current["enabled"], False, "; ".join(result.errors), result.warnings)
            self._states[feature_id] = result.normalized
            listeners = tuple(self._listeners)
        transition = Transition(feature_id, enabled, True, "Enabled." if enabled else "Disabled.", result.warnings)
        for listener in listeners:
            try: listener(transition)
            except Exception: pass
        return transition

    def configure(self, feature_id: str, config: dict[str, Any]) -> Transition:
        result = validate_configuration(feature_id, config)
        if not result.ok:
            return Transition(feature_id, False, False, "; ".join(result.errors), result.warnings)
        spec = FEATURE_REGISTRY.require(feature_id)
        if result.normalized["enabled"] and not spec.activation_ready:
            return Transition(feature_id, False, False, "Feature settings saved, but this feature is not connected to a runtime hook.", result.warnings)
        with self._lock: self._states[feature_id] = result.normalized
        return Transition(feature_id, result.normalized["enabled"], True, "Configuration updated.", result.warnings)

    def disable_all(self) -> int:
        with self._lock:
            changed = 0
            for fid, old in list(self._states.items()):
                if old.get("enabled"):
                    self._states[fid] = {**old, "enabled": False}
                    changed += 1
            return changed
