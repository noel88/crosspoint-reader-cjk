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
    char line[512];
    int len = 0;
    int c;
    while (len < static_cast<int>(sizeof(line)) - 1 && (c = file.read()) >= 0) {
      if (c == '\n') break;
      line[len++] = static_cast<char>(c);
    }
    if (len == 0) continue;
    line[len] = '\0';

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

void VocabularyViewActivity::buildDueList() {
  dueIndices.clear();
  dueIndices.reserve(32);
  for (int i = 0; i < static_cast<int>(entries.size()); i++) {
    if (srs.isDue(entries[i].word)) {
      dueIndices.push_back(i);
    }
  }
}

void VocabularyViewActivity::advanceReview() {
  duePosition++;
  if (duePosition >= static_cast<int>(dueIndices.size())) {
    srs.save();
    srsDirty = false;
    state = ViewState::REVIEW_DONE;
  } else {
    currentIndex = dueIndices[duePosition];
    flipped = false;
  }
  requestUpdate();
}

void VocabularyViewActivity::onEnter() {
  Activity::onEnter();
  loadEntries();
  srs.load();
  state = ViewState::MODE_SELECT;
  modeSelection = 0;
  requestUpdate();
}

void VocabularyViewActivity::onExit() {
  if (srsDirty) {
    srs.save();
    srsDirty = false;
  }
  Activity::onExit();
}

void VocabularyViewActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (state == ViewState::MODE_SELECT) {
      finish();
    } else {
      if (srsDirty) {
        srs.save();
        srsDirty = false;
      }
      state = ViewState::MODE_SELECT;
      modeSelection = 0;
      requestUpdate();
    }
    return;
  }

  switch (state) {
    case ViewState::MODE_SELECT: {
      buttonNavigator.onNext([this] {
        if (modeSelection < 1) {
          modeSelection++;
          requestUpdate();
        }
      });
      buttonNavigator.onPrevious([this] {
        if (modeSelection > 0) {
          modeSelection--;
          requestUpdate();
        }
      });
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        if (entries.empty()) return;
        if (modeSelection == 0) {
          state = ViewState::BROWSE;
          currentIndex = 0;
          flipped = false;
        } else {
          srs.beginReviewSession();
          buildDueList();
          if (dueIndices.empty()) {
            state = ViewState::REVIEW_DONE;
          } else {
            state = ViewState::REVIEW;
            duePosition = 0;
            currentIndex = dueIndices[0];
            flipped = false;
          }
        }
        requestUpdate();
      }
      break;
    }
    case ViewState::BROWSE: {
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
      break;
    }
    case ViewState::REVIEW: {
      if (!flipped) {
        if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
          flipped = true;
          requestUpdate();
        }
      } else {
        // Back of card: rate with Down=Again, Confirm=Good, Up=Easy
        if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
          srs.rate(entries[currentIndex].word, 0);
          srsDirty = true;
          advanceReview();
        } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
          srs.rate(entries[currentIndex].word, 1);
          srsDirty = true;
          advanceReview();
        } else if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
          srs.rate(entries[currentIndex].word, 2);
          srsDirty = true;
          advanceReview();
        }
      }
      break;
    }
    case ViewState::REVIEW_DONE: {
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        state = ViewState::MODE_SELECT;
        modeSelection = 0;
        requestUpdate();
      }
      break;
    }
  }
}

void VocabularyViewActivity::render(RenderLock&&) {
  renderer.clearScreen();

  // Bounds check before rendering card states
  if (state == ViewState::BROWSE || state == ViewState::REVIEW) {
    if (currentIndex < 0 || currentIndex >= static_cast<int>(entries.size())) {
      LOG_ERR("VOC", "Invalid index %d (size=%d)", currentIndex, static_cast<int>(entries.size()));
      state = ViewState::MODE_SELECT;
    }
  }

  switch (state) {
    case ViewState::MODE_SELECT:
      renderModeSelect();
      break;
    case ViewState::BROWSE:
      if (flipped) {
        renderBack();
      } else {
        renderFront();
      }
      break;
    case ViewState::REVIEW:
      if (flipped) {
        renderReviewBack();
      } else {
        renderFront();
      }
      break;
    case ViewState::REVIEW_DONE:
      renderReviewDone();
      break;
  }

  renderer.displayBuffer();
}

