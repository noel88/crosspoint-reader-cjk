#include "VocabularyViewActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "MappedInputManager.h"
#include "VocabularyManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

VocabularyViewActivity::VocabularyViewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("VocabularyView", renderer, mappedInput) {}

void VocabularyViewActivity::loadEntries() {
  entries.clear();

  FsFile file;
  if (!Storage.openFileForRead("VOC", VocabularyManager::VOCAB_PATH, file)) {
    LOG_DBG("VOC", "No vocabulary file found");
    return;
  }

  static constexpr int MAX_ENTRIES = 200;
  entries.reserve(64);
  while (file.available() && static_cast<int>(entries.size()) < MAX_ENTRIES) {
    // Read one line character by character
    char line[512];
    int len = 0;
    int c;
    while (len < static_cast<int>(sizeof(line)) - 1 && (c = file.read()) >= 0) {
      if (c == '\n') break;
      line[len++] = static_cast<char>(c);
    }
    if (len == 0) continue;
    line[len] = '\0';

    // Parse TSV: word\tdefinition\tbook_title
    VocabEntry entry;
    char* tab1 = strchr(line, '\t');
    if (!tab1) continue;
    *tab1 = '\0';
    entry.word = line;

    char* defStart = tab1 + 1;
    char* tab2 = strchr(defStart, '\t');
    if (tab2) {
      *tab2 = '\0';
      entry.definition = defStart;
      entry.bookTitle = tab2 + 1;
    } else {
      entry.definition = defStart;
    }

    if (!entry.word.empty()) {
      entries.push_back(std::move(entry));
    }
  }
  file.close();
  LOG_DBG("VOC", "Loaded %d vocabulary entries", static_cast<int>(entries.size()));
}

void VocabularyViewActivity::onEnter() {
  Activity::onEnter();
  loadEntries();
  requestUpdate();
}

void VocabularyViewActivity::onExit() { Activity::onExit(); }

void VocabularyViewActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (entries.empty()) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    flipped = !flipped;
    requestUpdate();
    return;
  }

  buttonNavigator.onNext([this] {
    if (currentIndex < static_cast<int>(entries.size()) - 1) {
      currentIndex++;
      flipped = false;
      requestUpdate();
    }
  });

  buttonNavigator.onPrevious([this] {
    if (currentIndex > 0) {
      currentIndex--;
      flipped = false;
      requestUpdate();
    }
  });
}

void VocabularyViewActivity::render(RenderLock&&) {
  renderer.clearScreen();

  if (entries.empty()) {
    const auto pageHeight = renderer.getScreenHeight();
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2, tr(STR_EMPTY_VOCABULARY));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (flipped) {
    renderBack();
  } else {
    renderFront();
  }

  renderer.displayBuffer();
}

