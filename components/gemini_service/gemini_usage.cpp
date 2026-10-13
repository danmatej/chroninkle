#include "gemini_usage.h"

#include <cstdint>
#include <ctime>
#include <mutex>

#include "esp_log.h"
#include "gemini_service.h"
#include "nvs.h"

namespace gemini_service {
namespace {

constexpr const char* kTag = "GeminiUsage";
constexpr const char* kNvsNamespace = "gemini_usage";
constexpr const char* kNvsKey = "stats";
constexpr uint32_t kStoredVersion = 1;
// Before SNTP/RTC sync the clock reads 1970; such requests still count toward the
// all-time total but can't be attributed to a day or month.
constexpr int kMinValidYear = 2024;

struct StoredUsage {
    uint32_t version = kStoredVersion;
    uint32_t day_key = 0;    // YYYYMMDD of the day `today_tokens` belongs to
    uint32_t month_key = 0;  // YYYYMM of the month `month_tokens` belongs to
    uint32_t total_requests = 0;
    uint64_t today_tokens = 0;
    uint64_t month_tokens = 0;
    uint64_t total_tokens = 0;
};

std::mutex s_mutex;
bool s_loaded = false;
StoredUsage s_usage = {};

bool CurrentKeys(uint32_t* day_key, uint32_t* month_key)
{
    const time_t now = time(nullptr);
    struct tm local = {};
    if (localtime_r(&now, &local) == nullptr || local.tm_year + 1900 < kMinValidYear) {
        return false;
    }
    const uint32_t year = static_cast<uint32_t>(local.tm_year + 1900);
    const uint32_t month = static_cast<uint32_t>(local.tm_mon + 1);
    *month_key = (year * 100U) + month;
    *day_key = (*month_key * 100U) + static_cast<uint32_t>(local.tm_mday);
    return true;
}

void EnsureLoadedLocked()
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    StoredUsage stored = {};
    size_t size = sizeof(stored);
    if (nvs_get_blob(handle, kNvsKey, &stored, &size) == ESP_OK && size == sizeof(stored) &&
        stored.version == kStoredVersion) {
        s_usage = stored;
    }
    nvs_close(handle);
}

void SaveLocked()
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, kNvsKey, &s_usage, sizeof(s_usage));
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Saving token usage failed: %s", esp_err_to_name(err));
    }
}

// Zeroes the day/month buckets once the calendar has moved past them.
void RollOverLocked(uint32_t day_key, uint32_t month_key)
{
    if (s_usage.day_key != day_key) {
        s_usage.day_key = day_key;
        s_usage.today_tokens = 0;
    }
    if (s_usage.month_key != month_key) {
        s_usage.month_key = month_key;
        s_usage.month_tokens = 0;
    }
}

int IntField(const cJSON* object, const char* name)
{
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) && item->valuedouble > 0 ? static_cast<int>(item->valuedouble) : 0;
}

}  // namespace

namespace usage {

void RecordFromResponse(const cJSON* root)
{
    const cJSON* metadata =
        root != nullptr ? cJSON_GetObjectItemCaseSensitive(root, "usageMetadata") : nullptr;
    if (!cJSON_IsObject(metadata)) {
        return;
    }
    const int prompt = IntField(metadata, "promptTokenCount");
    const int output = IntField(metadata, "candidatesTokenCount");
    const int thoughts = IntField(metadata, "thoughtsTokenCount");
    int total = IntField(metadata, "totalTokenCount");
    if (total == 0) {
        total = prompt + output + thoughts;
    }
    if (total == 0) {
        return;
    }

    uint32_t day_key = 0;
    uint32_t month_key = 0;
    const bool dated = CurrentKeys(&day_key, &month_key);

    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureLoadedLocked();
    if (dated) {
        RollOverLocked(day_key, month_key);
        s_usage.today_tokens += static_cast<uint64_t>(total);
        s_usage.month_tokens += static_cast<uint64_t>(total);
    }
    s_usage.total_tokens += static_cast<uint64_t>(total);
    ++s_usage.total_requests;
    SaveLocked();
    ESP_LOGI(kTag, "Request tokens: prompt=%d output=%d total=%d (today=%llu all-time=%llu)",
             prompt, output, total, static_cast<unsigned long long>(s_usage.today_tokens),
             static_cast<unsigned long long>(s_usage.total_tokens));
}

}  // namespace usage

UsageStats GetUsageStats()
{
    uint32_t day_key = 0;
    uint32_t month_key = 0;
    const bool dated = CurrentKeys(&day_key, &month_key);

    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureLoadedLocked();
    UsageStats stats = {};
    stats.total_tokens = s_usage.total_tokens;
    stats.total_requests = s_usage.total_requests;
    if (dated) {
        stats.today_tokens = s_usage.day_key == day_key ? s_usage.today_tokens : 0;
        stats.month_tokens = s_usage.month_key == month_key ? s_usage.month_tokens : 0;
    }
    return stats;
}

void ResetUsageStats()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_loaded = true;
    s_usage = {};
    SaveLocked();
    ESP_LOGI(kTag, "Token usage counters reset");
}

}  // namespace gemini_service
