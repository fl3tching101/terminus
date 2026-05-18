#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"

struct AppEntry {
  std::string name;
  int id; // unique identifier to launch the right activity
};

class AppsLauncherActivity final : public Activity {
 public:
  explicit AppsLauncherActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Apps", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::vector<AppEntry> apps;
  int selectedIndex = 0;

  void launchApp(int id);
};
