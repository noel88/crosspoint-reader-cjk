#!/usr/bin/env python3
"""
Convert Japanese Wiktionary XML dump to sorted TSV for CrossPoint Reader dictionary lookup.

Usage:
  python3 scripts/convert_wiktionary_jj.py [output_path]

Downloads jawiktionary-latest-pages-articles.xml.bz2 from Wikimedia if not cached,
parses Japanese-Japanese definitions, and produces a sorted TSV file:
  word<TAB>reading - definition1; definition2

Output: ./dict_jj.tsv (or specified path)
Copy to SD card: /dictionaries/dict_jj.tsv
"""

import bz2
import os
import re
import sys
import urllib.request
import xml.etree.ElementTree as ET

DUMP_URL = "https://dumps.wikimedia.org/jawiktionary/latest/jawiktionary-latest-pages-articles.xml.bz2"
CACHE_FILE = "jawiktionary-pages-articles.xml.bz2"
MW_NS = "{http://www.mediawiki.org/xml/export-0.11/}"

# Part-of-speech template names that contain definitions
POS_TEMPLATES = {
    "noun", "verb", "adjective", "adj", "adjectivenoun", "adverb",
    "pronoun", "auxverb", "parti", "interj", "interjection",
    "conj", "conjunction", "pref", "prefix", "suffix", "numeral",
    "name", "intrverb", "tranverb",
}

# Regex patterns compiled once
RE_JA_SECTION = re.compile(
    r'^==\s*\{\{(?:L\|ja|ja)\}\}\s*==\s*$', re.MULTILINE
)
RE_KANJI_SECTION = re.compile(
    r'^==\s*漢字\s*==\s*$', re.MULTILINE
)
RE_LEVEL2 = re.compile(r'^==[^=]', re.MULTILINE)
RE_POS_HEADING = re.compile(
    r'^===\s*\{\{(' + '|'.join(POS_TEMPLATES) + r')\}\}[^=]*===\s*$', re.MULTILINE
)
RE_IGI_HEADING = re.compile(r'^===\s*意義\s*===\s*$', re.MULTILINE)
RE_LEVEL3 = re.compile(r'^===[^=]', re.MULTILINE)
RE_DEF_LINE = re.compile(r'^#([^*:#].*)', re.MULTILINE)
RE_JA_PRON = re.compile(r'\{\{ja-pron\|([^|}]+)')
RE_JA_HEADWORD = re.compile(r'\{\{ja-(?:noun|verb|adj|adv)\|([^|}]+)')
RE_WIKILINK_DISP = re.compile(r'\[\[[^|\]]*\|([^\]]*)\]\]')
RE_WIKILINK_PLAIN = re.compile(r'\[\[([^\]|]*)\]\]')
RE_BOLD = re.compile(r"'''(.*?)'''")
RE_ITALIC = re.compile(r"''(.*?)''")
RE_TEMPLATE_SIMPLE = re.compile(r'\{\{[^}]*\}\}')
RE_HTML_TAG = re.compile(r'<[^>]+>')
RE_PARENS_READING = re.compile(r'^（[^）]*）\s*')
RE_WAGOKANJI_OF = re.compile(r'\{\{wagokanji of\|([^|}]+)')
RE_KANGOKANJI_OF = re.compile(r'\{\{kangokanji of\|([^|}]+)')
RE_WAGO_SECTION = re.compile(r'^===\s*和語の漢字表記\s*===\s*$', re.MULTILINE)
# Pattern: '''[[よむ]]''' 参照  or  '''[[うつくしい]]'''　参照
RE_SANKOU_REF = re.compile(r"'''\[\[([^\]]+)\]\]'''[　\s]*参照")


def download_dump(cache_path):
    if os.path.exists(cache_path):
        print(f"Using cached: {cache_path}")
        return
    print(f"Downloading Japanese Wiktionary dump (~87MB)...")
    print(f"URL: {DUMP_URL}")
    urllib.request.urlretrieve(DUMP_URL, cache_path)
    print(f"Downloaded: {cache_path} ({os.path.getsize(cache_path) // (1024*1024)}MB)")


