"""
Automated tests for fontconvert output.

Run with:
    cd AdafruitGFX/fontconvert/tests
    pip install pytest pypng
    pytest -v

Dependencies: pypng only — no PIL/Pillow required.

Each test class covers one scenario.  Tests that require the NotoColorEmoji
font (not bundled) are skipped automatically when the font file is absent.

-------------------------------------------------------------------------------
Visual inspection examples (visualize_font.py)
-------------------------------------------------------------------------------
These commands parse checked-in .h headers and write PNG contact sheets to
font_test_output/.  All verified passing as of 2026-04-27.

  GENERATED=../../../../qmk_firmware/keyboards/handwired/polykybd/base/fonts/generated

  # Latin base (0x20–0x7E, 95 glyphs, 2611 bytes)
  python3 visualize_font.py $GENERATED/0NotoSans_Regular_Base_14pt.h --out-dir font_test_output/

  # Greek (0x384–0x3C9, 70 glyphs, 2005 bytes)
  python3 visualize_font.py $GENERATED/NotoSans_Regular_Greek_14pt.h --out-dir font_test_output/

  # Cyrillic (0x401–0x4E9, 233 glyphs, 8325 bytes)
  python3 visualize_font.py $GENERATED/NotoSans_Regular_Cyrillic_16pt.h --out-dir font_test_output/

  # Hebrew (0x5D0–0x5F4, 37 glyphs, 898 bytes)
  python3 visualize_font.py $GENERATED/NotoSans_Medium_Hebrew_16pt.h --out-dir font_test_output/

  # Arabic — WARNs expected: diacritics (0x64B–0x65F) have xAdvance=0 by design
  python3 visualize_font.py $GENERATED/NotoSansAR_Regular_Isolated_16pt.h --out-dir font_test_output/

  # Hiragana (0x3041–0x309F, 95 glyphs) — two combining marks have xAdvance=0 (correct)
  python3 visualize_font.py $GENERATED/NotoSansJP_Regular_Hiragana_15pt.h --out-dir font_test_output/

  # Emoji: brightness (8 glyphs, offset-mapped to 0xE311)
  python3 visualize_font.py $GENERATED/5NotoEmoji_Medium_Brightness_16pt.h --out-dir font_test_output/

  # Emoji: face set (0xF600–0xF64F, 80 glyphs, 15890 bytes)
  python3 visualize_font.py $GENERATED/6NotoEmoji_Medium_Emoji0_20pt.h --out-dir font_test_output/

  # Chess symbols (0x2654–0x265F, 12 glyphs)
  python3 visualize_font.py $GENERATED/7NotoSansSymbols2_Regular_Chess_20pt.h --out-dir font_test_output/

  # Arrows (0x2B6F–0x2BA0, 50 glyphs — sparse range, most are blanks by design)
  python3 visualize_font.py $GENERATED/3NotoSansSymbols2_Regular_Arrows_20pt.h --out-dir font_test_output/
"""

import itertools
import subprocess
import tempfile
from pathlib import Path

import pytest

from visualize_font import (
    parse_h_file,
    run_assertions,
    glyph_to_rows,
    make_sheet_png,
    save_glyph_png,
)

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

FONTCONVERT = Path(__file__).parent.parent / "cmake-build-debug" / "fontconvert"
DEJAVU      = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
NOTO_COLOR  = Path(__file__).parent.parent.parent.parent / \
              "qmk_firmware/keyboards/handwired/polykybd/fonts/Noto_CEmoji/NotoColorEmoji-Regular.ttf"
FIXTURE_BASE = Path(__file__).parent.parent.parent.parent / \
               "qmk_firmware/keyboards/handwired/polykybd/base/fonts/generated/" \
               "0NotoSans_Regular_Base_14pt.h"
FONTS_ROOT   = Path(__file__).parent.parent.parent.parent / \
               "qmk_firmware/keyboards/handwired/polykybd/base/fonts"

def _all_font_headers():
    """All *.h files under FONTS_ROOT that contain a GFXfont Bitmaps array."""
    if not FONTS_ROOT.exists():
        return []
    return sorted(
        p for p in FONTS_ROOT.rglob("*.h")
        if "Bitmaps[]" in p.read_text(encoding="utf-8", errors="replace")
    )


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def run_fontconvert(*extra_args) -> str:
    if not FONTCONVERT.exists():
        pytest.skip(f"fontconvert binary not found at {FONTCONVERT}")
    result = subprocess.run([str(FONTCONVERT)] + list(extra_args),
                            capture_output=True, text=True)
    assert result.returncode == 0, f"fontconvert failed:\n{result.stderr}"
    return result.stdout


def h_to_font(content: str) -> dict:
    with tempfile.NamedTemporaryFile(suffix='.h', delete=False, mode='w') as f:
        f.write(content)
        f.flush()
    return parse_h_file(f.name)


# ---------------------------------------------------------------------------
# Test: parser against checked-in fixture
# ---------------------------------------------------------------------------

