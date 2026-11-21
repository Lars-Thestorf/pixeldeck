#!/usr/bin/env python3
"""Asset-Konverter für pixeldeck (SPEC 16.3).

Wandelt PNGs eines Spiels in einen Header mit constexpr-Arrays (RGB565) um:

    pd_assets.py --out build/generated/snake/snake_assets.h games/snake/assets/*.png

- Symbolname = Dateiname ohne Endung und ohne Rastergröße, z. B.
  player_8x8.png -> assets::player (Spritesheet mit 8x8-Frames, zeilenweise).
- Alpha < 50 % wird zu TRANSPARENT (0xF81F). Opakes reines Magenta würde damit
  verwechselt und wird mit Warnung auf 0xF81E gesetzt.
- icon.png ist Pflicht und muss 16x16 groß sein.
"""
import argparse
import os
import re
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("pd_assets.py: Pillow fehlt (pip install pillow)")

TRANSPARENT = 0xF81F
GRID_RE = re.compile(r"^(?P<name>.+?)_(?P<w>\d+)x(?P<h>\d+)$")
IDENT_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def convert(path):
    stem = os.path.splitext(os.path.basename(path))[0]
    img = Image.open(path).convert("RGBA")
    iw, ih = img.size
    m = GRID_RE.match(stem)
    if m:
        name, fw, fh = m.group("name"), int(m.group("w")), int(m.group("h"))
    else:
        name, fw, fh = stem, iw, ih
    if not IDENT_RE.match(name):
        sys.exit(f"{path}: '{name}' ist kein gültiger C++-Bezeichner")
    if fw < 1 or fh < 1 or fw > 255 or fh > 255 or iw % fw or ih % fh:
        sys.exit(f"{path}: Bild {iw}x{ih} lässt sich nicht in {fw}x{fh}-Frames teilen (max. 255)")
    if name == "icon" and (fw, fh) != (16, 16):
        sys.exit(f"{path}: icon muss 16x16 sein, ist {fw}x{fh}")

    px = img.load()
    data, magenta = [], 0
    for fy in range(ih // fh):
        for fx in range(iw // fw):
            for y in range(fh):
                for x in range(fw):
                    r, g, b, a = px[fx * fw + x, fy * fh + y]
                    if a < 128:
                        data.append(TRANSPARENT)
                        continue
                    c = rgb565(r, g, b)
                    if c == TRANSPARENT:
                        c, magenta = 0xF81E, magenta + 1
                    data.append(c)
    if magenta:
        print(f"pd_assets.py: Warnung: {path}: {magenta} opake Magenta-Pixel auf 0xF81E gesetzt",
              file=sys.stderr)
    frames = (iw // fw) * (ih // fh)
    return name, fw, fh, frames, data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("pngs", nargs="*")
    args = ap.parse_args()

    assets = [convert(p) for p in sorted(args.pngs)]
    names = [a[0] for a in assets]
    dup = {n for n in names if names.count(n) > 1}
    if dup:
        sys.exit(f"pd_assets.py: doppelte Asset-Namen: {', '.join(sorted(dup))}")
    if "icon" not in names:
        sys.exit("pd_assets.py: assets/icon.png (16x16) fehlt")

    lines = [
        "// Generiert von tools/pd_assets.py - nicht bearbeiten.",
        "#pragma once",
        "#include <cstdint>",
        '#include "pd/sprite.h"',
        "",
        "namespace assets {",
    ]
    for name, w, h, frames, data in assets:
        lines.append(f"constexpr uint16_t {name}_data[{len(data)}] = {{")
        for i in range(0, len(data), 12):
            lines.append("  " + ", ".join(f"0x{v:04X}" for v in data[i:i + 12]) + ",")
        lines.append("};")
        lines.append(f"constexpr pd::Sprite {name}{{{w}, {h}, {frames}, {name}_data}};")
        lines.append("")
    lines.append("}  // namespace assets")
    text = "\n".join(lines) + "\n"

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    old = None
    if os.path.exists(args.out):
        with open(args.out, encoding="utf-8") as f:
            old = f.read()
    if old != text:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(text)


if __name__ == "__main__":
    main()
