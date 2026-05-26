#pragma once

#include <HalStorage.h>
#include <Logging.h>

#include <cstdint>
#include <string>

#include "CrossPointSettings.h"

// Per-book dark-mode override. Persisted as a 1-byte sidecar file named
// "darkmode.bin" inside the same per-book cache directory that already holds
// progress.bin. A new file (rather than extending progress.bin) keeps existing
// devices' progress files readable with no migration.
namespace ReaderDarkMode {

enum BookOverride : uint8_t {
  USE_SYSTEM = 0,
  FORCE_ON = 1,
  FORCE_OFF = 2,
  BOOK_OVERRIDE_COUNT = 3,
};

inline std::string sidecarPath(const std::string& bookCachePath) {
  return bookCachePath + "/darkmode.bin";
}

inline BookOverride loadBookOverride(const std::string& bookCachePath) {
  HalFile f;
  if (!Storage.openFileForRead("RDM", sidecarPath(bookCachePath), f)) {
    return USE_SYSTEM;
  }
  uint8_t byte = 0;
  const size_t n = f.read(&byte, 1);
  f.close();
  if (n != 1 || byte >= BOOK_OVERRIDE_COUNT) return USE_SYSTEM;
  return static_cast<BookOverride>(byte);
}

inline bool saveBookOverride(const std::string& bookCachePath, BookOverride override) {
  HalFile f;
  if (!Storage.openFileForWrite("RDM", sidecarPath(bookCachePath), f)) {
    LOG_ERR("RDM", "Cannot open darkmode.bin for write at %s", bookCachePath.c_str());
    return false;
  }
  const uint8_t byte = static_cast<uint8_t>(override);
  const size_t n = f.write(&byte, 1);
  if (n != 1) {
    LOG_ERR("RDM", "Short write to darkmode.bin (%u/1) at %s", (unsigned)n, bookCachePath.c_str());
    return false;
  }
  return true;
}

inline bool effective(BookOverride override) {
  switch (override) {
    case FORCE_ON:
      return true;
    case FORCE_OFF:
      return false;
    case USE_SYSTEM:
    default:
      return CrossPointSettings::getInstance().readerDarkMode != 0;
  }
}

inline bool effectiveForBook(const std::string& bookCachePath) {
  return effective(loadBookOverride(bookCachePath));
}

inline BookOverride cycleNext(BookOverride current) {
  return static_cast<BookOverride>((static_cast<uint8_t>(current) + 1) % BOOK_OVERRIDE_COUNT);
}

}  // namespace ReaderDarkMode
