#include "SdFont.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Utf8.h>

#include <algorithm>
#include <cstring>
#include <new>

SdFont::~SdFont() { unload(); }

bool SdFont::load(const char* filepath, int cacheSize) {
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

  // Allocate cache (e.g. ~36KB for 128 entries, ~9KB for 32 entries)
  _cacheSize = cacheSize;
  _cache = new (std::nothrow) CacheEntry[_cacheSize]();
  if (!_cache) {
    LOG_ERR("SDFONT", "Cache alloc failed: %d bytes", (int)(_cacheSize * sizeof(CacheEntry)));
    _fontFile.close();
    delete[] _intervals;
    _intervals = nullptr;
    return false;
  }
  _hashTable = new (std::nothrow) int16_t[_cacheSize];
  if (!_hashTable) {
    LOG_ERR("SDFONT", "Hash table alloc failed");
    _fontFile.close();
    delete[] _intervals;
    _intervals = nullptr;
    delete[] _cache;
    _cache = nullptr;
    return false;
  }
  for (int i = 0; i < _cacheSize; i++) {
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
  _cacheMisses = 0;
  _cacheHits = 0;
  LOG_DBG("SDFONT", "Loaded: %s (advY=%d, asc=%d, desc=%d, 2bit=%d, cache=%d)", _fontName, _advanceY, _ascender, _descender, _is2Bit, _cacheSize);

  // Diagnostic: dump a sample glyph bitmap to verify font data integrity
  dumpSampleGlyph(0x4E00);  // 一 (simplest CJK character - horizontal line)

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
  _intervalsOffset = 0;
  _useLazyIntervals = false;
  _cacheSize = 0;
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
  _intervalsOffset = header.intervalsOffset;
  _glyphsOffset = header.glyphsOffset;
  _bitmapOffset = header.bitmapOffset;

  // Load interval table - use lazy loading for large fonts to save memory
  if (_intervalCount > LAZY_INTERVAL_THRESHOLD) {
    // Lazy loading: don't allocate memory, read from file during search
    _useLazyIntervals = true;
    _intervals = nullptr;
    LOG_DBG("SDFONT", "Using lazy intervals: %u entries (threshold=%u)",
            _intervalCount, LAZY_INTERVAL_THRESHOLD);
  } else if (_intervalCount > 0) {
    // Small font: load intervals into memory for fast lookup
    _useLazyIntervals = false;
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
  if (_intervalCount == 0) {
    return -1;
  }

  if (_useLazyIntervals) {
    // Lazy mode: binary search reading from file
    uint32_t low = 0;
    uint32_t high = _intervalCount;
    EpdfontInterval interval;

    while (low < high) {
      uint32_t mid = low + (high - low) / 2;
      uint32_t offset = _intervalsOffset + mid * sizeof(EpdfontInterval);

      // Need to cast away const for file operations (file position is mutable state)
      HalFile& file = const_cast<HalFile&>(_fontFile);
      if (!file.seek(offset)) {
        return -1;
      }
      if (file.read(&interval, sizeof(interval)) != sizeof(interval)) {
        return -1;
      }

      if (codepoint < interval.first) {
        high = mid;
      } else if (codepoint > interval.last) {
        low = mid + 1;
      } else {
        // Found: codepoint is within this interval
        return interval.offset + (codepoint - interval.first);
      }
    }
    return -1;
  }

  // Memory mode: use std::upper_bound
  if (!_intervals) {
    return -1;
  }

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
  for (int i = 0; i < _cacheSize; i++) {
    if (_cache[i].codepoint == codepoint && _cache[i].valid) {
      return i;
    }
  }

  return -1;
}

int SdFont::getLruSlot() {
  uint32_t oldest = UINT32_MAX;
  int oldestSlot = 0;

  for (int i = 0; i < _cacheSize; i++) {
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
    LOG_ERR("SDFONT", "Seek failed for U+%04X at offset %u", codepoint, glyphOffset);
    return false;
  }

  EpdfontGlyph fileGlyph;
  if (_fontFile.read(&fileGlyph, sizeof(fileGlyph)) != sizeof(fileGlyph)) {
    LOG_ERR("SDFONT", "Read glyph metadata failed for U+%04X", codepoint);
    return false;
  }

  // Check bitmap size fits in cache
  if (fileGlyph.dataLength > MAX_BITMAP_BYTES) {
    LOG_DBG("SDFONT", "Glyph U+%04X bitmap too large: %u > %d bytes", codepoint, fileGlyph.dataLength, MAX_BITMAP_BYTES);
    entry->notFound = true;
    entry->valid = true;
    return false;
  }

  // Validate bitmap size matches glyph dimensions
  uint32_t pixels = (uint32_t)fileGlyph.width * fileGlyph.height;
  uint32_t expectedLen = _is2Bit ? (pixels + 3) / 4 : (pixels + 7) / 8;
  if (fileGlyph.dataLength != expectedLen && fileGlyph.width > 0 && fileGlyph.height > 0) {
    LOG_ERR("SDFONT", "U+%04X dataLength mismatch: %u vs expected %u (%ux%u %s)",
            codepoint, fileGlyph.dataLength, expectedLen,
            fileGlyph.width, fileGlyph.height, _is2Bit ? "2bit" : "1bit");
  }

  // Convert to EpdGlyph format (note: advanceX needs conversion to 12.4 fixed-point)
  entry->glyph.width = fileGlyph.width;
  entry->glyph.height = fileGlyph.height;
  entry->glyph.advanceX = static_cast<uint16_t>(fileGlyph.advanceX) << 4;  // Convert to 12.4 fixed-point
  entry->glyph.left = fileGlyph.left;
  entry->glyph.top = fileGlyph.top;
  entry->glyph.dataLength = fileGlyph.dataLength;
  entry->glyph.dataOffset = 0;  // We store bitmap directly, not offset

  // Clear bitmap to prevent stale data from previous cache occupant
  memset(entry->bitmap, 0, MAX_BITMAP_BYTES);

  // Read bitmap data
  if (fileGlyph.dataLength > 0) {
    uint32_t bitmapOffset = _bitmapOffset + fileGlyph.dataOffset;
    if (!_fontFile.seek(bitmapOffset)) {
      LOG_ERR("SDFONT", "Seek bitmap failed for U+%04X at offset %u", codepoint, bitmapOffset);
      return false;
    }

    if (_fontFile.read(entry->bitmap, fileGlyph.dataLength) != (int)fileGlyph.dataLength) {
      LOG_ERR("SDFONT", "Read bitmap failed for U+%04X (%u bytes)", codepoint, fileGlyph.dataLength);
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
    _cacheHits++;
    if (_cache[slot].notFound) {
      // Try replacement character
      if (cp != REPLACEMENT_GLYPH) {
        return getGlyph(REPLACEMENT_GLYPH);
      }
      return nullptr;
    }
    return &_cache[slot].glyph;
  }

  // Load from file - cache miss
  _cacheMisses++;
  if ((_cacheMisses % 50) == 1) {
    LOG_DBG("SDFONT", "Cache miss #%u (hits=%u, ratio=%.0f%%) U+%04X",
            _cacheMisses, _cacheHits,
            _cacheHits + _cacheMisses > 0 ? 100.0f * _cacheHits / (_cacheHits + _cacheMisses) : 0.0f, cp);
  }
  slot = getLruSlot();
  CacheEntry* entry = &_cache[slot];

  // Invalidate before read to prevent stale data if read fails partway
  entry->valid = false;

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

void SdFont::dumpSampleGlyph(uint32_t codepoint) {
  int glyphIndex = findGlyphIndex(codepoint);
  if (glyphIndex < 0) {
    LOG_DBG("SDFONT", "DIAG: U+%04X not in font", codepoint);
    return;
  }

  // Read raw glyph data from file
  uint32_t glyphOffset = _glyphsOffset + glyphIndex * sizeof(EpdfontGlyph);
  if (!_fontFile.seek(glyphOffset)) return;

  EpdfontGlyph fg;
  if (_fontFile.read(&fg, sizeof(fg)) != sizeof(fg)) return;

  LOG_DBG("SDFONT", "DIAG U+%04X: w=%u h=%u advX=%u left=%d top=%d dLen=%u dOff=%u",
          codepoint, fg.width, fg.height, fg.advanceX, fg.left, fg.top, fg.dataLength, fg.dataOffset);

  if (fg.dataLength == 0 || fg.dataLength > MAX_BITMAP_BYTES) return;

  uint8_t bmp[MAX_BITMAP_BYTES];
  uint32_t bitmapFileOffset = _bitmapOffset + fg.dataOffset;
  if (!_fontFile.seek(bitmapFileOffset)) return;
  if (_fontFile.read(bmp, fg.dataLength) != (int)fg.dataLength) return;

  // Log first 16 bytes as hex
  char hex[80];
  int pos = 0;
  for (uint32_t i = 0; i < fg.dataLength && i < 16 && pos < 75; i++) {
    pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", bmp[i]);
  }
  LOG_DBG("SDFONT", "DIAG hex: %s", hex);

  // Render as ASCII art (max 32 rows to avoid excessive output)
  uint8_t maxRows = fg.height < 32 ? fg.height : 32;
  for (uint8_t y = 0; y < maxRows; y++) {
    char line[65];  // max 64 cols + null
    uint8_t maxCols = fg.width < 64 ? fg.width : 64;
    for (uint8_t x = 0; x < maxCols; x++) {
      int pixelPos = y * fg.width + x;
      if (_is2Bit) {
        uint8_t byte = bmp[pixelPos >> 2];
        uint8_t bitIdx = (3 - (pixelPos & 3)) * 2;
        uint8_t val = (byte >> bitIdx) & 0x3;
        line[x] = (val == 0) ? '.' : (val == 1) ? '+' : (val == 2) ? '#' : '@';
      } else {
        uint8_t byte = bmp[pixelPos >> 3];
        uint8_t bitIdx = 7 - (pixelPos & 7);
        line[x] = ((byte >> bitIdx) & 1) ? '#' : '.';
      }
    }
    line[maxCols] = '\0';
    LOG_DBG("SDFONT", "DIAG|%s|", line);
  }
}

const uint8_t* SdFont::getGlyphBitmap(const EpdGlyph* glyph) {
  if (!glyph || !_isLoaded) {
    return nullptr;
  }

  // Find the cache entry that contains this glyph
  for (int i = 0; i < _cacheSize; i++) {
    if (_cache[i].valid && &_cache[i].glyph == glyph) {
      return _cache[i].bitmap;
    }
  }

  return nullptr;
}
