#pragma once

#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

namespace chroninkle_task_config {

inline constexpr BaseType_t kSystemCore = 0;

#if CONFIG_FREERTOS_UNICORE
inline constexpr BaseType_t kAppCore = 0;
#else
inline constexpr BaseType_t kAppCore = 1;
#endif

inline constexpr UBaseType_t kPriorityRecordCapture = 5;
inline constexpr UBaseType_t kPriorityTouch = 5;
// Captive-portal DNS responder (AP setup mode). High priority so a client device's
// captive-portal detection probe gets answered promptly instead of timing out.
inline constexpr UBaseType_t kPriorityCaptiveDns = 5;
inline constexpr UBaseType_t kPriorityUiRefresh = 4;
inline constexpr UBaseType_t kPriorityAppSleep = 4;
inline constexpr UBaseType_t kPriorityAppShutdown = 4;
inline constexpr UBaseType_t kPriorityDisplay = 3;
inline constexpr UBaseType_t kPrioritySleepMotion = 3;
inline constexpr UBaseType_t kPriorityWifiTransition = 3;
inline constexpr UBaseType_t kPriorityWifiCallbacks = 3;
inline constexpr UBaseType_t kPriorityStorage = 2;
inline constexpr UBaseType_t kPriorityTimezoneSync = 2;
inline constexpr UBaseType_t kPriorityGemini = 2;
// One-shot batch task for retrying offline-queued transcriptions. Must NOT run on
// gemini_service's own shared worker task: it blocks waiting on a transcription result that
// is itself queued onto that same worker, which would never get a turn to run.
inline constexpr UBaseType_t kPriorityTranscriptionRetry = 2;
// Background battery/RTC telemetry poll. Low priority on purpose: it caches
// last-good values off the UI path, so a poll that loses a race to SD/display
// bus activity simply retries on the next cycle without ever blocking a refresh.
inline constexpr UBaseType_t kPrioritySensorPoll = 2;
// Meeting-mode SD writer (CPU1): drains recording_service's stream ring to segment WAVs.
// Above storage/Gemini so a long upload can't starve it, below capture so it never steals
// time from the I2S read that fills the ring.
inline constexpr UBaseType_t kPriorityMeetingWriter = 3;
// Meeting-mode background transcriber (CPU0, network side). Blocks on a Gemini job queued
// onto gemini_service's worker, so like transcription retry it must be its own task.
inline constexpr UBaseType_t kPriorityMeetingTranscribe = 2;

}  // namespace chroninkle_task_config
