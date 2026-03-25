#pragma once
#include <string>
#include <vector>

#include "../Activity.h"
#include "SpacedRepetition.h"
#include "util/ButtonNavigator.h"

// Vocabulary flashcard viewer with Leitner-box spaced repetition.
// MODE_SELECT: choose Browse All or Review (due cards).
// BROWSE: navigate all cards freely. REVIEW: SRS-guided review with ratings.
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

  enum class ViewState { MODE_SELECT, BROWSE, REVIEW, REVIEW_DONE };

  std::vector<VocabEntry> entries;
  int currentIndex = 0;
  bool flipped = false;
  ButtonNavigator buttonNavigator;

  // Mode selection and SRS
  ViewState state = ViewState::MODE_SELECT;
  int modeSelection = 0;
  SpacedRepetition srs;
  std::vector<int> dueIndices;
  int duePosition = 0;
  bool srsDirty = false;

  void loadEntries();
  void buildDueList();
  void advanceReview();
  void renderModeSelect();
  void renderFront();
  void renderBack();
  void renderReviewBack();
  void renderReviewDone();
  void wrapText(int fontId, const char* text, int x, int y, int maxWidth, int lineHeight, int maxLines) const;
};
