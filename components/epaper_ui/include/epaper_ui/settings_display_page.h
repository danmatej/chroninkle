#ifndef EPAPER_UI_SETTINGS_DISPLAY_PAGE_H_
#define EPAPER_UI_SETTINGS_DISPLAY_PAGE_H_

#include <cstdint>
#include <string_view>

#include "epaper_ui/button.h"
#include "epaper_ui/global_footer.h"
#include "epaper_ui/select_input.h"
#include "epaper_ui/status_bar.h"

namespace epaper_ui {

struct SettingsDisplayPageState {
    int navigation_focus_index = -1;
    std::string_view title_text = "Display";
    // "Screen cleanup" select field; activating it opens a select modal owned by the caller.
    SelectInputState cleanup = {};
    std::string_view help_text =
        "Screen cleanup briefly flashes the whole screen to clear faint leftover images. "
        "Less often means fewer flashes but more ghosting between cleanups.";
    ButtonState back = {};
};

void DrawSettingsDisplayPage(uint8_t* framebuffer,
                             int raw_width,
                             int raw_height,
                             int portrait_width,
                             int portrait_height,
                             const SettingsDisplayPageState& state,
                             const StatusBarState& status_bar_state,
                             const GlobalFooterState& footer_state);

}  // namespace epaper_ui

#endif  // EPAPER_UI_SETTINGS_DISPLAY_PAGE_H_
