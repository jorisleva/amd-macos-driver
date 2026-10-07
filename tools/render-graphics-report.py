#!/usr/bin/env python3
"""Convert checked RGBA8 readbacks/references to lossless PNGs (standard library)."""
import argparse
import json
from pathlib import Path
import struct
import zlib


def png(width, height, pixels):
    if len(pixels) != width * height * 4:
        raise ValueError("RGBA8 image length does not match report dimensions")

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    rows = b"".join(b"\0" + pixels[y * width * 4:(y + 1) * width * 4] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report_directory", type=Path)
    args = parser.parse_args()
    report = json.loads((args.report_directory / "result.json").read_text(encoding="utf-8-sig"))
    directory = args.report_directory / "images"
    count = 0
    for case in report["graphics"]["cases"]:
        name = case["name"]
        if Path(name).name != name or "\\" in name:
            raise ValueError("Case name must be a filename")
        for suffix in ("", ".reference"):
            raw = (directory / (name + suffix + ".rgba")).read_bytes()
            (directory / (name + suffix + ".png")).write_bytes(png(case["width"], case["height"], raw))
            count += 1
    print(f"Wrote {count} PNGs; RGBA8 values and alpha are preserved.")


if __name__ == "__main__":
    main()
