#include "SdFontSelectActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <SdFontManager.h>

#include "components/UITheme.h"
#include "fontIds.h"

// Constructor definition (moved from header to cpp)
SdFontSelectActivity::SdFontSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, SdFontType type)
    : Activity("SdFontSelect", renderer, mappedInput), fontType(type) {}

void SdFontSelectActivity::onEnter() {
  Activity::onEnter();

  auto& mgr = SdFontManager::getInstance();
  mgr.scanFonts();
  int sdFontCount = mgr.getFontCount();
  fontCount = sdFontCount + 1;  // +1 for "Builtin" option at index 0

  // Set initial selection: -1 (builtin) maps to index 0, SD fonts map to index+1
  int current = mgr.getSelectedIndex(fontType);
  selectedIndex = (current >= 0) ? (current + 1) : 0;

  requestUpdate();
}

void SdFontSelectActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    auto& mgr = SdFontManager::getInstance();
    // selectedIndex 0 = builtin (-1), 1+ = SD font (index-1)
    int sdFontIndex = (selectedIndex == 0) ? -1 : (selectedIndex - 1);
    mgr.selectFont(sdFontIndex, fontType);
    mgr.saveSettings();

    // Update renderer's SdFont fallback for UI CJK rendering
    SdFont* uiFont = mgr.getActiveFont(SdFontType::UI);
    renderer.setSdFontFallback((uiFont && uiFont->isLoaded()) ? uiFont : nullptr);

    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    if (fontCount > 0) {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, fontCount);
      requestUpdate();
    }
  });

  buttonNavigator.onPreviousRelease([this] {
    if (fontCount > 0) {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, fontCount);
      requestUpdate();
    }
  });
}

void SdFontSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  const char* headerText = fontType == SdFontType::UI ? tr(STR_SD_UI_FONT) : tr(STR_SD_FONT);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, headerText, "");

  GUI.drawList(
      renderer,
      Rect{0, metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing, pageWidth,
           pageHeight - (metrics.topPadding + metrics.headerHeight + metrics.buttonHintsHeight +
                         metrics.verticalSpacing * 2)},
      fontCount, selectedIndex,
      [this](int index) {
        if (index == 0) {
          return std::string(tr(STR_BUILTIN_DISABLED));
        }
        auto& mgr = SdFontManager::getInstance();
        const SdFontInfo* info = mgr.getFontInfo(index - 1);
        if (info) {
          char buf[48];
          if (info->size > 0) {
            snprintf(buf, sizeof(buf), "%s (%dpt)", info->name, info->size);
          } else {
            snprintf(buf, sizeof(buf), "%s", info->name);
          }
          return std::string(buf);
        }
        return std::string("???");
      },
      nullptr, nullptr, nullptr, true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
