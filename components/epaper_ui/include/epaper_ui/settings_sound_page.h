#ifndef EPAPER_UI_SETTINGS_SOUND_PAGE_H_
#define EPAPER_UI_SETTINGS_SOUND_PAGE_H_

#include <array>
#include <cstdint>
#include <string_view>

#include "epaper_ui/button.h"
#include "epaper_ui/global_footer.h"
#include "epaper_ui/menu_toggle.h"
#include "epaper_ui/status_bar.h"

namespace epaper_ui {

// Row 0 is the master "All sounds" switch; rows 1.. are the individual categories in
// sound_settings::Category order.
inline constexpr int kSettingsSoundToggleCount = 10;

struct SettingsSoundPageState {
    int navigation_focus_index = -1;
    std::string_view title_text = "Sound";
    std::array<MenuToggleState, kSettingsSoundToggleCount> toggles = {};
    // Greys out category rows while the master switch is off (their values are kept).
    bool categories_dimmed = false;
    ButtonState back = {};
};

void DrawSettingsSoundPage(uint8_t* framebuffer,
                           int raw_width,
                           int raw_height,
                           int portrait_width,
                           int portrait_height,
                           const SettingsSoundPageState& state,
                           const StatusBarState& status_bar_state,
                           const GlobalFooterState& footer_state);

}  // namespace epaper_ui

#endif  // EPAPER_UI_SETTINGS_SOUND_PAGE_H_
