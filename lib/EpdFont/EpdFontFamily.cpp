#include "EpdFontFamily.h"

#include <Utf8.h>

const EpdFont* EpdFontFamily::getFont(const Style style) const {
  // Extract font style bits (ignore UNDERLINE bit for font selection)
  const bool hasBold = (style & BOLD) != 0;
  const bool hasItalic = (style & ITALIC) != 0;

  if (hasBold && hasItalic) {
    if (boldItalic) return boldItalic;
    if (bold) return bold;
    if (italic) return italic;
  } else if (hasBold && bold) {
    return bold;
  } else if (hasItalic && italic) {
    return italic;
  }

  return regular;
}

void EpdFontFamily::getTextDimensions(const char* string, int* w, int* h, const Style style) const {
  // If no fallback font, use simple measurement
  if (!fallback) {
    getFont(style)->getTextDimensions(string, w, h);
    return;
  }

  // With fallback font, we need to measure each character individually
  // to account for glyphs coming from different fonts
  const EpdFont* primaryFont = getFont(style);
  int totalWidth = 0;
  int maxHeight = primaryFont->data->advanceY;

  const unsigned char* p = reinterpret_cast<const unsigned char*>(string);
  while (*p) {
    uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0) break;

    const EpdFontData* fontData;
    const EpdGlyph* glyph = getGlyphWithData(cp, &fontData, style);

    if (glyph) {
      // Use 12.4 fixed-point advanceX, convert to pixels
      totalWidth += (glyph->advanceX + 8) >> 4;  // Round to nearest pixel
    }
  }

  *w = totalWidth;
  *h = maxHeight;
}

const EpdFontData* EpdFontFamily::getData(const Style style) const { return getFont(style)->data; }

const EpdGlyph* EpdFontFamily::getGlyph(const uint32_t cp, const Style style) const {
  const EpdFontData* fontData;
  return getGlyphWithData(cp, &fontData, style);
}

const EpdGlyph* EpdFontFamily::getGlyphWithData(const uint32_t cp, const EpdFontData** fontDataOut, const Style style) const {
  const EpdFont* font = getFont(style);
  *fontDataOut = font->data;

  // Try primary font first
  const EpdGlyph* glyph = font->getGlyph(cp);

  // If primary font doesn't have the glyph (returns nullptr or replacement glyph),
  // try the fallback font. This enables CJK fallback for Latin-only UI fonts.
  if (fallback && cp != REPLACEMENT_GLYPH) {
    bool needFallback = false;

    if (!glyph) {
      // Primary font returned nullptr - definitely need fallback
      needFallback = true;
    } else {
      // Check if primary font returned the replacement glyph
      const EpdGlyph* replacementGlyph = font->getGlyph(REPLACEMENT_GLYPH);
      if (replacementGlyph && glyph == replacementGlyph) {
        needFallback = true;
      }
    }

    if (needFallback) {
      const EpdGlyph* fallbackGlyph = fallback->getGlyph(cp);
      if (fallbackGlyph) {
        // Check fallback didn't also return its replacement glyph
        const EpdGlyph* fallbackReplacement = fallback->getGlyph(REPLACEMENT_GLYPH);
        if (!fallbackReplacement || fallbackGlyph != fallbackReplacement) {
          *fontDataOut = fallback->data;
          return fallbackGlyph;
        }
      }
    }
  }

  return glyph;
}

int8_t EpdFontFamily::getKerning(const uint32_t leftCp, const uint32_t rightCp, const Style style) const {
  return getFont(style)->getKerning(leftCp, rightCp);
}

uint32_t EpdFontFamily::applyLigatures(const uint32_t cp, const char*& text, const Style style) const {
  return getFont(style)->applyLigatures(cp, text);
}
