#!/usr/bin/env python3
"""
Convert JMdict XML to sorted TSV for CrossPoint Reader dictionary lookup.

Usage:
  python3 scripts/convert_jmdict.py [output_path]

Downloads JMdict_e.gz (Japanese-English) from EDRDG if not cached locally,
parses it, and produces a sorted TSV file:
  word<TAB>reading - definition1; definition2

Output: ./dict.tsv (or specified path)
Copy to SD card: /dictionaries/dict.tsv
"""

import gzip
import os
import sys
import urllib.request
import xml.etree.ElementTree as ET

JMDICT_URL = "http://ftp.edrdg.org/pub/Nihongo/JMdict_e.gz"
CACHE_FILE = "JMdict_e.gz"


def download_jmdict(cache_path):
    if os.path.exists(cache_path):
        print(f"Using cached: {cache_path}")
        return
    print(f"Downloading JMdict from {JMDICT_URL}...")
    urllib.request.urlretrieve(JMDICT_URL, cache_path)
    print(f"Downloaded: {cache_path} ({os.path.getsize(cache_path) // 1024}KB)")


def extract_entities(raw):
    """Extract entity definitions from DOCTYPE and return as dict."""
    import re
    entities = {}
    for m in re.finditer(r'<!ENTITY\s+(\S+)\s+"([^"]*)"', raw):
        entities[m.group(1)] = m.group(2)
    return entities


def resolve_entities(xml_text, entities):
    """Replace &entity; references with their text values."""
    import re
    def replacer(m):
        name = m.group(1)
        # Preserve standard XML entities
        if name in ("amp", "lt", "gt", "quot", "apos"):
            return m.group(0)
        return entities.get(name, "")
    # Entity names can contain hyphens (e.g. adj-i, v5k-s, vs-i)
    return re.sub(r'&([\w-]+);', replacer, xml_text)


def parse_jmdict(gz_path):
    """Parse JMdict XML and yield (word, definition_string) tuples."""
    print("Parsing JMdict XML (this may take a minute)...")

    with gzip.open(gz_path, "rb") as f:
        raw = f.read().decode("utf-8")

    # Extract custom entity definitions from DOCTYPE before stripping it
    entities = extract_entities(raw)
    print(f"Found {len(entities)} entity definitions")

    # Strip DOCTYPE, keep only the XML content
    start = raw.find("<JMdict>")
    if start == -1:
        raise ValueError("Cannot find <JMdict> tag")
    xml_data = raw[start:]

    # Resolve all custom entity references to plain text
    xml_data = resolve_entities(xml_data, entities)
    xml_data = '<?xml version="1.0" encoding="UTF-8"?>\n' + xml_data

    root = ET.fromstring(xml_data)
    entry_count = 0

    for entry in root.iter("entry"):
        # Get kanji elements (written forms)
        kanjis = [keb.text for keb in entry.iter("keb") if keb.text]
        # Get reading elements (kana)
        readings = [reb.text for reb in entry.iter("reb") if reb.text]
        # Get English glosses grouped by sense
        senses = []
        for sense in entry.iter("sense"):
            glosses = [g.text for g in sense.iter("gloss") if g.text]
            if glosses:
                senses.append("; ".join(glosses))

        if not senses:
            continue

        definition = " / ".join(senses)
        reading_str = readings[0] if readings else ""

        if kanjis:
            # Entry has kanji: create entry for each kanji form
            for kanji in kanjis:
                defn = f"{reading_str} - {definition}" if reading_str else definition
                yield (kanji, defn)
        elif readings:
            # Kana-only entry
            yield (readings[0], definition)

        entry_count += 1

    print(f"Parsed {entry_count} entries")


def write_sorted_tsv(entries, output_path):
    """Sort entries by word and write TSV."""
    print(f"Sorting {len(entries)} entries...")
    entries.sort(key=lambda x: x[0])

    # Deduplicate: if same word appears multiple times, merge definitions
    deduped = []
    prev_word = None
    for word, defn in entries:
        if word == prev_word and deduped:
            # Append to previous entry's definition
            existing = deduped[-1]
            deduped[-1] = (existing[0], existing[1] + " | " + defn)
        else:
            deduped.append((word, defn))
        prev_word = word

    print(f"Writing {len(deduped)} unique entries to {output_path}...")
    with open(output_path, "w", encoding="utf-8") as f:
        for word, defn in deduped:
            # Ensure no tabs or newlines in fields
            word_clean = word.replace("\t", " ").replace("\n", " ")
            defn_clean = defn.replace("\t", " ").replace("\n", " ")
            f.write(f"{word_clean}\t{defn_clean}\n")

    size_kb = os.path.getsize(output_path) // 1024
    print(f"Done! {output_path} ({size_kb}KB, {len(deduped)} entries)")
    print(f"\nCopy to SD card: /dictionaries/dict.tsv")


def main():
    output_path = sys.argv[1] if len(sys.argv) > 1 else "dict.tsv"

    download_jmdict(CACHE_FILE)
    entries = list(parse_jmdict(CACHE_FILE))
    write_sorted_tsv(entries, output_path)


if __name__ == "__main__":
    main()
