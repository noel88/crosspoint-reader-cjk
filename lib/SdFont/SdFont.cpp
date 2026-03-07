#include "SdFont.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Utf8.h>

#include <algorithm>
#include <cstring>
#include <new>

SdFont::~SdFont() { unload(); }

bool SdFont::load(const char* filepath) {
  unload();

  // Extract font name from filepath
  const char* filename = strrchr(filepath, '/');
  filename = filename ? filename + 1 : filepath;

  // Copy name without extension
  const char* ext = strstr(filename, ".epdfont");
  if (!ext) {
    ext = strstr(filename, ".EPDFONT");
  }
  size_t nameLen = ext ? (size_t)(ext - filename) : strlen(filename);
  if (nameLen >= sizeof(_fontName)) {
    nameLen = sizeof(_fontName) - 1;
  }
  strncpy(_fontName, filename, nameLen);
  _fontName[nameLen] = '\0';

  // Open font file
  _fontFile = Storage.open(filepath, O_RDONLY);
  if (!_fontFile) {
    LOG_ERR("SDFONT", "Failed to open: %s", filepath);
    return false;
  }

  // Load header and intervals
  if (!loadHeader()) {
    _fontFile.close();
    return false;
  }

  // Allocate cache (~36KB for 128 entries with 256-byte bitmaps)
  _cache = new (std::nothrow) CacheEntry[CACHE_SIZE]();
  if (!_cache) {
    LOG_ERR("SDFONT", "Cache alloc failed: %d bytes", (int)(CACHE_SIZE * sizeof(CacheEntry)));
    _fontFile.close();
    delete[] _intervals;
    _intervals = nullptr;
    return false;
  }
  _hashTable = new (std::nothrow) int16_t[CACHE_SIZE];
  if (!_hashTable) {
    LOG_ERR("SDFONT", "Hash table alloc failed");
    _fontFile.close();
    delete[] _intervals;
    _intervals = nullptr;
    delete[] _cache;
    _cache = nullptr;
    return false;
  }
  for (int i = 0; i < CACHE_SIZE; i++) {
    _hashTable[i] = -1;
  }

  // Set up EpdFontData for compatibility
  _fontData.bitmap = nullptr;  // Managed by cache
  _fontData.glyph = nullptr;   // Managed by cache
  _fontData.intervals = nullptr;
  _fontData.intervalCount = _intervalCount;
  _fontData.advanceY = _advanceY;
  _fontData.ascender = _ascender;
  _fontData.descender = _descender;
  _fontData.is2Bit = _is2Bit;
  _fontData.groups = nullptr;
  _fontData.groupCount = 0;
  _fontData.kernLeftClasses = nullptr;
  _fontData.kernRightClasses = nullptr;
  _fontData.kernMatrix = nullptr;
  _fontData.kernLeftEntryCount = 0;
  _fontData.kernRightEntryCount = 0;
  _fontData.kernLeftClassCount = 0;
  _fontData.kernRightClassCount = 0;
  _fontData.ligaturePairs = nullptr;
  _fontData.ligaturePairCount = 0;

  _isLoaded = true;
  LOG_DBG("SDFONT", "Loaded: %s (advanceY=%d, ascender=%d, descender=%d)", _fontName, _advanceY, _ascender, _descender);
  return true;
}

void SdFont::unload() {
  if (_fontFile) {
    _fontFile.close();
  }

  delete[] _intervals;
  _intervals = nullptr;

  delete[] _cache;
  _cache = nullptr;

  delete[] _hashTable;
  _hashTable = nullptr;

  _isLoaded = false;
  _intervalCount = 0;
  _accessCounter = 0;
}

bool SdFont::loadHeader() {
  EpdfontHeader header;

  if (_fontFile.read(&header, sizeof(header)) != sizeof(header)) {
    LOG_ERR("SDFONT", "Failed to read header");
    return false;
  }

  if (header.magic != EPDFONT_MAGIC) {
    LOG_ERR("SDFONT", "Invalid magic: 0x%08X", header.magic);
    return false;
  }

  if (header.version > 1) {
    LOG_ERR("SDFONT", "Unsupported version: %d", header.version);
    return false;
  }

  _advanceY = header.advanceY;
  _ascender = header.ascender;
  _descender = header.descender;
  _is2Bit = header.is2Bit != 0;
  _intervalCount = header.intervalCount;
  _glyphsOffset = header.glyphsOffset;
  _bitmapOffset = header.bitmapOffset;

  // Load interval table
  if (_intervalCount > 0) {
    _intervals = new (std::nothrow) EpdfontInterval[_intervalCount];
    if (!_intervals) {
      LOG_ERR("SDFONT", "Interval alloc failed: %u entries", _intervalCount);
      return false;
    }

    if (!_fontFile.seek(header.intervalsOffset)) {
      LOG_ERR("SDFONT", "Failed to seek to intervals");
      delete[] _intervals;
      _intervals = nullptr;
      return false;
    }

    size_t intervalBytes = _intervalCount * sizeof(EpdfontInterval);
    if (_fontFile.read(_intervals, intervalBytes) != (int)intervalBytes) {
      LOG_ERR("SDFONT", "Failed to read intervals");
      delete[] _intervals;
      _intervals = nullptr;
      return false;
    }
  }

  return true;
}

