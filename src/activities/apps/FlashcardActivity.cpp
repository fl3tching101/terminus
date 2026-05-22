#include "FlashcardActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <string>
#include "esp_random.h"
#include "bootloader_random.h"

#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "CrossPointSettings.h"
#include "SdCardFontSystem.h"
#include <FontCacheManager.h>
#include "fontIds.h"

void FlashcardActivity::scanDecks() {
  deckFiles.clear();
  Storage.mkdir(DECK_DIR);
  HalFile dir = Storage.open(DECK_DIR);
  if (!dir) return;
  HalFile entry;
  while ((entry = dir.openNextFile())) {
    char nameBuf[64];
    entry.getName(nameBuf, sizeof(nameBuf));
    std::string name = nameBuf;
    entry.close();
    if (name.size() > 4 && name.substr(name.size() - 4) == ".csv") {
      deckFiles.push_back(std::string(DECK_DIR) + "/" + name);
    }
  }
  dir.close();
}

int FlashcardActivity::countCards(const std::string& path) {
  auto file = Storage.open(path.c_str());
  if (!file) return 0;

  char line[300];
  int count = 0;
  while (file.available()) {
    int len = 0;
    while (file.available() && len < (int)sizeof(line) - 1) {
      char c = (char)file.read();
      if (c == '\n') break;
      line[len++] = c;
    }
    line[len] = '\0';
    if (len == 0) continue;

    char* comma = strchr(line, ',');
    if (!comma) continue;
    count++;
  }
  file.close();
  return count;
}

bool FlashcardActivity::parseNextCard() {
  char line[300];
  while (csvFile.available()) {
    int len = 0;
    while (csvFile.available() && len < (int)sizeof(line) - 1) {
      char c = (char)csvFile.read();
      if (c == '\n') break;
      line[len++] = c;
    }
    line[len] = '\0';
    if (len == 0) continue;

    char* comma = strchr(line, ',');
    if (!comma) continue;
    *comma = '\0';

    currentCard = std::make_unique<Card>();
    memset(currentCard.get(), 0, sizeof(Card));
    strncpy(currentCard->front, line, sizeof(currentCard->front) - 1);
    strncpy(currentCard->back, comma + 1, sizeof(currentCard->back) - 1);
    return true;
  }
  return false;
}

void FlashcardActivity::buildOffsets(const std::string& path) {
  auto file = Storage.open(path.c_str());
  if (!file) return;

  offsetTable.clear();
  
  // Scan the entire file to find byte offsets of all non-empty lines.
  // We record the position *before* reading each line, so we can seek back there later.
  char line[300];
  while (file.available()) {
    size_t startPos = file.position();
    int len = 0;
    bool hasNewline = false;
    while (file.available() && len < (int)sizeof(line) - 1) {
      char c = (char)file.read();
      if (c == '\n') {
        hasNewline = true;
        break;
      }
      line[len++] = c;
    }
    line[len] = '\0';

    // Only record offsets for non-empty lines that contain a comma (valid CSV cards)
    if (len > 0 && strchr(line, ',')) {
      offsetTable.push_back(static_cast<int>(startPos));
    }
  }
  file.close();
}

void FlashcardActivity::shuffleDeck() {
  // Fisher-Yates shuffle using esp_random() for uniform distribution.
  // Cast to uint32_t first — esp_random() returns signed int and % of negative
  // values produces a negative index, corrupting the offset table heap.
  int n = static_cast<int>(offsetTable.size());
  for (int i = n - 1; i > 0; --i) {
    int j = static_cast<int>(static_cast<uint32_t>(esp_random()) % (i + 1));
    std::swap(offsetTable[i], offsetTable[j]);
  }
}

void FlashcardActivity::onEnter() {
  Activity::onEnter();
  scanDecks();
  state = DECK_SELECT;
  deckIndex = 0;
  cardIndex = 0;
  bootloader_random_enable();

  sdFontSystem.ensureLoaded(renderer);

  requestUpdate();
}

void FlashcardActivity::onExit() {
  if (csvFile.isOpen()) csvFile.close();
  currentCard.reset();
  offsetTable.clear();
  bootloader_random_disable();
  Activity::onExit();
}

void FlashcardActivity::loop() {
  if (state == DECK_SELECT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) { finish(); return; }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) || mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      deckIndex = ButtonNavigator::previousIndex(deckIndex, (int)deckFiles.size());
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down) || mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      deckIndex = ButtonNavigator::nextIndex(deckIndex, (int)deckFiles.size());
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) && !deckFiles.empty()) {
      totalCards = countCards(deckFiles[deckIndex]);
      
      // Build offset table and shuffle deck order
      buildOffsets(deckFiles[deckIndex]);
      shuffleDeck();

      cardIndex = 0;
      correctCount = 0;
      wrongCount = 0;
      eofReached = false;

      csvFile = Storage.open(deckFiles[deckIndex].c_str());
      if (csvFile && !offsetTable.empty()) {
        // Seek to the first shuffled offset and load the first card
        csvFile.seekSet(offsetTable[cardIndex]);
        parseNextCard();
        state = CARD_FRONT;
        requestUpdate();
      } else {
        LOG_ERR("FLASHCARD", "No valid cards in deck");
        if (csvFile.isOpen()) csvFile.close();
      }
    }
    return;
  }

  if (state == CARD_FRONT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = DECK_SELECT; requestUpdate(); return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      state = CARD_BACK; requestUpdate();
    }
    return;
  }

  if (state == CARD_BACK) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      csvFile.close();
      currentCard.reset();
      state = DECK_SELECT; requestUpdate(); return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      wrongCount++;
      cardIndex++;
      if (cardIndex >= totalCards) {
        csvFile.close();
        state = STATS; requestUpdate(); return;
      }
      // Seek to next shuffled offset and parse the card
      csvFile.seekSet(offsetTable[cardIndex]);
      parseNextCard();
      state = CARD_FRONT; requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      correctCount++;
      cardIndex++;
      if (cardIndex >= totalCards) {
        csvFile.close();
        state = STATS; requestUpdate(); return;
      }
      // Seek to next shuffled offset and parse the card
      csvFile.seekSet(offsetTable[cardIndex]);
      parseNextCard();
      state = CARD_FRONT; requestUpdate();
    }
    return;
  }

  if (state == STATS) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      currentCard.reset();
      state = DECK_SELECT; requestUpdate();
    }
    return;
  }
}

void FlashcardActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Flashcards");

  switch (state) {
    case DECK_SELECT: renderDeckSelect(); break;
    case CARD_FRONT:  renderCardFront();  break;
    case CARD_BACK:   renderCardBack();   break;
    case STATS:       renderStats();      break;
  }
  renderer.displayBuffer();
}

void FlashcardActivity::renderDeckSelect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int listTop = metrics.topPadding + metrics.headerHeight;
  const int listH = pageHeight - listTop - metrics.buttonHintsHeight;

  if (deckFiles.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, listTop + listH / 2, "No decks in /terminus/flashcards/");
  } else {
    GUI.drawList(renderer, Rect{0, listTop, pageWidth, listH}, (int)deckFiles.size(), deckIndex,
      [this](int i) -> std::string {
        const std::string& p = deckFiles[i];
        size_t slash = p.rfind('/');
        return (slash != std::string::npos) ? p.substr(slash + 1) : p;
      });
  }
  const auto labels = mappedInput.mapLabels("Back", "Load", "^", "v");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void FlashcardActivity::renderCardFront() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int mid = (metrics.topPadding + metrics.headerHeight + pageHeight - metrics.buttonHintsHeight) / 2;

  char prog[32];
  snprintf(prog, sizeof(prog), "Card %d / %d", cardIndex + 1, totalCards);

  // Prewarming SD font
  auto* fcm = renderer.getFontCacheManager();
  fcm->resetStats();
  auto scope = fcm->createPrewarmScope();
  renderer.drawCenteredText(SETTINGS.getReaderFontId(), mid - 80, prog);
  renderer.drawCenteredText(SETTINGS.getReaderFontId(), mid - 20, currentCard->front, true, EpdFontFamily::REGULAR);
  scope.endScanAndPrewarm();
  fcm->logStats("prewarm");

  renderer.drawCenteredText(SETTINGS.getReaderFontId(), mid - 80, prog);
  renderer.drawCenteredText(SETTINGS.getReaderFontId(), mid - 20, currentCard->front, true, EpdFontFamily::REGULAR);

  const auto labels = mappedInput.mapLabels("Quit", "Flip", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void FlashcardActivity::renderCardBack() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int mid = (metrics.topPadding + metrics.headerHeight + pageHeight - metrics.buttonHintsHeight) / 2;

  // Prewarming SD font
  auto* fcm = renderer.getFontCacheManager();
  fcm->resetStats();
  auto scope = fcm->createPrewarmScope();
  renderer.drawCenteredText(SETTINGS.getReaderFontId(), mid - 80, currentCard->front, true, EpdFontFamily::REGULAR);
  renderer.drawCenteredText(SETTINGS.getReaderFontId(), mid - 20, currentCard->back, true, EpdFontFamily::REGULAR);
  scope.endScanAndPrewarm();
  fcm->logStats("prewarm");

  renderer.drawCenteredText(SETTINGS.getReaderFontId(), mid - 80, currentCard->front, true, EpdFontFamily::REGULAR);
  renderer.drawCenteredText(SETTINGS.getReaderFontId(), mid - 20, currentCard->back, true, EpdFontFamily::REGULAR);

  const auto labels = mappedInput.mapLabels("Quit", "", "Wrong", "Correct");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void FlashcardActivity::renderStats() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int top = metrics.topPadding + metrics.headerHeight + 30;

  int total = correctCount + wrongCount;
  int pct = (total > 0) ? (correctCount * 100 / total) : 0;

  renderer.drawCenteredText(UI_12_FONT_ID, top, "Results", true, EpdFontFamily::BOLD);

  char buf[48];
  snprintf(buf, sizeof(buf), "Total: %d", totalCards);
  renderer.drawCenteredText(UI_10_FONT_ID, top + 50, buf);
  snprintf(buf, sizeof(buf), "Correct: %d", correctCount);
  renderer.drawCenteredText(UI_10_FONT_ID, top + 80, buf);
  snprintf(buf, sizeof(buf), "Wrong: %d", wrongCount);
  renderer.drawCenteredText(UI_10_FONT_ID, top + 110, buf);
  snprintf(buf, sizeof(buf), "Score: %d%%", pct);
  renderer.drawCenteredText(UI_12_FONT_ID, top + 150, buf, true, EpdFontFamily::BOLD);

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