class TestFixtureParse:
    @pytest.mark.skipif(not FIXTURE_BASE.exists(), reason="fixture file missing")
    def test_parse_existing_header(self):
        font = parse_h_file(str(FIXTURE_BASE))
        assert font['name'] == 'NotoSans_Regular_Base_14pt7b'
        assert font['first'] == 0x20
        assert font['last']  == 0x7e
        assert len(font['glyphs']) == 0x7e - 0x20 + 1
        assert len(font['bitmap']) > 0

    @pytest.mark.skipif(not FIXTURE_BASE.exists(), reason="fixture file missing")
    def test_no_fail_assertions_on_fixture(self):
        font = parse_h_file(str(FIXTURE_BASE))
        fails = [i for i in run_assertions(font) if i.startswith('FAIL')]
        assert fails == [], f"Unexpected FAIL assertions: {fails}"

    @pytest.mark.skipif(not FIXTURE_BASE.exists(), reason="fixture file missing")
    def test_most_glyphs_have_bitmap_content(self):
        font = parse_h_file(str(FIXTURE_BASE))
        printable = [g for g in font['glyphs'] if g['width'] > 0]
        nonblank = sum(
            1 for g in printable
            if any(font['bitmap'][
                g['bitmapOffset']:
                g['bitmapOffset'] + (g['width'] * g['height'] + 7) // 8
            ])
        )
        assert nonblank >= len(printable) * 0.8, \
            f"Only {nonblank}/{len(printable)} printable glyphs have bitmap content"


# ---------------------------------------------------------------------------
# Test: monochrome rendering (A–Z, DejaVuSans)
# ---------------------------------------------------------------------------

@pytest.mark.skipif(not DEJAVU.exists(), reason="DejaVuSans not installed")
class TestMonoRendering:
    def _font(self):
        return h_to_font(run_fontconvert(f'-f{DEJAVU}', '-s14', '-v_PyTest_',
                                         '0x41', '0x5a'))

    def test_glyph_count(self):
        assert len(self._font()['glyphs']) == 26

    def test_all_glyphs_nonzero_size(self):
        font = self._font()
        for i, g in enumerate(font['glyphs']):
            assert g['width'] > 0 and g['height'] > 0, \
                f"Glyph {i} (U+{0x41+i:X}) has zero dimensions"

    def test_bitmap_offsets_in_bounds(self):
        font = self._font()
        for i, g in enumerate(font['glyphs']):
            end = g['bitmapOffset'] + (g['width'] * g['height'] + 7) // 8
            assert end <= len(font['bitmap']), \
                f"Glyph {i} overflows bitmap"

    def test_all_glyphs_have_content(self):
        font = self._font()
        for i, g in enumerate(font['glyphs']):
            start = g['bitmapOffset']
            end   = start + (g['width'] * g['height'] + 7) // 8
            assert any(font['bitmap'][start:end]), \
                f"Glyph {i} (U+{0x41+i:04X}) bitmap is all zeros"

    def test_no_fail_assertions(self):
        fails = [i for i in run_assertions(self._font()) if i.startswith('FAIL')]
        assert fails == []

    def test_glyph_to_rows_produces_valid_scanlines(self):
        font = self._font()
        g = font['glyphs'][0]  # 'A'
        rows = glyph_to_rows(g, font['bitmap'], scale=4)
        assert len(rows) == g['height'] * 4
        assert all(len(r) == g['width'] * 4 for r in rows)
        assert all(px in (0, 255) for row in rows for px in row)

    def test_sheet_png_writes(self, tmp_path):
        font = self._font()
        out = str(tmp_path / "sheet.png")
        w, h = make_sheet_png(font, out, scale=4, cols=8)
        assert Path(out).exists()
        assert w > 0 and h > 0


# ---------------------------------------------------------------------------
# Test: grayscale rendering
# ---------------------------------------------------------------------------

@pytest.mark.skipif(not DEJAVU.exists(), reason="DejaVuSans not installed")
class TestGrayscaleRendering:
    def _font(self):
        return h_to_font(run_fontconvert(f'-f{DEJAVU}', '-s14', '-g',
                                         '-v_PyTestGray_', '0x41', '0x5a'))

    def test_glyph_count(self):
        assert len(self._font()['glyphs']) == 26

    def test_no_fail_assertions(self):
        fails = [i for i in run_assertions(self._font()) if i.startswith('FAIL')]
        assert fails == []

    def test_all_glyphs_have_content(self):
        font = self._font()
        for i, g in enumerate(font['glyphs']):
            start = g['bitmapOffset']
            end   = start + (g['width'] * g['height'] + 7) // 8
            assert any(font['bitmap'][start:end]), \
                f"Glyph {i} (U+{0x41+i:04X}) grayscale bitmap is all zeros"


# ---------------------------------------------------------------------------
# Test: HarfBuzz sequence mode (-S flag)
# ---------------------------------------------------------------------------

@pytest.mark.skipif(not DEJAVU.exists(), reason="DejaVuSans not installed")
class TestSequenceMode:
    def _font_abc(self):
        """A B C are 3 separate glyphs — DejaVuSans won't ligate them."""
        return h_to_font(run_fontconvert(f'-f{DEJAVU}', '-s14', '-g',
                                         '-v_Seq_', '-S', '0x41 0x42 0x43'))

    def test_three_codepoints_give_three_glyphs(self):
        assert len(self._font_abc()['glyphs']) == 3

    def test_sequence_glyphs_have_content(self):
        font = self._font_abc()
        for i, g in enumerate(font['glyphs']):
            start = g['bitmapOffset']
            end   = start + (g['width'] * g['height'] + 7) // 8
            assert any(font['bitmap'][start:end]), \
                f"Sequence glyph {i} bitmap is all zeros"

    def test_font_struct_first_is_zero(self):
        assert self._font_abc()['first'] == 0

    def test_no_fail_assertions(self):
        fails = [i for i in run_assertions(self._font_abc()) if i.startswith('FAIL')]
        assert fails == []

    def test_individual_png_writes(self, tmp_path):
        font = self._font_abc()
        for i, g in enumerate(font['glyphs']):
            p = str(tmp_path / f"seq_{i}.png")
            save_glyph_png(g, font['bitmap'], p, scale=4)
            assert Path(p).exists()


