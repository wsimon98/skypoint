#include "SleepGlance.h"

#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "CrossPointSettings.h"
#include "network/HttpDownloader.h"

namespace {

constexpr const char* TAG = "GLN";
constexpr const char* CACHE_PATH = "/.crosspoint/glance.bin";
constexpr const char* STREAK_PATH = "/.crosspoint/streak.bin";
constexpr uint32_t CACHE_MAGIC = 0x314C4753;   // "SGL1"
constexpr uint32_t STREAK_MAGIC = 0x314B5253;  // "SRK1"
// Anything earlier than 2020-09 means the system clock was never SNTP-synced.
constexpr time_t MIN_VALID_EPOCH = 1600000000;
constexpr int64_t REFRESH_MIN_S = 30 * 60;     // at most one fetch per 30 min
constexpr int64_t WEATHER_FRESH_S = 6 * 3600;  // show cached weather up to 6 h old

struct GlanceCache {
  uint32_t magic = 0;
  float lat = 0;
  float lon = 0;
  int32_t utcOffsetSec = 0;
  float tempC = 0;
  uint8_t weatherCode = 0;
  uint8_t useFahrenheit = 0;
  uint8_t hasLocation = 0;
  uint8_t hasWeather = 0;
  int64_t fetchedAt = 0;
};

struct StreakData {
  uint32_t magic = 0;
  int32_t lastDay = 0;
  int32_t count = 0;
};

GlanceCache cache;
bool cacheLoaded = false;

void loadCache() {
  if (cacheLoaded) return;
  cacheLoaded = true;
  HalFile f;
  if (!Storage.openFileForRead(TAG, CACHE_PATH, f)) return;
  GlanceCache tmp;
  const int n = f.read(reinterpret_cast<uint8_t*>(&tmp), sizeof(tmp));
  f.close();
  if (n == static_cast<int>(sizeof(tmp)) && tmp.magic == CACHE_MAGIC) cache = tmp;
}

void saveCache() {
  cache.magic = CACHE_MAGIC;
  HalFile f;
  if (!Storage.openFileForWrite(TAG, CACHE_PATH, f)) return;
  f.write(reinterpret_cast<const uint8_t*>(&cache), sizeof(cache));
  f.close();
}

// Minimal JSON field extraction for the two fixed, well-formed API responses
// used here. `from` lets the caller scope the search to a sub-object.
bool jsonNumber(const char* from, const char* key, double& out) {
  const char* p = strstr(from, key);
  if (!p) return false;
  p += strlen(key);
  while (*p == ' ') p++;
  char* end = nullptr;
  const double v = strtod(p, &end);
  if (end == p) return false;
  out = v;
  return true;
}

bool jsonString(const char* from, const char* key, char* out, size_t outSize) {
  const char* p = strstr(from, key);
  if (!p) return false;
  p += strlen(key);
  while (*p == ' ') p++;
  if (*p != '"') return false;
  p++;
  size_t i = 0;
  while (*p && *p != '"' && i + 1 < outSize) out[i++] = *p++;
  out[i] = 0;
  return *p == '"';
}

bool fetchGeo() {
  std::string body;
  // ip-api.com resolves the device's own public IP — no configuration, works
  // wherever the user is. `offset` is the UTC offset in seconds incl. DST.
  if (!HttpDownloader::fetchUrl("http://ip-api.com/json/?fields=status,countryCode,lat,lon,offset", body)) {
    LOG_DBG(TAG, "geo fetch failed");
    return false;
  }
  char status[16];
  if (!jsonString(body.c_str(), "\"status\":", status, sizeof(status)) || strcmp(status, "success") != 0) return false;
  double lat, lon, off;
  if (!jsonNumber(body.c_str(), "\"lat\":", lat) || !jsonNumber(body.c_str(), "\"lon\":", lon) ||
      !jsonNumber(body.c_str(), "\"offset\":", off)) {
    return false;
  }
  char cc[4] = "";
  jsonString(body.c_str(), "\"countryCode\":", cc, sizeof(cc));

  cache.lat = static_cast<float>(lat);
  cache.lon = static_cast<float>(lon);
  cache.utcOffsetSec = static_cast<int32_t>(off);
  static constexpr const char* F_COUNTRIES[] = {"US", "BS", "BZ", "KY", "PW", "FM", "MH", "LR"};
  cache.useFahrenheit = 0;
  for (const char* c : F_COUNTRIES) {
    if (strcmp(cc, c) == 0) {
      cache.useFahrenheit = 1;
      break;
    }
  }
  cache.hasLocation = 1;
  LOG_DBG(TAG, "geo ok: %.2f,%.2f offset=%ld cc=%s", cache.lat, cache.lon, (long)cache.utcOffsetSec, cc);
  return true;
}

bool fetchWeather() {
  if (!cache.hasLocation) return false;
  char url[160];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code",
           cache.lat, cache.lon);
  std::string body;
  if (!HttpDownloader::fetchUrl(url, body)) {
    LOG_DBG(TAG, "weather fetch failed");
    return false;
  }
  // Scope to the "current" object — "current_units" holds the same keys with
  // string values and must not match. ("current_units" does not match the
  // needle "\"current\":" because of the trailing quote-colon.)
  const char* cur = strstr(body.c_str(), "\"current\":");
  if (!cur) return false;
  double t, code;
  if (!jsonNumber(cur, "\"temperature_2m\":", t) || !jsonNumber(cur, "\"weather_code\":", code)) return false;
  cache.tempC = static_cast<float>(t);
  cache.weatherCode = static_cast<uint8_t>(code);
  cache.hasWeather = 1;
  LOG_DBG(TAG, "weather ok: %.1fC code=%d", cache.tempC, cache.weatherCode);
  return true;
}

