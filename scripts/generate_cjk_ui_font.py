#!/usr/bin/env python3
"""
Generate a minimal CJK UI font containing only characters used in i18n translations.
This font is used for displaying Korean and Japanese menu text in the UI.
"""
import yaml
import subprocess
import sys
import os

# Get the project root directory
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)

def extract_cjk_chars():
    """Extract unique CJK characters from Korean and Japanese translation files."""
    chars = set()

    # Read Korean
    korean_path = os.path.join(PROJECT_ROOT, 'lib/I18n/translations/korean.yaml')
    with open(korean_path, 'r', encoding='utf-8') as f:
        data = yaml.safe_load(f)
        for key, value in data.items():
            if isinstance(value, str):
                for c in value:
                    # Korean Hangul syllables: AC00-D7AF
                    if '\uAC00' <= c <= '\uD7AF':
                        chars.add(c)

    # Read Japanese
    japanese_path = os.path.join(PROJECT_ROOT, 'lib/I18n/translations/japanese.yaml')
    with open(japanese_path, 'r', encoding='utf-8') as f:
        data = yaml.safe_load(f)
        for key, value in data.items():
            if isinstance(value, str):
                for c in value:
                    # Hiragana: 3040-309F, Katakana: 30A0-30FF, CJK Unified: 4E00-9FFF
                    if '\u3040' <= c <= '\u309F' or '\u30A0' <= c <= '\u30FF' or '\u4E00' <= c <= '\u9FFF':
                        chars.add(c)

    return sorted(chars)

def get_japanese_kana_and_kanji():
    """Get all Hiragana, Katakana, and Joyo Kanji (常用漢字) for book title display."""
    codepoints = set()

    # Hiragana: 3040-309F (full block)
    for cp in range(0x3040, 0x30A0):
        codepoints.add(cp)

    # Katakana: 30A0-30FF (full block)
    for cp in range(0x30A0, 0x3100):
        codepoints.add(cp)

    # Katakana Phonetic Extensions: 31F0-31FF
    for cp in range(0x31F0, 0x3200):
        codepoints.add(cp)

    # Halfwidth Katakana: FF65-FF9F
    for cp in range(0xFF65, 0xFFA0):
        codepoints.add(cp)

    # CJK Punctuation: 3000-303F (Japanese punctuation marks)
    for cp in range(0x3000, 0x3040):
        codepoints.add(cp)

    # Joyo Kanji (常用漢字) - 2136 characters used in everyday Japanese
    # Instead of listing all 2136 individually, we include common CJK blocks
    # that cover most Joyo Kanji: 4E00-9FFF (CJK Unified Ideographs)
    # This is ~20,000 characters - too many for embedded.
    # Use JIS Level 1 Kanji (~3000 chars) as a practical subset.
    # For now, add most common ~3000 kanji from CJK Unified block
    # JIS X 0208 Level 1 covers 2965 kanji commonly used

    # Common Kanji ranges (approximate JIS Level 1 coverage)
    # These ranges cover the most frequently used kanji
    kanji_ranges = [
        (0x4E00, 0x4FFF),  # Common kanji block 1
        (0x5000, 0x5FFF),  # Common kanji block 2
        (0x6000, 0x6FFF),  # Common kanji block 3
        (0x7000, 0x7FFF),  # Common kanji block 4
        (0x8000, 0x8FFF),  # Common kanji block 5
        (0x9000, 0x9FFF),  # Common kanji block 6
    ]

    for start, end in kanji_ranges:
        for cp in range(start, end + 1):
            codepoints.add(cp)

    return codepoints

def group_codepoints(codepoints):
    """Group consecutive codepoints into intervals."""
    if not codepoints:
        return []

    intervals = []
    start = codepoints[0]
    end = start

    for cp in codepoints[1:]:
        if cp == end + 1:
            end = cp
        else:
            intervals.append((start, end))
            start = cp
            end = cp

    intervals.append((start, end))
    return intervals

def main():
    print("Extracting CJK characters from i18n files...", file=sys.stderr)
    chars = extract_cjk_chars()
    codepoints = set(ord(c) for c in chars)

    print(f"Found {len(codepoints)} unique CJK characters from i18n", file=sys.stderr)

    # NOTE: Full Japanese kanji support (21k+ chars) is too large for embedded.
    # The UI font only includes characters from i18n translations.
    # For book content with Japanese kanji, use SD card .epdfont files.

    # Add replacement glyph (0xFFFD) for proper fallback detection
    codepoints.add(0xFFFD)
    codepoints = sorted(codepoints)

    # Group into intervals
    intervals = group_codepoints(codepoints)
    print(f"Grouped into {len(intervals)} intervals", file=sys.stderr)

    # Build fontconvert.py arguments
    fontconvert_path = os.path.join(PROJECT_ROOT, 'lib/EpdFont/scripts/fontconvert.py')
    font_path = os.path.join(PROJECT_ROOT, 'fonts/NotoSansCJKjp-Regular.otf')
    output_path = os.path.join(PROJECT_ROOT, 'lib/GfxRenderer/cjk_ui_font_10.h')

    # Build interval arguments
    interval_args = []
    for start, end in intervals:
        interval_args.extend(['--additional-intervals', f'0x{start:X},0x{end:X}'])

    # Run fontconvert.py
    # We use only CJK intervals (skip base Latin intervals)
    # and skip kerning/ligatures (not needed for CJK)
    cmd = [
        sys.executable,
        fontconvert_path,
        'cjk_ui_font_10',  # font name
        '10',              # size (small for UI)
        font_path,         # font file
        '--no-base-intervals',  # only use CJK intervals
        '--skip-kerning',       # CJK doesn't need kerning
        '--skip-ligatures',     # CJK doesn't have ligatures
    ] + interval_args

    print(f"Running fontconvert.py...", file=sys.stderr)
    print(f"Command: {' '.join(cmd[:5])} ... ({len(interval_args)} interval args)", file=sys.stderr)

    with open(output_path, 'w') as outfile:
        result = subprocess.run(cmd, stdout=outfile, stderr=sys.stderr)

    if result.returncode == 0:
        print(f"Generated: {output_path}", file=sys.stderr)
        # Get file size
        size = os.path.getsize(output_path)
        print(f"File size: {size / 1024:.1f} KB", file=sys.stderr)
    else:
        print(f"Error: fontconvert.py failed with code {result.returncode}", file=sys.stderr)
        sys.exit(1)

if __name__ == '__main__':
    main()