def extract_ja_sections(text):
    """Extract Japanese language sections from wikitext (may include kanji section)."""
    sections = []

    # Standard Japanese section: =={{L|ja}}== or =={{ja}}==
    m = RE_JA_SECTION.search(text)
    if m:
        start = m.end()
        m2 = RE_LEVEL2.search(text, start)
        end = m2.start() if m2 else len(text)
        sections.append(text[start:end])

    # Kanji section: ==漢字== (often has ===意義=== with definitions)
    m = RE_KANJI_SECTION.search(text)
    if m:
        start = m.end()
        m2 = RE_LEVEL2.search(text, start)
        end = m2.start() if m2 else len(text)
        sections.append(text[start:end])

    return sections


def extract_reading(ja_section):
    """Extract reading (pronunciation) from Japanese section."""
    m = RE_JA_PRON.search(ja_section)
    if m:
        return m.group(1).strip()
    m = RE_JA_HEADWORD.search(ja_section)
    if m:
        return m.group(1).strip()
    return ""


def clean_wikitext(line):
    """Strip wikitext markup from a definition line."""
    # [[target|display]] -> display
    line = RE_WIKILINK_DISP.sub(r'\1', line)
    # [[word]] -> word
    line = RE_WIKILINK_PLAIN.sub(r'\1', line)
    # '''bold''' -> content
    line = RE_BOLD.sub(r'\1', line)
    # ''italic'' -> content
    line = RE_ITALIC.sub(r'\1', line)
    # Remove remaining templates {{...}}
    line = RE_TEMPLATE_SIMPLE.sub('', line)
    # Remove HTML tags and entities
    line = RE_HTML_TAG.sub('', line)
    line = line.replace('&rarr;', '→').replace('&larr;', '←')
    line = line.replace('&amp;', '&').replace('&lt;', '<').replace('&gt;', '>')
    # Remove leading reading in parens like （みず）
    line = RE_PARENS_READING.sub('', line)
    # Clean up whitespace
    line = line.strip()
    return line


def extract_defs_from_section(section_text, start_pos):
    """Extract definition lines from a section starting at start_pos."""
    defs = []
    next_l3 = RE_LEVEL3.search(section_text, start_pos)
    sec_end = next_l3.start() if next_l3 else len(section_text)
    content = section_text[start_pos:sec_end]
    for def_m in RE_DEF_LINE.finditer(content):
        raw = def_m.group(1).strip()
        cleaned = clean_wikitext(raw)
        if cleaned and len(cleaned) > 1:
            defs.append(cleaned)
    return defs


def extract_definitions(sections):
    """Extract definition lines from POS and 意義 sections."""
    definitions = []
    for section in sections:
        # POS template headings (===noun===, ===verb===, etc.)
        for pos_m in RE_POS_HEADING.finditer(section):
            definitions.extend(extract_defs_from_section(section, pos_m.end()))
        # Literal ===意義=== heading (common in kanji entries)
        for igi_m in RE_IGI_HEADING.finditer(section):
            definitions.extend(extract_defs_from_section(section, igi_m.end()))
    return definitions


def extract_redirect(wikitext):
    """Check if entry redirects to a kana form."""
    # {{wagokanji of|たべる}} or {{kangokanji of|X}}
    m = RE_WAGOKANJI_OF.search(wikitext)
    if m:
        return m.group(1).strip()
    m = RE_KANGOKANJI_OF.search(wikitext)
    if m:
        return m.group(1).strip()
    # ===和語の漢字表記=== with '''[[よむ]]'''　参照
    if RE_WAGO_SECTION.search(wikitext):
        m = RE_SANKOU_REF.search(wikitext)
        if m:
            return m.group(1).strip()
    return None


