#include "settings_display_page_coordinator.h"

#include <string>

#include "display_settings.h"

namespace {

using page_navigation::NavigationItemRole;

}  // namespace

SettingsDisplayPageCoordinator::SettingsDisplayPageCoordinator() = default;

void SettingsDisplayPageCoordinator::Show()
{
    focus_.Configure(navigation_model_.item_count, 0);
}

bool SettingsDisplayPageCoordinator::MoveFocus(int delta)
{
    return focus_.Move(delta);
}

bool SettingsDisplayPageCoordinator::SetFocusIndex(int index)
{
    return focus_.SetIndex(index);
}

bool SettingsDisplayPageCoordinator::IsRoleFocused(NavigationItemRole role) const
{
    return navigation_model_.IsRoleSelected(focus_.index(), role);
}

epaper_ui::SettingsDisplayPageState SettingsDisplayPageCoordinator::BuildState() const
{
    epaper_ui::SettingsDisplayPageState state = {};
    state.navigation_focus_index = focus_.index();

    const int cleanup_index = display_settings::GetCleanupIndex();
    state.cleanup.label_text = "Screen cleanup";
    state.cleanup.value_text = std::string(display_settings::CleanupLabel(cleanup_index)) +
                               " - " + display_settings::CleanupDetail(cleanup_index);
    state.cleanup.focused = IsRoleFocused(NavigationItemRole::kSettingsDisplayCleanup);

    state.back = {
        .label_text = "Back",
        .selected = IsRoleFocused(NavigationItemRole::kSettingsDisplayBackButton),
    };
    return state;
}
