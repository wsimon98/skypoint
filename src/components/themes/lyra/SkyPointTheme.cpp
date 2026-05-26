#include "components/themes/lyra/SkyPointTheme.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "components/icons/book.h"
#include "components/icons/folder.h"
#include "components/icons/hotspot.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "components/icons/wifi.h"
#include "components/themes/lyra/LyraTheme.h"
#include "fontIds.h"

namespace {
// Tile grid metrics. Kept local to the SkyPoint theme so they don't bleed into
// other layouts. 32px icons match what LyraTheme uses elsewhere so the icon
// headers we already ship work without resizing.
constexpr int gridCols = 2;
constexpr int gridGap = 12;
constexpr int gridPaddingSide = 20;
constexpr int tileCornerRadius = 8;
constexpr int tileIconSize = 32;
constexpr int tileLabelTopGap = 10;
constexpr int tileTopInset = 18;

const uint8_t* skyPointIconFor(UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:
      return FolderIcon;
    case UIIcon::Book:
      return BookIcon;
    case UIIcon::Recent:
      return RecentIcon;
    case UIIcon::Settings:
      return Settings2Icon;
    case UIIcon::Transfer:
      return TransferIcon;
    case UIIcon::Library:
      return LibraryIcon;
    case UIIcon::Wifi:
      return WifiIcon;
    case UIIcon::Hotspot:
      return HotspotIcon;
    default:
      return nullptr;
  }
}
}  // namespace

void SkyPointTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const {
  if (title != nullptr) {
    Lyra3CoversTheme::drawHeader(renderer, rect, title, subtitle);
    return;
  }

  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryX = rect.x + rect.width - 12 - LyraMetrics::values.batteryWidth;
  drawBatteryRight(renderer,
                   Rect{batteryX, rect.y + 5, LyraMetrics::values.batteryWidth, LyraMetrics::values.batteryHeight},
                   showBatteryPercentage);

  const int textY = rect.y + (rect.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
  renderer.drawText(UI_12_FONT_ID, rect.x + LyraMetrics::values.contentSidePadding, textY, "SkyPoint", true,
                    EpdFontFamily::BOLD);
}

void SkyPointTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                   const std::function<std::string(int index)>& buttonLabel,
                                   const std::function<UIIcon(int index)>& rowIcon) const {
  if (buttonCount <= 0) return;

  const int rows = (buttonCount + gridCols - 1) / gridCols;
  const int gridW = rect.width - 2 * gridPaddingSide;
  const int tileW = (gridW - gridGap * (gridCols - 1)) / gridCols;
  // Available vertical space is split across rows. Cap each tile so even a
  // small grid (e.g. two items) doesn't produce huge stretched tiles.
  const int availH = rect.height - gridGap * std::max(0, rows - 1);
  const int tileH = std::min(140, availH / std::max(1, rows));

  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);

  for (int i = 0; i < buttonCount; ++i) {
    const int col = i % gridCols;
    const int row = i / gridCols;
    const int x = rect.x + gridPaddingSide + col * (tileW + gridGap);
    const int y = rect.y + row * (tileH + gridGap);

    const bool selected = (i == selectedIndex);

    if (selected) {
      renderer.fillRoundedRect(x, y, tileW, tileH, tileCornerRadius, Color::LightGray);
    } else {
      // 7-arg form: (x, y, w, h, lineWidth, cornerRadius, state=true draws black)
      renderer.drawRoundedRect(x, y, tileW, tileH, 1, tileCornerRadius, true);
    }

    if (rowIcon != nullptr) {
      const uint8_t* iconBitmap = skyPointIconFor(rowIcon(i));
      if (iconBitmap != nullptr) {
        const int iconX = x + (tileW - tileIconSize) / 2;
        const int iconY = y + tileTopInset;
        renderer.drawIcon(iconBitmap, iconX, iconY, tileIconSize, tileIconSize);
      }
    }

    const std::string labelStr = buttonLabel(i);
    const int labelWidth = renderer.getTextWidth(UI_12_FONT_ID, labelStr.c_str(), EpdFontFamily::BOLD);
    const int labelX = x + (tileW - labelWidth) / 2;
    const int labelY = y + tileTopInset + tileIconSize + tileLabelTopGap;
    if (labelY + lineHeight <= y + tileH) {
      renderer.drawText(UI_12_FONT_ID, labelX, labelY, labelStr.c_str(), true, EpdFontFamily::BOLD);
    }
  }
}
