#include "sound_settings.h"

#include <mutex>

#include "esp_log.h"
#include "nvs.h"

namespace sound_settings {
namespace {

constexpr const char* kTag = "SoundSettings";
constexpr const char* kNvsNamespace = "sound";
constexpr const char* kMaskKey = "mask";

// Bit 0 is the master switch; bit (1 + category) is each category.
constexpr uint32_t kMasterBit = 1u << 0;
constexpr uint32_t kAllEnabled = 0xFFFFFFFFu;

std::mutex s_mutex;
bool s_loaded = false;
uint32_t s_mask = kAllEnabled;

constexpr uint32_t CategoryBit(Category category)
{
    return 1u << (1 + static_cast<int>(category));
}

void EnsureLoadedLocked()
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
        uint32_t mask = kAllEnabled;
        if (nvs_get_u32(handle, kMaskKey, &mask) == ESP_OK) {
            s_mask = mask;
        }
        nvs_close(handle);
    }
    ESP_LOGI(kTag, "Loaded sound settings mask 0x%08lx", static_cast<unsigned long>(s_mask));
}

bool SaveLocked()
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "nvs_open failed: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_set_u32(handle, kMaskKey, s_mask);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Saving sound settings failed: %s", esp_err_to_name(err));
    }
    return err == ESP_OK;
}

bool SetBitLocked(uint32_t bit, bool enabled)
{
    EnsureLoadedLocked();
    const uint32_t next = enabled ? (s_mask | bit) : (s_mask & ~bit);
    if (next == s_mask) {
        return true;
    }
    s_mask = next;
    return SaveLocked();
}

bool IsValid(Category category)
{
    return category < Category::kCount;
}

}  // namespace

bool IsMasterEnabled()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureLoadedLocked();
    return (s_mask & kMasterBit) != 0;
}

bool SetMasterEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return SetBitLocked(kMasterBit, enabled);
}

bool IsCategoryEnabled(Category category)
{
    if (!IsValid(category)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureLoadedLocked();
    return (s_mask & CategoryBit(category)) != 0;
}

bool SetCategoryEnabled(Category category, bool enabled)
{
    if (!IsValid(category)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_mutex);
    return SetBitLocked(CategoryBit(category), enabled);
}

bool ShouldPlay(Category category)
{
    if (!IsValid(category)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureLoadedLocked();
    return (s_mask & kMasterBit) != 0 && (s_mask & CategoryBit(category)) != 0;
}

const char* CategoryLabel(Category category)
{
    switch (category) {
        case Category::kButtons:
            return "Button clicks";
        case Category::kNotifications:
            return "Notifications";
        case Category::kErrors:
            return "Errors";
        case Category::kLockUnlock:
            return "Lock / unlock";
        case Category::kStartup:
            return "Startup";
        case Category::kOnline:
            return "Online";
        case Category::kRecordingStart:
            return "Recording start";
        case Category::kRecordingStop:
            return "Recording stop";
        case Category::kRecordingReplay:
            return "Replay recording";
        case Category::kCount:
        default:
            return "";
    }
}

}  // namespace sound_settings
