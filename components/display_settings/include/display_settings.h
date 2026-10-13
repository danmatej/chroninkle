#ifndef DISPLAY_SETTINGS_H_
#define DISPLAY_SETTINGS_H_

// User-configurable display preferences (Settings > Display), persisted in NVS.
namespace display_settings {

// Screen cleanup frequency: how many fast (partial) e-paper updates are allowed before the
// whole screen is re-driven with a full refresh to clear ghosting.
inline constexpr int kCleanupOptionCount = 4;

int GetCleanupIndex();
bool SetCleanupIndex(int index);

// Partial-update budget for the current (or given) option.
int GetCleanupInterval();
int CleanupIntervalForIndex(int index);

// e.g. "Normal"; and a short "every N updates" detail for the option.
const char* CleanupLabel(int index);
const char* CleanupDetail(int index);

}  // namespace display_settings

#endif  // DISPLAY_SETTINGS_H_
