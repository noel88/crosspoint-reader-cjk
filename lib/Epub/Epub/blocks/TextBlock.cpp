#include "TextBlock.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Serialization.h>
#include <Utf8.h>


void TextBlock::collectCodepoints(std::vector<uint32_t>& out, size_t max) const {
  if (max == 0 || out.size() >= max) {
    return;
  }

  for (const auto& word : words) {
    const unsigned char* ptr = reinterpret_cast<const unsigned char*>(word.c_str());
    uint32_t cp;
    while ((cp = utf8NextCodepoint(&ptr))) {
      // Check if already exists (simple linear search, OK for small sets)
      bool exists = false;
      for (uint32_t existing : out) {
        if (existing == cp) {
          exists = true;
          break;
        }
      }
      if (!exists) {
        out.push_back(cp);
        if (out.size() >= max) {
          return;
        }
      }
    }
  }
}

void TextBlock::render(const GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  // Validate iterator bounds before rendering
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size()) {
    LOG_ERR("TXB", "Render skipped: size mismatch (words=%u, xpos=%u, styles=%u)\n", (uint32_t)words.size(),
            (uint32_t)wordXpos.size(), (uint32_t)wordStyles.size());
    return;
  }

  if (blockStyle.writingMode == CssWritingMode::VerticalRl) {
    renderVertical(renderer, fontId, x, y);
    return;
  }

  for (size_t i = 0; i < words.size(); i++) {
    const int wordX = wordXpos[i] + x;
    const EpdFontFamily::Style currentStyle = wordStyles[i];
    renderer.drawText(fontId, wordX, y, words[i].c_str(), true, currentStyle);

    if ((currentStyle & EpdFontFamily::UNDERLINE) != 0) {
      const std::string& w = words[i];
      const int fullWordWidth = renderer.getTextWidth(fontId, w.c_str(), currentStyle);
      // y is the top of the text line; add ascender to reach baseline, then offset 2px below
      const int underlineY = y + renderer.getFontAscenderSize(fontId) + 2;

      int startX = wordX;
      int underlineWidth = fullWordWidth;

      // if word starts with em-space ("\xe2\x80\x83"), account for the additional indent before drawing the line
      if (w.size() >= 3 && static_cast<uint8_t>(w[0]) == 0xE2 && static_cast<uint8_t>(w[1]) == 0x80 &&
          static_cast<uint8_t>(w[2]) == 0x83) {
        const char* visiblePtr = w.c_str() + 3;
        const int prefixWidth = renderer.getTextAdvanceX(fontId, "\xe2\x80\x83", currentStyle);
        const int visibleWidth = renderer.getTextWidth(fontId, visiblePtr, currentStyle);
        startX = wordX + prefixWidth;
        underlineWidth = visibleWidth;
      }

      renderer.drawLine(startX, underlineY, startX + underlineWidth, underlineY, true);
    }
  }
}

void TextBlock::renderVertical(const GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  // Vertical rendering: x = column X position, wordXpos[i] = Y offset within column
  // CJK characters drawn upright, punctuation rotated/repositioned, Latin rotated 90° CW
  const int lineHeight = renderer.getLineHeight(fontId);

  for (size_t i = 0; i < words.size(); i++) {
    const int charY = wordXpos[i] + y;
    const EpdFontFamily::Style currentStyle = wordStyles[i];
    const std::string& word = words[i];

    const auto* ptr = reinterpret_cast<const unsigned char*>(word.c_str());
    const uint32_t cp = utf8NextCodepoint(&ptr);
    if (cp == 0) continue;

    // Determine if this is a bracket (opening or closing)
    const bool isBracket = isVerticalOpeningBracket(cp) ||
                           (isVerticalRotatedPunctuation(cp) && !isHorizontalStrokeChar(cp) && cp != 0x2026);

    if (isBracket) {
      // Symmetric brackets (〈〉（）etc.): mirror then CW rotate.
      // Corner brackets (「」『』): CW rotation alone is correct (L-shape rotates naturally).
      const uint32_t renderCp = isCornerStyleBracket(cp) ? cp : mirrorBracket(cp);
      char buf[4];
      int len = 0;
      if (renderCp < 0x80) {
        buf[0] = static_cast<char>(renderCp);
        len = 1;
      } else if (renderCp < 0x800) {
        buf[0] = static_cast<char>(0xC0 | (renderCp >> 6));
        buf[1] = static_cast<char>(0x80 | (renderCp & 0x3F));
        len = 2;
      } else {
        buf[0] = static_cast<char>(0xE0 | (renderCp >> 12));
        buf[1] = static_cast<char>(0x80 | ((renderCp >> 6) & 0x3F));
        buf[2] = static_cast<char>(0x80 | (renderCp & 0x3F));
        len = 3;
      }
      buf[len] = '\0';
      const int cellHeight = renderer.getTextAdvanceX(fontId, word.c_str(), currentStyle);
      const bool isClosing = !isVerticalOpeningBracket(cp);
      const int yPad = isClosing ? (lineHeight / 5) : 0;
      const int cursorY = charY + cellHeight + yPad;
      renderer.drawTextRotated90CW(fontId, x, cursorY, buf, true, currentStyle);
      renderer.drawTextRotated90CW(fontId, x + 1, cursorY, buf, true, currentStyle);
    } else if (isVerticalRotatedPunctuation(cp)) {
      // Horizontal strokes (ー〜—…): rotate 90° CW.
      // CW renders glyphs extending ABOVE cursorY, offset by lineHeight.
      renderer.drawTextRotated90CW(fontId, x, charY + lineHeight, word.c_str(), true, currentStyle);
    } else if (isVerticalRepositionedPunctuation(cp)) {
      // Commas/periods (、。): draw upright, shifted to top-right of character cell
      const int xOffset = lineHeight / 2;
      const int yOffset = -(lineHeight / 2);
      renderer.drawText(fontId, x + xOffset, charY + yOffset, word.c_str(), true, currentStyle);
    } else if (isCjkCodepoint(cp)) {
      // Regular CJK character: draw upright
      renderer.drawText(fontId, x, charY, word.c_str(), true, currentStyle);
    } else {
      // Latin/number in vertical mode: split into individual characters,
      // each drawn upright with fixed centering within the CJK column.
      const int xCenter = lineHeight / 4;  // Approximate centering offset
      const auto* p = reinterpret_cast<const unsigned char*>(word.c_str());
      int yOff = 0;
      while (*p) {
        const unsigned char* start = p;
        utf8NextCodepoint(&p);
        const int cLen = static_cast<int>(p - start);
        char ch[5];
        memcpy(ch, start, cLen);
        ch[cLen] = '\0';
        renderer.drawText(fontId, x + xCenter, charY + yOff, ch, true, currentStyle);
        yOff += lineHeight;
      }
    }
  }
}

