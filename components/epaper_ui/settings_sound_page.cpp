#include "epaper_ui/settings_sound_page.h"

#include <algorithm>

#include "render_utils.h"

namespace epaper_ui {
namespace {

constexpr auto kTitleRole = design::TypographyRole::kHeadingH1;
constexpr auto kRowRole = design::TypographyRole::kLabelMediumBlack;
constexpr int kSideInset = design::spacing::k16;
constexpr int kTopGap = design::spacing::k16;
constexpr int kTitleBottomGap = design::spacing::k12;
constexpr int kBackButtonGap = design::spacing::k12;
constexpr int kFooterGap = design::spacing::k8;
constexpr int kMaxRowHeight = design::menu_toggle::kHeight;

// Ten rows have to share the screen with the title, Back button and footer, so this page uses a
// compact toggle (44px tall including its focus ring) instead of the default 64px one.
ToggleStyle CompactToggleStyle()
{
    ToggleStyle style = {};
    style.width = 64;
    style.height = 32;
    style.thumb_size = 24;
    style.thumb_inset = 4;
    style.thumb_border_thickness = 3;
    style.focus_ring_thickness = 4;
    style.focus_gap = 2;
    return style;
}

struct Layout {
    int page_width = 0;
    int rows_y = 0;
    int row_height = 0;
    UiRect back = {};
};

Layout BuildLayout(int portrait_width,
                   int portrait_height,
                   const SettingsSoundPageState& state,
                   const GlobalFooterState& footer_state)
{
    Layout layout = {};
    layout.page_width = std::max(0, portrait_width - (2 * kSideInset));
    const int title_y = StatusBarHeight() + kTopGap;
    layout.rows_y = title_y + LineHeight(kTitleRole) + kTitleBottomGap;

    const UiRect footer = GlobalFooterBounds(portrait_width, portrait_height, footer_state);
    const int content_bottom = (footer.IsEmpty() ? portrait_height : footer.y) - kFooterGap;
    const int available_rows_height =
        content_bottom - layout.rows_y - kBackButtonGap - design::button::kHeight;
    layout.row_height =
        std::clamp(available_rows_height / kSettingsSoundToggleCount, 0, kMaxRowHeight);

    ButtonStyle back_style = {};
    back_style.width = layout.page_width;
    back_style.center_label = true;
    const int rows_bottom = layout.rows_y + (layout.row_height * kSettingsSoundToggleCount);
    layout.back = ButtonBounds(kSideInset, rows_bottom + kBackButtonGap, state.back, back_style);
    return layout;
}

}  // namespace

void DrawSettingsSoundPage(uint8_t* framebuffer,
                           int raw_width,
                           int raw_height,
                           int portrait_width,
                           int portrait_height,
                           const SettingsSoundPageState& state,
                           const StatusBarState& status_bar_state,
                           const GlobalFooterState& footer_state)
{
    if (framebuffer == nullptr) {
        return;
    }

    FillPortraitRect(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                     {0, 0, portrait_width, portrait_height}, design::color::kWhite);
    DrawStatusBar(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                  status_bar_state);

    const Layout layout = BuildLayout(portrait_width, portrait_height, state, footer_state);
    DrawTypographyText(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                       kSideInset, StatusBarHeight() + kTopGap, state.title_text, kTitleRole,
                       design::color::kBlack);

    MenuToggleStyle row_style = {};
    row_style.role = kRowRole;
    row_style.width = layout.page_width;
    row_style.height = layout.row_height;
    row_style.toggle = CompactToggleStyle();
    for (int index = 0; index < kSettingsSoundToggleCount; ++index) {
        MenuToggleStyle style = row_style;
        if (index == 0) {
            // Heavier divider under the master switch to separate it from the categories.
            style.bottom_border_thickness = 3;
        } else if (state.categories_dimmed) {
            style.text_color = design::color::kGrayDark;
        }
        DrawMenuToggle(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                       kSideInset, layout.rows_y + (index * layout.row_height),
                       state.toggles[static_cast<size_t>(index)], style);
    }

    DrawButton(framebuffer, raw_width, raw_height, portrait_width, portrait_height, layout.back.x,
               layout.back.y, state.back, {.width = layout.back.width, .center_label = true});

    DrawGlobalFooter(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                     footer_state);
}

}  // namespace epaper_ui
