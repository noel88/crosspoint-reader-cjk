#include "SdFontManager.h"

#include <HalStorage.h>
#include <HardwareSerial.h>
#include <Serialization.h>

#include <cstring>

// Out-of-class definitions for static constexpr members
constexpr int SdFontManager::MAX_FONTS;
constexpr const char* SdFontManager::FONTS_DIR;
constexpr const char* SdFontManager::SETTINGS_FILE;
constexpr uint8_t SdFontManager::SETTINGS_VERSION;

SdFontManager& SdFontManager::getInstance() {
  static SdFontManager instance;
  return instance;
}

void SdFontManager::scanFonts() {
  _fontCount = 0;

  HalFile dir = Storage.open(FONTS_DIR, O_RDONLY);
  if (!dir) {
    Serial.printf("[SDFONT_MGR] Cannot open fonts directory: %s\n", FONTS_DIR);
    return;
  }

  if (!dir.isDirectory()) {
    Serial.printf("[SDFONT_MGR] %s is not a directory\n", FONTS_DIR);
    dir.close();
    return;
  }

  dir.rewindDirectory();
  HalFile entry = dir.openNextFile();
  while (_fontCount < MAX_FONTS && entry) {
    if (entry.isDirectory()) {
      entry.close();
      entry = dir.openNextFile();
      continue;
    }

    char filename[64];
    entry.getName(filename, sizeof(filename));
    entry.close();

    // Check .epdfont extension (case insensitive)
    const char* ext = strstr(filename, ".epdfont");
    if (!ext) {
      ext = strstr(filename, ".EPDFONT");
    }
    if (!ext) {
      entry = dir.openNextFile();
      continue;
    }

    SdFontInfo& info = _fonts[_fontCount];
    if (parseFilename(filename, info)) {
      Serial.printf("[SDFONT_MGR] Found font: %s (%dpt)\n", info.name, info.size);
      _fontCount++;
    }

    entry = dir.openNextFile();
  }

  dir.close();
  Serial.printf("[SDFONT_MGR] Scan complete: %d fonts found\n", _fontCount);
}

bool SdFontManager::parseFilename(const char* filename, SdFontInfo& info) {
  strncpy(info.filename, filename, sizeof(info.filename) - 1);
  info.filename[sizeof(info.filename) - 1] = '\0';

  // Copy filename for parsing
  char nameCopy[64];
  strncpy(nameCopy, filename, sizeof(nameCopy) - 1);
  nameCopy[sizeof(nameCopy) - 1] = '\0';

  // Remove .epdfont extension
  char* extPos = strstr(nameCopy, ".epdfont");
  if (!extPos) {
    extPos = strstr(nameCopy, ".EPDFONT");
  }
  if (extPos) {
    *extPos = '\0';
  }

  // Try to parse _size suffix (e.g., "FontName_28")
  char* lastUnderscore = strrchr(nameCopy, '_');
  if (lastUnderscore) {
    int size;
    if (sscanf(lastUnderscore + 1, "%d", &size) == 1) {
      info.size = (uint8_t)size;
      *lastUnderscore = '\0';
    } else {
      info.size = 0;  // Unknown size
    }
  } else {
    info.size = 0;
  }

  // Font name is whatever remains
  strncpy(info.name, nameCopy, sizeof(info.name) - 1);
  info.name[sizeof(info.name) - 1] = '\0';

  return true;
}

const SdFontInfo* SdFontManager::getFontInfo(int index) const {
  if (index < 0 || index >= _fontCount) {
    return nullptr;
  }
  return &_fonts[index];
}

void SdFontManager::selectFont(int index) {
  if (index == _selectedIndex) {
    return;
  }

  _selectedIndex = index;

  if (index >= 0) {
    loadSelectedFont();
  } else {
    _activeFont.unload();
  }

  saveSettings();
}

bool SdFontManager::loadSelectedFont() {
  _activeFont.unload();

  if (_selectedIndex < 0 || _selectedIndex >= _fontCount) {
    return false;
  }

  char filepath[80];
  snprintf(filepath, sizeof(filepath), "%s/%s", FONTS_DIR, _fonts[_selectedIndex].filename);

  return _activeFont.load(filepath);
}

SdFont* SdFontManager::getActiveFont() {
  if (_selectedIndex >= 0 && _activeFont.isLoaded()) {
    return &_activeFont;
  }
  return nullptr;
}

void SdFontManager::saveSettings() {
  Storage.mkdir("/.crosspoint");

  HalFile file;
  if (!Storage.openFileForWrite("SDFONT_MGR", SETTINGS_FILE, file)) {
    Serial.printf("[SDFONT_MGR] Failed to save settings\n");
    return;
  }

  serialization::writePod(file, SETTINGS_VERSION);
  serialization::writePod(file, _selectedIndex);

  // Save selected font filename (for matching when restoring)
  if (_selectedIndex >= 0 && _selectedIndex < _fontCount) {
    serialization::writeString(file, std::string(_fonts[_selectedIndex].filename));
  } else {
    serialization::writeString(file, std::string(""));
  }

  file.close();
  Serial.printf("[SDFONT_MGR] Settings saved\n");
}

void SdFontManager::loadSettings() {
  HalFile file;
  if (!Storage.openFileForRead("SDFONT_MGR", SETTINGS_FILE, file)) {
    Serial.printf("[SDFONT_MGR] No settings file, using defaults\n");
    return;
  }

  uint8_t version;
  serialization::readPod(file, version);
  if (version > SETTINGS_VERSION) {
    Serial.printf("[SDFONT_MGR] Settings version mismatch (%d vs %d)\n", version, SETTINGS_VERSION);
    file.close();
    return;
  }

  int savedIndex;
  serialization::readPod(file, savedIndex);

  std::string savedFilename;
  serialization::readString(file, savedFilename);

  // Find matching font by filename
  if (savedIndex >= 0 && !savedFilename.empty()) {
    for (int i = 0; i < _fontCount; i++) {
      if (savedFilename == _fonts[i].filename) {
        _selectedIndex = i;
        loadSelectedFont();
        Serial.printf("[SDFONT_MGR] Restored font: %s\n", savedFilename.c_str());
        break;
      }
    }
    if (_selectedIndex < 0) {
      Serial.printf("[SDFONT_MGR] Saved font not found: %s\n", savedFilename.c_str());
    }
  }

  file.close();
}
