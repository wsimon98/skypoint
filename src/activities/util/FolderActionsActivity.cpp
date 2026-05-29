#include "FolderActionsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/FolderProfile.h"

namespace {
constexpr int ITEM_SAVE = 0;
constexpr int ITEM_CLEAR = 1;
constexpr int ITEM_DELETE = 2;
constexpr int ITEM_COUNT = 3;
}  // namespace

void FolderActionsActivity::onEnter() {
  Activity::onEnter();
  profileExists = FolderProfile::hasFolderProfile(folderPath);
  selectorIndex = 0;
  requestUpdate();
}

void FolderActionsActivity::loop() {
  buttonNavigator.onNextRelease([this] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, ITEM_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, ITEM_COUNT);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    switch (selectorIndex) {
      case ITEM_SAVE:
        doSave();
        break;
      case ITEM_CLEAR:
        doClear();
        break;
      case ITEM_DELETE:
        doDelete();
        break;
      default:
        break;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult res;
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
  }
}

void FolderActionsActivity::doSave() {
  const bool ok = FolderProfile::saveForFolder(folderPath);
  if (!ok) {
    LOG_ERR("FAA", "Failed to save folder profile for %s", folderPath.c_str());
  }
  ActivityResult res{MenuResult{ok ? static_cast<int>(SAVED) : static_cast<int>(NONE), 0, 0, 0}};
  res.isCancelled = !ok;
  setResult(std::move(res));
  finish();
}

void FolderActionsActivity::doClear() {
  if (!profileExists) {
    ActivityResult res{MenuResult{static_cast<int>(NONE), 0, 0, 0}};
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
    return;
  }
  auto handler = [this](const ActivityResult& confirm) {
    if (confirm.isCancelled) {
      ActivityResult res;
      res.isCancelled = true;
      setResult(std::move(res));
      finish();
      return;
    }
    const bool ok = FolderProfile::clearForFolder(folderPath);
    if (!ok) {
      LOG_ERR("FAA", "Failed to clear folder profile for %s", folderPath.c_str());
    }
    ActivityResult res{MenuResult{ok ? static_cast<int>(CLEARED) : static_cast<int>(NONE), 0, 0, 0}};
    res.isCancelled = !ok;
    setResult(std::move(res));
    finish();
  };
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput,
                                                                tr(STR_CLEAR_FOLDER_PROFILE_PROMPT), folderDisplayName),
                         std::move(handler));
}

void FolderActionsActivity::doDelete() {
  // Re-use existing delete-confirm flow inside the FileBrowser by returning a
  // DELETE_REQUESTED result; the caller already knows how to delete.
  ActivityResult res{MenuResult{static_cast<int>(DELETE_REQUESTED), 0, 0, 0}};
  res.isCancelled = false;
  setResult(std::move(res));
  finish();
}

void FolderActionsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderDisplayName.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawButtonMenu(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, ITEM_COUNT, selectorIndex,
      [this](int index) -> std::string {
        switch (index) {
          case ITEM_SAVE:
            return tr(STR_SAVE_FOLDER_PROFILE);
          case ITEM_CLEAR:
            return profileExists ? tr(STR_CLEAR_FOLDER_PROFILE) : tr(STR_NO_FOLDER_PROFILE);
          case ITEM_DELETE:
            return tr(STR_DELETE_FOLDER);
          default:
            return "";
        }
      },
      [](int index) -> UIIcon {
        switch (index) {
          case ITEM_SAVE:
            return Settings;
          case ITEM_CLEAR:
            return Settings;
          case ITEM_DELETE:
            return Folder;
          default:
            return Folder;
        }
      });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
