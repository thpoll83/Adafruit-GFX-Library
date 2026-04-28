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
    return ' '.join(_flag_seq(cc) for cc in _FLAG_CODES)


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
