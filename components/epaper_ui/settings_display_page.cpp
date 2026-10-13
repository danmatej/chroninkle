#include "epaper_ui/settings_display_page.h"

#include <algorithm>
#include <string>
#include <vector>

#include "render_utils.h"

namespace epaper_ui {
namespace {

constexpr auto kTitleRole = design::TypographyRole::kHeadingH1;
constexpr auto kBodyRole = design::TypographyRole::kBody;
constexpr int kSideInset = design::spacing::k16;
constexpr int kTopGap = design::spacing::k24;
constexpr int kTitleBottomGap = design::spacing::k24;
constexpr int kFieldHelpGap = design::spacing::k16;
constexpr int kBodyLineGap = design::spacing::k4;
constexpr int kBackButtonGap = design::spacing::k24;

TextInputStyle FieldStyle(int width)
{
    TextInputStyle style = {};
    style.width = width;
    return style;
}

struct Layout {
    int page_width = 0;
    UiRect cleanup = {};
    int help_y = 0;
    UiRect back = {};
};

Layout BuildLayout(int portrait_width, const SettingsDisplayPageState& state)
{
    Layout layout = {};
    layout.page_width = std::max(0, portrait_width - (2 * kSideInset));
    const int title_y = StatusBarHeight() + kTopGap;
    const int field_y = title_y + LineHeight(kTitleRole) + kTitleBottomGap;
    layout.cleanup =
        SelectInputBounds(kSideInset, field_y, state.cleanup, FieldStyle(layout.page_width));
    layout.help_y = layout.cleanup.bottom() + kFieldHelpGap;

    const std::vector<std::string> help_lines =
        WrapTextToWidth(kBodyRole, std::string(state.help_text), layout.page_width);
    const int help_bottom =
        layout.help_y +
        static_cast<int>(help_lines.size()) * (LineHeight(kBodyRole) + kBodyLineGap);

    ButtonStyle back_style = {};
    back_style.width = layout.page_width;
    back_style.center_label = true;
    layout.back = ButtonBounds(kSideInset, help_bottom + kBackButtonGap, state.back, back_style);
    return layout;
}

}  // namespace

void DrawSettingsDisplayPage(uint8_t* framebuffer,
                             int raw_width,
                             int raw_height,
                             int portrait_width,
                             int portrait_height,
                             const SettingsDisplayPageState& state,
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

    const Layout layout = BuildLayout(portrait_width, state);
    DrawTypographyText(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                       kSideInset, StatusBarHeight() + kTopGap, state.title_text, kTitleRole,
                       design::color::kBlack);

    DrawSelectInput(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                    layout.cleanup.x, layout.cleanup.y, state.cleanup,
                    FieldStyle(layout.cleanup.width));

    const std::vector<std::string> help_lines =
        WrapTextToWidth(kBodyRole, std::string(state.help_text), layout.page_width);
    int cursor_y = layout.help_y;
    for (const std::string& line : help_lines) {
        DrawTypographyText(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                           kSideInset, cursor_y, line, kBodyRole, design::color::kTextPrimary);
        cursor_y += LineHeight(kBodyRole) + kBodyLineGap;
    }

    DrawButton(framebuffer, raw_width, raw_height, portrait_width, portrait_height, layout.back.x,
               layout.back.y, state.back, {.width = layout.back.width, .center_label = true});

    DrawGlobalFooter(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                     footer_state);
}

}  // namespace epaper_ui
