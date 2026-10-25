"""Generate a large, fast, data-driven configuration contract suite.

Each fixture tuple is formatted across multiple source lines for reviewability;
contract evaluation is batched per module so pytest avoids collecting tens of
thousands of separate function objects. Every feature x enabled state x hotkey
combination is still validated individually.
"""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CATALOG = json.loads((ROOT / "data" / "features.json").read_text(encoding="utf-8"))
HOTKEYS = ["NONE", "TAB", "ESC", "SPACE", "ENTER", "LEFT", "RIGHT", "UP", "DOWN"] + [f"F{i}" for i in range(1, 25)] + [f"NUMPAD{i}" for i in range(9)]
PROFILES = [(enabled, key) for enabled in (False, True) for key in HOTKEYS]
OUT = ROOT / "tests" / "generated_contracts"
OUT.mkdir(parents=True, exist_ok=True)
for old in OUT.glob("test_contracts_*.py"):
    old.unlink()

PER_FILE = 600
contracts: list[tuple[str, bool, str]] = []
for feature in CATALOG["features"]:
    for enabled, key in PROFILES:
        contracts.append((feature["id"], enabled, key))

for file_index, start in enumerate(range(0, len(contracts), PER_FILE)):
    batch = contracts[start : start + PER_FILE]
    lines = [
        '"""Generated data-driven feature configuration contracts. Do not edit by hand."""',
        "from clientcore.registry import validate_configuration",
        "",
        "CONTRACTS = [",
    ]
    for feature_id, enabled, key in batch:
        lines.extend(
            [
                "    (",
                f"        {feature_id!r}, {enabled!r}, {key!r},",
                "    ),",
                "",
            ]
        )
    lines.extend(
        [
            "]",
            "",
            f"def test_contract_batch_{file_index:03d}():",
            "    for feature_id, enabled, hotkey in CONTRACTS:",
            '        result = validate_configuration(feature_id, {"enabled": enabled, "hotkey": hotkey, "settings": {}})',
            "        assert result.ok, (feature_id, enabled, hotkey, result.errors)",
            '        assert result.normalized["enabled"] is enabled',
            '        assert result.normalized["hotkey"] == hotkey',
            "",
        ]
    )
    (OUT / f"test_contracts_{file_index:03d}.py").write_text("\n".join(lines), encoding="utf-8")

print(f"Generated {len(contracts):,} distinct configuration contracts in {len(list(OUT.glob('test_contracts_*.py')))} batched test modules.")
