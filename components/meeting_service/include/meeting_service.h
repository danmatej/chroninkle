#ifndef MEETING_SERVICE_H_
#define MEETING_SERVICE_H_

#include <cstdint>
#include <string>

#include "esp_err.h"

// Meeting mode: hands-free, open-ended recording. Audio streams from recording_service into
// fixed-length WAV segments under /sdcard/meetings/<id>/; a background task transcribes each
// closed segment with Gemini, appends the text to the meeting's transcript.txt and deletes the
// WAV. When the meeting has ended and every segment is transcribed, the transcript is
// published as one Note via recording_archive_service. Untranscribed segments survive reboots
// and offline periods and are retried until they succeed.
namespace meeting_service {

enum class EventType : uint8_t {
    kStarted,
    kProgress,
    kStopped,
    kSaved,
    kError,
};

struct Snapshot {
    // Audio is being captured into the current meeting.
    bool active = false;
    // Elapsed recorded audio of the current (or most recent) meeting.
    uint32_t elapsed_ms = 0;
    // Segments of the current/most recent meeting: started, and transcribed so far.
    int segments_recorded = 0;
    int segments_transcribed = 0;
    // Closed segments waiting for transcription, across every meeting on the card.
    int segments_pending = 0;
    // An upload is in flight right now.
    bool transcribing = false;
    // Work is pending but Gemini isn't ready (no Wi-Fi / not authenticated).
    bool waiting_for_network = false;
    // Last transcription attempt failed and is backing off before a retry.
    bool retry_scheduled = false;
    std::string last_error;
    // Tail of the current meeting's transcript, for the on-screen preview.
    std::string transcript_tail;
    // Set when the current meeting's note was published.
    std::string saved_recording_id;
};

struct Event {
    EventType type = EventType::kProgress;
    Snapshot snapshot;
};

using EventHandler = void (*)(const Event& event, void* context);

esp_err_t Init();
void SetEventHandler(EventHandler handler, void* context);
Snapshot GetSnapshot();

// Starts a meeting. Fails with ESP_ERR_NOT_FOUND when no SD card is usable,
// ESP_ERR_NO_MEM when the card is nearly full, and ESP_ERR_INVALID_STATE while a meeting or a
// press-and-hold recording is already running.
esp_err_t Start();
// Ends the current meeting (non-blocking). Capture stops immediately; the tail of the audio is
// flushed and transcribed in the background.
esp_err_t Stop();
// Like Stop, but waits (bounded) for the last audio to reach the SD card. For power-off.
void StopForShutdown();
bool IsActive();
// True while there is transcription work that can run right now (pending segments and Gemini
// ready), so callers can hold off light sleep until it drains.
bool IsTranscribing();
// Wakes the transcriber early, e.g. on Wi-Fi reconnect or Gemini becoming ready.
void Kick();

}  // namespace meeting_service

#endif  // MEETING_SERVICE_H_
