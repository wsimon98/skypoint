#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Network hub reached from the home screen's single "Network" entry:
 * - "File Transfer" — the Wi-Fi file manager / Calibre / hotspot flow
 * - "Web"           — SkyPortal, the portal.md link reader
 *
 * Grouping both behind one home entry keeps the home menu at four base items,
 * which matters because the home selector index spans the theme's recent-book
 * cover tiles as well as the menu.
 */
class NetworkMenuActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

 public:
  explicit NetworkMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("NetworkMenu", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
