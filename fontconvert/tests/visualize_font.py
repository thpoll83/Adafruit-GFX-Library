#!/usr/bin/env python3
"""
Visualize and validate a fontconvert-generated GFX .h file.

Each glyph is rendered to a white-background PNG (1 bit per pixel, upscaled
for readability).  A contact-sheet of all glyphs is also saved.  Structural
assertions are printed and, if --strict is given, cause a non-zero exit code.

Requires: pypng   (pip install pypng)   — no PIL/Pillow dependency.

Usage examples
--------------
  # Show all glyphs from an existing header; write PNGs to ./out/
  python visualize_font.py path/to/FontName.h --out-dir out/

  # Generate a test header from DejaVuSans then visualize it
  python visualize_font.py --generate-from /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \\
      --fontconvert ../cmake-build-debug/fontconvert --size 14 --range 0x41 0x7e --out-dir out/

  # Test a sequence-mode header (-S flag output)
  python visualize_font.py flag_de.h --check-nonblank 1 --strict

  # Same but treat assertion failures as errors (for CI)
  python visualize_font.py path/to/FontName.h --strict
"""

import argparse
import re
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

try:
    import png
except ImportError:
    sys.exit("pypng is required:  pip install pypng")


# ---------------------------------------------------------------------------
# PNG writer (thin wrapper so callers never touch the png module directly)
# ---------------------------------------------------------------------------

def _write_png(path: str, rows, width: int, height: int) -> None:
    """Write a grayscale 8-bit PNG.  rows is a list of lists (one per scanline)."""
    w = png.Writer(width=width, height=height, greyscale=True, bitdepth=8)
    with open(path, 'wb') as f:
        w.write(f, rows)


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

def parse_h_file(path: str) -> dict:
    """Return dict: name, bitmap (bytes), glyphs (list of dicts), first, last, yAdvance."""
    text = Path(path).read_text(encoding="utf-8", errors="replace")

    # Bitmap array — works for both range-mode and sequence-mode headers
    m = re.search(
        r'const uint8_t (\w+)Bitmaps\[\].*?=\s*\{(.*?)\};',
        text, re.DOTALL
    )
    if not m:
        raise ValueError(f"No *Bitmaps[] array found in {path}")
    font_name = m.group(1)
    # Strip C block comments before extracting hex bytes — the range annotations
    # embedded as comments (e.g. "/* range 0 (0x20 - 0x7e): */") contain hex
    # literals that would otherwise be mistakenly parsed as bitmap data.
    bitmap_content = re.sub(r'/\*.*?\*/', '', m.group(2), flags=re.DOTALL)
    bitmap_bytes = bytes(
        int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', bitmap_content)
    )

    # Glyph array  { bmpOff, w, h, xAdv, xOff, yOff }
    m = re.search(
        r'const GFXglyph \w+Glyphs\[\].*?=\s*\{(.*?)\};',
        text, re.DOTALL
    )
    if not m:
        raise ValueError(f"No *Glyphs[] array found in {path}")
    glyphs = [
        {
            'bitmapOffset': int(gm.group(1)),
            'width':        int(gm.group(2)),
            'height':       int(gm.group(3)),
            'xAdvance':     int(gm.group(4)),
            'xOffset':      int(gm.group(5)),
            'yOffset':      int(gm.group(6)),
        }
        for gm in re.finditer(
            r'\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(-?\d+),\s*(-?\d+)\s*\}',
            m.group(1)
        )
    ]

    # Font struct — try range-mode format first, fall back to sequence-mode
    m = re.search(
        r'const GFXfont \w+ PROGMEM\s*=\s*\{.*?'
        r'0x([0-9A-Fa-f]+),\s*// first\s*\n\s*'
        r'0x([0-9A-Fa-f]+),\s*// last\s*\n\s*'
        r'(\d+)',
        text, re.DOTALL
    )
    first_cp  = int(m.group(1), 16) if m else 0
    last_cp   = int(m.group(2), 16) if m else len(glyphs) - 1
    y_advance = int(m.group(3))     if m else 0

    return dict(
        name=font_name,
        bitmap=bitmap_bytes,
        glyphs=glyphs,
        first=first_cp,
        last=last_cp,
        yAdvance=y_advance,
    )


# ---------------------------------------------------------------------------
# Rendering helpers (pure pypng, no PIL)
# ---------------------------------------------------------------------------

def glyph_to_rows(glyph: dict, bitmap: bytes, scale: int = 4):
    """Return a list-of-lists (scanlines × pixel values 0/255) for one glyph."""
    w, h = glyph['width'], glyph['height']
    if w <= 0 or h <= 0:
        return [[255] * scale] * scale  # 1×1 upscaled white tile

    offset = glyph['bitmapOffset']
    rows = []
    for py in range(h * scale):
        gy = py // scale
        row = []
        for px in range(w * scale):
            gx = px // scale
            bit_idx  = gy * w + gx
            byte_pos = offset + bit_idx // 8
            bit_pos  = 7 - (bit_idx % 8)
            pixel = 0 if (byte_pos < len(bitmap) and
                          (bitmap[byte_pos] >> bit_pos) & 1) else 255
            row.append(pixel)
        rows.append(row)
    return rows


