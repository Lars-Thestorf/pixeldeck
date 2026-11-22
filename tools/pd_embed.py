#!/usr/bin/env python3
"""Bettet die WebPad-Seite gzip-komprimiert als C++-Array ein (SPEC 13.1).

Aufruf: pd_embed.py --out webpad_page.cpp --symbol kWebPadPage web/webpad.html

Einrückung und Leerzeilen werden entfernt (die Seite darf deshalb keine mehrzeiligen
Strings oder <pre>-Blöcke enthalten), dann gzip mit mtime=0 für reproduzierbare Builds.
"""
import argparse
import gzip
import os
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--symbol", required=True)
    ap.add_argument("src")
    a = ap.parse_args()

    with open(a.src, encoding="utf-8") as f:
        lines = [ln.strip() for ln in f.read().splitlines()]
    text = "\n".join(ln for ln in lines if ln)
    raw = text.encode("utf-8")
    gz = gzip.compress(raw, compresslevel=9, mtime=0)

    out = [
        "// Generiert von tools/pd_embed.py aus %s - nicht bearbeiten." % os.path.basename(a.src),
        "#include <cstddef>",
        "#include <cstdint>",
        "namespace pd::os {",
        "extern const uint8_t %s[];" % a.symbol,
        "extern const size_t %sLen;" % a.symbol,
        "const uint8_t %s[] = {" % a.symbol,
    ]
    for i in range(0, len(gz), 20):
        out.append("  " + ",".join(str(b) for b in gz[i:i + 20]) + ",")
    out += ["};", "const size_t %sLen = %d;" % (a.symbol, len(gz)), "}  // namespace pd::os", ""]
    data = "\n".join(out)

    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    old = None
    if os.path.exists(a.out):
        with open(a.out, encoding="utf-8") as f:
            old = f.read()
    if old != data:
        with open(a.out, "w", encoding="utf-8") as f:
            f.write(data)
    print("%s: %d Bytes, gzip %d Bytes" % (os.path.basename(a.src), len(raw), len(gz)),
          file=sys.stderr)
    if len(gz) > 20 * 1024:
        print("WARNUNG: Seite über dem Ziel von 20 KB (SPEC 13.1)", file=sys.stderr)


if __name__ == "__main__":
    main()
