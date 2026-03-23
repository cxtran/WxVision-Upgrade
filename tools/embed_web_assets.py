from pathlib import Path

Import("env")


PROJECT_DIR = Path(env["PROJECT_DIR"])
DATA_DIR = PROJECT_DIR / "data"
OUTPUT = PROJECT_DIR / "include" / "generated_web_assets.h"

ASSETS = [
    ("config.html", "text/html; charset=utf-8", "no-cache"),
    ("index.html", "text/html; charset=utf-8", "no-cache"),
    ("ota.html", "text/html; charset=utf-8", "no-cache"),
    ("script.js", "application/javascript; charset=utf-8", "max-age=86400, public"),
    ("status.html", "text/html; charset=utf-8", "no-cache"),
    ("style.css", "text/css; charset=utf-8", "max-age=86400, public"),
]


def symbol_name(filename: str) -> str:
    return "".join(ch if ch.isalnum() else "_" for ch in filename)


def byte_lines(data: bytes) -> str:
    parts = []
    for index, value in enumerate(data):
        if index % 12 == 0:
            parts.append("    ")
        parts.append(f"0x{value:02x}")
        if index + 1 != len(data):
            parts.append(", ")
        if index % 12 == 11 or index + 1 == len(data):
            parts.append("\n")
    return "".join(parts)


def generate_header() -> None:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)

    lines = [
        "#pragma once\n",
        "\n",
        "#include <Arduino.h>\n",
        "#include <pgmspace.h>\n",
        "\n",
        "namespace web_assets\n",
        "{\n",
        "struct EmbeddedAsset\n",
        "{\n",
        "    const char *path;\n",
        "    const char *contentType;\n",
        "    const char *cacheControl;\n",
        "    const uint8_t *data;\n",
        "    size_t size;\n",
        "};\n",
        "\n",
    ]

    asset_entries = []
    for filename, content_type, cache_control in ASSETS:
        path = DATA_DIR / filename
        data = path.read_bytes()
        symbol = symbol_name(filename)
        lines.append(f"static const uint8_t {symbol}[] PROGMEM = {{\n")
        lines.append(byte_lines(data))
        lines.append("};\n\n")
        asset_entries.append(
            f'    {{"/{filename}", "{content_type}", "{cache_control}", {symbol}, sizeof({symbol})}}'
        )

    lines.append("static const EmbeddedAsset kAssets[] = {\n")
    lines.append(",\n".join(asset_entries))
    lines.append("\n};\n\n")
    lines.append("static constexpr size_t kAssetCount = sizeof(kAssets) / sizeof(kAssets[0]);\n")
    lines.append("} // namespace web_assets\n")

    OUTPUT.write_text("".join(lines), encoding="utf-8", newline="\n")


generate_header()
