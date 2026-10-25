
from __future__ import annotations
import json, os, tempfile
from pathlib import Path
from typing import Any, Mapping
from .registry import FEATURE_REGISTRY, validate_configuration

APP_NAME = "GDClient"
def config_dir() -> Path:
    appdata = os.environ.get("APPDATA")
    base = Path(appdata) if appdata else Path.home() / ".config"
    return base / APP_NAME

class ConfigStore:
    """Atomic JSON settings store. Unknown feature IDs are ignored on load with a warning."""
    def __init__(self, path: Path | None = None):
        self.path = path or (config_dir() / "client-config.json")
        self.warnings: list[str] = []

    def load(self) -> dict[str, dict[str, Any]]:
        self.warnings = []
        if not self.path.exists(): return {}
        try:
            raw = json.loads(self.path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            self.warnings.append(f"Could not read config: {exc}")
            return {}
        if not isinstance(raw, dict) or not isinstance(raw.get("features", {}), dict):
            self.warnings.append("Config has an invalid structure; defaults will be used.")
            return {}
        output = {}
        for fid, config in raw.get("features", {}).items():
            if FEATURE_REGISTRY.get(fid) is None:
                self.warnings.append(f"Ignored unknown feature ID: {fid}")
                continue
            result = validate_configuration(fid, config)
            if result.ok:
                output[fid] = result.normalized
            else:
                self.warnings.append(f"Ignored invalid config for {fid}: {'; '.join(result.errors)}")
        return output

    def save(self, features: Mapping[str, Mapping[str, Any]]) -> None:
        normalized: dict[str, Any] = {}
        for fid, config in features.items():
            if FEATURE_REGISTRY.get(fid) is None:
                raise KeyError(f"Unknown feature ID: {fid}")
            result = validate_configuration(fid, config)
            if not result.ok:
                raise ValueError(f"Invalid configuration for {fid}: {'; '.join(result.errors)}")
            normalized[fid] = result.normalized
        payload = {"schema_version": 1, "features": normalized}
        self.path.parent.mkdir(parents=True, exist_ok=True)
        fd, temp_name = tempfile.mkstemp(prefix=".gdclient-", suffix=".tmp", dir=self.path.parent)
        try:
            with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
                json.dump(payload, stream, ensure_ascii=False, indent=2, sort_keys=True)
                stream.write("\n")
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temp_name, self.path)
        finally:
            try: os.unlink(temp_name)
            except FileNotFoundError: pass
