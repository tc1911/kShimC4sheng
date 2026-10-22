#!/usr/bin/env python3
"""Build the menu's embedded Noto Sans CJK SC subset (src/ui/fonts/).

The subset keeps the outlines (CFF) of printable ASCII, Latin-1 and Latin
Extended-A, general and CJK punctuation, kana, full-width forms and the
3,755 GB2312 level-1 hanzi; LVGL's TinyTTF rasterizes it at any size. It
is stored LZ4-compressed (LVGL's decoder) behind a small header and is
inflated into RAM outside the runtime slot at boot (src/ui/font.c).

--coverage full (the default) keeps that complete outline set. --coverage
menu selects a much smaller preset of ASCII, Latin-1/Extended-A, general
and CJK punctuation and full-width Latin/punctuation only; it drops kana
and the GB2312 hanzi, so it guarantees outlines for just the Chinese labels
named in --required-text (which menu mode requires). Any Chinese label
added later needs the generator rerun with that text to gain its outlines.

Needs fonttools and lz4 (pip install fonttools lz4). The source font is
downloaded unless --source is given; its SHA-256 is recorded either way.
"""
import argparse
import hashlib
import io
import json
import struct
import urllib.request
import zlib
from pathlib import Path

import lz4.block
from fontTools import subset
from fontTools.ttLib import TTFont

SOURCE_URL = ("https://github.com/notofonts/noto-cjk/raw/main/"
              "Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf")
LICENSE_URL = "https://github.com/notofonts/noto-cjk/raw/main/Sans/LICENSE"
MAGIC = b"KSF1"  # magic, raw size, compressed size, CRC-32 of the raw font
DEFAULT_OUTPUT_STEM = "noto_sans_cjk_sc"
LICENSE_DESCRIPTION = "SIL Open Font License 1.1"


def coverage():
    codepoints = set(range(0x20, 0x7F)) | set(range(0xA0, 0x180))
    codepoints |= set(range(0x2000, 0x2070))   # general punctuation
    codepoints |= set(range(0x3000, 0x3100))   # CJK punctuation, kana
    codepoints |= set(range(0xFF00, 0xFFEF))   # full-width forms
    for high in range(0xB0, 0xD8):             # GB2312 level 1
        for low in range(0xA1, 0xFF):
            try:
                codepoints.add(ord(bytes([high, low]).decode("gb2312")))
            except UnicodeDecodeError:
                pass
    return codepoints


def menu_coverage():
    """Minimal preset for the menu's own bilingual labels: printable ASCII,
    Latin-1/Extended-A, general punctuation, CJK punctuation and full-width
    Latin/punctuation. Kana and the GB2312 hanzi are deliberately excluded;
    the real menu characters must come from --required-text."""
    codepoints = set(range(0x20, 0x7F))        # printable ASCII
    codepoints |= set(range(0xA0, 0x180))      # Latin-1, Latin Extended-A
    codepoints |= set(range(0x2000, 0x2070))   # general punctuation
    codepoints |= set(range(0x3000, 0x3040))   # CJK punctuation
    codepoints |= set(range(0xFF00, 0xFF65))   # full-width Latin/punctuation
    return codepoints


def required_codepoints(path):
    """Non-whitespace characters in a UTF-8 text file, as a codepoint set."""
    if path is None:
        return set()
    return {ord(ch) for ch in path.read_text(encoding="utf-8")
            if not ch.isspace()}


