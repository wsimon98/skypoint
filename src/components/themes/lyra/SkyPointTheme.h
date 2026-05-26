#pragma once

#include "components/themes/lyra/Lyra3CoversTheme.h"

class GfxRenderer;
struct Rect;

// SkyPoint theme metrics — inherits the Lyra3Covers layout (three-cover hero
// strip across the top) and just rebrands the header. Keeping the metrics
// identical means we don't fork render math; we only customize chrome.
namespace SkyPointMetrics {
constexpr ThemeMetrics values = Lyra3CoversMetrics::values;
}  // namespace SkyPointMetrics

class SkyPointTheme : public Lyra3CoversTheme {
 public:
  // When a title is supplied (settings page, file browser, etc.) we defer to
  // the LyraTheme header. When the caller passes nullptr — which HomeActivity
  // does on the home screen — we render a fixed "SkyPoint" wordmark so the
  // home feels branded instead of empty.
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                  const char* subtitle = nullptr) const override;

  // Two-column tile grid (icon top, label bottom) instead of the inherited
  // vertical list. Gives the home screen a more visually distinct shape.
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
};
