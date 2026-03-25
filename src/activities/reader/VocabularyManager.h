#pragma once

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <string>

// Appends word + definition entries to a TSV vocabulary file on SD card.
// Format: word<TAB>definition<TAB>book_title
// Compatible with Anki import (first two columns = front/back).
class VocabularyManager {
 public:
  static constexpr const char* VOCAB_PATH = "/vocabulary.tsv";

  // Append a vocabulary entry. Returns true on success.
  static bool addEntry(const std::string& word, const std::string& definition, const std::string& bookTitle) {
    FsFile file = Storage.open(VOCAB_PATH, O_RDWR | O_CREAT | O_APPEND);
    if (!file) {
      LOG_ERR("VOC", "Cannot open vocabulary file for append");
      return false;
    }

    // Format: word\tdefinition\tbook_title\n
    char buf[512];
    int len = snprintf(buf, sizeof(buf), "%s\t%s\t%s\n", word.c_str(), definition.c_str(), bookTitle.c_str());

    if (len > 0 && len < static_cast<int>(sizeof(buf))) {
      file.write(reinterpret_cast<const uint8_t*>(buf), len);
    }

    file.close();
    LOG_DBG("VOC", "Saved: %s", word.c_str());
    return true;
  }
};
