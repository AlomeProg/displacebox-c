#!/usr/bin/env python3
"""Compares the C conversion engine with the original Python implementation.

Run with: make test
"""
from __future__ import annotations

import json
import math
import os
import random
import shutil
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ORIG_SRC = os.path.abspath(os.path.join(ROOT, "..", "displacebin_gui_converter-main", "src"))
WORK = os.path.join(ROOT, ".test-tmp")
CLI = os.path.join(ROOT, "tests", "convert_cli")

sys.path.insert(0, ORIG_SRC)

import converter as py_converter  # noqa: E402
import reverse_converter as py_reverse  # noqa: E402
from PIL import Image  # noqa: E402

FAILURES = []


def check(condition: bool, message: str) -> None:
    if condition:
        print(f"  ok: {message}")
    else:
        print(f"  FAIL: {message}")
        FAILURES.append(message)


def run_cli(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([CLI, *args], capture_output=True, text=True)


def make_source(path: str, size: int = 256) -> None:
    rng = random.Random(1234)
    values = []
    for y in range(size):
        for x in range(size):
            value = 50.0 * math.sin(x / 17.0) * math.cos(y / 23.0) + rng.uniform(-5.0, 5.0)
            values.append(value)
    with open(path, "wb") as handle:
        handle.write(struct.pack(f"<{len(values)}f", *values))


def png_pixels(path: str) -> tuple[str, tuple[int, int], list[int]]:
    with Image.open(path) as image:
        return image.mode, image.size, list(image.getdata())


def main() -> int:
    if not os.path.exists(CLI):
        print("tests/convert_cli is missing; run 'make tests/convert_cli' first")
        return 2

    if not os.path.isdir(ORIG_SRC):
        print(f"skip: original Python sources not found at {ORIG_SRC}")
        return 0

    try:
        from PIL import Image  # noqa: F401
    except ImportError:
        print("skip: Pillow is not installed (needed only for the comparison test)")
        return 0

    shutil.rmtree(WORK, ignore_errors=True)
    for name in ("py", "c"):
        os.makedirs(os.path.join(WORK, name, "3m2"))

    py_dir = os.path.join(WORK, "py", "3m2")
    c_dir = os.path.join(WORK, "c", "3m2")
    py_bin = os.path.join(py_dir, "displace.bin")
    c_bin = os.path.join(c_dir, "displace.bin")
    make_source(py_bin)
    shutil.copyfile(py_bin, c_bin)

    print("== forward conversion ==")
    py_raw = os.path.join(py_dir, "displace.raw")
    py_png = os.path.join(py_dir, "displace.png")
    py_converter.process_raw(py_bin, py_raw, 256)
    py_converter.process_png(py_bin, py_png, 256)

    c_raw = os.path.join(c_dir, "displace.raw")
    c_png = os.path.join(c_dir, "displace.png")
    result = run_cli("forward", c_bin, c_raw, "256", "raw")
    check(result.returncode == 0, f"raw conversion exited cleanly ({result.stderr.strip()})")
    result = run_cli("forward", c_bin, c_png, "256", "png")
    check(result.returncode == 0, f"png conversion exited cleanly ({result.stderr.strip()})")

    with open(py_raw, "rb") as handle:
        py_raw_bytes = handle.read()
    with open(c_raw, "rb") as handle:
        c_raw_bytes = handle.read()
    check(py_raw_bytes == c_raw_bytes, "RAW output is byte-identical to Python")

    py_mode, py_size, py_data = png_pixels(py_png)
    c_mode, c_size, c_data = png_pixels(c_png)
    check(py_mode == c_mode == "I;16", f"PNG mode is I;16 (py={py_mode}, c={c_mode})")
    check(py_size == c_size == (256, 256), f"PNG dimensions match ({c_size})")
    check(py_data == c_data, "PNG pixel values are identical to Python")

    py_meta_path = os.path.join(py_dir, "displace_3m2_metadata.json")
    c_meta_path = os.path.join(c_dir, "displace_3m2_metadata.json")
    check(os.path.exists(py_meta_path), "Python metadata file name")
    check(os.path.exists(c_meta_path), "C metadata file name")
    with open(py_meta_path, "r", encoding="utf-8") as handle:
        py_meta = json.load(handle)
    with open(c_meta_path, "r", encoding="utf-8") as handle:
        c_meta = json.load(handle)
    check(py_meta == c_meta, "metadata JSON values are identical")
    with open(py_meta_path, "r", encoding="utf-8") as handle:
        py_meta_text = handle.read()
    with open(c_meta_path, "r", encoding="utf-8") as handle:
        c_meta_text = handle.read()
    if py_meta_text != c_meta_text:
        print("  note: metadata formatting differs")
        print("    py: " + py_meta_text.replace("\n", "\\n"))
        print("    c : " + c_meta_text.replace("\n", "\\n"))

    print("== reverse conversion ==")
    for label, source_png in (("python png", py_png), ("c png", c_png)):
        py_back = os.path.join(py_dir, f"back_{label.replace(' ', '_')}.bin")
        c_back = os.path.join(c_dir, f"back_{label.replace(' ', '_')}.bin")
        with open(c_meta_path, "r", encoding="utf-8") as handle:
            meta_data = json.load(handle)
        py_reverse.reverse_converter(source_png, py_back, meta_data)
        result = run_cli("reverse", source_png, c_back, c_meta_path)
        check(result.returncode == 0, f"{label}: reverse conversion exited cleanly")
        with open(py_back, "rb") as handle:
            py_back_bytes = handle.read()
        with open(c_back, "rb") as handle:
            c_back_bytes = handle.read()
        check(py_back_bytes == c_back_bytes, f"{label}: .bin output is byte-identical to Python")

    py_back_raw = os.path.join(py_dir, "back_raw.bin")
    c_back_raw = os.path.join(c_dir, "back_raw.bin")
    with open(c_meta_path, "r", encoding="utf-8") as handle:
        meta_data = json.load(handle)
    py_reverse.reverse_converter(py_raw, py_back_raw, meta_data)
    result = run_cli("reverse", py_raw, c_back_raw, c_meta_path)
    check(result.returncode == 0, "raw -> bin conversion exited cleanly")
    with open(py_back_raw, "rb") as handle:
        py_back_raw_bytes = handle.read()
    with open(c_back_raw, "rb") as handle:
        c_back_raw_bytes = handle.read()
    check(py_back_raw_bytes == c_back_raw_bytes, "raw -> bin output is byte-identical to Python")

    print("== edge cases ==")
    flat_bin = os.path.join(c_dir, "flat.bin")
    with open(flat_bin, "wb") as handle:
        handle.write(struct.pack("<65536f", *([7.5] * 65536)))
    flat_raw = os.path.join(c_dir, "flat.raw")
    result = run_cli("forward", flat_bin, flat_raw, "256", "raw")
    check(result.returncode == 0, "flat map conversion succeeds")
    with open(flat_raw, "rb") as handle:
        flat_values = struct.unpack("<65536H", handle.read())
    check(all(v == 0xFFFF for v in flat_values), "flat map is written as 0xFFFF")

    wrong_bin = os.path.join(c_dir, "wrong.bin")
    with open(wrong_bin, "wb") as handle:
        handle.write(b"\0" * 16)
    result = run_cli("forward", wrong_bin, os.path.join(c_dir, "wrong.raw"), "256", "raw")
    check(result.returncode != 0, "wrongly sized .bin is rejected")

    odd_raw = os.path.join(c_dir, "odd.raw")
    with open(odd_raw, "wb") as handle:
        handle.write(b"\1\2\3")
    result = run_cli("reverse", odd_raw, os.path.join(c_dir, "odd.bin"), c_meta_path)
    check(result.returncode != 0, "odd-sized .raw is rejected")

    eight_bit = os.path.join(c_dir, "eight.png")
    with Image.new("L", (16, 16), 128) as image:
        image.save(eight_bit)
    result = run_cli("reverse", eight_bit, os.path.join(c_dir, "eight.bin"), c_meta_path)
    check(result.returncode != 0, "8-bit grayscale PNG is rejected")

    zero_json = os.path.join(c_dir, "zero.json")
    with open(zero_json, "w", encoding="utf-8") as handle:
        json.dump({"Min": 0.0, "Max": 0.0, "Delta": 0.0, "DGCVer": "DisplaceGUI_2.2"}, handle)
    result = run_cli("reverse", py_png, os.path.join(c_dir, "zero.bin"), zero_json)
    check(result.returncode != 0, "zero metadata is rejected")

    missing_json = os.path.join(c_dir, "missing.json")
    with open(missing_json, "w", encoding="utf-8") as handle:
        json.dump({"Min": -1.0}, handle)
    result = run_cli("reverse", py_png, os.path.join(c_dir, "missing.bin"), missing_json)
    check(result.returncode != 0, "incomplete metadata is rejected")

    print()
    if FAILURES:
        print(f"{len(FAILURES)} check(s) failed")
        return 1
    print("All comparison checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
