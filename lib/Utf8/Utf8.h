#pragma once

#include <cstdint>
#include <string>
#define REPLACEMENT_GLYPH 0xFFFD

int utf8CodepointLen(unsigned char c);
uint32_t utf8NextCodepoint(const unsigned char** string);
// Remove the last UTF-8 codepoint from a std::string and return the new size.
size_t utf8RemoveLastChar(std::string& str);
// Truncate string by removing N UTF-8 codepoints from the end.
void utf8TruncateChars(std::string& str, size_t numChars);

// Returns true for CJK codepoints that should be treated as individual word tokens.
// Korean (Hangul) is excluded — it uses spaces between words like Latin text.
inline bool isCjkCodepoint(const uint32_t cp) {
  if (cp >= 0x2E80 && cp <= 0x2FDF) return true;   // CJK Radicals, Kangxi Radicals
  if (cp >= 0x3000 && cp <= 0x303F) return true;   // CJK Symbols and Punctuation
  if (cp >= 0x3040 && cp <= 0x309F) return true;   // Hiragana
  if (cp >= 0x30A0 && cp <= 0x30FF) return true;   // Katakana
  if (cp >= 0x31F0 && cp <= 0x31FF) return true;   // Katakana Phonetic Extensions
  if (cp >= 0x3400 && cp <= 0x4DBF) return true;   // CJK Extension A
  if (cp >= 0x4E00 && cp <= 0x9FFF) return true;   // CJK Unified Ideographs
  if (cp >= 0xF900 && cp <= 0xFAFF) return true;   // CJK Compatibility Ideographs
  if (cp >= 0xFF00 && cp <= 0xFFEF) return true;   // Fullwidth Forms
  return false;
}

// Punctuation that needs 90° clockwise rotation in vertical text mode.
// Horizontal strokes become vertical, brackets rotate to vertical orientation.
inline bool isVerticalRotatedPunctuation(const uint32_t cp) {
  switch (cp) {
    // Horizontal strokes → rotate to vertical
    case 0x30FC:  // ー katakana prolonged sound mark
    case 0x301C:  // 〜 wave dash
    case 0xFF5E:  // ～ fullwidth tilde
    case 0x2014:  // — em dash
    case 0x2015:  // ― horizontal bar
    case 0x2026:  // … horizontal ellipsis
    case 0xFF0D:  // － fullwidth hyphen-minus
    // CJK brackets → rotate for vertical orientation
    case 0x3008:  // 〈
    case 0x3009:  // 〉
    case 0x300A:  // 《
    case 0x300B:  // 》
    case 0x300C:  // 「
    case 0x300D:  // 」
    case 0x300E:  // 『
    case 0x300F:  // 』
    case 0x3010:  // 【
    case 0x3011:  // 】
    case 0x3014:  // 〔
    case 0x3015:  // 〕
    case 0x3016:  // 〖
    case 0x3017:  // 〗
    case 0x3018:  // 〘
    case 0x3019:  // 〙
    case 0x301A:  // 〚
    case 0x301B:  // 〛
    case 0xFF08:  // （
    case 0xFF09:  // ）
    case 0xFF3B:  // ［
    case 0xFF3D:  // ］
    case 0xFF5B:  // ｛
    case 0xFF5D:  // ｝
      return true;
    default:
      return false;
  }
}

// Punctuation that needs repositioning (top-right offset) in vertical text mode.
// These marks are drawn upright but shifted within the character cell.
inline bool isVerticalRepositionedPunctuation(const uint32_t cp) {
  switch (cp) {
    case 0x3001:  // 、 ideographic comma
    case 0x3002:  // 。 ideographic full stop
    case 0xFF0C:  // ， fullwidth comma
    case 0xFF0E:  // ． fullwidth full stop
      return true;
    default:
      return false;
  }
}

// Returns true for Unicode combining diacritical marks that should not advance the cursor.
inline bool utf8IsCombiningMark(const uint32_t cp) {
  return (cp >= 0x0300 && cp <= 0x036F)      // Combining Diacritical Marks
         || (cp >= 0x1DC0 && cp <= 0x1DFF)   // Combining Diacritical Marks Supplement
         || (cp >= 0x20D0 && cp <= 0x20FF)   // Combining Diacritical Marks for Symbols
         || (cp >= 0xFE20 && cp <= 0xFE2F);  // Combining Half Marks
}
