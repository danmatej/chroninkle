#ifndef SETTINGS_SOUND_PAGE_RUNTIME_H_
#define SETTINGS_SOUND_PAGE_RUNTIME_H_

#include "app_interaction_target.h"
#include "display_service.h"
#include "esp_err.h"
#include "footer_runtime.h"
#include "page_action_result.h"
#include "settings_sound_page_interactions.h"

namespace settings_sound_page_runtime {

esp_err_t UpdateDisplayState();
esp_err_t UpdateDisplayStateAndRequestRefresh(
    display_service::RefreshMode refresh_mode = display_service::RefreshMode::kPartial);
esp_err_t UpdateDisplayStateAndRequestRefresh(
    const display_service::RefreshRequest& refresh_request);
page_actions::FocusMoveOutcome MoveFocus(int delta);
settings_sound_page_interactions::ActivateResult ActivateFocusedItem();
// Flips the setting behind toggle row toggle_index (0 = "All sounds"), persisting it to NVS.
void ToggleSetting(int toggle_index);

footer_runtime::ProjectionState BuildFooterProjectionState();
page_actions::FocusUpdateOutcome FocusFooterItem(footer_runtime::FooterFocusItem item);
void ResetFocus();

}  // namespace settings_sound_page_runtime

#endif  // SETTINGS_SOUND_PAGE_RUNTIME_H_
