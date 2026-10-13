#include "feedback_service.h"

#include "esp_log.h"
#include "waveshare_board.h"
#include "sound_settings.h"
#include "system_sound_service.h"

namespace feedback_service {
namespace {

constexpr const char* kTag = "FeedbackService";

// Maps a feedback event onto the closest system-sound cue (played through the
// ES8311 codec). There is no dedicated buzzer on this board.
SoundCue CueForEvent(FeedbackEvent event)
{
    switch (event) {
        case FeedbackEvent::kStartup:
            return SoundCue::kStartup;
        case FeedbackEvent::kGeminiConnected:
            return SoundCue::kOnline;
        case FeedbackEvent::kLock:
            return SoundCue::kLock;
        case FeedbackEvent::kUnlock:
            return SoundCue::kUnlock;
        case FeedbackEvent::kRecordingStart:
            return SoundCue::kButtonActivate;
        case FeedbackEvent::kModalOpen:
            return SoundCue::kModalNotification;
        case FeedbackEvent::kButtonClick:
        case FeedbackEvent::kButtonDoubleClick:
        case FeedbackEvent::kButtonLongPress:
            return SoundCue::kButtonActivate;
        case FeedbackEvent::kShutdown:
            return SoundCue::kModalNotification;
        case FeedbackEvent::kError:
        default:
            return SoundCue::kInterrupt;
    }
}

// Several events share one cue (e.g. kInterrupt is both "error" and "recording stop"), so the
// user-facing Settings > Sound categories are keyed by event rather than by cue.
sound_settings::Category CategoryForEvent(FeedbackEvent event)
{
    switch (event) {
        case FeedbackEvent::kStartup:
            return sound_settings::Category::kStartup;
        case FeedbackEvent::kGeminiConnected:
            return sound_settings::Category::kOnline;
        case FeedbackEvent::kLock:
        case FeedbackEvent::kUnlock:
            return sound_settings::Category::kLockUnlock;
        case FeedbackEvent::kRecordingStart:
            return sound_settings::Category::kRecordingStart;
        case FeedbackEvent::kModalOpen:
        case FeedbackEvent::kShutdown:
            return sound_settings::Category::kNotifications;
        case FeedbackEvent::kButtonClick:
        case FeedbackEvent::kButtonDoubleClick:
        case FeedbackEvent::kButtonLongPress:
            return sound_settings::Category::kButtons;
        case FeedbackEvent::kError:
        default:
            return sound_settings::Category::kErrors;
    }
}

}  // namespace

esp_err_t Init()
{
    AudioCodec* codec = waveshare_board::GetAudioCodec();
    if (codec == nullptr) {
        ESP_LOGW(kTag, "Audio codec unavailable; feedback cues disabled");
        return ESP_ERR_NOT_FOUND;
    }

    // Starts the sound-service playback task and warms (decodes) the cue cache.
    SystemSoundService::GetInstance().Initialize(codec);
    ESP_LOGI(kTag, "Feedback service initialized (audio cues via ES8311)");
    return ESP_OK;
}

esp_err_t Play(FeedbackEvent event)
{
    if (!sound_settings::ShouldPlay(CategoryForEvent(event))) {
        return ESP_OK;
    }
    SystemSoundService::GetInstance().PlayCue(CueForEvent(event));
    return ESP_OK;
}

}  // namespace feedback_service
