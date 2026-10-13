#ifndef SETTINGS_ABOUT_PAGE_INTERACTIONS_H_
#define SETTINGS_ABOUT_PAGE_INTERACTIONS_H_

#include <cstdint>
#include <functional>

#include "page_action_result.h"
#include "settings_about_page_coordinator.h"

namespace settings_about_page_interactions {

enum class ActivateIntent : uint8_t {
    kNone = 0,
    kShowHome,
    kShowSettings,
    kRefreshPage,
    kResetUsage,
};

struct ActivateResult {
    ActivateIntent intent = ActivateIntent::kNone;
    bool handled = false;
    bool play_activate_cue = false;
};

using FocusMoveResult = page_actions::FocusMoveOutcome;

struct ActivateCallbacks {
    std::function<void()> show_home;
    std::function<void()> show_settings;
    std::function<void()> refresh_page;
    std::function<void()> reset_usage;
};

ActivateResult HandlePrimaryActivate(SettingsAboutPageCoordinator& coordinator);
void ApplyPrimaryActivateResult(const ActivateResult& result, const ActivateCallbacks& callbacks);
FocusMoveResult HandleMoveFocus(SettingsAboutPageCoordinator& coordinator, int delta);

}  // namespace settings_about_page_interactions

#endif  // SETTINGS_ABOUT_PAGE_INTERACTIONS_H_
