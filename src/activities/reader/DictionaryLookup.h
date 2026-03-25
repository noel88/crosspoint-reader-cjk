#pragma once

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>
#include <string>

// Performs binary search on a sorted TSV dictionary file stored on SD card.
// Dictionary format: each line is "word\tdefinition\n", sorted by word.
// Uses SD seek-based binary search — no RAM needed for the dictionary.
class DictionaryLookup {
 public:
  // Look up a word in the dictionary file. Returns the definition, or empty string if not found.
  static std::string lookup(const char* dictPath, const std::string& word) {
    FsFile file;
    if (!Storage.openFileForRead("DIC", dictPath, file)) {
      LOG_ERR("DIC", "Cannot open dictionary: %s", dictPath);
      return "";
    }

    const uint32_t fileSize = file.size();
    if (fileSize == 0) {
      file.close();
      return "";
    }

    std::string result;
    uint32_t lo = 0;
    uint32_t hi = fileSize;

    while (lo < hi) {
      uint32_t mid = lo + (hi - lo) / 2;

      // Seek to mid, then find the start of the current line
      file.seek(mid);
      if (mid > 0) {
        // Skip to next line boundary
        skipToNextLine(file);
      }

      uint32_t lineStart = file.position();
      if (lineStart >= hi) {
        // Overshot — narrow search to lower half
        hi = mid;
        continue;
      }

      // Read the word portion (up to tab)
      char buf[128];
      int len = readUntilTab(file, buf, sizeof(buf));
      if (len <= 0) {
        // Empty line or read error
        lo = lineStart + 1;
        continue;
      }

      int cmp = word.compare(0, word.size(), buf, len);

      if (cmp == 0) {
        // Found! Read the definition (rest of line after tab)
        result = readLine(file);
        break;
      } else if (cmp < 0) {
        hi = mid;
      } else {
        // Skip to end of this line for the next iteration
        skipToNextLine(file);
        lo = file.position();
      }
    }

    file.close();
    return result;
  }

 private:
  static void skipToNextLine(FsFile& file) {
    int c;
    while ((c = file.read()) >= 0) {
      if (c == '\n') return;
    }
  }

  // Read characters until tab or newline. Returns length of word read.
  static int readUntilTab(FsFile& file, char* buf, int bufSize) {
    int i = 0;
    int c;
    while (i < bufSize - 1 && (c = file.read()) >= 0) {
      if (c == '\t' || c == '\n' || c == '\r') break;
      buf[i++] = static_cast<char>(c);
    }
    buf[i] = '\0';
    return i;
  }

  // Read the rest of the line (definition after tab).
  static std::string readLine(FsFile& file) {
    std::string line;
    line.reserve(128);
    int c;
    while ((c = file.read()) >= 0) {
      if (c == '\n' || c == '\r') break;
      line += static_cast<char>(c);
    }
    return line;
  }
};
