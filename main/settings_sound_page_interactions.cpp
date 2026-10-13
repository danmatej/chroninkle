#include "settings_sound_page_interactions.h"

namespace settings_sound_page_interactions {
namespace {

using page_navigation::NavigationItemRole;

}  // namespace

ActivateResult HandlePrimaryActivate(const SettingsSoundPageCoordinator& coordinator)
{
    ActivateResult result = {};

    const int toggle_index = coordinator.FocusedToggleIndex();
    if (toggle_index >= 0) {
        result.handled = true;
        result.play_activate_cue = true;
        result.intent = ActivateIntent::kToggle;
        result.toggle_index = toggle_index;
        return result;
    }
    if (coordinator.IsRoleFocused(NavigationItemRole::kSettingsSoundBackButton)) {
        result.handled = true;
        result.play_activate_cue = true;
        result.intent = ActivateIntent::kShowSettings;
        return result;
    }
    if (coordinator.IsRoleFocused(NavigationItemRole::kFooterHome)) {
        result.handled = true;
        result.play_activate_cue = true;
        result.intent = ActivateIntent::kShowHome;
        return result;
    }

    return result;
}

void ApplyPrimaryActivateResult(const ActivateResult& result, const ActivateCallbacks& callbacks)
{
    switch (result.intent) {
        case ActivateIntent::kShowHome:
            if (callbacks.show_home) {
                callbacks.show_home();
            }
            break;
        case ActivateIntent::kShowSettings:
            if (callbacks.show_settings) {
                callbacks.show_settings();
            }
            break;
        case ActivateIntent::kToggle:
            if (callbacks.toggle) {
                callbacks.toggle(result.toggle_index);
            }
            break;
        case ActivateIntent::kNone:
        default:
            break;
    }
}

FocusMoveResult HandleMoveFocus(SettingsSoundPageCoordinator& coordinator, int delta)
{
    FocusMoveResult result = {};
    if (!coordinator.MoveFocus(delta)) {
        return result;
    }
    result.handled = true;
    result.play_navigation_cue = true;
    result.apply_page_state = true;
    return result;
}

}  // namespace settings_sound_page_interactions
