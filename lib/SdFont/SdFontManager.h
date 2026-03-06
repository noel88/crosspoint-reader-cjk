#pragma once

#include "SdFont.h"

#include <cstdint>

/**
 * Font info structure for discovered fonts
 */
struct SdFontInfo {
  char filename[64];  // Filename only
  char name[32];      // Display name (extracted from filename)
  uint8_t size;       // Font size in points
};

/**
 * SD card font manager - scans fonts directory and manages selection
 */
class SdFontManager {
 public:
  static constexpr int MAX_FONTS = 16;
  static constexpr const char* FONTS_DIR = "/fonts";
  static constexpr const char* SETTINGS_FILE = "/.crosspoint/sdfont.dat";
  static constexpr uint8_t SETTINGS_VERSION = 1;

  static SdFontManager& getInstance();

  /**
   * Scan fonts directory for .epdfont files
   */
  void scanFonts();

  /**
   * Get number of available fonts
   */
  int getFontCount() const { return _fontCount; }

  /**
   * Get font info by index
   */
  const SdFontInfo* getFontInfo(int index) const;

  /**
   * Select a font by index (-1 for none/builtin)
   */
  void selectFont(int index);

  /**
   * Get currently selected font index (-1 if none)
   */
  int getSelectedIndex() const { return _selectedIndex; }

  /**
   * Get the active loaded font (nullptr if using builtin)
   */
  SdFont* getActiveFont();

  /**
   * Check if SD font is active (vs builtin)
   */
  bool isSdFontActive() const { return _selectedIndex >= 0 && _activeFont.isLoaded(); }

  /**
   * Save settings to SD card
   */
  void saveSettings();

  /**
   * Load settings from SD card
   */
  void loadSettings();

 private:
  SdFontManager() = default;

  SdFontInfo _fonts[MAX_FONTS];
  int _fontCount = 0;
  int _selectedIndex = -1;  // -1 = use builtin font

  SdFont _activeFont;

  bool loadSelectedFont();
  bool parseFilename(const char* filename, SdFontInfo& info);
};
