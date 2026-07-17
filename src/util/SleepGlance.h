#pragma once

#include <cstddef>

// SkyPoint: glanceable sleep-screen info (clock / weather / reading streak),
// zero-config and location-correct for any user:
//  - Time: X3 uses its DS3231 RTC via the existing upstream flow. The X4 has
//    no RTC chip, so the system clock is used instead — valid once an SNTP
//    sync has happened since cold boot (refresh() performs one opportunistically,
//    and time then survives deep sleep via the ESP32 RTC timer domain).
//  - Location/timezone/weather: IP geolocation (ip-api.com, no key) for
//    lat/lon/UTC-offset/country + Open-Meteo (no key) for current conditions,
//    so the data is for wherever the device actually is. Fetched ONLY when
//    Wi-Fi is already connected at sleep entry — the radio is never powered
//    on for this — and cached on SD for offline sleeps.
//  - Streak: consecutive days with any reading activity, tracked on-device.
// Every string function is best-effort: it returns false when its data isn't
// trustworthy yet, and the caller simply doesn't draw that line.
namespace SleepGlance {

// Opportunistic refresh at sleep entry. No-op when Wi-Fi is down; throttled
// to one fetch per 30 minutes otherwise.
void refresh();

// Local time, e.g. "14:05" or "2:05 PM" (follows the clock format setting).
bool timeString(char* buf, size_t bufSize);

// Current conditions, e.g. "72°F Partly cloudy". False when the cached
// weather is missing or older than 6 hours.
bool weatherString(char* buf, size_t bufSize);

// Reading streak, e.g. "5-day reading streak". False when below 2 days or broken.
bool streakString(char* buf, size_t bufSize);

// Called when a reading session ends; extends/starts the daily streak.
void recordReadingDay();

}  // namespace SleepGlance
