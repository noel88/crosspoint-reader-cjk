#include "SdFontManager.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include <cctype>
#include <cstring>

// Case-insensitive string comparison (avoid strings.h dependency)
static int strcasecmpLocal(const char* s1, const char* s2) {
  while (*s1 && *s2) {
    int c1 = tolower(static_cast<unsigned char>(*s1));
    int c2 = tolower(static_cast<unsigned char>(*s2));
    if (c1 != c2) return c1 - c2;
    s1++;
    s2++;
  }
  return tolower(static_cast<unsigned char>(*s1)) - tolower(static_cast<unsigned char>(*s2));
}

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

  LOG_DBG("SDFONT", "Scanning fonts directory: %s", FONTS_DIR);

  HalFile dir = Storage.open(FONTS_DIR, O_RDONLY);
  if (!dir) {
    LOG_DBG("SDFONT", "Cannot open fonts directory: %s", FONTS_DIR);
    return;
  }

  if (!dir.isDirectory()) {
    LOG_DBG("SDFONT", "%s is not a directory", FONTS_DIR);
    dir.close();
    return;
  }

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
    size_t len = strlen(filename);
    bool isEpdfont = false;
    if (len > 8) {
      const char* ext = filename + len - 8;
      isEpdfont = (strcasecmpLocal(ext, ".epdfont") == 0);
    }

    if (!isEpdfont) {
      entry = dir.openNextFile();
      continue;
    }

    SdFontInfo& info = _fonts[_fontCount];
    if (parseFilename(filename, info)) {
      LOG_DBG("SDFONT", "Found font: %s (%dpt)", info.name, info.size);
      _fontCount++;
    }

    entry = dir.openNextFile();
  }

  dir.close();
  LOG_DBG("SDFONT", "Scan complete: %d fonts found", _fontCount);
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

void SdFontManager::selectFont(int index, SdFontType type) {
  int& selectedIndex = (type == SdFontType::UI) ? _uiSelectedIndex : _selectedIndex;

  if (index == selectedIndex) {
    return;
  }

  selectedIndex = index;

  if (index >= 0) {
    loadSelectedFont(type);
  } else {
    if (type == SdFontType::UI) {
      _uiActiveFont.unload();
    } else {
      _activeFont.unload();
    }
  }

  saveSettings();
}

bool SdFontManager::loadSelectedFont(SdFontType type) {
  SdFont& activeFont = (type == SdFontType::UI) ? _uiActiveFont : _activeFont;
  int selectedIndex = (type == SdFontType::UI) ? _uiSelectedIndex : _selectedIndex;

  activeFont.unload();

  if (selectedIndex < 0 || selectedIndex >= _fontCount) {
    return false;
  }

  char filepath[80];
  snprintf(filepath, sizeof(filepath), "%s/%s", FONTS_DIR, _fonts[selectedIndex].filename);

  return activeFont.load(filepath);
}

SdFont* SdFontManager::getActiveFont(SdFontType type) {
  if (type == SdFontType::UI) {
    if (_uiSelectedIndex >= 0 && _uiActiveFont.isLoaded()) {
      return &_uiActiveFont;
    }
  } else {
    if (_selectedIndex >= 0 && _activeFont.isLoaded()) {
      return &_activeFont;
    }
  }
  return nullptr;
}

void SdFontManager::saveSettings() {
  Storage.mkdir("/.crosspoint");

  HalFile file;
  if (!Storage.openFileForWrite("SDFONT", SETTINGS_FILE, file)) {
    LOG_ERR("SDFONT", "Failed to save settings");
    return;
  }

  serialization::writePod(file, SETTINGS_VERSION);

  // Save reader font selection
  serialization::writePod(file, _selectedIndex);
  if (_selectedIndex >= 0 && _selectedIndex < _fontCount) {
    serialization::writeString(file, std::string(_fonts[_selectedIndex].filename));
  } else {
    serialization::writeString(file, std::string(""));
  }

  // Save UI font selection (new in v2)
  serialization::writePod(file, _uiSelectedIndex);
  if (_uiSelectedIndex >= 0 && _uiSelectedIndex < _fontCount) {
    serialization::writeString(file, std::string(_fonts[_uiSelectedIndex].filename));
  } else {
    serialization::writeString(file, std::string(""));
  }

  file.close();
  LOG_DBG("SDFONT", "Settings saved (reader=%d, ui=%d)", _selectedIndex, _uiSelectedIndex);
}

void SdFontManager::loadSettings() {
  HalFile file;
  if (!Storage.openFileForRead("SDFONT", SETTINGS_FILE, file)) {
    LOG_DBG("SDFONT", "No settings file, using defaults");
    return;
  }

  uint8_t version;
  serialization::readPod(file, version);
  if (version > SETTINGS_VERSION) {
    LOG_ERR("SDFONT", "Settings version mismatch (%d vs %d)", version, SETTINGS_VERSION);
    file.close();
    return;
  }

  // Load reader font selection
  int savedIndex;
  serialization::readPod(file, savedIndex);

  std::string savedFilename;
  serialization::readString(file, savedFilename);

  // Find matching reader font by filename
  if (savedIndex >= 0 && !savedFilename.empty()) {
    for (int i = 0; i < _fontCount; i++) {
      if (savedFilename == _fonts[i].filename) {
        _selectedIndex = i;
        loadSelectedFont(SdFontType::READER);
        LOG_DBG("SDFONT", "Restored reader font: %s", savedFilename.c_str());
        break;
      }
    }
    if (_selectedIndex < 0) {
      LOG_DBG("SDFONT", "Saved reader font not found: %s", savedFilename.c_str());
    }
  }

  // Load UI font selection (v2+)
  if (version >= 2) {
    int savedUiIndex;
    serialization::readPod(file, savedUiIndex);

    std::string savedUiFilename;
    serialization::readString(file, savedUiFilename);

    if (savedUiIndex >= 0 && !savedUiFilename.empty()) {
      for (int i = 0; i < _fontCount; i++) {
        if (savedUiFilename == _fonts[i].filename) {
          _uiSelectedIndex = i;
          loadSelectedFont(SdFontType::UI);
          LOG_DBG("SDFONT", "Restored UI font: %s", savedUiFilename.c_str());
          break;
        }
      }
      if (_uiSelectedIndex < 0) {
        LOG_DBG("SDFONT", "Saved UI font not found: %s", savedUiFilename.c_str());
      }
    }
  }

  file.close();
}
