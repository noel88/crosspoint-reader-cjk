#pragma once

#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <Epub/css/CssStyle.h>
#include <GfxRenderer.h>

#include <string>
#include <vector>

struct WordEntry {
  std::string text;
  int16_t screenX, screenY;
  int16_t width, height;
  EpdFontFamily::Style style;
};

// Build a flat list of selectable words from a rendered page.
// xOffset/yOffset are the oriented margin offsets applied during rendering.
inline std::vector<WordEntry> buildWordList(const Page& page, const GfxRenderer& renderer, int fontId, int xOffset,
                                            int yOffset) {
  std::vector<WordEntry> entries;
  entries.reserve(64);
  const int lineHeight = renderer.getLineHeight(fontId);

  for (const auto& el : page.elements) {
    if (el->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*el);
    const auto& block = line.getBlock();
    if (!block || block->isEmpty()) continue;

    const auto& words = block->getWords();
    const auto& xpos = block->getWordXpos();
    const auto& styles = block->getWordStyles();
    const bool isVertical = block->getBlockStyle().writingMode == CssWritingMode::VerticalRl;

    for (size_t i = 0; i < words.size(); i++) {
      if (words[i].empty()) continue;
      // Skip whitespace-only words (e.g. em-space used for indent)
      bool allWhitespace = true;
      for (unsigned char c : words[i]) {
        if (c > 0x20) {
          allWhitespace = false;
          break;
        }
      }
      if (allWhitespace) continue;

      WordEntry entry;
      entry.text = words[i];
      entry.style = styles[i];

      if (isVertical) {
        entry.screenX = el->xPos + xOffset;
        entry.screenY = xpos[i] + el->yPos + yOffset;
        entry.width = lineHeight;
        entry.height = lineHeight;
      } else {
        entry.screenX = xpos[i] + el->xPos + xOffset;
        entry.screenY = el->yPos + yOffset;
        entry.width = renderer.getTextWidth(fontId, words[i].c_str(), styles[i]);
        entry.height = lineHeight;
      }

      entries.push_back(std::move(entry));
    }
  }

  return entries;
}
