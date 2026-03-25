#pragma once

#include <HalStorage.h>
#include <Logging.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

// Leitner-box spaced repetition for vocabulary review.
// Uses session-based intervals (no RTC required).
// Storage format: first line = session counter, then word\tbox\tnextSession per line.
class SpacedRepetition {
 public:
  static constexpr const char* SRS_PATH = "/vocabulary_srs.tsv";
  static constexpr const char* SRS_TMP_PATH = "/vocabulary_srs.tmp";
  static constexpr int MAX_BOX = 4;
  static constexpr uint32_t BOX_INTERVALS[] = {1, 2, 4, 8, 16};

  struct Entry {
    std::string word;
    uint8_t box = 0;
    uint32_t nextSession = 0;
  };

  bool load() {
    entries.clear();
    currentSession = 0;

    FsFile file;
    if (!Storage.openFileForRead("SRS", SRS_PATH, file)) {
      LOG_DBG("SRS", "No SRS file found, starting fresh");
      return true;
    }

    entries.reserve(64);
    char line[256];
    bool firstLine = true;

    while (file.available()) {
      int len = 0;
      int c;
      while (len < static_cast<int>(sizeof(line)) - 1 && (c = file.read()) >= 0) {
        if (c == '\n') break;
        if (c == '\r') continue;
        line[len++] = static_cast<char>(c);
      }
      if (len == 0) continue;
      line[len] = '\0';

      if (firstLine) {
        firstLine = false;
        currentSession = static_cast<uint32_t>(strtoul(line, nullptr, 10));
        continue;
      }

      char* tab1 = strchr(line, '\t');
      if (!tab1) continue;
      *tab1 = '\0';

      char* tab2 = strchr(tab1 + 1, '\t');
      if (!tab2) continue;
      *tab2 = '\0';

      Entry entry;
      entry.word = line;
      entry.box = static_cast<uint8_t>(atoi(tab1 + 1));
      entry.nextSession = static_cast<uint32_t>(strtoul(tab2 + 1, nullptr, 10));
      if (entry.box > MAX_BOX) entry.box = MAX_BOX;

      entries.push_back(std::move(entry));
    }
    file.close();
    LOG_DBG("SRS", "Loaded %d entries, session=%lu", static_cast<int>(entries.size()),
            static_cast<unsigned long>(currentSession));
    return true;
  }

  bool save() const {
    // Write to temp file first, then rename for crash safety
    FsFile file;
    if (!Storage.openFileForWrite("SRS", SRS_TMP_PATH, file)) {
      LOG_ERR("SRS", "Failed to open SRS temp file for write");
      return false;
    }

    char buf[256];
    int len = snprintf(buf, sizeof(buf), "%lu\n", static_cast<unsigned long>(currentSession));
    if (len > 0) file.write(reinterpret_cast<const uint8_t*>(buf), len);

    for (const auto& e : entries) {
      len = snprintf(buf, sizeof(buf), "%s\t%d\t%lu\n", e.word.c_str(), e.box,
                     static_cast<unsigned long>(e.nextSession));
      if (len > 0 && len < static_cast<int>(sizeof(buf))) {
        file.write(reinterpret_cast<const uint8_t*>(buf), len);
      } else {
        LOG_ERR("SRS", "Entry too long, skipping: %s", e.word.c_str());
      }
    }
    file.close();

    Storage.remove(SRS_PATH);
    if (!Storage.rename(SRS_TMP_PATH, SRS_PATH)) {
      LOG_ERR("SRS", "Failed to rename SRS temp file");
      return false;
    }
    return true;
  }

  void beginReviewSession() { currentSession++; }

  // Rate: 0=Again, 1=Good, 2=Easy
  void rate(const std::string& word, int quality) {
    Entry* e = findOrCreate(word);
    if (!e) return;

    if (quality == 0) {
      e->box = 0;
      e->nextSession = currentSession + BOX_INTERVALS[0];
    } else if (quality == 2) {
      e->box = MAX_BOX;
      e->nextSession = currentSession + BOX_INTERVALS[MAX_BOX];
    } else {
      if (e->box < MAX_BOX) e->box++;
      e->nextSession = currentSession + BOX_INTERVALS[e->box];
    }
  }

  bool isDue(const std::string& word) const {
    const Entry* e = find(word);
    if (!e) return true;  // new word is always due
    return currentSession >= e->nextSession;
  }

  uint32_t getSession() const { return currentSession; }

 private:
  std::vector<Entry> entries;
  uint32_t currentSession = 0;

  Entry* findOrCreate(const std::string& word) {
    for (auto& e : entries) {
      if (e.word == word) return &e;
    }
    if (entries.size() >= 200) return nullptr;
    Entry newEntry;
    newEntry.word = word;
    entries.push_back(std::move(newEntry));
    return &entries.back();
  }

  const Entry* find(const std::string& word) const {
    for (const auto& e : entries) {
      if (e.word == word) return &e;
    }
    return nullptr;
  }
};