void VocabularyViewActivity::renderModeSelect() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Title
  renderer.drawCenteredText(UI_12_FONT_ID, 30, tr(STR_VOCABULARY_LIST), true, EpdFontFamily::BOLD);
  renderer.drawLine(20, 55, pageWidth - 20, 55);

  if (entries.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_EMPTY_VOCABULARY));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }

  // Count due cards
  int dueCount = 0;
  for (const auto& entry : entries) {
    if (srs.isDue(entry.word)) dueCount++;
  }

  // Build option labels
  char browseBuf[64];
  char reviewBuf[64];
  snprintf(browseBuf, sizeof(browseBuf), "%s (%d)", tr(STR_BROWSE_ALL), static_cast<int>(entries.size()));
  snprintf(reviewBuf, sizeof(reviewBuf), "%s (%d)", tr(STR_REVIEW), dueCount);

  // Draw as a simple list
  const Rect listRect{0, 70, pageWidth, pageHeight - 70 - 50};
  GUI.drawList(renderer, listRect, 2, modeSelection,
               [&](int index) -> std::string { return index == 0 ? browseBuf : reviewBuf; });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void VocabularyViewActivity::renderFront() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& entry = entries[currentIndex];

  // Card counter at top (mode-aware)
  char counter[32];
  if (state == ViewState::REVIEW) {
    snprintf(counter, sizeof(counter), "%d / %d", duePosition + 1, static_cast<int>(dueIndices.size()));
  } else {
    snprintf(counter, sizeof(counter), "%d / %d", currentIndex + 1, static_cast<int>(entries.size()));
  }
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

  // Button hints (mode-aware)
  if (state == ViewState::REVIEW) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_FLIP), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_FLIP), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
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

void VocabularyViewActivity::renderReviewBack() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& entry = entries[currentIndex];

  // Card counter
  char counter[32];
  snprintf(counter, sizeof(counter), "%d / %d", duePosition + 1, static_cast<int>(dueIndices.size()));
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

  // Rating hints: Down=Again, Confirm=Good, Up=Easy
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SRS_GOOD), tr(STR_SRS_EASY), tr(STR_SRS_AGAIN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void VocabularyViewActivity::renderReviewDone() {
  const auto pageHeight = renderer.getScreenHeight();

  renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 10, tr(STR_REVIEW_COMPLETE), true, EpdFontFamily::BOLD);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
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

    memcpy(buf, ptr, fitLen);
    buf[fitLen] = '\0';

    while (fitLen > 1 && renderer.getTextWidth(fontId, buf) > maxWidth) {
      size_t breakPos = 0;
      for (size_t i = fitLen; i > 0; i--) {
        const auto c = static_cast<uint8_t>(ptr[i - 1]);
        if (c == ';' || c == ' ') {
          breakPos = i;
          break;
        }
        if (c >= 0xC0) {
          breakPos = i - 1;
          break;
        }
      }
      fitLen = (breakPos > 0) ? breakPos : fitLen - 1;

      while (fitLen > 0 && (static_cast<uint8_t>(ptr[fitLen]) & 0xC0) == 0x80) {
        fitLen--;
      }

      memcpy(buf, ptr, fitLen);
      buf[fitLen] = '\0';
    }

    if (fitLen == 0) {
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

  if (*ptr && linesDrawn >= maxLines) {
    const int ellipsisW = renderer.getTextWidth(fontId, "...");
    const int lastLineY = lineY - lineHeight;
    renderer.fillRect(x + maxWidth - ellipsisW, lastLineY, ellipsisW, lineHeight, false);
    renderer.drawText(fontId, x + maxWidth - ellipsisW, lastLineY, "...");
  }
}
