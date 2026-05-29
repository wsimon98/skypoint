#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Action menu shown when the user long-presses a folder in the file browser.
//
// Items:
//   - Save Folder Profile  → captures current SETTINGS into the folder's sidecar
//   - Clear Folder Profile → removes the sidecar (if any)
//   - Delete Folder        → returns MenuResult{action=DELETE} to the caller
//
// Save/Clear are handled inline (with confirmation popups). Delete is
// delegated back to the caller via setResult so the caller can refresh its
// file listing after the directory tree changes.
class FolderActionsActivity final : public Activity {
 public:
  enum ResultAction : int { NONE = 0, SAVED = 1, CLEARED = 2, DELETE_REQUESTED = 3 };

 private:
  std::string folderPath;
  std::string folderDisplayName;
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool profileExists = false;

  void doSave();
  void doClear();
  void doDelete();

 public:
  FolderActionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string folderPath,
                        std::string folderDisplayName)
      : Activity("FolderActions", renderer, mappedInput),
        folderPath(std::move(folderPath)),
        folderDisplayName(std::move(folderDisplayName)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
