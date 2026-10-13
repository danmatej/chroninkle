#include "meeting_page_runtime.h"

#include <cinttypes>
#include <cstdio>
#include <mutex>
#include <string>

#include "epaper_ui/meeting_page.h"
#include "epaper_ui/select_modal.h"
#include "esp_timer.h"
#include "meeting_service.h"
#include "overlay_runtime.h"
#include "ui_refresh_runtime.h"

namespace meeting_page_runtime {
namespace {

constexpr int kEndModalEndIndex = 0;
constexpr uint64_t kElapsedTickUs = 60ULL * 1000ULL * 1000ULL;

std::mutex s_mutex;
bool s_end_modal_active = false;
esp_timer_handle_t s_ticker = nullptr;

std::string FormatElapsed(uint32_t elapsed_ms)
{
    const uint32_t total_seconds = elapsed_ms / 1000U;
    const uint32_t hours = total_seconds / 3600U;
    const uint32_t minutes = (total_seconds / 60U) % 60U;
    char text[24] = {};
    if (hours > 0) {
        std::snprintf(text, sizeof(text), "%" PRIu32 " h %02" PRIu32 " min", hours, minutes);
    } else {
        std::snprintf(text, sizeof(text), "%" PRIu32 " min", minutes);
    }
    return text;
}

std::string FlattenWhitespace(const std::string& text)
{
    std::string flat = {};
    flat.reserve(text.size());
    bool last_space = false;
    for (char c : text) {
        const bool space = c == '\n' || c == '\r' || c == '\t' || c == ' ';
        if (space) {
            if (!last_space && !flat.empty()) {
                flat.push_back(' ');
            }
            last_space = true;
        } else {
            flat.push_back(c);
            last_space = false;
        }
    }
    return flat;
}

epaper_ui::MeetingPageState BuildState()
{
    const meeting_service::Snapshot snapshot = meeting_service::GetSnapshot();
    epaper_ui::MeetingPageState state = {};
    state.title_text = "Meeting";
    state.recording = snapshot.active;
    state.status_text = snapshot.active ? "Recording" : "Finishing transcription";
    state.elapsed_text = FormatElapsed(snapshot.elapsed_ms);

    char progress[64] = {};
    std::snprintf(progress, sizeof(progress), "Parts transcribed: %d of %d",
                  snapshot.segments_transcribed, snapshot.segments_recorded);
    state.progress_text = progress;

    if (snapshot.waiting_for_network) {
        state.detail_text = "Waiting for Wi-Fi. Audio is kept on the SD card.";
    } else if (snapshot.retry_scheduled) {
        state.detail_text = snapshot.last_error.empty()
                                ? std::string("Transcription will retry shortly.")
                                : "Retrying: " + snapshot.last_error;
    } else if (snapshot.transcribing) {
        state.detail_text = "Transcribing...";
    }

    state.transcript_tail = FlattenWhitespace(snapshot.transcript_tail);
    state.hint_text = snapshot.active ? "Press FN or ACTION to end the meeting" : "";
    return state;
}

void TickerCallback(void* /*arg*/)
{
    if (meeting_service::IsActive()) {
        (void)UpdateDisplayStateAndRequestRefresh(display_service::RefreshMode::kPartial);
    }
}

}  // namespace

esp_err_t UpdateDisplayState()
{
    return display_service::SetMeetingPageState(BuildState());
}

esp_err_t UpdateDisplayStateAndRequestRefresh(display_service::RefreshMode refresh_mode)
{
    return UpdateDisplayStateAndRequestRefresh(
        display_service::RefreshRequest{.refresh_mode = refresh_mode});
}

esp_err_t UpdateDisplayStateAndRequestRefresh(
    const display_service::RefreshRequest& refresh_request)
{
    return ui_refresh_runtime::Schedule(ui_refresh_runtime::SurfaceKey::kMeetingPage,
                                        &UpdateDisplayState, refresh_request);
}

void StartElapsedTicker()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_ticker == nullptr) {
        const esp_timer_create_args_t args = {
            .callback = &TickerCallback,
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "meeting_tick",
            .skip_unhandled_events = true,
        };
        if (esp_timer_create(&args, &s_ticker) != ESP_OK) {
            s_ticker = nullptr;
            return;
        }
    }
    (void)esp_timer_stop(s_ticker);
    (void)esp_timer_start_periodic(s_ticker, kElapsedTickUs);
}

void StopElapsedTicker()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_ticker != nullptr) {
        (void)esp_timer_stop(s_ticker);
    }
}

esp_err_t ShowEndModal()
{
    epaper_ui::SelectModalState state = {};
    state.visible = true;
    state.title_text = "End meeting?";
    state.selected_index = kEndModalEndIndex;
    state.items.push_back({.label_text = "End & save"});
    state.items.push_back({.label_text = "Keep recording"});
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_end_modal_active = true;
    }
    return overlay_runtime::ShowSelectModal(state);
}

bool HandleSelectModalSubmit(int selected_index, bool* end_requested)
{
    bool was_active = false;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        was_active = s_end_modal_active;
        s_end_modal_active = false;
    }
    if (end_requested != nullptr) {
        *end_requested = was_active && selected_index == kEndModalEndIndex;
    }
    return was_active;
}

void ClearPendingSelectModal()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_end_modal_active = false;
}

bool IsEndModalActive()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_end_modal_active;
}

}  // namespace meeting_page_runtime
