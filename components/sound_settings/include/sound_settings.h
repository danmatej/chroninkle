#ifndef SOUND_SETTINGS_H_
#define SOUND_SETTINGS_H_

#include <cstdint>

// User-configurable sound preferences (Settings > Sound), persisted in NVS. Every category
// defaults to enabled so a fresh device behaves exactly like before the setting existed.
namespace sound_settings {

enum class Category : uint8_t {
    kButtons = 0,
    kNotifications,
    kErrors,
    kLockUnlock,
    kStartup,
    kOnline,
    kRecordingStart,
    kRecordingStop,
    kRecordingReplay,
    kCount,
};

inline constexpr int kCategoryCount = static_cast<int>(Category::kCount);

// Master switch: when off, nothing plays regardless of the per-category values (which are kept
// so turning the master back on restores the previous mix).
bool IsMasterEnabled();
bool SetMasterEnabled(bool enabled);

// Raw per-category value, ignoring the master switch.
bool IsCategoryEnabled(Category category);
bool SetCategoryEnabled(Category category, bool enabled);

// Effective check used by playback call sites: master && category.
bool ShouldPlay(Category category);

const char* CategoryLabel(Category category);

}  // namespace sound_settings

#endif  // SOUND_SETTINGS_H_
