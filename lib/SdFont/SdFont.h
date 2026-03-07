#pragma once

#include <EpdFontData.h>
#include <HalStorage.h>

#include <cstddef>
#include <cstdint>

// Magic number for .epdfont format
#define EPDFONT_MAGIC 0x46445045  // "EPDF"

/**
 * .epdfont file header structure (32 bytes)
 */
#pragma pack(push, 1)
struct EpdfontHeader {
  uint32_t magic;            // 0x46445045 ("EPDF")
  uint16_t version;          // Format version (1)
  uint8_t is2Bit;            // 1 = 2-bit grayscale, 0 = 1-bit
  uint8_t reserved1;         // Reserved for alignment
  uint8_t advanceY;          // Line height
  int8_t ascender;           // Max height above baseline
  int8_t descender;          // Max depth below baseline (negative)
  uint8_t reserved2;         // Reserved for alignment
  uint32_t intervalCount;    // Number of unicode intervals
  uint32_t glyphCount;       // Total number of glyphs
  uint32_t intervalsOffset;  // File offset to intervals array
  uint32_t glyphsOffset;     // File offset to glyphs array
  uint32_t bitmapOffset;     // File offset to bitmap data
};

struct EpdfontInterval {
  uint32_t first;   // First unicode code point
  uint32_t last;    // Last unicode code point
  uint32_t offset;  // Index into glyph array
};

struct EpdfontGlyph {
  uint8_t width;        // Bitmap width in pixels
  uint8_t height;       // Bitmap height in pixels
  uint8_t advanceX;     // Horizontal advance (integer pixels)
  uint8_t reserved;     // Reserved for alignment
  int16_t left;         // X offset from cursor
  int16_t top;          // Y offset from cursor
  uint32_t dataLength;  // Bitmap data size in bytes
  uint32_t dataOffset;  // Offset into bitmap section
};
#pragma pack(pop)

/**
 * SD card font loader for .epdfont format
 *
 * Provides interface compatible with EpdFont for use with GfxRenderer.
 * Uses LRU cache for glyph data to minimize SD card reads.
 */
class SdFont {
 public:
  SdFont() = default;
  ~SdFont();

  // Disable copy
  SdFont(const SdFont&) = delete;
  SdFont& operator=(const SdFont&) = delete;

  /**
   * Load font from .epdfont file on SD card
   * @param filepath Full path on SD card (e.g. "/fonts/NotoSansKR_28.epdfont")
   * @return true on success
   */
  bool load(const char* filepath);

  /**
   * Unload font and free resources
   */
  void unload();

  /**
   * Check if font is loaded
   */
  bool isLoaded() const { return _isLoaded; }

  /**
   * Get glyph for a codepoint
   * @param cp Unicode codepoint
   * @return Pointer to EpdGlyph (cached), nullptr if not found
   */
  const EpdGlyph* getGlyph(uint32_t cp);

  /**
   * Get glyph bitmap data
   * Must call getGlyph() first to ensure glyph is loaded!
   * @param glyph Glyph from getGlyph()
   * @return Bitmap data pointer, nullptr if not available
   */
  const uint8_t* getGlyphBitmap(const EpdGlyph* glyph);

  // Font metrics (EpdFontData compatible)
  uint8_t getAdvanceY() const { return _advanceY; }
  int getAscender() const { return _ascender; }
  int getDescender() const { return _descender; }
  bool is2Bit() const { return _is2Bit; }

  // Additional info
  const char* getFontName() const { return _fontName; }

  /**
   * Get EpdFontData-compatible structure for integration with existing code
   * Note: bitmap and glyph pointers are managed internally
   */
  const EpdFontData* getData() const { return &_fontData; }

 private:
  // Font file handle
  HalFile _fontFile;
  bool _isLoaded = false;

  // Header info
  char _fontName[32] = {0};
  uint8_t _advanceY = 0;
  int8_t _ascender = 0;
  int8_t _descender = 0;
  bool _is2Bit = false;

  // Interval table (loaded once)
  EpdfontInterval* _intervals = nullptr;
  uint32_t _intervalCount = 0;
  uint32_t _glyphsOffset = 0;
  uint32_t _bitmapOffset = 0;

  // LRU cache for glyphs and bitmaps
  static constexpr int CACHE_SIZE = 128;
  static constexpr int MAX_BITMAP_BYTES = 256;

  struct CacheEntry {
    uint32_t codepoint = 0xFFFFFFFF;
    EpdGlyph glyph;
    uint8_t bitmap[MAX_BITMAP_BYTES];
    uint32_t lastUsed = 0;
    bool valid = false;
    bool notFound = false;  // Glyph doesn't exist in font
  };
  CacheEntry* _cache = nullptr;
  uint32_t _accessCounter = 0;

  // Hash table for O(1) cache lookup
  int16_t* _hashTable = nullptr;
  static int hashCodepoint(uint32_t cp) { return cp % CACHE_SIZE; }

  // EpdFontData structure for compatibility
  EpdFontData _fontData = {};

  // Private methods
  bool loadHeader();
  int findGlyphIndex(uint32_t codepoint) const;
  bool readGlyphFromFile(uint32_t codepoint, CacheEntry* entry);
  int findInCache(uint32_t codepoint);
  int getLruSlot();
};
