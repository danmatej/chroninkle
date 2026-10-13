#ifndef EPAPER_UI_MEETING_PAGE_H_
#define EPAPER_UI_MEETING_PAGE_H_

#include <cstdint>
#include <string>

#include "epaper_ui/status_bar.h"

namespace epaper_ui {

// Full-screen "meeting in progress" page. It has no focusable controls: the runtime routes
// FN/ACTION to an "End meeting?" select modal, so this is purely a status display.
struct MeetingPageState {
    std::string title_text = "Meeting";
    // Shows a filled recording dot before the status line while audio is being captured.
    bool recording = false;
    std::string status_text;
    std::string elapsed_text;
    std::string progress_text;
    std::string detail_text;
    // Most recent transcribed text; only the last lines that fit are drawn.
    std::string transcript_tail;
    std::string hint_text;
};

void DrawMeetingPage(uint8_t* framebuffer,
                     int raw_width,
                     int raw_height,
                     int portrait_width,
                     int portrait_height,
                     const MeetingPageState& state,
                     const StatusBarState& status_bar_state);

}  // namespace epaper_ui

#endif  // EPAPER_UI_MEETING_PAGE_H_
