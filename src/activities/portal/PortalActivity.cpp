#include "PortalActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "util/HtmlToText.h"

namespace {
constexpr const char* TAG = "PRT";
constexpr const char* PORTAL_MD = "/portal.md";
constexpr const char* READ_DIR = "/.portal/read";
constexpr int PAGE_ITEMS = 23;
constexpr size_t MAX_ENTRIES = 64;
constexpr size_t PORTAL_MD_CAP = 16 * 1024;      // portal.md read cap
constexpr size_t PAGE_FETCH_CAP = 1024 * 1024;   // stop streaming a page after 1 MB
}  // namespace

void PortalActivity::onEnter() {
  Activity::onEnter();

  state = PortalState::BROWSING;
  entries.clear();
  navStack.clear();
  navSelection.clear();
  pageLinks.clear();
  selectorIndex = 0;
  menuIndex = 0;
  consumeConfirm = false;
  errorMessage.clear();

  if (!loadPortalFile()) {
    state = PortalState::NO_FILE;
  }
  requestUpdate();
}

void PortalActivity::onExit() {
  Activity::onExit();
  entries.clear();
  navStack.clear();
  pageLinks.clear();

  // Same teardown as the OPDS browser: a Wi-Fi session fragments the heap, so
  // leave via a silent restart instead of handing a holey heap to the reader.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void PortalActivity::loop() {
  if (state == PortalState::WIFI_SELECTION || state == PortalState::FETCHING) {
    return;
  }

  if (consumeConfirm && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    consumeConfirm = false;
    return;
  }

  if (state == PortalState::NO_FILE) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      // Retry — the user may have just uploaded portal.md over the web UI.
      if (loadPortalFile()) {
        state = PortalState::BROWSING;
        selectorIndex = 0;
      }
      requestUpdate();
    }
    return;
  }

  if (state == PortalState::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      openEntry(pendingEntry);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = PortalState::BROWSING;
      requestUpdate();
    }
    return;
  }

  if (state == PortalState::PAGE_MENU) {
    const bool hasLinks = !pageLinks.empty();
    const int menuCount = hasLinks ? 3 : 2;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = PortalState::BROWSING;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (menuIndex == 0) {
        openReader();
      } else if (hasLinks && menuIndex == 1) {
        enterLinksList();
      } else {
        state = PortalState::BROWSING;
        requestUpdate();
      }
      return;
    }
    buttonNavigator.onNextRelease([this, menuCount] {
      menuIndex = ButtonNavigator::nextIndex(menuIndex, menuCount);
      requestUpdate();
    });
    buttonNavigator.onPreviousRelease([this, menuCount] {
      menuIndex = ButtonNavigator::previousIndex(menuIndex, menuCount);
      requestUpdate();
    });
    return;
  }

  // BROWSING
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!entries.empty()) {
      openEntry(entries[selectorIndex]);
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    navigateBack();
    return;
  }

  if (!entries.empty()) {
    buttonNavigator.onNextRelease([this] {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, entries.size());
      requestUpdate();
    });
    buttonNavigator.onPreviousRelease([this] {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, entries.size());
      requestUpdate();
    });
    buttonNavigator.onNextContinuous([this] {
      selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, entries.size(), PAGE_ITEMS);
      requestUpdate();
    });
    buttonNavigator.onPreviousContinuous([this] {
      selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, entries.size(), PAGE_ITEMS);
      requestUpdate();
    });
  }
}

void PortalActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.drawCenteredText(UI_12_FONT_ID, 15, tr(STR_PORTAL), true, EpdFontFamily::BOLD);

  if (state == PortalState::NO_FILE) {
    const int y = pageHeight / 2 - 80;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_PORTAL_NO_FILE), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(SMALL_FONT_ID, y + 40, tr(STR_PORTAL_HELP_1));
    renderer.drawCenteredText(SMALL_FONT_ID, y + 65, tr(STR_PORTAL_HELP_2));
    renderer.drawCenteredText(SMALL_FONT_ID, y + 95, tr(STR_PORTAL_HELP_3));
    renderer.drawCenteredText(SMALL_FONT_ID, y + 125, tr(STR_PORTAL_HELP_4));
    renderer.drawCenteredText(SMALL_FONT_ID, y + 150, tr(STR_PORTAL_HELP_5));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == PortalState::WIFI_SELECTION || state == PortalState::FETCHING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_PORTAL_FETCHING));
    renderer.displayBuffer();
    return;
  }

  if (state == PortalState::ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_PORTAL_FETCH_FAILED));
    auto detail = renderer.truncatedText(SMALL_FONT_ID, errorMessage.c_str(), pageWidth - 40);
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 10, detail.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == PortalState::PAGE_MENU) {
    auto title = renderer.truncatedText(UI_10_FONT_ID, pageTitle.c_str(), pageWidth - 40);
    renderer.drawCenteredText(UI_10_FONT_ID, 80, title.c_str(), true, EpdFontFamily::BOLD);

    char linksLabel[48];
    snprintf(linksLabel, sizeof(linksLabel), tr(STR_PORTAL_LINKS_FORMAT), static_cast<unsigned>(pageLinks.size()));
    const char* items[3] = {tr(STR_PORTAL_READ_PAGE), pageLinks.empty() ? tr(STR_PORTAL_BACK_TO_LIST) : linksLabel,
                            tr(STR_PORTAL_BACK_TO_LIST)};
    const int menuCount = pageLinks.empty() ? 2 : 3;
    for (int i = 0; i < menuCount; i++) {
      if (i == menuIndex) {
        renderer.fillRect(0, 140 + i * 40 - 4, pageWidth - 1, 36);
      }
      renderer.drawText(UI_10_FONT_ID, 30, 140 + i * 40, items[i], i != menuIndex);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // BROWSING
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (entries.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_NO_ENTRIES));
  } else {
    const auto pageStartIndex = selectorIndex / PAGE_ITEMS * PAGE_ITEMS;
    renderer.fillRect(0, 60 + (selectorIndex % PAGE_ITEMS) * 30 - 2, pageWidth - 1, 30);
    for (size_t i = pageStartIndex; i < entries.size() && i < static_cast<size_t>(pageStartIndex + PAGE_ITEMS); i++) {
      auto item = renderer.truncatedText(UI_10_FONT_ID, entries[i].title.c_str(), pageWidth - 40);
      renderer.drawText(UI_10_FONT_ID, 20, 60 + (i % PAGE_ITEMS) * 30, item.c_str(),
                        i != static_cast<size_t>(selectorIndex));
    }
  }
  renderer.displayBuffer();
}

bool PortalActivity::loadPortalFile() {
  HalFile f;
  if (!Storage.openFileForRead(TAG, PORTAL_MD, f)) return false;

  // portal.md is tiny (16 KB cap below); one cold-path owning string is fine.
  std::string content;
  content.reserve(2048);
  uint8_t chunk[512];
  while (content.size() < PORTAL_MD_CAP) {
    const int n = f.read(chunk, sizeof(chunk));
    if (n <= 0) break;
    content.append(reinterpret_cast<const char*>(chunk), static_cast<size_t>(n));
  }
  f.close();

  entries.clear();
  entries.reserve(16);
  size_t lineStart = 0;
  while (lineStart < content.size() && entries.size() < MAX_ENTRIES) {
    size_t lineEnd = content.find('\n', lineStart);
    if (lineEnd == std::string::npos) lineEnd = content.size();
    std::string line = content.substr(lineStart, lineEnd - lineStart);
    lineStart = lineEnd + 1;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();

    // `- [Title](http...)` markdown form
    const size_t lb = line.find('[');
    const size_t rb = lb == std::string::npos ? std::string::npos : line.find(']', lb + 1);
    const size_t lp = rb == std::string::npos ? std::string::npos : line.find('(', rb);
    const size_t rp = lp == std::string::npos ? std::string::npos : line.find(')', lp + 1);
    if (rp != std::string::npos) {
      std::string title = line.substr(lb + 1, rb - lb - 1);
      std::string url = line.substr(lp + 1, rp - lp - 1);
      if (!title.empty() && (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0)) {
        entries.push_back(PortalEntry{std::move(title), std::move(url)});
        continue;
      }
    }

    // Bare URL line
    const size_t firstNonWs = line.find_first_not_of(" \t");
    if (firstNonWs != std::string::npos) {
      std::string url = line.substr(firstNonWs);
      if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
        entries.push_back(PortalEntry{url, url});
      }
    }
  }

  LOG_DBG(TAG, "portal.md: %zu entries", entries.size());
  return !entries.empty();
}

std::string PortalActivity::cachePathForUrl(const std::string& url) {
  // FNV-1a 32-bit — stable slug so re-visits hit the same cache file.
  uint32_t h = 2166136261u;
  for (const char c : url) {
    h ^= static_cast<uint8_t>(c);
    h *= 16777619u;
  }
  char path[48];
  snprintf(path, sizeof(path), "%s/%08lx.txt", READ_DIR, static_cast<unsigned long>(h));
  return std::string(path);
}

