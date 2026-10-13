#ifndef MEETING_PAGE_RUNTIME_H_
#define MEETING_PAGE_RUNTIME_H_

#include "display_service.h"
#include "esp_err.h"

// Composes meeting_service state into the Meeting screen and owns the "End meeting?" confirm
// modal. Meeting capture/transcription mechanics live in meeting_service.
namespace meeting_page_runtime {

esp_err_t UpdateDisplayState();
esp_err_t UpdateDisplayStateAndRequestRefresh(
    display_service::RefreshMode refresh_mode = display_service::RefreshMode::kPartial);
esp_err_t UpdateDisplayStateAndRequestRefresh(
    const display_service::RefreshRequest& refresh_request);

// Starts/stops a once-a-minute repaint of the elapsed time while the Meeting screen is shown.
void StartElapsedTicker();
void StopElapsedTicker();

esp_err_t ShowEndModal();
// Returns true when the submit belonged to the end-meeting modal; `*end_requested` is set when
// the user chose to end the meeting.
bool HandleSelectModalSubmit(int selected_index, bool* end_requested);
void ClearPendingSelectModal();
bool IsEndModalActive();

}  // namespace meeting_page_runtime

#endif  // MEETING_PAGE_RUNTIME_H_