// WMO weather interpretation codes -> short label.
const char* weatherLabel(uint8_t code) {
  if (code == 0) return "Clear";
  if (code <= 2) return "Partly cloudy";
  if (code == 3) return "Overcast";
  if (code == 45 || code == 48) return "Fog";
  if (code >= 51 && code <= 57) return "Drizzle";
  if (code >= 61 && code <= 67) return "Rain";
  if (code >= 71 && code <= 77) return "Snow";
  if (code >= 80 && code <= 82) return "Showers";
  if (code == 85 || code == 86) return "Snow";
  if (code >= 95) return "Storm";
  return "";
}

// Local day number for streak tracking. Requires a synced system clock.
bool localDayNumber(int32_t& out) {
  loadCache();
  const time_t now = time(nullptr);
  if (now < MIN_VALID_EPOCH) return false;
  out = static_cast<int32_t>((now + (cache.hasLocation ? cache.utcOffsetSec : 0)) / 86400);
  return true;
}

bool loadStreak(StreakData& s) {
  HalFile f;
  if (!Storage.openFileForRead(TAG, STREAK_PATH, f)) return false;
  StreakData tmp;
  const int n = f.read(reinterpret_cast<uint8_t*>(&tmp), sizeof(tmp));
  f.close();
  if (n != static_cast<int>(sizeof(tmp)) || tmp.magic != STREAK_MAGIC) return false;
  s = tmp;
  return true;
}

}  // namespace

namespace SleepGlance {

void refresh() {
  if (WiFi.status() != WL_CONNECTED) return;
  loadCache();

  // X4 has no RTC chip: opportunistically SNTP-sync the system clock once per
  // cold boot. It then survives deep sleep via the ESP32 RTC timer domain.
  // (X3 clock sync is handled by the existing HalClock/DS3231 flow.)
  if (!halClock.isAvailable() && time(nullptr) < MIN_VALID_EPOCH) {
    configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");
    for (int i = 0; i < 40 && sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED; i++) {
      delay(100);
    }
  }

  const time_t now = time(nullptr);
  const bool timeValid = now >= MIN_VALID_EPOCH;
  if (timeValid && cache.fetchedAt > 0 && static_cast<int64_t>(now) - cache.fetchedAt < REFRESH_MIN_S) return;

  const bool gotGeo = fetchGeo();
  const bool gotWeather = fetchWeather();
  if (gotGeo || gotWeather) {
    if (timeValid) cache.fetchedAt = static_cast<int64_t>(time(nullptr));
    saveCache();
  }
}

bool timeString(char* buf, size_t bufSize) {
  if (bufSize < 9) return false;

  // X3: hardware RTC, same source/format as the status-bar clock.
  if (halClock.isAvailable()) {
    if (!SETTINGS.clockHasBeenSynced) return false;
    return halClock.formatTime(buf, bufSize, SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1);
  }

  // X4: system clock + IP-derived UTC offset. Without a known offset the
  // time would be UTC (wrong for most users), so show nothing instead.
  loadCache();
  const time_t now = time(nullptr);
  if (now < MIN_VALID_EPOCH || !cache.hasLocation) return false;
  const time_t local = now + cache.utcOffsetSec;
  struct tm tmv;
  gmtime_r(&local, &tmv);
  if (SETTINGS.clockFormat == 1) {
    int h12 = tmv.tm_hour % 12;
    if (h12 == 0) h12 = 12;
    snprintf(buf, bufSize, "%d:%02d %s", h12, tmv.tm_min, tmv.tm_hour >= 12 ? "PM" : "AM");
  } else {
    snprintf(buf, bufSize, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
  }
  return true;
}

bool weatherString(char* buf, size_t bufSize) {
  loadCache();
  if (!cache.hasWeather || cache.fetchedAt <= 0) return false;
  const time_t now = time(nullptr);
  // Freshness can only be judged with a valid clock.
  if (now < MIN_VALID_EPOCH) return false;
  if (static_cast<int64_t>(now) - cache.fetchedAt > WEATHER_FRESH_S) return false;

  const int temp = cache.useFahrenheit ? static_cast<int>(lroundf(cache.tempC * 9.0f / 5.0f + 32.0f))
                                       : static_cast<int>(lroundf(cache.tempC));
  const char* label = weatherLabel(cache.weatherCode);
  // "\xC2\xB0" is UTF-8 for the degree sign.
  if (label[0] != '\0') {
    snprintf(buf, bufSize, "%d\xC2\xB0%c %s", temp, cache.useFahrenheit ? 'F' : 'C', label);
  } else {
    snprintf(buf, bufSize, "%d\xC2\xB0%c", temp, cache.useFahrenheit ? 'F' : 'C');
  }
  return true;
}

bool streakString(char* buf, size_t bufSize) {
  int32_t today;
  if (!localDayNumber(today)) return false;
  StreakData s;
  if (!loadStreak(s)) return false;
  if (s.count < 2) return false;
  if (today - s.lastDay > 1) return false;  // streak broken
  snprintf(buf, bufSize, tr(STR_SLEEP_STREAK_FORMAT), static_cast<unsigned>(s.count));
  return true;
}

void recordReadingDay() {
  int32_t today;
  if (!localDayNumber(today)) return;
  StreakData s;
  loadStreak(s);
  if (s.count > 0 && s.lastDay == today) return;  // already counted today
  if (s.count > 0 && today - s.lastDay == 1) {
    s.count++;
  } else {
    s.count = 1;
  }
  s.lastDay = today;
  s.magic = STREAK_MAGIC;
  HalFile f;
  if (!Storage.openFileForWrite(TAG, STREAK_PATH, f)) return;
  f.write(reinterpret_cast<const uint8_t*>(&s), sizeof(s));
  f.close();
}

}  // namespace SleepGlance