# ---------------------------------------------------------------------------
# Test: comma-separated -S syntax
# ---------------------------------------------------------------------------

@pytest.mark.skipif(not DEJAVU.exists(), reason="DejaVuSans not installed")
class TestCommaSequence:
    """
    Validates the comma-as-glyph-separator extension of -S.

    Rules under test:
      - Each comma group is shaped as an independent HarfBuzz call.
      - Spaces within a group are codepoints for that one glyph.
      - Trailing / empty comma groups are silently skipped.
      - For a non-ligating font (DejaVuSans Latin), comma-separated single
        codepoints give the same glyph count as the space-only equivalent.
    """

    def _run(self, seq):
        return h_to_font(run_fontconvert(
            f'-f{DEJAVU}', '-s14', '-g', '-v_CSeq_', '-S', seq
        ))

    def test_three_comma_glyphs_count(self):
        """'A, B, C' — three comma groups each with one codepoint → 3 glyphs."""
        assert len(self._run('41, 42, 43')['glyphs']) == 3

    def test_two_comma_groups_with_multi_cp_first(self):
        """'A B, C' — first group has two codepoints, second one → still 3 glyphs
        (DejaVuSans has no AB ligature, so HarfBuzz emits A and B separately)."""
        assert len(self._run('41 42, 43')['glyphs']) == 3

    def test_comma_matches_space_for_nonligating_font(self):
        """For Latin letters in DejaVuSans, '41, 42, 43' and '41 42 43' produce
        the same number of glyphs because no clusters form."""
        assert (len(self._run('41, 42, 43')['glyphs']) ==
                len(self._run('41 42 43')['glyphs']))

    def test_trailing_comma_ignored(self):
        """'41, 42,' — trailing empty group must not produce an extra slot."""
        assert len(self._run('41, 42,')['glyphs']) == 2

    def test_comma_glyphs_have_bitmap_content(self):
        """Every glyph from a comma-separated sequence must have actual pixels."""
        font = self._run('41, 42, 43, 44, 45')  # A B C D E
        for i, g in enumerate(font['glyphs']):
            start = g['bitmapOffset']
            end   = start + (g['width'] * g['height'] + 7) // 8
            assert any(font['bitmap'][start:end]), \
                f"Glyph {i} from comma sequence has empty bitmap"

    def test_comma_no_fail_assertions(self):
        fails = [m for m in run_assertions(self._run('41, 42, 43'))
                 if m.startswith('FAIL')]
        assert fails == []

    def test_mixed_comma_and_space_offsets_in_bounds(self):
        """Bitmap offsets must stay within the flat bitmap array."""
        font = self._run('41 42, 43 44, 45')  # groups: AB, CD, E
        for i, g in enumerate(font['glyphs']):
            end = g['bitmapOffset'] + (g['width'] * g['height'] + 7) // 8
            assert end <= len(font['bitmap']), \
                f"Glyph {i} offset+size overflows bitmap"


# ---------------------------------------------------------------------------
# Test: BGRA color-emoji path (NotoColorEmoji) — skipped until font present
# ---------------------------------------------------------------------------

@pytest.mark.skipif(not NOTO_COLOR.exists(),
                    reason="NotoColorEmoji font not present — "
                           "download it to fonts/Noto_CEmoji/")
class TestColorEmoji:
    def _font(self):
        return h_to_font(run_fontconvert(
            f'-f{NOTO_COLOR}', '-s20', '-g', '-r50',
            '-v_PyTestEmoji_', '0x1f600', '0x1f60f'
        ))

    def test_glyph_count(self):
        assert len(self._font()['glyphs']) == 0x1f60f - 0x1f600 + 1

    def test_no_fail_assertions(self):
        fails = [i for i in run_assertions(self._font()) if i.startswith('FAIL')]
        assert fails == []

    def test_emoji_bitmaps_not_all_zeros(self):
        """Core BGRA regression: pre-fix, all emoji pixels were 0."""
        font = self._font()
        nonblank = sum(
            1 for g in font['glyphs']
            if g['width'] > 0 and g['height'] > 0 and
               any(font['bitmap'][
                   g['bitmapOffset']:
                   g['bitmapOffset'] + (g['width'] * g['height'] + 7) // 8
               ])
        )
        assert nonblank >= len(font['glyphs']) * 0.8, \
            f"Only {nonblank}/{len(font['glyphs'])} emoji have bitmap content — " \
            "BGRA extraction may still be broken"

    def test_emoji_dimensions_reasonable(self):
        font = self._font()
        for i, g in enumerate(font['glyphs']):
            assert 5 <= g['width'] <= 200, \
                f"U+{0x1f600+i:X} width={g['width']} outside expected range"
            assert 5 <= g['height'] <= 200, \
                f"U+{0x1f600+i:X} height={g['height']} outside expected range"

    def test_flag_sequence_via_harfbuzz(self):
        """German flag via regional indicator pair — requires HarfBuzz shaping."""
        font = h_to_font(run_fontconvert(
            f'-f{NOTO_COLOR}', '-s20', '-g', '-r50',
            '-v_FlagDE_', '-S', '1F1E9 1F1EA'
        ))
        assert len(font['glyphs']) >= 1
        g = font['glyphs'][0]
        assert g['width'] > 0 and g['height'] > 0, \
            "Flag glyph has zero dimensions — HarfBuzz shaping may have failed"
        start = g['bitmapOffset']
        end   = start + (g['width'] * g['height'] + 7) // 8
        assert any(font['bitmap'][start:end]), \
            "Flag glyph bitmap is all zeros — BGRA+HarfBuzz path broken"