def parse_wiktionary(bz2_path):
    """Parse Wiktionary XML dump and return (entries, redirects)."""
    print("Parsing Japanese Wiktionary dump (this may take a few minutes)...")

    entries = []
    redirects = []  # (kanji_form, kana_target)
    entry_count = 0
    skipped = 0

    with bz2.open(bz2_path, "rb") as f:
        context = ET.iterparse(f, events=("end",))
        for event, elem in context:
            if elem.tag != f"{MW_NS}page":
                continue

            ns_elem = elem.find(f"{MW_NS}ns")
            if ns_elem is None or ns_elem.text != "0":
                elem.clear()
                continue

            title_elem = elem.find(f"{MW_NS}title")
            text_elem = elem.find(f".//{MW_NS}text")

            if title_elem is None or text_elem is None or not text_elem.text:
                elem.clear()
                continue

            title = title_elem.text.strip()
            if ":" in title:
                elem.clear()
                continue

            wikitext = text_elem.text

            # Check for kanji-to-kana redirects
            redirect_target = extract_redirect(wikitext)
            if redirect_target:
                redirects.append((title, redirect_target))
                elem.clear()
                continue

            sections = extract_ja_sections(wikitext)
            if not sections:
                elem.clear()
                skipped += 1
                continue

            reading = ""
            for sec in sections:
                reading = extract_reading(sec)
                if reading:
                    break
            definitions = extract_definitions(sections)

            if definitions:
                defn_str = "; ".join(definitions)
                if reading and reading != title:
                    defn_str = f"{reading} - {defn_str}"
                entries.append((title, defn_str))
                entry_count += 1

            elem.clear()

            if entry_count % 10000 == 0 and entry_count > 0:
                print(f"  ...{entry_count} entries extracted")

    print(f"Parsed {entry_count} J-J entries, {len(redirects)} redirects "
          f"(skipped {skipped} non-Japanese pages)")
    return entries, redirects


def write_sorted_tsv(entries, output_path):
    """Sort entries by word and write TSV."""
    print(f"Sorting {len(entries)} entries...")
    entries.sort(key=lambda x: x[0])

    # Deduplicate: merge definitions for same word
    deduped = []
    prev_word = None
    for word, defn in entries:
        if word == prev_word and deduped:
            existing = deduped[-1]
            deduped[-1] = (existing[0], existing[1] + " | " + defn)
        else:
            deduped.append((word, defn))
        prev_word = word

    print(f"Writing {len(deduped)} unique entries to {output_path}...")
    with open(output_path, "w", encoding="utf-8") as f:
        for word, defn in deduped:
            word_clean = word.replace("\t", " ").replace("\n", " ")
            defn_clean = defn.replace("\t", " ").replace("\n", " ")
            f.write(f"{word_clean}\t{defn_clean}\n")

    size_kb = os.path.getsize(output_path) // 1024
    print(f"Done! {output_path} ({size_kb}KB, {len(deduped)} entries)")
    print(f"\nCopy to SD card: /dictionaries/dict_jj.tsv")


def resolve_redirects(entries, redirects):
    """Resolve kanji redirects by copying definitions from kana targets."""
    # Build lookup: word -> definition
    defn_map = {word: defn for word, defn in entries}
    resolved = 0
    for kanji_form, kana_target in redirects:
        if kana_target in defn_map and kanji_form not in defn_map:
            entries.append((kanji_form, defn_map[kana_target]))
            resolved += 1
    print(f"Resolved {resolved} kanji redirects (e.g. 食べる→たべる)")
    return entries


def main():
    output_path = sys.argv[1] if len(sys.argv) > 1 else "dict_jj.tsv"

    download_dump(CACHE_FILE)
    entries, redirects = parse_wiktionary(CACHE_FILE)
    entries = resolve_redirects(entries, redirects)
    write_sorted_tsv(entries, output_path)


if __name__ == "__main__":
    main()
