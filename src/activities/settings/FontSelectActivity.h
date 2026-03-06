#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Font selection activity - shows built-in fonts and SD card fonts together
 */
class FontSelectActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  int totalFonts = 0;
  int builtinFontCount = 0;  // Number of built-in fonts

 public:
  explicit FontSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FontSelect", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Get display name for font at index
  std::string getFontName(int index) const;
  // Apply selected font
  void applySelection();
};
