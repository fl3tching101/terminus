#pragma once
#include <vector>
#include <string>
#include <memory>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class FlashcardActivity final : public Activity {
  public:
    explicit FlashcardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
        : Activity("Flashcards", renderer, mappedInput) {}

    void onEnter() override;
    void onExit() override;
    void loop() override;
    void render(RenderLock&&) override;

  private:
    enum State { DECK_SELECT, CARD_FRONT, CARD_BACK, STATS };
    State state = DECK_SELECT;

    struct Card {
      char front[128];
      char back[128];
      int correct;
      int wrong;
    };

    std::vector<std::string> deckFiles;
    std::unique_ptr<Card> currentCard;
    
    // Shuffle support: byte offsets into CSV file for each non-empty line
    std::vector<int> offsetTable;

    int totalCards = 0;
    int correctCount = 0;
    int wrongCount = 0;
    bool eofReached = false;
    HalFile csvFile;

    int deckIndex = 0;
    int cardIndex = 0;

    static constexpr const char* DECK_DIR = "/terminus/flashcards";

    void scanDecks();
    int countCards(const std::string& path);
    bool parseNextCard();
    void buildOffsets(const std::string& path);
    void shuffleDeck();
    void renderDeckSelect() const;
    void renderCardFront() const;
    void renderCardBack() const;
    void renderStats() const;
};