void VocabularyViewActivity::renderFront() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& entry = entries[currentIndex];

  // Card counter at top
  char counter[32];
  snprintf(counter, sizeof(counter), "%d / %d", currentIndex + 1, static_cast<int>(entries.size()));
  renderer.drawCenteredText(UI_10_FONT_ID, 15, counter);

  // Horizontal line
  renderer.drawLine(20, 40, pageWidth - 20, 40);

  // Word centered in large font
  const int wordY = pageHeight / 2 - 20;
  renderer.drawCenteredText(UI_20_FONT_ID, wordY, entry.word.c_str(), true, EpdFontFamily::BOLD);

  // Book title at bottom in small font
  if (!entry.bookTitle.empty()) {
    const std::string truncBook = renderer.truncatedText(SMALL_FONT_ID, entry.bookTitle.c_str(), pageWidth - 40);
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 65, truncBook.c_str());
  }

  // Horizontal line above hints
  renderer.drawLine(20, pageHeight - 50, pageWidth - 20, pageHeight - 50);

  // Button hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_FLIP), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void VocabularyViewActivity::renderBack() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& entry = entries[currentIndex];

  // Card counter at top
  char counter[32];
  snprintf(counter, sizeof(counter), "%d / %d", currentIndex + 1, static_cast<int>(entries.size()));
  renderer.drawCenteredText(UI_10_FONT_ID, 15, counter);

  // Word at top
  renderer.drawCenteredText(UI_12_FONT_ID, 45, entry.word.c_str(), true, EpdFontFamily::BOLD);

  // Horizontal line
  renderer.drawLine(20, 70, pageWidth - 20, 70);

  // Definition with word wrap
  constexpr int defX = 25;
  constexpr int defY = 85;
  constexpr int lineHeight = 22;
  const int maxWidth = pageWidth - 50;
  const int maxLines = (pageHeight - 130 - defY) / lineHeight;
  wrapText(UI_10_FONT_ID, entry.definition.c_str(), defX, defY, maxWidth, lineHeight, maxLines);

  // Book title at bottom
  if (!entry.bookTitle.empty()) {
    const std::string truncBook = renderer.truncatedText(SMALL_FONT_ID, entry.bookTitle.c_str(), pageWidth - 40);
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 65, truncBook.c_str());
  }

  // Horizontal line above hints
  renderer.drawLine(20, pageHeight - 50, pageWidth - 20, pageHeight - 50);

  // Button hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_FLIP), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void VocabularyViewActivity::wrapText(int fontId, const char* text, int x, int y, int maxWidth, int lineHeight,
                                      int maxLines) const {
  const char* ptr = text;
  int lineY = y;
  int linesDrawn = 0;
  char buf[256];

  while (*ptr && linesDrawn < maxLines) {
    const size_t totalLen = strlen(ptr);
    size_t fitLen = std::min(totalLen, sizeof(buf) - 1);

    // Copy into stack buffer and measure
    memcpy(buf, ptr, fitLen);
    buf[fitLen] = '\0';

    while (fitLen > 1 && renderer.getTextWidth(fontId, buf) > maxWidth) {
      // Search backward for a good break point
      size_t breakPos = 0;
      for (size_t i = fitLen; i > 0; i--) {
        const auto c = static_cast<uint8_t>(ptr[i - 1]);
        if (c == ';' || c == ' ') {
          breakPos = i;  // break after separator
          break;
        }
        // UTF-8 multi-byte lead byte (0xC0+): break before this character
        if (c >= 0xC0) {
          breakPos = i - 1;
          break;
        }
      }
      fitLen = (breakPos > 0) ? breakPos : fitLen - 1;

      // Don't end mid-UTF-8 sequence: move back to character boundary
      while (fitLen > 0 && (static_cast<uint8_t>(ptr[fitLen]) & 0xC0) == 0x80) {
        fitLen--;
      }

      memcpy(buf, ptr, fitLen);
      buf[fitLen] = '\0';
    }

    if (fitLen == 0) {
      // At least one full UTF-8 character
      fitLen = 1;
      while (fitLen < totalLen && (static_cast<uint8_t>(ptr[fitLen]) & 0xC0) == 0x80) {
        fitLen++;
      }
      const size_t copyLen = std::min(fitLen, sizeof(buf) - 1);
      memcpy(buf, ptr, copyLen);
      buf[copyLen] = '\0';
    }

    renderer.drawText(fontId, x, lineY, buf);
    lineY += lineHeight;
    linesDrawn++;

    ptr += fitLen;
    while (*ptr == ' ') ptr++;
  }

  // Truncation indicator: ellipsis at right edge of last line
  if (*ptr && linesDrawn >= maxLines) {
    const int ellipsisW = renderer.getTextWidth(fontId, "...");
    const int lastLineY = lineY - lineHeight;
    renderer.fillRect(x + maxWidth - ellipsisW, lastLineY, ellipsisW, lineHeight, false);
    renderer.drawText(fontId, x + maxWidth - ellipsisW, lastLineY, "...");
  }
}