# ---------------------------------------------------------------------------
# Test: all Unicode country flags — color→B/W rasterization contact sheet
# ---------------------------------------------------------------------------

# ISO 3166-1 alpha-2 codes whose flags NotoColorEmoji supports.
_FLAG_CODES = [
    'AC','AD','AE','AF','AG','AI','AL','AM','AO','AQ','AR','AS','AT','AU',
    'AW','AX','AZ','BA','BB','BD','BE','BF','BG','BH','BI','BJ','BL','BM',
    'BN','BO','BQ','BR','BS','BT','BV','BW','BY','BZ','CA','CC','CD','CF',
    'CG','CH','CI','CK','CL','CM','CN','CO','CP','CR','CU','CV','CW','CX',
    'CY','CZ','DE','DG','DJ','DK','DM','DO','DZ','EA','EC','EE','EG','EH',
    'ER','ES','ET','EU','FI','FJ','FK','FM','FO','FR','GA','GB','GD','GE',
    'GF','GG','GH','GI','GL','GM','GN','GP','GQ','GR','GS','GT','GU','GW',
    'GY','HK','HM','HN','HR','HT','HU','IC','ID','IE','IL','IM','IN','IO',
    'IQ','IR','IS','IT','JE','JM','JO','JP','KE','KG','KH','KI','KM','KN',
    'KP','KR','KW','KY','KZ','LA','LB','LC','LI','LK','LR','LS','LT','LU',
    'LV','LY','MA','MC','MD','ME','MF','MG','MH','MK','ML','MM','MN','MO',
    'MP','MQ','MR','MS','MT','MU','MV','MW','MX','MY','MZ','NA','NC','NE',
    'NF','NG','NI','NL','NO','NP','NR','NU','NZ','OM','PA','PE','PF','PG',
    'PH','PK','PL','PM','PN','PR','PS','PT','PW','PY','QA','RE','RO','RS',
    'RU','RW','SA','SB','SC','SD','SE','SG','SH','SI','SJ','SK','SL','SM',
    'SN','SO','SR','SS','ST','SV','SX','SY','SZ','TA','TC','TD','TF','TG',
    'TH','TJ','TK','TL','TM','TN','TO','TR','TT','TV','TW','TZ','UA','UG',
    'UM','UN','US','UY','UZ','VA','VC','VE','VG','VI','VN','VU','WF','WS',
    'XK','YE','YT','ZA','ZM','ZW',
]


def _ri(letter: str) -> int:
    return 0x1F1E6 + (ord(letter) - ord('A'))


def _flag_seq(code: str) -> str:
    return f'{_ri(code[0]):X} {_ri(code[1]):X}'


def _all_flags_sequence() -> str:
    """Comma-separated flag sequences: each pair is shaped independently by HarfBuzz."""
    return ', '.join(_flag_seq(cc) for cc in _FLAG_CODES)


@pytest.mark.skipif(not NOTO_COLOR.exists(),
                    reason="NotoColorEmoji font not present at "
                           f"{NOTO_COLOR}")
class TestColorEmojiFlags:
    """
    Generates all ISO 3166-1 alpha-2 country flags in a single fontconvert
    call (-S with all regional-indicator pairs) and writes a contact-sheet PNG.

    Visual inspection:
        cd AdafruitGFX/fontconvert/tests
        pytest -v -k TestColorEmojiFlags
        # → font_test_output/NotoColorEmoji_Regular_Flags_20pt*_sheet.png
    """

    def _font(self):
        seq = _all_flags_sequence()
        return h_to_font(run_fontconvert(
            f'-f{NOTO_COLOR}', '-s20', '-g', '-r36', '-W60',
            '-v_Flags_', '-S', seq
        ))

    def test_flag_count(self):
        font = self._font()
        assert len(font['glyphs']) == len(_FLAG_CODES), \
            f"Expected {len(_FLAG_CODES)} flag glyphs, got {len(font['glyphs'])}"

    def test_all_flags_have_content(self):
        font = self._font()
        blank = []
        for i, g in enumerate(font['glyphs']):
            if g['width'] <= 0 or g['height'] <= 0:
                blank.append(_FLAG_CODES[i])
                continue
            start = g['bitmapOffset']
            end   = start + (g['width'] * g['height'] + 7) // 8
            if not any(font['bitmap'][start:end]):
                blank.append(_FLAG_CODES[i])
        assert not blank, f"Flags with empty bitmaps: {blank}"

    def test_flags_sheet_png(self, tmp_path):
        """Write a contact sheet and verify it was created with non-zero size."""
        font = self._font()
        out = str(tmp_path / 'flags_sheet.png')
        w, h = make_sheet_png(font, out, scale=4, cols=20)
        assert Path(out).exists()
        assert w > 0 and h > 0
        print(f"\n  Contact sheet: {out}  ({w}×{h} px, {len(font['glyphs'])} flags)")

    def test_generate_flags_sheet_to_output_dir(self):
        """Write a persistent contact sheet to font_test_output/ for visual inspection."""
        font = self._font()
        out_dir = Path(__file__).parent / 'font_test_output'
        out_dir.mkdir(exist_ok=True)
        sheet_path = str(out_dir / f'{font["name"]}_sheet.png')
        w, h = make_sheet_png(font, sheet_path, scale=4, cols=20)
        print(f"\n  Flags sheet written: {sheet_path}  ({w}×{h} px)")
        assert Path(sheet_path).exists()