bool TextBlock::serialize(FsFile& file) const {
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size()) {
    LOG_ERR("TXB", "Serialization failed: size mismatch (words=%u, xpos=%u, styles=%u)\n", words.size(),
            wordXpos.size(), wordStyles.size());
    return false;
  }

  // Word data
  serialization::writePod(file, static_cast<uint16_t>(words.size()));
  for (const auto& w : words) serialization::writeString(file, w);
  for (auto x : wordXpos) serialization::writePod(file, x);
  for (auto s : wordStyles) serialization::writePod(file, s);

  // Style (alignment + margins/padding/indent)
  serialization::writePod(file, blockStyle.alignment);
  serialization::writePod(file, blockStyle.textAlignDefined);
  serialization::writePod(file, blockStyle.marginTop);
  serialization::writePod(file, blockStyle.marginBottom);
  serialization::writePod(file, blockStyle.marginLeft);
  serialization::writePod(file, blockStyle.marginRight);
  serialization::writePod(file, blockStyle.paddingTop);
  serialization::writePod(file, blockStyle.paddingBottom);
  serialization::writePod(file, blockStyle.paddingLeft);
  serialization::writePod(file, blockStyle.paddingRight);
  serialization::writePod(file, blockStyle.textIndent);
  serialization::writePod(file, blockStyle.textIndentDefined);
  serialization::writePod(file, blockStyle.writingMode);

  return true;
}

std::unique_ptr<TextBlock> TextBlock::deserialize(FsFile& file) {
  uint16_t wc;
  std::vector<std::string> words;
  std::vector<int16_t> wordXpos;
  std::vector<EpdFontFamily::Style> wordStyles;
  BlockStyle blockStyle;

  // Word count
  serialization::readPod(file, wc);

  // Sanity check: prevent allocation of unreasonably large vectors (max 10000 words per block)
  if (wc > 10000) {
    LOG_ERR("TXB", "Deserialization failed: word count %u exceeds maximum", wc);
    return nullptr;
  }

  // Word data
  words.resize(wc);
  wordXpos.resize(wc);
  wordStyles.resize(wc);
  for (auto& w : words) serialization::readString(file, w);
  for (auto& x : wordXpos) serialization::readPod(file, x);
  for (auto& s : wordStyles) serialization::readPod(file, s);

  // Style (alignment + margins/padding/indent)
  serialization::readPod(file, blockStyle.alignment);
  serialization::readPod(file, blockStyle.textAlignDefined);
  serialization::readPod(file, blockStyle.marginTop);
  serialization::readPod(file, blockStyle.marginBottom);
  serialization::readPod(file, blockStyle.marginLeft);
  serialization::readPod(file, blockStyle.marginRight);
  serialization::readPod(file, blockStyle.paddingTop);
  serialization::readPod(file, blockStyle.paddingBottom);
  serialization::readPod(file, blockStyle.paddingLeft);
  serialization::readPod(file, blockStyle.paddingRight);
  serialization::readPod(file, blockStyle.textIndent);
  serialization::readPod(file, blockStyle.textIndentDefined);
  serialization::readPod(file, blockStyle.writingMode);

  return std::unique_ptr<TextBlock>(
      new TextBlock(std::move(words), std::move(wordXpos), std::move(wordStyles), blockStyle));
}
