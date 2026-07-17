#pragma once
#include "activities/Activity.h"

class Bitmap;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout) {}
  void onEnter() override;

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  // glance: overlay clock/weather/streak (skipped for book-cover screens so the art stays clean)
  void renderBitmapSleepScreen(const Bitmap& bitmap, bool glance = false) const;
  void renderLastScreenSleepScreen() const;
  void renderBlankSleepScreen() const;
  // SkyPoint: draw the glanceable info pill (top center). No-op when disabled
  // in settings or when no trustworthy data is available.
  void drawGlanceOverlay() const;

  bool fromTimeout = false;
};