# ---------------------------------------------------------------------------
# Test: flags rendered with every dithering mode × exposure combination
# ---------------------------------------------------------------------------

_DITHER_MODES  = ['fs', 'stucki', 'bayer', 'threshold', 'random']
_EXPOSURES     = [-0.1, 0.0, 0.1]
_FLAG_VARIANTS = list(itertools.product(_DITHER_MODES, _EXPOSURES))


@pytest.mark.skipif(not NOTO_COLOR.exists(),
                    reason="NotoColorEmoji font not present at "
                           f"{NOTO_COLOR}")
class TestFlagDitheringVariants:
    """
    Renders all country flags for every combination of dithering algorithm
    and exposure value, writing a contact-sheet PNG for each to
    font_test_output/ for visual comparison.

    15 combinations: {fs, stucki, bayer, threshold, random} × {-0.1, 0.0, +0.1}
    """

    @pytest.mark.parametrize(
        "dither,exposure",
        _FLAG_VARIANTS,
        ids=[f"{d}_e{e:+.1f}" for d, e in _FLAG_VARIANTS],
    )
    def test_flags_dither_sheet(self, dither, exposure):
        seq = _all_flags_sequence()
        font = h_to_font(run_fontconvert(
            f'-f{NOTO_COLOR}', '-s20', '-g', '-r36', '-W60',
            f'-D{dither}', f'-e{exposure:.2f}',
            '-v_Flags_', '-S', seq,
        ))

        assert len(font['glyphs']) == len(_FLAG_CODES), \
            f"[{dither} e{exposure:+.1f}] Expected {len(_FLAG_CODES)} glyphs, " \
            f"got {len(font['glyphs'])}"

        out_dir = Path(__file__).parent / 'font_test_output'
        out_dir.mkdir(exist_ok=True)
        sheet_path = out_dir / f"flags_{dither}_e{exposure:+.1f}_sheet.png"
        w, h = make_sheet_png(font, str(sheet_path), scale=4, cols=20)
        assert sheet_path.exists() and w > 0 and h > 0
        print(f"\n  {sheet_path.name}  ({w}×{h} px)")


# ---------------------------------------------------------------------------
# Test: outline option (-O N)
# ---------------------------------------------------------------------------

