#include "AppsLauncherActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

#include "CalculatorActivity.h"
#include "FlashcardActivity.h"

static constexpr int APP_CALCULATOR = 0;
static constexpr int APP_FLASHCARD = 1;

void AppsLauncherActivity::onEnter() {
  Activity::onEnter();

  apps.clear();
  apps.push_back({"Calculator", APP_CALCULATOR});
  apps.push_back({"Flashcards", APP_FLASHCARD});

  selectedIndex = 0;
  requestUpdate();
}

void AppsLauncherActivity::onExit() { Activity::onExit(); }

void AppsLauncherActivity::launchApp(int id) {
  switch (id) {
    case APP_CALCULATOR:
      activityManager.pushActivity(
          std::make_unique<CalculatorActivity>(renderer, mappedInput));
      break;
    case APP_FLASHCARD:
      activityManager.pushActivity(
          std::make_unique<FlashcardActivity>(renderer, mappedInput));
      break;
  }
}

void AppsLauncherActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    selectedIndex = (selectedIndex > 0) ? selectedIndex - 1 : static_cast<int>(apps.size()) - 1;
    requestUpdate();
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    selectedIndex = (selectedIndex > 0) ? selectedIndex - 1 : static_cast<int>(apps.size()) - 1;
    requestUpdate();
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    selectedIndex = (selectedIndex + 1) % static_cast<int>(apps.size());
    requestUpdate();
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    selectedIndex = (selectedIndex + 1) % static_cast<int>(apps.size());
    requestUpdate();
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) && !apps.empty()) {
    launchApp(apps[selectedIndex].id);
  }
}

void AppsLauncherActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Apps");

  // List area below header
  const int listTop = metrics.topPadding + metrics.headerHeight;
  const int listH = pageHeight - listTop - metrics.buttonHintsHeight;

  GUI.drawList(renderer, Rect{0, listTop, pageWidth, listH}, static_cast<int>(apps.size()), selectedIndex,
      [this](int i) -> std::string { return apps[i].name; });

  const auto labels = mappedInput.mapLabels("Back", "Select", "^", "v");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
