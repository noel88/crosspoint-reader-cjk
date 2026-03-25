#pragma once
#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

// Displays saved vocabulary entries as flashcards.
// Front: word (large text). Press Confirm to flip and show definition.
// Up/Down: navigate between cards. Back: exit.
class VocabularyViewActivity final : public Activity {
 public:
  explicit VocabularyViewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct VocabEntry {
    std::string word;
    std::string definition;
    std::string bookTitle;
  };

  std::vector<VocabEntry> entries;
  int currentIndex = 0;
  bool flipped = false;
  ButtonNavigator buttonNavigator;

  void loadEntries();
  void renderFront();
  void renderBack();
  void wrapText(int fontId, const char* text, int x, int y, int maxWidth, int lineHeight, int maxLines) const;
};
