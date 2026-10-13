#include "display_settings.h"

#include <cstdint>
#include <mutex>

#include "esp_log.h"
#include "nvs.h"

namespace display_settings {
namespace {

constexpr const char* kTag = "DisplaySettings";
constexpr const char* kNvsNamespace = "display";
constexpr const char* kCleanupKey = "cleanup";

struct CleanupOption {
    const char* label;
    const char* detail;
    int interval;
};

// "Often" matches the original fixed budget (8); the default is one step rarer.
constexpr CleanupOption kCleanupOptions[kCleanupOptionCount] = {
    {"Often", "Every 8 updates", 8},
    {"Normal", "Every 16 updates", 16},
    {"Rarely", "Every 32 updates", 32},
    {"Minimal", "Every 64 updates", 64},
};
constexpr int kDefaultCleanupIndex = 1;

std::mutex s_mutex;
bool s_loaded = false;
int s_cleanup_index = kDefaultCleanupIndex;

bool IsValidIndex(int index)
{
    return index >= 0 && index < kCleanupOptionCount;
}

void EnsureLoadedLocked()
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
        uint8_t value = 0;
        if (nvs_get_u8(handle, kCleanupKey, &value) == ESP_OK && IsValidIndex(value)) {
            s_cleanup_index = value;
        }
        nvs_close(handle);
    }
    ESP_LOGI(kTag, "Loaded screen cleanup option %d (%s)", s_cleanup_index,
             kCleanupOptions[s_cleanup_index].label);
}

bool SaveLocked()
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "nvs_open failed: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_set_u8(handle, kCleanupKey, static_cast<uint8_t>(s_cleanup_index));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Saving display settings failed: %s", esp_err_to_name(err));
    }
    return err == ESP_OK;
}

}  // namespace

int GetCleanupIndex()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureLoadedLocked();
    return s_cleanup_index;
}

bool SetCleanupIndex(int index)
{
    if (!IsValidIndex(index)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureLoadedLocked();
    if (index == s_cleanup_index) {
        return true;
    }
    s_cleanup_index = index;
    return SaveLocked();
}

int GetCleanupInterval()
{
    return CleanupIntervalForIndex(GetCleanupIndex());
}

int CleanupIntervalForIndex(int index)
{
    return kCleanupOptions[IsValidIndex(index) ? index : kDefaultCleanupIndex].interval;
}

const char* CleanupLabel(int index)
{
    return IsValidIndex(index) ? kCleanupOptions[index].label : "";
}

const char* CleanupDetail(int index)
{
    return IsValidIndex(index) ? kCleanupOptions[index].detail : "";
}

}  // namespace display_settings
