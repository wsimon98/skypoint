#pragma once
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * SkyPortal: a button-only web-article reader. /portal.md on the SD card (one
 * markdown link per line, edited off-device) is the user's link list. Selecting
 * an entry fetches the page, strips it to clean text (HtmlToText), caches it as
 * a .txt under /.portal/read/, and offers "Read page" plus a browsable list of
 * the links found on the page — so the web is walked entirely with buttons.
 * Cached pages open offline. There is deliberately no URL entry: no typing.
 */
class PortalActivity final : public Activity {
 public:
  enum class PortalState { NO_FILE, BROWSING, WIFI_SELECTION, FETCHING, PAGE_MENU, ERROR };

  explicit PortalActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Portal", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct PortalEntry {
    std::string title;
    std::string url;
  };

  // The page menu is built as an explicit action list rather than fixed slots,
  // so entries can be omitted (e.g. no readable text) without index arithmetic.
  enum class PageAction { ReadPage, ShowLinks, BackToList };

  ButtonNavigator buttonNavigator;
  PortalState state = PortalState::BROWSING;
  std::vector<PortalEntry> entries;               // current list (portal.md or a page's links)
  std::vector<std::vector<PortalEntry>> navStack;  // parent lists for Back
  std::vector<int> navSelection;                   // saved selector per level
  int selectorIndex = 0;
  int menuIndex = 0;  // PAGE_MENU selector
  bool consumeConfirm = false;
  std::string errorMessage;
  // Result of the last successful fetch/cache-hit
  std::string pageTitle;
  std::string pageTxtPath;
  std::vector<PortalEntry> pageLinks;
  std::vector<PageAction> pageMenu;
  bool pageHasText = false;
  PortalEntry pendingEntry;  // entry to open once Wi-Fi comes up

  bool loadPortalFile();
  void buildPageMenu();
  void openEntry(const PortalEntry& entry);
  void fetchPage(const PortalEntry& entry);
  void launchWifiSelection();
  void openReader();
  void enterLinksList();
  void navigateBack();
  static std::string cachePathForUrl(const std::string& url);
  bool preventAutoSleep() override { return true; }
};