def hex_codepoints(codepoints):
    return ["0x%04X" % cp for cp in sorted(codepoints)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, help="NotoSansCJKsc-Regular.otf")
    parser.add_argument("--source-url", default=SOURCE_URL,
                        help="font download URL used when --source is omitted")
    parser.add_argument("--output-stem", default=DEFAULT_OUTPUT_STEM,
                        help="output basename stem (default: noto_sans_cjk_sc)")
    parser.add_argument("--license-file", type=Path,
                        help="license file copied verbatim into the output")
    parser.add_argument("--required-text", type=Path,
                        help="UTF-8 text whose non-whitespace characters are "
                             "forced into the subset (finalized menu text)")
    parser.add_argument("--coverage", choices=("full", "menu"), default="full",
                        help="full keeps the complete GB2312 outline set "
                             "(default); menu keeps only the small menu preset "
                             "and requires --required-text")
    parser.add_argument("--out", type=Path,
                        default=Path(__file__).resolve().parents[1] / "src/ui/fonts")
    args = parser.parse_args()
    if args.coverage == "menu" and args.required_text is None:
        parser.error("--coverage menu requires --required-text: the menu preset "
                     "only covers ASCII/Latin/punctuation, so every actual menu "
                     "character (all Chinese labels) must be supplied explicitly")
    source = args.source.read_bytes() if args.source else \
        urllib.request.urlopen(args.source_url).read()

    custom_stem = args.output_stem != DEFAULT_OUTPUT_STEM
    if custom_stem:
        font_name = f"{args.output_stem}.ksf"
        license_name = f"{args.output_stem}-OFL.txt"
        provenance_name = f"{args.output_stem}.provenance.json"
    else:
        font_name = "noto_sans_cjk_sc.ksf"
        license_name = "OFL.txt"
        provenance_name = "provenance.json"
    if custom_stem and args.license_file is None:
        raise SystemExit("error: --output-stem requires an explicit "
                         "--license-file so the correct license is recorded")

    required = required_codepoints(args.required_text)
    requested = (coverage() if args.coverage == "full" else menu_coverage()) \
        | required

    options = subset.Options()
    options.layout_features = []
    options.hinting = False
    options.notdef_outline = True
    options.name_IDs = [0, 1, 2, 3, 4, 5, 6, 13, 14]   # keep the OFL notice
    options.drop_tables += ["GSUB", "GPOS", "GDEF", "BASE", "VORG", "vhea",
                            "vmtx", "DSIG"]
    font = TTFont(io.BytesIO(source))
    source_cmap = set(font.getBestCmap())
    missing_required = required - source_cmap
    if missing_required:
        raise SystemExit(
            f"error: {args.required_text} needs {len(missing_required)} "
            "character(s) absent from the source font: "
            + ", ".join("U+%04X" % cp for cp in sorted(missing_required)))
    missing = requested - source_cmap
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=requested)
    subsetter.subset(font)
    subset_cmap = set(font.getBestCmap())
    lost_required = required - subset_cmap
    if lost_required:
        raise SystemExit(
            f"error: {len(lost_required)} required character(s) missing from "
            "the subset: "
            + ", ".join("U+%04X" % cp for cp in sorted(lost_required)))
    raw = io.BytesIO()
    font.save(raw)
    raw = raw.getvalue()
    packed = lz4.block.compress(raw, mode="high_compression", compression=12,
                                store_size=False)

    args.out.mkdir(parents=True, exist_ok=True)
    blob = MAGIC + struct.pack("<III", len(raw), len(packed),
                               zlib.crc32(raw) & 0xFFFFFFFF) + packed
    (args.out / font_name).write_bytes(blob)
    if args.license_file is not None:
        (args.out / license_name).write_bytes(args.license_file.read_bytes())
    elif not (args.out / license_name).exists():
        (args.out / license_name).write_bytes(urllib.request.urlopen(LICENSE_URL).read())
    if args.coverage == "full":
        coverage_description = ("ASCII, Latin-1, Latin Extended-A, general and "
                                "CJK punctuation, kana, full-width forms, "
                                "GB2312 level 1")
    else:
        coverage_description = ("menu preset: ASCII, Latin-1, Latin Extended-A, "
                                "general punctuation, CJK punctuation, "
                                "full-width Latin/punctuation, plus the "
                                f"characters of {args.required_text}")
    (args.out / provenance_name).write_text(json.dumps({
        "source": args.source_url,
        "source_sha256": hashlib.sha256(source).hexdigest(),
        "license": f"{LICENSE_DESCRIPTION} ({license_name})",
        "coverage": coverage_description,
        "coverage_mode": args.coverage,
        "glyphs": len(font.getGlyphOrder()),
        "raw_bytes": len(raw), "compressed_bytes": len(packed),
        "blob_sha256": hashlib.sha256(blob).hexdigest(),
        "generator": "tools/gen_ui_font.py",
        "requested_codepoints": len(requested),
        "missing_codepoints": hex_codepoints(missing),
        "required_characters": len(required),
    }, indent=2) + "\n")
    print(f"{len(font.getGlyphOrder())} glyphs, {len(raw)} -> {len(packed)} bytes")


if __name__ == "__main__":
    main()
