#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Forward declare to avoid including full SdFontManager.h in header
enum class SdFontType : uint8_t;

class SdFontSelectActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  int fontCount = 0;
  SdFontType fontType;

 public:
  explicit SdFontSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, SdFontType type);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