@pytest.mark.skipif(not DEJAVU.exists(), reason="DejaVuSans not installed")
class TestOutline:
    """
    Tests the -O N outline option.

    For BGRA (color emoji/flags): forces the inner N-pixel ring of every content
    pixel (alpha > 0) to lit, creating a bright frame around the emoji regardless
    of what colour its edge dithered to.
    For mono/gray text: morphological dilation — dark pixels adjacent to lit pixels
    become lit.  Original lit pixels are never cleared in either mode.

    glyph_to_rows convention (matches physical OLED): bit=1 (lit) → pixel=255,
    bit=0 (dark/off) → pixel=0.
    """

    # 'M' (0x4D) at size 20 has clear strokes and surrounding dark background
    # pixels — ideal for testing that the dilation halo appears correctly.
    _CP = '0x4D'

    def _outline(self, thickness):
        return h_to_font(run_fontconvert(
            f'-f{DEJAVU}', '-s20', '-v_Outline_',
            f'-O{thickness}', self._CP, self._CP
        ))

    def _plain(self):
        return h_to_font(run_fontconvert(
            f'-f{DEJAVU}', '-s20', '-v_Plain_',
            self._CP, self._CP
        ))

    def _rows(self, font):
        return glyph_to_rows(font['glyphs'][0], font['bitmap'], scale=1)

    def test_dimensions_unchanged(self):
        """Dilation must not alter glyph width or height."""
        g_out   = self._outline(1)['glyphs'][0]
        g_plain = self._plain()['glyphs'][0]
        assert g_out['width']  == g_plain['width'],  "-O 1 changed glyph width"
        assert g_out['height'] == g_plain['height'], "-O 1 changed glyph height"

    def test_original_lit_pixels_preserved(self):
        """Every pixel lit in the plain render must remain lit after -O 1."""
        rows_p = self._rows(self._plain())
        rows_o = self._rows(self._outline(1))
        h, w = len(rows_p), len(rows_p[0])
        for y in range(h):
            for x in range(w):
                if rows_p[y][x] == 255:  # lit in plain (255 = white/lit on OLED)
                    assert rows_o[y][x] == 255, \
                        f"Pixel ({x},{y}) was lit in plain but cleared by -O 1"

    def test_dark_neighbors_of_lit_become_lit(self):
        """Dark pixels adjacent (8-connected) to lit pixels must be lit after -O 1."""
        plain   = self._plain()
        outline = self._outline(1)
        g = plain['glyphs'][0]
        rows_p = self._rows(plain)
        rows_o = glyph_to_rows(g, outline['bitmap'], scale=1)
        h, w = g['height'], g['width']
        checked = 0
        for y in range(h):
            for x in range(w):
                if rows_p[y][x] != 0:  # lit in plain — skip (0 = dark/off on OLED)
                    continue
                # Dark in plain — does any 8-neighbour in plain have a lit pixel?
                adjacent = False
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        if dy == 0 and dx == 0:
                            continue
                        ny, nx = y + dy, x + dx
                        if 0 <= ny < h and 0 <= nx < w and rows_p[ny][nx] == 255:
                            adjacent = True
                if adjacent:
                    assert rows_o[y][x] == 255, \
                        f"Dark pixel ({x},{y}) adjacent to lit content was not set by -O 1"
                    checked += 1
        assert checked > 0, "No dark-adjacent-to-lit pixels found — test glyph too small?"

    def test_outline_expands_lit_region(self):
        """The outline version must have strictly more lit pixels than plain."""
        rows_p = self._rows(self._plain())
        rows_o = self._rows(self._outline(1))
        lit_plain   = sum(px == 255 for row in rows_p for px in row)
        lit_outline = sum(px == 255 for row in rows_o for px in row)
        assert lit_outline > lit_plain, \
            f"Outline ({lit_outline} lit) has no more pixels than plain ({lit_plain} lit)"

    def test_larger_thickness_expands_more(self):
        """O2 must produce at least as many lit pixels as O1."""
        rows_1 = self._rows(self._outline(1))
        rows_2 = self._rows(self._outline(2))
        lit_1 = sum(px == 255 for row in rows_1 for px in row)
        lit_2 = sum(px == 255 for row in rows_2 for px in row)
        assert lit_2 >= lit_1, \
            f"-O 2 ({lit_2} lit) has fewer lit pixels than -O 1 ({lit_1} lit)"

    def test_zero_outline_is_noop(self):
        """-O 0 must produce the identical bitmap as no -O flag."""
        font_plain = self._plain()
        font_zero  = h_to_font(run_fontconvert(
            f'-f{DEJAVU}', '-s20', '-v_Zero_',
            '-O0', self._CP, self._CP
        ))
        g_plain = font_plain['glyphs'][0]
        g_zero  = font_zero['glyphs'][0]
        assert g_plain['width']  == g_zero['width']
        assert g_plain['height'] == g_zero['height']
        plain_bits = font_plain['bitmap'][
            g_plain['bitmapOffset']:
            g_plain['bitmapOffset'] + (g_plain['width'] * g_plain['height'] + 7) // 8
        ]
        zero_bits = font_zero['bitmap'][
            g_zero['bitmapOffset']:
            g_zero['bitmapOffset'] + (g_zero['width'] * g_zero['height'] + 7) // 8
        ]
        assert list(plain_bits) == list(zero_bits), \
            "-O 0 produced a different bitmap from no -O flag"

    def test_outline_contact_sheet(self, tmp_path):
        """Write a contact sheet PNG of outlined A–Z for visual inspection."""
        font = h_to_font(run_fontconvert(
            f'-f{DEJAVU}', '-s20', '-v_OutlineAZ_',
            '-O1', '0x41', '0x5a'
        ))
        out = str(tmp_path / "outline_az.png")
        w, h = make_sheet_png(font, out, scale=4, cols=8)
        assert Path(out).exists()
        assert w > 0 and h > 0

    @pytest.mark.skipif(not NOTO_COLOR.exists(),
                        reason="NotoColorEmoji font not present")
    def test_flag_outline_preserves_and_expands(self):
        """German flag with -O 1: all plain-lit pixels preserved, more lit overall."""
        plain   = h_to_font(run_fontconvert(
            f'-f{NOTO_COLOR}', '-s20', '-g', '-r36', '-W60',
            '-v_FlagDE_', '-S', '1F1E9 1F1EA'
        ))
        outline = h_to_font(run_fontconvert(
            f'-f{NOTO_COLOR}', '-s20', '-g', '-r36', '-W60',
            '-O1', '-v_FlagDE_', '-S', '1F1E9 1F1EA'
        ))
        assert len(plain['glyphs']) >= 1 and len(outline['glyphs']) >= 1
        g = plain['glyphs'][0]
        assert g['width'] == outline['glyphs'][0]['width']
        assert g['height'] == outline['glyphs'][0]['height']
        rows_p = glyph_to_rows(g,                    plain['bitmap'],   scale=1)
        rows_o = glyph_to_rows(outline['glyphs'][0], outline['bitmap'], scale=1)
        lit_p = sum(px == 255 for row in rows_p for px in row)
        lit_o = sum(px == 255 for row in rows_o for px in row)
        # All plain-lit pixels must remain lit
        for y in range(g['height']):
            for x in range(g['width']):
                if rows_p[y][x] == 255:
                    assert rows_o[y][x] == 255, \
                        f"Flag pixel ({x},{y}) lit in plain was cleared by -O 1"
        # The outline version must have at least as many lit pixels
        assert lit_o >= lit_p, \
            f"Flag outline ({lit_o} lit) has fewer lit pixels than plain ({lit_p} lit)"


# ---------------------------------------------------------------------------
# Test: outline PNG contact sheets for visual inspection
# ---------------------------------------------------------------------------

_OUTLINE_THICKNESSES = [0, 1, 2, 3]


