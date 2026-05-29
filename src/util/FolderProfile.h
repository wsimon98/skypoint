#pragma once

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdint>
#include <string>

#include "CrossPointSettings.h"

// SkyPoint per-folder profiles.
//
// A folder profile is a sidecar file (".skypoint-folder.bin") placed inside a
// folder on the SD card. When a book inside (or under) that folder is opened,
// the reader applies the profile's settings overrides for the duration of the
// reading session, then restores the previous values on exit.
//
// Sidecar format (binary, 10 bytes):
//   [0] version (currently 1)
//   [1] override mask — see MaskBit below
//   [2..9] eight value bytes, one per overridable field
//
// The current implementation captures a snapshot of *all* relevant reader
// fields when the profile is saved (mask = MASK_ALL). Selective per-field
// override is supported by the sidecar format for forward compatibility.
//
// Lookup walks up the directory tree from the book's parent folder; the
// nearest sidecar wins. The root folder is included in the walk.
//
// Caveat: mutations to SETTINGS while a profile is active are restored on
// reader exit. If the user changes a setting mid-session, that change is
// reverted when they leave the book. v2 will distinguish user-intent changes.

namespace FolderProfile {

constexpr const char* SIDECAR_NAME = ".skypoint-folder.bin";
constexpr uint8_t SIDECAR_VERSION = 1;
constexpr size_t SIDECAR_BYTES = 10;

// Mask bits — which fields are overridden by the profile.
enum MaskBit : uint8_t {
  MASK_FONT_FAMILY = 1 << 0,
  MASK_FONT_SIZE = 1 << 1,
  MASK_LINE_SPACING = 1 << 2,
  MASK_PARAGRAPH_ALIGN = 1 << 3,
  MASK_SCREEN_MARGIN = 1 << 4,
  MASK_READER_DARK_MODE = 1 << 5,
  MASK_HYPHENATION = 1 << 6,
  MASK_FOCUS_READING = 1 << 7,
  MASK_ALL = 0xFF,
};

struct Profile {
  uint8_t mask = 0;
  uint8_t fontFamily = 0;
  uint8_t fontSize = 0;
  uint8_t lineSpacing = 0;
  uint8_t paragraphAlign = 0;
  uint8_t screenMargin = 0;
  uint8_t readerDarkMode = 0;
  uint8_t hyphenationEnabled = 0;
  uint8_t focusReadingEnabled = 0;
};

inline std::string sidecarPath(const std::string& folderPath) {
  std::string s = folderPath.empty() ? std::string("/") : folderPath;
  if (s.back() != '/') s += '/';
  s += SIDECAR_NAME;
  return s;
}

inline bool loadFromFile(const std::string& path, Profile& out) {
  HalFile f;
  if (!Storage.openFileForRead("FP", path, f)) return false;
  uint8_t buf[SIDECAR_BYTES] = {0};
  const size_t n = f.read(buf, SIDECAR_BYTES);
  f.close();
  if (n < 2) return false;
  if (buf[0] != SIDECAR_VERSION) return false;
  out.mask = buf[1];
  out.fontFamily = buf[2];
  out.fontSize = buf[3];
  out.lineSpacing = buf[4];
  out.paragraphAlign = buf[5];
  out.screenMargin = buf[6];
  out.readerDarkMode = buf[7];
  out.hyphenationEnabled = buf[8];
  out.focusReadingEnabled = buf[9];
  return true;
}

inline bool saveToFile(const std::string& path, const Profile& p) {
  HalFile f;
  if (!Storage.openFileForWrite("FP", path, f)) {
    LOG_ERR("FP", "Cannot open folder profile for write: %s", path.c_str());
    return false;
  }
  uint8_t buf[SIDECAR_BYTES];
  buf[0] = SIDECAR_VERSION;
  buf[1] = p.mask;
  buf[2] = p.fontFamily;
  buf[3] = p.fontSize;
  buf[4] = p.lineSpacing;
  buf[5] = p.paragraphAlign;
  buf[6] = p.screenMargin;
  buf[7] = p.readerDarkMode;
  buf[8] = p.hyphenationEnabled;
  buf[9] = p.focusReadingEnabled;
  const size_t n = f.write(buf, SIDECAR_BYTES);
  if (n != SIDECAR_BYTES) {
    LOG_ERR("FP", "Short write to folder profile (%u/%u) at %s", (unsigned)n, (unsigned)SIDECAR_BYTES, path.c_str());
    return false;
  }
  return true;
}

// Walk up from the book's parent folder; return path of nearest sidecar, or "".
inline std::string findNearestSidecar(const std::string& bookPath) {
  std::string dir = FsHelpers::extractFolderPath(bookPath);
  while (true) {
    if (dir.empty()) dir = "/";
    const std::string candidate = sidecarPath(dir);
    if (Storage.exists(candidate.c_str())) return candidate;
    if (dir == "/" || dir.size() <= 1) return "";
    const auto pos = dir.find_last_of('/');
    if (pos == std::string::npos) return "";
    dir = (pos == 0) ? std::string("/") : dir.substr(0, pos);
  }
}

// Make a Profile from current SETTINGS, with mask = MASK_ALL.
inline Profile snapshotFromSettings() {
  const auto& s = CrossPointSettings::getInstance();
  Profile p;
  p.mask = MASK_ALL;
  p.fontFamily = s.fontFamily;
  p.fontSize = s.fontSize;
  p.lineSpacing = s.lineSpacing;
  p.paragraphAlign = s.paragraphAlignment;
  p.screenMargin = s.screenMargin;
  p.readerDarkMode = s.readerDarkMode;
  p.hyphenationEnabled = s.hyphenationEnabled;
  p.focusReadingEnabled = s.focusReadingEnabled;
  return p;
}

inline bool saveForFolder(const std::string& folderPath) {
  return saveToFile(sidecarPath(folderPath), snapshotFromSettings());
}

inline bool clearForFolder(const std::string& folderPath) {
  const std::string p = sidecarPath(folderPath);
  if (!Storage.exists(p.c_str())) return true;
  return Storage.remove(p.c_str());
}

inline bool hasFolderProfile(const std::string& folderPath) {
  return Storage.exists(sidecarPath(folderPath).c_str());
}

// Apply/restore overlay against SETTINGS.
struct OverlayState {
  bool active = false;
  Profile baseline;  // pre-overlay values; mask = which fields we overlaid
};

inline OverlayState& overlayState() {
  static OverlayState s;
  return s;
}

// Apply the nearest folder profile (if any) found by walking up from bookPath.
// Returns true if an overlay was applied. Safe to call multiple times; only the
// first call mutates state until restore() is called.
inline bool apply(const std::string& bookPath) {
  auto& state = overlayState();
  if (state.active) {
    LOG_DBG("FP", "Folder profile already active, skipping apply");
    return false;
  }
  const std::string scPath = findNearestSidecar(bookPath);
  if (scPath.empty()) return false;
  Profile overlay;
  if (!loadFromFile(scPath, overlay)) {
    LOG_ERR("FP", "Failed to load sidecar at %s", scPath.c_str());
    return false;
  }
  if (overlay.mask == 0) return false;
  auto& s = CrossPointSettings::getInstance();
  state.baseline = Profile{};
  state.baseline.mask = overlay.mask;
  if (overlay.mask & MASK_FONT_FAMILY) {
    state.baseline.fontFamily = s.fontFamily;
    s.fontFamily = overlay.fontFamily;
  }
  if (overlay.mask & MASK_FONT_SIZE) {
    state.baseline.fontSize = s.fontSize;
    s.fontSize = overlay.fontSize;
  }
  if (overlay.mask & MASK_LINE_SPACING) {
    state.baseline.lineSpacing = s.lineSpacing;
    s.lineSpacing = overlay.lineSpacing;
  }
  if (overlay.mask & MASK_PARAGRAPH_ALIGN) {
    state.baseline.paragraphAlign = s.paragraphAlignment;
    s.paragraphAlignment = overlay.paragraphAlign;
  }
  if (overlay.mask & MASK_SCREEN_MARGIN) {
    state.baseline.screenMargin = s.screenMargin;
    s.screenMargin = overlay.screenMargin;
  }
  if (overlay.mask & MASK_READER_DARK_MODE) {
    state.baseline.readerDarkMode = s.readerDarkMode;
    s.readerDarkMode = overlay.readerDarkMode;
  }
  if (overlay.mask & MASK_HYPHENATION) {
    state.baseline.hyphenationEnabled = s.hyphenationEnabled;
    s.hyphenationEnabled = overlay.hyphenationEnabled;
  }
  if (overlay.mask & MASK_FOCUS_READING) {
    state.baseline.focusReadingEnabled = s.focusReadingEnabled;
    s.focusReadingEnabled = overlay.focusReadingEnabled;
  }
  state.active = true;
  LOG_DBG("FP", "Applied folder profile from %s (mask=0x%02x)", scPath.c_str(), overlay.mask);
  return true;
}

// Restore SETTINGS to pre-overlay values. No-op if no overlay active.
inline void restore() {
  auto& state = overlayState();
  if (!state.active) return;
  auto& s = CrossPointSettings::getInstance();
  const auto mask = state.baseline.mask;
  if (mask & MASK_FONT_FAMILY) s.fontFamily = state.baseline.fontFamily;
  if (mask & MASK_FONT_SIZE) s.fontSize = state.baseline.fontSize;
  if (mask & MASK_LINE_SPACING) s.lineSpacing = state.baseline.lineSpacing;
  if (mask & MASK_PARAGRAPH_ALIGN) s.paragraphAlignment = state.baseline.paragraphAlign;
  if (mask & MASK_SCREEN_MARGIN) s.screenMargin = state.baseline.screenMargin;
  if (mask & MASK_READER_DARK_MODE) s.readerDarkMode = state.baseline.readerDarkMode;
  if (mask & MASK_HYPHENATION) s.hyphenationEnabled = state.baseline.hyphenationEnabled;
  if (mask & MASK_FOCUS_READING) s.focusReadingEnabled = state.baseline.focusReadingEnabled;
  state.active = false;
  state.baseline = Profile{};
  LOG_DBG("FP", "Restored folder profile baseline");
}

}  // namespace FolderProfile