def save_glyph_png(glyph: dict, bitmap: bytes, path: str, scale: int = 4) -> None:
    w, h = max(glyph['width'], 1), max(glyph['height'], 1)
    rows = glyph_to_rows(glyph, bitmap, scale)
    _write_png(path, rows, w * scale, len(rows))


def make_sheet_png(font: dict, path: str, scale: int = 4, cols: int = 16) -> tuple:
    """Build a contact-sheet PNG and write it to *path*.  Returns (width, height)."""
    glyphs = font['glyphs']
    bitmap = font['bitmap']
    if not glyphs:
        _write_png(path, [[200]], 1, 1)
        return 1, 1

    max_w = max((g['width']  for g in glyphs if g['width']  > 0), default=1)
    max_h = max((g['height'] for g in glyphs if g['height'] > 0), default=1)
    cell_w = (max_w + 2) * scale
    cell_h = (max_h + 2) * scale
    n_rows = (len(glyphs) + cols - 1) // cols
    img_w  = cols  * cell_w
    img_h  = n_rows * cell_h

    # Build pixel buffer: list of lists, initialised to mid-grey
    canvas = [[200] * img_w for _ in range(img_h)]

    for i, g in enumerate(glyphs):
        col, row  = i % cols, i // cols
        x_off = col * cell_w + scale
        y_off = row * cell_h + scale
        glyph_rows = glyph_to_rows(g, bitmap, scale)
        for dy, scanline in enumerate(glyph_rows):
            cy = y_off + dy
            if cy < img_h:
                for dx, px in enumerate(scanline):
                    cx = x_off + dx
                    if cx < img_w:
                        canvas[cy][cx] = px

    _write_png(path, canvas, img_w, img_h)
    return img_w, img_h


# ---------------------------------------------------------------------------
# Assertions
# ---------------------------------------------------------------------------

def run_assertions(font: dict, min_nonblank: int = 0) -> list:
    issues = []
    glyphs  = font['glyphs']
    bitmap  = font['bitmap']
    first   = font['first']
    last    = font['last']

    if not glyphs:
        return ['FAIL: no glyphs found in font']

    expected = last - first + 1
    if len(glyphs) != expected:
        issues.append(
            f"WARN: glyph count {len(glyphs)} != expected {expected} "
            f"(range 0x{first:X}–0x{last:X})"
        )

    nonblank = 0
    for i, g in enumerate(glyphs):
        cp = first + i
        if g['width'] < 0 or g['height'] < 0:
            issues.append(f"FAIL: glyph 0x{cp:X} has negative dimensions "
                          f"({g['width']}x{g['height']})")
        if g['width'] > 0 and g['height'] > 0:
            end_byte = g['bitmapOffset'] + (g['width'] * g['height'] + 7) // 8
            if end_byte > len(bitmap):
                issues.append(
                    f"FAIL: glyph 0x{cp:X} bitmapOffset {g['bitmapOffset']} "
                    f"+ size overflows bitmap ({len(bitmap)} bytes)"
                )
            if g['xAdvance'] == 0:
                issues.append(f"WARN: glyph 0x{cp:X} has xAdvance=0 but non-zero size")
            start = g['bitmapOffset']
            chunk = bitmap[start:end_byte]
            if any(chunk):
                nonblank += 1

    if min_nonblank > 0 and nonblank < min_nonblank:
        issues.append(
            f"FAIL: only {nonblank}/{len(glyphs)} glyphs have non-zero bitmap content "
            f"(expected at least {min_nonblank}) — likely a rendering bug"
        )

    return issues


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

_DEFAULT_SCAN_DIR = (
    Path(__file__).parent.parent.parent.parent /
    "qmk_firmware/keyboards/handwired/polykybd/base/fonts/generated"
)


