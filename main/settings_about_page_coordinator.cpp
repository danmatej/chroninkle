#include "settings_about_page_coordinator.h"

#include <cstdio>
#include <string>

#include "esp_app_desc.h"
#include "gemini_service.h"

namespace {

std::string FormatCount(uint64_t value)
{
    std::string digits = std::to_string(value);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) {
        digits.insert(static_cast<size_t>(i), ",");
    }
    return digits;
}

}  // namespace

SettingsAboutPageCoordinator::SettingsAboutPageCoordinator() = default;

void SettingsAboutPageCoordinator::Show()
{
    focus_.Configure(navigation_model_.item_count, 0);
    reset_usage_armed_ = false;
}

bool SettingsAboutPageCoordinator::MoveFocus(int delta)
{
    if (!focus_.Move(delta)) {
        return false;
    }
    reset_usage_armed_ = false;
    return true;
}

bool SettingsAboutPageCoordinator::SetFocusIndex(int index)
{
    return focus_.SetIndex(index);
}

bool SettingsAboutPageCoordinator::IsRoleFocused(page_navigation::NavigationItemRole role) const
{
    return navigation_model_.IsRoleSelected(focus_.index(), role);
}

epaper_ui::SettingsAboutPageState SettingsAboutPageCoordinator::BuildState() const
{
    epaper_ui::SettingsAboutPageState state = {};
    state.navigation_focus_index = focus_.index();

    // esp_app_desc_t::version is itself a fixed 32-byte field, so "Version " (8 bytes) + that
    // + a null terminator needs more than 32 bytes of headroom to avoid a format-truncation
    // warning (treated as an error by this project's build flags).
    char version_buffer[48] = {};
    std::snprintf(version_buffer, sizeof(version_buffer), "Version %s",
                 esp_app_get_description()->version);
    state.version_text = version_buffer;

    const gemini_service::UsageStats usage = gemini_service::GetUsageStats();
    state.usage_lines[0] = "Today: " + FormatCount(usage.today_tokens) + " tokens";
    state.usage_lines[1] = "This month: " + FormatCount(usage.month_tokens) + " tokens";
    state.usage_lines[2] = "All time: " + FormatCount(usage.total_tokens) + " tokens (" +
                           FormatCount(usage.total_requests) + " requests)";

    state.reset_usage = {
        .label_text = reset_usage_armed_ ? "Press again to reset" : "Reset usage",
        .selected =
            IsRoleFocused(page_navigation::NavigationItemRole::kSettingsAboutResetUsageButton),
    };

    state.back = {
        .label_text = "Back",
        .selected =
            IsRoleFocused(page_navigation::NavigationItemRole::kSettingsAboutBackButton),
    };
    return state;
}
