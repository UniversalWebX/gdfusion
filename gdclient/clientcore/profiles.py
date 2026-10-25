"""JSON export/import for GD Client's Win32 settings.ini.

Profiles contain only scalar INI settings; they never include macros, DLLs,
executables, account data, or arbitrary file paths from the saved config.
"""
from __future__ import annotations

import configparser
import json
import os
from pathlib import Path
from typing import Any

PROFILE_SCHEMA = 1
ALLOWED_SECTIONS = {
    "UI", "System", "Gameplay", "Assist", "Player", "Physics",
    "Practice", "Labels", "Security", "Keybinds",
}
MAX_PROFILE_BYTES = 1_000_000
MAX_KEY_LENGTH = 128
MAX_VALUE_LENGTH = 4096


def settings_file_path() -> Path:
    """Return the settings path used by the native Windows client."""
    if os.name == "nt":
        root = os.environ.get("APPDATA")
        if not root:
            raise RuntimeError("APPDATA is unavailable; cannot locate GD Client settings.ini")
        return Path(root) / "GDClient" / "settings.ini"
    return Path.home() / ".config" / "GDClient" / "settings.ini"


def _decode_ini(raw: bytes) -> str:
    if raw.startswith((b"\xff\xfe", b"\xfe\xff")) or b"\x00" in raw[:128]:
        return raw.decode("utf-16")
    try:
        return raw.decode("utf-8-sig")
    except UnicodeDecodeError:
        return raw.decode("cp1252")


def _read_settings(path: Path) -> configparser.ConfigParser:
    parser = configparser.ConfigParser(interpolation=None, strict=True)
    parser.optionxform = str  # preserve Windows profile key casing
    if not path.exists():
        return parser
    raw = path.read_bytes()
    if len(raw) > MAX_PROFILE_BYTES:
        raise ValueError("settings.ini is unusually large; refusing to export/import it")
    parser.read_string(_decode_ini(raw))
    return parser


def export_profile(settings_path: str | Path, output_path: str | Path) -> Path:
    """Export scalar settings to a portable, versioned UTF-8 JSON file."""
    source = Path(settings_path)
    destination = Path(output_path)
    parser = _read_settings(source)
    settings: dict[str, dict[str, str]] = {}
    for section in parser.sections():
        if section not in ALLOWED_SECTIONS:
            continue
        values: dict[str, str] = {}
        for key, value in parser.items(section):
            values[key] = value
        if values:
            settings[section] = values
    document = {
        "schema_version": PROFILE_SCHEMA,
        "application": "GDClient",
        "settings": settings,
    }
    encoded = json.dumps(document, ensure_ascii=False, indent=2).encode("utf-8")
    if len(encoded) > MAX_PROFILE_BYTES:
        raise ValueError("exported profile exceeds size limit")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(encoded + b"\n")
    return destination


def import_profile(settings_path: str | Path, input_path: str | Path) -> Path:
    """Validate and import a JSON profile into the native settings INI.

    A settings file is only replaced after the JSON has been fully validated.
    Sections outside the client's settings schema are rejected.
    """
    source = Path(input_path)
    destination = Path(settings_path)
    raw = source.read_bytes()
    if len(raw) > MAX_PROFILE_BYTES:
        raise ValueError("profile exceeds 1 MB size limit")
    try:
        document: Any = json.loads(raw.decode("utf-8-sig"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"profile is not valid UTF-8 JSON: {exc}") from exc
    if not isinstance(document, dict) or document.get("schema_version") != PROFILE_SCHEMA:
        raise ValueError(f"unsupported profile schema; expected {PROFILE_SCHEMA}")
    if document.get("application") != "GDClient":
        raise ValueError("this JSON file is not a GDClient profile")
    supplied = document.get("settings")
    if not isinstance(supplied, dict):
        raise ValueError("profile settings must be an object")

    validated: dict[str, dict[str, str]] = {}
    for section, values in supplied.items():
        if not isinstance(section, str) or section not in ALLOWED_SECTIONS:
            raise ValueError(f"unsupported settings section: {section!r}")
        if not isinstance(values, dict):
            raise ValueError(f"settings section {section!r} must be an object")
        safe_values: dict[str, str] = {}
        for key, value in values.items():
            if (not isinstance(key, str) or not key or len(key) > MAX_KEY_LENGTH
                    or any(ch in key for ch in "\r\n=[]")):
                raise ValueError(f"invalid setting key in section {section!r}")
            if isinstance(value, bool):
                rendered = "1" if value else "0"
            elif isinstance(value, (str, int, float)):
                rendered = str(value)
            else:
                raise ValueError(f"setting {section}.{key} must be a scalar value")
            if len(rendered) > MAX_VALUE_LENGTH or "\x00" in rendered or "\r" in rendered or "\n" in rendered:
                raise ValueError(f"setting {section}.{key} has an invalid/oversized value")
            safe_values[key] = rendered
        validated[section] = safe_values

    # Preserve settings that were not included in the imported profile.
    current = _read_settings(destination)
    for section, values in validated.items():
        if not current.has_section(section):
            current.add_section(section)
        for key, value in values.items():
            current.set(section, key, value)
    from io import StringIO
    buffer = StringIO()
    current.write(buffer)
    payload = buffer.getvalue().encode("utf-16-le")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(b"\xff\xfe" + payload)
    return destination