@pytest.mark.skipif(not DEJAVU.exists(), reason="DejaVuSans not installed")
class TestOutlinePNGs:
    """
    Writes contact-sheet PNGs to font_test_output/ for every outline thickness
    variant so the effect can be inspected visually.

    Visual inspection:
        cd AdafruitGFX/fontconvert/tests
        pytest -v -k TestOutlinePNGs
        # → font_test_output/outline_O{N}_az_sheet.png  (N = 0..3)
        # → font_test_output/…_O{N}_flags_sheet.png      (if NotoColorEmoji present)
    """

    @pytest.mark.parametrize(
        "thickness", _OUTLINE_THICKNESSES,
        ids=[f"O{t}" for t in _OUTLINE_THICKNESSES],
    )
    def test_mono_az_sheet(self, thickness):
        """A–Z monochrome at each outline thickness → font_test_output/."""
        font = h_to_font(run_fontconvert(
            f'-f{DEJAVU}', '-s20', '-v_Outline_',
            f'-O{thickness}', '0x41', '0x5a'
        ))
        out_dir = Path(__file__).parent / 'font_test_output'
        out_dir.mkdir(exist_ok=True)
        path = out_dir / f'outline_O{thickness}_az_sheet.png'
        w, h = make_sheet_png(font, str(path), scale=4, cols=8)
        assert path.exists() and w > 0 and h > 0
        print(f"\n  {path.name}  ({w}×{h} px)")

    @pytest.mark.parametrize(
        "thickness", _OUTLINE_THICKNESSES,
        ids=[f"O{t}" for t in _OUTLINE_THICKNESSES],
    )
    def test_gray_az_sheet(self, thickness):
        """A–Z grayscale at each outline thickness → font_test_output/."""
        font = h_to_font(run_fontconvert(
            f'-f{DEJAVU}', '-s20', '-g', '-v_OutlineGray_',
            f'-O{thickness}', '0x41', '0x5a'
        ))
        out_dir = Path(__file__).parent / 'font_test_output'
        out_dir.mkdir(exist_ok=True)
        path = out_dir / f'outline_gray_O{thickness}_az_sheet.png'
        w, h = make_sheet_png(font, str(path), scale=4, cols=8)
        assert path.exists() and w > 0 and h > 0
        print(f"\n  {path.name}  ({w}×{h} px)")

    @pytest.mark.skipif(not NOTO_COLOR.exists(),
                        reason="NotoColorEmoji font not present")
    @pytest.mark.parametrize(
        "thickness", _OUTLINE_THICKNESSES,
        ids=[f"O{t}" for t in _OUTLINE_THICKNESSES],
    )
    def test_flags_sheet(self, thickness):
        """All country flags at each outline thickness → font_test_output/."""
        seq = _all_flags_sequence()
        font = h_to_font(run_fontconvert(
            f'-f{NOTO_COLOR}', '-s20', '-g', '-r36', '-W60',
            f'-O{thickness}', '-v_Flags_', '-S', seq
        ))
        assert len(font['glyphs']) == len(_FLAG_CODES), \
            f"[O{thickness}] Expected {len(_FLAG_CODES)} flags, got {len(font['glyphs'])}"
        out_dir = Path(__file__).parent / 'font_test_output'
        out_dir.mkdir(exist_ok=True)
        path = out_dir / f'{font["name"]}_O{thickness}_flags_sheet.png'
        w, h = make_sheet_png(font, str(path), scale=4, cols=20)
        assert path.exists() and w > 0 and h > 0
        print(f"\n  {path.name}  ({w}×{h} px, {len(font['glyphs'])} flags)")


# ---------------------------------------------------------------------------
# Test: lang_layer flags (from lang_lut.h) × outline thicknesses
# ---------------------------------------------------------------------------

# Country codes extracted from enum lang_layer in
# qmk_firmware/keyboards/handwired/polykybd/lang/lang_lut.h
# Each LANG_XXYY entry uses YY as the ISO 3166-1 alpha-2 country code.
_LANG_LAYER_COUNTRIES = [
    'US',  # LANG_ENUS
    'DE',  # LANG_DEDE
    'FR',  # LANG_FRFR
    'ES',  # LANG_ESES
    'PT',  # LANG_PTPT
    'IT',  # LANG_ITIT
    'TR',  # LANG_TRTR
    'KR',  # LANG_KOKR
    'JP',  # LANG_JAJP
    'SA',  # LANG_ARSA
    'GR',  # LANG_ELGR
    'UA',  # LANG_UKUA
    'RU',  # LANG_RURU
    'BY',  # LANG_BEBY
    'KZ',  # LANG_KKKZ
    'BG',  # LANG_BGBG
    'PL',  # LANG_PLPL
    'RO',  # LANG_RORO
    'CN',  # LANG_ZHCN
    'NL',  # LANG_NLNL
    'IL',  # LANG_HEIL
    'SE',  # LANG_SVSE
    'FI',  # LANG_FIFI
    'NO',  # LANG_NNNO
    'DK',  # LANG_DADK
    'HU',  # LANG_HUHU
    'CZ',  # LANG_CSCZ
]

_LANG_FLAG_SEQUENCE = ', '.join(_flag_seq(cc) for cc in _LANG_LAYER_COUNTRIES)

_LANG_OUTLINE_THICKNESSES = [0, 1, 2]
_LANG_CONTRASTS = [0.5, 1.0, 1.5, 2.0]


@pytest.mark.skipif(not NOTO_COLOR.exists(),
                    reason="NotoColorEmoji font not present at "
                           f"{NOTO_COLOR}")