int SdFont::findGlyphIndex(uint32_t codepoint) const {
  if (_intervalCount == 0 || !_intervals) {
    return -1;
  }

  // Binary search using upper_bound (same as EpdFont)
  const EpdfontInterval* begin = _intervals;
  const EpdfontInterval* end = _intervals + _intervalCount;
  const EpdfontInterval* it = std::upper_bound(
      begin, end, codepoint,
      [](uint32_t value, const EpdfontInterval& interval) { return value < interval.first; });

  if (it != begin) {
    const EpdfontInterval& interval = *(it - 1);
    if (codepoint <= interval.last) {
      return interval.offset + (codepoint - interval.first);
    }
  }

  return -1;
}

int SdFont::findInCache(uint32_t codepoint) {
  int slot = _hashTable[hashCodepoint(codepoint)];
  if (slot >= 0 && _cache[slot].codepoint == codepoint) {
    return slot;
  }

  // Linear search fallback (for hash collisions)
  for (int i = 0; i < CACHE_SIZE; i++) {
    if (_cache[i].codepoint == codepoint && _cache[i].valid) {
      return i;
    }
  }

  return -1;
}

int SdFont::getLruSlot() {
  uint32_t oldest = UINT32_MAX;
  int oldestSlot = 0;

  for (int i = 0; i < CACHE_SIZE; i++) {
    if (!_cache[i].valid) {
      return i;  // Empty slot
    }
    if (_cache[i].lastUsed < oldest) {
      oldest = _cache[i].lastUsed;
      oldestSlot = i;
    }
  }

  // Clear hash entry for evicted glyph
  int oldHash = hashCodepoint(_cache[oldestSlot].codepoint);
  if (_hashTable[oldHash] == oldestSlot) {
    _hashTable[oldHash] = -1;
  }

  return oldestSlot;
}

bool SdFont::readGlyphFromFile(uint32_t codepoint, CacheEntry* entry) {
  int glyphIndex = findGlyphIndex(codepoint);
  if (glyphIndex < 0) {
    entry->notFound = true;
    entry->valid = true;
    return false;
  }

  // Read glyph metadata
  uint32_t glyphOffset = _glyphsOffset + glyphIndex * sizeof(EpdfontGlyph);
  if (!_fontFile.seek(glyphOffset)) {
    return false;
  }

  EpdfontGlyph fileGlyph;
  if (_fontFile.read(&fileGlyph, sizeof(fileGlyph)) != sizeof(fileGlyph)) {
    return false;
  }

  // Check bitmap size fits in cache
  if (fileGlyph.dataLength > MAX_BITMAP_BYTES) {
    LOG_DBG("SDFONT", "Glyph U+%04X bitmap too large: %u > %d bytes", codepoint, fileGlyph.dataLength, MAX_BITMAP_BYTES);
    entry->notFound = true;
    entry->valid = true;
    return false;
  }

  // Convert to EpdGlyph format (note: advanceX needs conversion to 12.4 fixed-point)
  entry->glyph.width = fileGlyph.width;
  entry->glyph.height = fileGlyph.height;
  entry->glyph.advanceX = static_cast<uint16_t>(fileGlyph.advanceX) << 4;  // Convert to 12.4 fixed-point
  entry->glyph.left = fileGlyph.left;
  entry->glyph.top = fileGlyph.top;
  entry->glyph.dataLength = fileGlyph.dataLength;
  entry->glyph.dataOffset = 0;  // We store bitmap directly, not offset

  // Read bitmap data
  if (fileGlyph.dataLength > 0) {
    uint32_t bitmapOffset = _bitmapOffset + fileGlyph.dataOffset;
    if (!_fontFile.seek(bitmapOffset)) {
      return false;
    }

    if (_fontFile.read(entry->bitmap, fileGlyph.dataLength) != (int)fileGlyph.dataLength) {
      return false;
    }
  }

  entry->codepoint = codepoint;
  entry->valid = true;
  entry->notFound = false;
  return true;
}

const EpdGlyph* SdFont::getGlyph(uint32_t cp) {
  if (!_isLoaded) {
    return nullptr;
  }

  // Check cache first
  int slot = findInCache(cp);
  if (slot >= 0) {
    _cache[slot].lastUsed = ++_accessCounter;
    if (_cache[slot].notFound) {
      // Try replacement character
      if (cp != REPLACEMENT_GLYPH) {
        return getGlyph(REPLACEMENT_GLYPH);
      }
      return nullptr;
    }
    return &_cache[slot].glyph;
  }

  // Load from file
  slot = getLruSlot();
  CacheEntry* entry = &_cache[slot];

  if (!readGlyphFromFile(cp, entry)) {
    if (entry->notFound) {
      entry->codepoint = cp;
      entry->lastUsed = ++_accessCounter;
      _hashTable[hashCodepoint(cp)] = slot;

      // Try replacement character
      if (cp != REPLACEMENT_GLYPH) {
        return getGlyph(REPLACEMENT_GLYPH);
      }
    }
    return nullptr;
  }

  entry->lastUsed = ++_accessCounter;
  _hashTable[hashCodepoint(cp)] = slot;

  return &entry->glyph;
}

const uint8_t* SdFont::getGlyphBitmap(const EpdGlyph* glyph) {
  if (!glyph || !_isLoaded) {
    return nullptr;
  }

  // Find the cache entry that contains this glyph
  for (int i = 0; i < CACHE_SIZE; i++) {
    if (_cache[i].valid && &_cache[i].glyph == glyph) {
      return _cache[i].bitmap;
    }
  }

  return nullptr;
}
