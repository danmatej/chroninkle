#include "settings_display_page_interactions.h"

namespace settings_display_page_interactions {
namespace {

using page_navigation::NavigationItemRole;

ActivateResult Handled(ActivateIntent intent)
{
    ActivateResult result = {};
    result.handled = true;
    result.play_activate_cue = true;
    result.intent = intent;
    return result;
}

}  // namespace

ActivateResult HandlePrimaryActivate(const SettingsDisplayPageCoordinator& coordinator)
{
    if (coordinator.IsRoleFocused(NavigationItemRole::kSettingsDisplayCleanup)) {
        return Handled(ActivateIntent::kShowCleanupModal);
    }
    if (coordinator.IsRoleFocused(NavigationItemRole::kSettingsDisplayBackButton)) {
        return Handled(ActivateIntent::kShowSettings);
    }
    if (coordinator.IsRoleFocused(NavigationItemRole::kFooterHome)) {
        return Handled(ActivateIntent::kShowHome);
    }
    return {};
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
        case ActivateIntent::kShowCleanupModal:
            if (callbacks.show_cleanup_modal) {
                callbacks.show_cleanup_modal();
            }
            break;
        case ActivateIntent::kNone:
        default:
            break;
    }
}

FocusMoveResult HandleMoveFocus(SettingsDisplayPageCoordinator& coordinator, int delta)
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

}  // namespace settings_display_page_interactions
