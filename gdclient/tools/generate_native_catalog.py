"""Generate the native C++ feature catalog from data/features.json."""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "data" / "features.json"
OUTPUT = ROOT / "src" / "generated_feature_catalog.hpp"

STATUS = {
    "implemented": "Implemented",
    "experimental": "Experimental",
    "not-wired": "NotWired",
}


def wide(value: str) -> str:
    """Encode a JSON string as a valid C++ wide string literal."""
    value = value.replace("\\", "\\\\").replace('"', '\\"')
    value = value.replace("\r", "\\r").replace("\n", "\\n").replace("\t", "\\t")
    # Preserve source as UTF-8 for ordinary Unicode, and avoid literal control chars.
    return 'L"' + value + '"'


def main() -> None:
    catalog = json.loads(SOURCE.read_text(encoding="utf-8"))
    features = catalog.get("features")
    if not isinstance(features, list) or not features:
        raise SystemExit("feature catalog must contain a non-empty features array")
    ids = [item.get("id") for item in features]
    if len(ids) != len(set(ids)):
        raise SystemExit("feature catalog contains duplicate IDs")

    lines = [
        "#pragma once",
        "#include <cstddef>",
        "namespace gdclient {",
        "enum class FeatureStatus { Implemented, Experimental, NotWired };",
        "struct FeatureCatalogEntry { const wchar_t* id; const wchar_t* name; const wchar_t* category; const wchar_t* description; FeatureStatus status; const wchar_t* activation; const wchar_t* risk; };",
        "inline constexpr FeatureCatalogEntry kFeatureCatalog[] = {",
    ]
    for f in features:
        try:
            status = STATUS[f["implementation_status"]]
            values = [f["id"], f["name"], f["category"], f["description"], f["activation_type"], f["risk_level"]]
        except KeyError as exc:
            raise SystemExit(f"invalid feature metadata: missing/invalid {exc} in {f.get('id')!r}") from exc
        lines.append(
            "    {" + ", ".join([*(wide(v) for v in values[:4]), f"FeatureStatus::{status}", *(wide(v) for v in values[4:])]) + "},"
        )
    lines.extend([
        "};",
        "inline constexpr std::size_t kFeatureCatalogSize = sizeof(kFeatureCatalog) / sizeof(kFeatureCatalog[0]);",
        "} // namespace gdclient",
        "",
    ])
    OUTPUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"Generated {len(features)} native feature catalog entries: {OUTPUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
