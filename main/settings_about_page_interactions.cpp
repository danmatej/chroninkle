#include "settings_about_page_interactions.h"

namespace settings_about_page_interactions {
namespace {

using page_navigation::NavigationItemRole;

}  // namespace

ActivateResult HandlePrimaryActivate(SettingsAboutPageCoordinator& coordinator)
{
    ActivateResult result = {};

    if (coordinator.IsRoleFocused(NavigationItemRole::kSettingsAboutResetUsageButton)) {
        result.handled = true;
        result.play_activate_cue = true;
        if (coordinator.reset_usage_armed()) {
            coordinator.DisarmResetUsage();
            result.intent = ActivateIntent::kResetUsage;
        } else {
            coordinator.ArmResetUsage();
            result.intent = ActivateIntent::kRefreshPage;
        }
        return result;
    }

    if (coordinator.IsRoleFocused(NavigationItemRole::kSettingsAboutBackButton)) {
        // Returns to the Settings hub -- this page is only ever reached from there, so there's
        // no other source to track.
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
        case ActivateIntent::kRefreshPage:
            if (callbacks.refresh_page) {
                callbacks.refresh_page();
            }
            break;
        case ActivateIntent::kResetUsage:
            if (callbacks.reset_usage) {
                callbacks.reset_usage();
            }
            break;
        case ActivateIntent::kNone:
        default:
            break;
    }
}

FocusMoveResult HandleMoveFocus(SettingsAboutPageCoordinator& coordinator, int delta)
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

}  // namespace settings_about_page_interactions
