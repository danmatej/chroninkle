#include "settings_sound_page_coordinator.h"

#include "sound_settings.h"

namespace {

using page_navigation::NavigationItemRole;

static_assert(epaper_ui::kSettingsSoundToggleCount == 1 + sound_settings::kCategoryCount,
              "Sound page rows must be the master switch plus one row per category");

}  // namespace

SettingsSoundPageCoordinator::SettingsSoundPageCoordinator() = default;

void SettingsSoundPageCoordinator::Show()
{
    focus_.Configure(navigation_model_.item_count, 0);
}

bool SettingsSoundPageCoordinator::MoveFocus(int delta)
{
    return focus_.Move(delta);
}

bool SettingsSoundPageCoordinator::SetFocusIndex(int index)
{
    return focus_.SetIndex(index);
}

bool SettingsSoundPageCoordinator::IsRoleFocused(NavigationItemRole role) const
{
    return navigation_model_.IsRoleSelected(focus_.index(), role);
}

int SettingsSoundPageCoordinator::FocusedToggleIndex() const
{
    const page_navigation::NavigationItemDescriptor* item =
        navigation_model_.ItemAt(focus_.index());
    if (item == nullptr || item->role != NavigationItemRole::kSettingsSoundToggle) {
        return -1;
    }
    return item->item_index;
}

epaper_ui::SettingsSoundPageState SettingsSoundPageCoordinator::BuildState() const
{
    epaper_ui::SettingsSoundPageState state = {};
    state.navigation_focus_index = focus_.index();

    const bool master = sound_settings::IsMasterEnabled();
    const int focused_toggle = FocusedToggleIndex();
    for (int index = 0; index < epaper_ui::kSettingsSoundToggleCount; ++index) {
        bool on = master;
        const char* label = "All sounds";
        if (index > 0) {
            const auto category = static_cast<sound_settings::Category>(index - 1);
            on = sound_settings::IsCategoryEnabled(category);
            label = sound_settings::CategoryLabel(category);
        }
        const bool focused = index == focused_toggle;
        epaper_ui::MenuToggleState& toggle = state.toggles[static_cast<size_t>(index)];
        toggle.label_text = label;
        toggle.toggle_state = focused ? (on ? epaper_ui::ToggleVisualState::kFocusOn
                                            : epaper_ui::ToggleVisualState::kFocusOff)
                                      : (on ? epaper_ui::ToggleVisualState::kOn
                                            : epaper_ui::ToggleVisualState::kOff);
    }
    state.categories_dimmed = !master;

    state.back = {
        .label_text = "Back",
        .selected = IsRoleFocused(NavigationItemRole::kSettingsSoundBackButton),
    };
    return state;
}