void PortalActivity::openEntry(const PortalEntry& entry) {
  pendingEntry = entry;
  pageTxtPath = cachePathForUrl(entry.url);

  const bool wifiUp = WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
  if (wifiUp) {
    fetchPage(entry);
    return;
  }

  // Offline: a cached copy opens directly (no link list without a fresh parse).
  if (Storage.exists(pageTxtPath.c_str())) {
    LOG_DBG(TAG, "offline cache hit: %s", pageTxtPath.c_str());
    openReader();
    return;
  }

  launchWifiSelection();
}

void PortalActivity::launchWifiSelection() {
  state = PortalState::WIFI_SELECTION;
  requestUpdate();

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             fetchPage(pendingEntry);
                           } else {
                             state = PortalState::ERROR;
                             errorMessage = tr(STR_WIFI_CONN_FAILED);
                             requestUpdate();
                           }
                         });
}

void PortalActivity::fetchPage(const PortalEntry& entry) {
  state = PortalState::FETCHING;
  requestUpdate(true);

  if (!Storage.exists("/.portal")) Storage.mkdir("/.portal");
  if (!Storage.exists(READ_DIR)) Storage.mkdir(READ_DIR);

  HalFile outFile;
  if (!Storage.openFileForWrite(TAG, pageTxtPath, outFile)) {
    state = PortalState::ERROR;
    errorMessage = tr(STR_ERROR_GENERAL_FAILURE);
    requestUpdate();
    return;
  }

  LOG_DBG(TAG, "fetching %s -> %s", entry.url.c_str(), pageTxtPath.c_str());
  // HtmlToText holds only small fixed buffers + the capped link list; the page
  // itself streams network -> converter -> SD without ever living in RAM.
  HtmlToText converter(outFile, entry.url, MAX_ENTRIES);
  size_t total = 0;
  const bool fetched = HttpDownloader::fetchUrl(entry.url, [&](const uint8_t* data, const size_t len) {
    total += len;
    if (total > PAGE_FETCH_CAP) return false;  // huge page: keep what we have
    converter.feed(data, len);
    return true;
  });
  converter.finish();
  outFile.close();

  // An over-cap abort still produced a readable article; a real failure didn't.
  if (!fetched && total <= PAGE_FETCH_CAP) {
    Storage.remove(pageTxtPath.c_str());  // don't let a broken file shadow future fetches
    state = PortalState::ERROR;
    errorMessage = pendingEntry.title;
    LOG_ERR(TAG, "fetch failed: %s", entry.url.c_str());
    requestUpdate();
    return;
  }

  pageTitle = converter.getTitle().empty() ? entry.title : converter.getTitle();
  pageLinks.clear();
  pageLinks.reserve(converter.getLinks().size());
  for (auto& link : converter.getLinks()) {
    pageLinks.push_back(PortalEntry{std::move(link.text), std::move(link.url)});
  }

  menuIndex = 0;
  state = PortalState::PAGE_MENU;
  requestUpdate();
}

void PortalActivity::openReader() {
  // A Wi-Fi session fragments the heap; hand off to the reader through the
  // same silent-restart path KOReader sync uses. Without Wi-Fi, open directly.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    APP_STATE.openEpubPath = pageTxtPath;
    APP_STATE.saveToFile();
    silentRestartToReader();
    return;
  }
  activityManager.goToReader(pageTxtPath);
}

void PortalActivity::enterLinksList() {
  if (navStack.size() >= 8) {
    // Depth cap: replace the current level instead of stacking deeper.
    entries = std::move(pageLinks);
  } else {
    navStack.push_back(std::move(entries));
    navSelection.push_back(selectorIndex);
    entries = std::move(pageLinks);
  }
  pageLinks.clear();
  selectorIndex = 0;
  state = PortalState::BROWSING;
  requestUpdate();
}

void PortalActivity::navigateBack() {
  if (navStack.empty()) {
    onGoHome();
    return;
  }
  entries = std::move(navStack.back());
  navStack.pop_back();
  selectorIndex = navSelection.empty() ? 0 : navSelection.back();
  if (!navSelection.empty()) navSelection.pop_back();
  if (selectorIndex >= static_cast<int>(entries.size())) selectorIndex = 0;
  state = PortalState::BROWSING;
  requestUpdate();
}