class TestLangLayerFlags:
    """
    Renders all country flags from the lang_layer enum in lang_lut.h for three
    outline thicknesses (0, 1, 2) and writes a contact-sheet PNG for each to
    font_test_output/ for visual comparison.

    Flags correspond to the 27 keyboard language layers: US, DE, FR, ES, PT,
    IT, TR, KR, JP, SA, GR, UA, RU, BY, KZ, BG, PL, RO, CN, NL, IL, SE, FI,
    NO, DK, HU, CZ.

    Visual inspection:
        cd AdafruitGFX/fontconvert/tests
        pytest -v -k TestLangLayerFlags
        # → font_test_output/lang_flags_O{N}_sheet.png  (N = 0, 1, 2)
    """

    @pytest.mark.parametrize(
        "thickness", _LANG_OUTLINE_THICKNESSES,
        ids=[f"O{t}" for t in _LANG_OUTLINE_THICKNESSES],
    )
    def test_lang_flags_sheet(self, thickness):
        """All lang_layer flags at the given outline thickness → font_test_output/."""
        extra = [f'-O{thickness}'] if thickness > 0 else []
        font = h_to_font(run_fontconvert(
            f'-f{NOTO_COLOR}', '-s20', '-g', '-r36', '-W60',
            *extra, '-v_LangFlags_', '-S', _LANG_FLAG_SEQUENCE,
        ))
        assert len(font['glyphs']) == len(_LANG_LAYER_COUNTRIES), (
            f"[O{thickness}] Expected {len(_LANG_LAYER_COUNTRIES)} flags, "
            f"got {len(font['glyphs'])}"
        )
        blank = [
            _LANG_LAYER_COUNTRIES[i]
            for i, g in enumerate(font['glyphs'])
            if g['width'] <= 0 or g['height'] <= 0 or not any(
                font['bitmap'][
                    g['bitmapOffset']:
                    g['bitmapOffset'] + (g['width'] * g['height'] + 7) // 8
                ]
            )
        ]
        assert not blank, f"[O{thickness}] Flags with empty bitmaps: {blank}"

        out_dir = Path(__file__).parent / 'font_test_output'
        out_dir.mkdir(exist_ok=True)
        path = out_dir / f'lang_flags_O{thickness}_sheet.png'
        w, h = make_sheet_png(font, str(path), scale=4, cols=9)
        assert path.exists() and w > 0 and h > 0
        print(f"\n  {path.name}  ({w}×{h} px, {len(font['glyphs'])} flags)")

    @pytest.mark.parametrize(
        "dither,exposure",
        _FLAG_VARIANTS,
        ids=[f"{d}_e{e:+.1f}" for d, e in _FLAG_VARIANTS],
    )
    def test_lang_flags_O1_dither_sheet(self, dither, exposure):
        """All lang_layer flags with outline=1, each dither mode × exposure → font_test_output/."""
        font = h_to_font(run_fontconvert(
            f'-f{NOTO_COLOR}', '-s20', '-g', '-r36', '-W60',
            '-O1', f'-D{dither}', f'-e{exposure:.2f}',
            '-v_LangFlags_', '-S', _LANG_FLAG_SEQUENCE,
        ))
        assert len(font['glyphs']) == len(_LANG_LAYER_COUNTRIES), (
            f"[O1 {dither} e{exposure:+.1f}] Expected {len(_LANG_LAYER_COUNTRIES)} flags, "
            f"got {len(font['glyphs'])}"
        )
        out_dir = Path(__file__).parent / 'font_test_output'
        out_dir.mkdir(exist_ok=True)
        path = out_dir / f'lang_flags_O1_{dither}_e{exposure:+.1f}_sheet.png'
        w, h = make_sheet_png(font, str(path), scale=4, cols=9)
        assert path.exists() and w > 0 and h > 0
        print(f"\n  {path.name}  ({w}×{h} px)")

    @pytest.mark.parametrize(
        "contrast", _LANG_CONTRASTS,
        ids=[f"c{c:.1f}" for c in _LANG_CONTRASTS],
    )
    def test_lang_flags_O1_contrast_sheet(self, contrast):
        """All lang_layer flags with outline=1, each contrast value → font_test_output/."""
        font = h_to_font(run_fontconvert(
            f'-f{NOTO_COLOR}', '-s20', '-g', '-r36', '-W60',
            '-O1', f'-c{contrast:.2f}',
            '-v_LangFlags_', '-S', _LANG_FLAG_SEQUENCE,
        ))
        assert len(font['glyphs']) == len(_LANG_LAYER_COUNTRIES), (
            f"[O1 c{contrast:.1f}] Expected {len(_LANG_LAYER_COUNTRIES)} flags, "
            f"got {len(font['glyphs'])}"
        )
        out_dir = Path(__file__).parent / 'font_test_output'
        out_dir.mkdir(exist_ok=True)
        path = out_dir / f'lang_flags_O1_c{contrast:.1f}_sheet.png'
        w, h = make_sheet_png(font, str(path), scale=4, cols=9)
        assert path.exists() and w > 0 and h > 0
        print(f"\n  {path.name}  ({w}×{h} px)")


# ---------------------------------------------------------------------------
# Test: all GFXfont headers in the firmware fonts tree
# ---------------------------------------------------------------------------

_HEADERS = _all_font_headers()


@pytest.mark.skipif(not FONTS_ROOT.exists(), reason=f"fonts root not found: {FONTS_ROOT}")
class TestAllFirmwareFonts:
    """
    Parses every *.h file containing a GFXfont Bitmaps array under
    qmk_firmware/keyboards/handwired/polykybd/base/fonts/ (including subfolders)
    and checks structural integrity — no FAIL assertions, offsets in bounds.

    Each font becomes a separate parametrized test case.
    """

    @pytest.mark.parametrize("h_path", _HEADERS, ids=[p.name for p in _HEADERS])
    def test_parse_and_validate(self, h_path):
        font = parse_h_file(str(h_path))
        assert len(font['glyphs']) > 0, "No glyphs found"
        assert len(font['bitmap']) > 0, "Empty bitmap"
        fails = [i for i in run_assertions(font) if i.startswith('FAIL')]
        assert fails == [], f"FAIL assertions in {h_path.name}:\n" + "\n".join(fails)

        out_dir = Path(__file__).parent / 'font_test_output'
        out_dir.mkdir(exist_ok=True)
        sheet_path = str(out_dir / f"{font['name']}_sheet.png")
        make_sheet_png(font, sheet_path, scale=4, cols=16)
