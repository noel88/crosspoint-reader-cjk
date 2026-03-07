#include "FontSelectActivity.h"

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <SdFontManager.h>

#include "components/UITheme.h"
#include "fontIds.h"

// Built-in font names (must match CrossPointSettings::fontFamily enum order)
static const StrId BUILTIN_FONT_NAMES[] = {
    StrId::STR_BOOKERLY,
    StrId::STR_NOTO_SANS,
    StrId::STR_OPEN_DYSLEXIC,
};
static constexpr int BUILTIN_FONT_COUNT = sizeof(BUILTIN_FONT_NAMES) / sizeof(BUILTIN_FONT_NAMES[0]);

void FontSelectActivity::onEnter() {
  Activity::onEnter();

  builtinFontCount = BUILTIN_FONT_COUNT;

  // Scan SD card fonts
  auto& mgr = SdFontManager::getInstance();
  mgr.scanFonts();
  int sdFontCount = mgr.getFontCount();

  totalFonts = builtinFontCount + sdFontCount;

  // Determine current selection
  // If SD font is active, find its index
  if (mgr.isSdFontActive(SdFontType::READER)) {
    int sdIndex = mgr.getSelectedIndex(SdFontType::READER);
    if (sdIndex >= 0) {
      selectedIndex = builtinFontCount + sdIndex;
    } else {
      selectedIndex = SETTINGS.fontFamily;
    }
  } else {
    // Using built-in font
    selectedIndex = SETTINGS.fontFamily;
  }

  // Clamp to valid range
  if (selectedIndex < 0 || selectedIndex >= totalFonts) {
    selectedIndex = 0;
  }

  requestUpdate();
}

std::string FontSelectActivity::getFontName(int index) const {
  if (index < builtinFontCount) {
    // Built-in font
    return std::string(I18N.get(BUILTIN_FONT_NAMES[index]));
  } else {
    // SD card font
    int sdIndex = index - builtinFontCount;
    auto& mgr = SdFontManager::getInstance();
    const SdFontInfo* info = mgr.getFontInfo(sdIndex);
    if (info) {
      char buf[48];
      if (info->size > 0) {
        snprintf(buf, sizeof(buf), "%s (%dpt) [SD]", info->name, info->size);
      } else {
        snprintf(buf, sizeof(buf), "%s [SD]", info->name);
      }
      return std::string(buf);
    }
    return "???";
  }
}

void FontSelectActivity::applySelection() {
  auto& mgr = SdFontManager::getInstance();

  if (selectedIndex < builtinFontCount) {
    // Selected a built-in font - disable SD font
    mgr.selectFont(-1, SdFontType::READER);
    SETTINGS.fontFamily = static_cast<uint8_t>(selectedIndex);
    SETTINGS.saveToFile();
  } else {
    // Selected an SD font
    int sdIndex = selectedIndex - builtinFontCount;
    mgr.selectFont(sdIndex, SdFontType::READER);
    mgr.saveSettings();
  }

  // Update renderer's SdFont fallback (uses UI font, or reader font as fallback)
  SdFont* uiFont = mgr.getActiveFont(SdFontType::UI);
  if (!uiFont) {
    uiFont = mgr.getActiveFont(SdFontType::READER);
  }
  renderer.setSdFontFallback((uiFont && uiFont->isLoaded()) ? uiFont : nullptr);
}

void FontSelectActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    applySelection();
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    if (totalFonts > 0) {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, totalFonts);
      requestUpdate();
    }
  });

  buttonNavigator.onPreviousRelease([this] {
    if (totalFonts > 0) {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, totalFonts);
      requestUpdate();
    }
  });
}

void FontSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FONT_FAMILY), "");

  // Determine current active font for marking
  auto& mgr = SdFontManager::getInstance();
  int currentIndex = -1;
  if (mgr.isSdFontActive(SdFontType::READER)) {
    int sdIndex = mgr.getSelectedIndex(SdFontType::READER);
    if (sdIndex >= 0) {
      currentIndex = builtinFontCount + sdIndex;
    }
  } else {
    currentIndex = SETTINGS.fontFamily;
  }

  GUI.drawList(
      renderer,
      Rect{0, metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing, pageWidth,
           pageHeight - (metrics.topPadding + metrics.headerHeight + metrics.buttonHintsHeight +
                         metrics.verticalSpacing * 2)},
      totalFonts, selectedIndex,
      [this](int index) { return getFontName(index); },
      nullptr, nullptr,
      [currentIndex](int index) { return index == currentIndex ? tr(STR_SELECTED) : ""; },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
