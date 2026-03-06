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
 * Font type for selection (UI vs Reader)
 */
enum class SdFontType : uint8_t {
  READER = 0,  // For book content (typically larger, e.g., 28pt)
  UI = 1       // For menus/settings (smaller, e.g., 10-12pt)
};

/**
 * SD card font manager - scans fonts directory and manages selection
 * Supports separate fonts for UI and Reader
 */
class SdFontManager {
 public:
  static constexpr int MAX_FONTS = 16;
  static constexpr const char* FONTS_DIR = "/fonts";
  static constexpr const char* SETTINGS_FILE = "/.crosspoint/sdfont.dat";
  static constexpr uint8_t SETTINGS_VERSION = 2;  // Bumped for UI font support

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
   * @param index Font index or -1 for builtin
   * @param type Which font type to set (UI or Reader)
   */
  void selectFont(int index, SdFontType type = SdFontType::READER);

  /**
   * Get currently selected font index (-1 if none)
   * @param type Which font type to query
   */
  int getSelectedIndex(SdFontType type = SdFontType::READER) const {
    return type == SdFontType::UI ? _uiSelectedIndex : _selectedIndex;
  }

  /**
   * Get the active loaded font (nullptr if using builtin)
   * @param type Which font type to get
   */
  SdFont* getActiveFont(SdFontType type = SdFontType::READER);

  /**
   * Check if SD font is active (vs builtin)
   * @param type Which font type to check
   */
  bool isSdFontActive(SdFontType type = SdFontType::READER) const {
    if (type == SdFontType::UI) {
      return _uiSelectedIndex >= 0 && _uiActiveFont.isLoaded();
    }
    return _selectedIndex >= 0 && _activeFont.isLoaded();
  }

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

  // Reader font (for book content)
  int _selectedIndex = -1;  // -1 = use builtin font
  SdFont _activeFont;

  // UI font (for menus/settings)
  int _uiSelectedIndex = -1;  // -1 = use builtin font
  SdFont _uiActiveFont;

  bool loadSelectedFont(SdFontType type);
  bool parseFilename(const char* filename, SdFontInfo& info);
};