def build_argparser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('h_file', nargs='?', help='Path to a single generated .h file')
    p.add_argument('--out-dir', default='font_test_output',
                   help='Directory for PNG output (default: font_test_output/)')
    p.add_argument('--scale', type=int, default=6,
                   help='Pixel magnification for PNG output (default: 6)')
    p.add_argument('--cols', type=int, default=16,
                   help='Glyphs per row on the contact sheet (default: 16)')
    p.add_argument('--strict', action='store_true',
                   help='Exit with code 1 if any FAIL assertion fires')
    p.add_argument('--check-nonblank', type=int, default=0, metavar='N',
                   help='Assert at least N glyphs have non-empty bitmaps')
    p.add_argument('--no-individual', action='store_true',
                   help='Skip saving individual per-glyph PNGs')

    scan = p.add_argument_group('directory scan mode')
    scan.add_argument('--scan-dir', nargs='?', const=str(_DEFAULT_SCAN_DIR),
                      metavar='DIR',
                      help='Scan a directory for *.h files and render all of them. '
                           f'Defaults to: {_DEFAULT_SCAN_DIR}')

    gen = p.add_argument_group('generate header on-the-fly')
    gen.add_argument('--generate-from', metavar='FONT.TTF',
                     help='Font file to pass to fontconvert')
    gen.add_argument('--fontconvert',
                     default='../cmake-build-debug/fontconvert',
                     help='Path to fontconvert binary')
    gen.add_argument('--size', type=int, default=14, help='Point size (default 14)')
    gen.add_argument('--grayscale', '-g', action='store_true',
                     help='Pass -g to fontconvert (grayscale/BGRA mode)')
    gen.add_argument('--variant', '-v', default='_Test_',
                     help='Font variant name (default: _Test_)')
    gen.add_argument('--range', nargs='+', metavar='HEX',
                     help='Codepoint range pairs, e.g. 0x41 0x7e')
    gen.add_argument('--sequence', '-S', metavar='CODEPOINTS',
                     help='Space-separated hex codepoint sequence for -S mode, '
                          'e.g. "1F1E9 1F1EA"')
    return p


def generate_header(args) -> str:
    """Run fontconvert and return path to a temp .h file."""
    cmd = [
        args.fontconvert,
        f'-f{args.generate_from}',
        f'-s{args.size}',
        f'-v{args.variant}',
    ]
    if args.grayscale:
        cmd.append('-g')
    if args.sequence:
        cmd += ['-S', args.sequence]
    elif args.range:
        cmd.extend(args.range)

    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print("fontconvert stderr:", result.stderr, file=sys.stderr)
        sys.exit(f"fontconvert failed (exit {result.returncode})")
    if result.stderr:
        print("fontconvert info:", result.stderr.strip())

    tmp = tempfile.NamedTemporaryFile(suffix='.h', delete=False, mode='w')
    tmp.write(result.stdout)
    tmp.close()
    return tmp.name


def process_font_file(h_path: str, args) -> bool:
    """Parse one .h file, print a summary, write PNGs.  Returns True if any FAIL fired."""
    print(f"\nParsing: {h_path}")
    try:
        font = parse_h_file(h_path)
    except Exception as exc:
        print(f"  ERROR: {exc}")
        return False

    print(f"  Font name : {font['name']}")
    print(f"  Codepoints: 0x{font['first']:X} – 0x{font['last']:X}  "
          f"({len(font['glyphs'])} glyphs)")
    print(f"  Bitmap    : {len(font['bitmap'])} bytes")
    print(f"  yAdvance  : {font['yAdvance']} px")

    issues = run_assertions(font, min_nonblank=args.check_nonblank)
    if issues:
        for issue in issues:
            print(f"  {issue}")
    else:
        print(f"  All OK — {len(font['glyphs'])} glyphs, "
              f"{len(font['bitmap'])} bitmap bytes")

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    sheet_path = str(out_dir / f"{font['name']}_sheet.png")
    w, h = make_sheet_png(font, sheet_path, scale=args.scale, cols=args.cols)
    print(f"  Contact sheet: {sheet_path}  ({w}×{h} px)")

    if not args.no_individual:
        glyph_dir = out_dir / font['name']
        glyph_dir.mkdir(exist_ok=True)
        for i, g in enumerate(font['glyphs']):
            cp = font['first'] + i
            save_glyph_png(g, font['bitmap'],
                           str(glyph_dir / f"U+{cp:04X}.png"),
                           scale=args.scale)
        print(f"  Individual glyphs: {glyph_dir}/  ({len(font['glyphs'])} files)")

    return any(issue.startswith('FAIL') for issue in issues)


def main() -> int:
    args = build_argparser().parse_args()

    if args.scan_dir:
        scan_path = Path(args.scan_dir)
        if not scan_path.is_dir():
            print(f"Error: scan directory not found: {scan_path}", file=sys.stderr)
            return 1
        headers = sorted(scan_path.glob("*.h"))
        if not headers:
            print(f"No .h files found in {scan_path}", file=sys.stderr)
            return 1
        print(f"Scanning {scan_path}  ({len(headers)} headers found)")
        any_fail = False
        for h in headers:
            any_fail |= process_font_file(str(h), args)
        if args.strict and any_fail:
            print("\nExit 1 (--strict, FAIL assertions present)")
            return 1
        return 0

    if args.h_file:
        h_path = args.h_file
    elif args.generate_from:
        h_path = generate_header(args)
    else:
        build_argparser().print_help()
        return 1

    has_fails = process_font_file(h_path, args)
    if args.strict and has_fails:
        print("\nExit 1 (--strict, FAIL assertions present)")
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
