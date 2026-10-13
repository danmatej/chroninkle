#include "epaper_ui/settings_about_page.h"

#include <algorithm>
#include <string>
#include <vector>

#include "render_utils.h"

namespace epaper_ui {
namespace {

constexpr auto kTitleRole = design::TypographyRole::kHeadingH1;
constexpr auto kVersionRole = design::TypographyRole::kLabelLarge;
constexpr auto kBodyRole = design::TypographyRole::kBody;
constexpr auto kUsageHeadingRole = design::TypographyRole::kLabelMediumBlack;
constexpr int kSideInset = design::spacing::k16;
constexpr int kTopGap = design::spacing::k16;
constexpr int kHeadingBottomPadding = design::spacing::k16;
constexpr int kSectionGap = design::spacing::k16;
constexpr int kBodyLineGap = design::spacing::k4;
constexpr int kButtonGap = design::spacing::k16;
constexpr int kBetweenButtonsGap = design::spacing::k12;

struct Layout {
    int page_width = 0;
    int version_y = 0;
    int usage_heading_y = 0;
    int usage_lines_y = 0;
    int body_y = 0;
    UiRect reset_usage = {};
    UiRect back = {};
};

Layout BuildLayout(int portrait_width, int portrait_height, const SettingsAboutPageState& state)
{
    (void)portrait_height;
    const int page_x = kSideInset;
    const int page_width = std::max(0, portrait_width - (2 * kSideInset));
    const int title_y = StatusBarHeight() + kTopGap;
    const int title_bottom = title_y + LineHeight(kTitleRole);

    Layout layout = {};
    layout.page_width = page_width;
    layout.version_y = title_bottom + kHeadingBottomPadding;
    const int version_bottom = layout.version_y + LineHeight(kVersionRole);
    layout.usage_heading_y = version_bottom + kSectionGap;
    layout.usage_lines_y = layout.usage_heading_y + LineHeight(kUsageHeadingRole) + kBodyLineGap;
    const int usage_bottom =
        layout.usage_lines_y +
        static_cast<int>(state.usage_lines.size()) * (LineHeight(kBodyRole) + kBodyLineGap);
    layout.body_y = usage_bottom + kSectionGap;

    const std::vector<std::string> body_lines =
        WrapTextToWidth(kBodyRole, std::string(state.acknowledgment_text), page_width);
    const int body_height =
        static_cast<int>(body_lines.size()) * (LineHeight(kBodyRole) + kBodyLineGap);
    const int body_bottom = layout.body_y + body_height;

    ButtonStyle button_style = {};
    button_style.width = page_width;
    button_style.center_label = true;
    layout.reset_usage =
        ButtonBounds(page_x, body_bottom + kButtonGap, state.reset_usage, button_style);
    layout.back = ButtonBounds(page_x, layout.reset_usage.bottom() + kBetweenButtonsGap,
                               state.back, button_style);

    return layout;
}

}  // namespace

UiRect SettingsAboutPageItemBounds(int portrait_width,
                                   int portrait_height,
                                   const SettingsAboutPageState& state,
                                   SettingsAboutPageItemId item)
{
    const Layout layout = BuildLayout(portrait_width, portrait_height, state);
    switch (item) {
        case SettingsAboutPageItemId::kResetUsage:
            return layout.reset_usage;
        case SettingsAboutPageItemId::kBack:
            return layout.back;
        case SettingsAboutPageItemId::kNone:
        default:
            return {};
    }
}

bool HitTestSettingsAboutPageItem(int portrait_width,
                                  int portrait_height,
                                  const SettingsAboutPageState& state,
                                  int x,
                                  int y,
                                  SettingsAboutPageItemId* item)
{
    if (item != nullptr) {
        *item = SettingsAboutPageItemId::kNone;
    }

    for (const SettingsAboutPageItemId candidate :
         {SettingsAboutPageItemId::kResetUsage, SettingsAboutPageItemId::kBack}) {
        const UiRect bounds =
            SettingsAboutPageItemBounds(portrait_width, portrait_height, state, candidate);
        if (!bounds.IsEmpty() && bounds.Contains(x, y)) {
            if (item != nullptr) {
                *item = candidate;
            }
            return true;
        }
    }
    return false;
}

void DrawSettingsAboutPage(uint8_t* framebuffer,
                           int raw_width,
                           int raw_height,
                           int portrait_width,
                           int portrait_height,
                           const SettingsAboutPageState& state,
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

    const Layout layout = BuildLayout(portrait_width, portrait_height, state);
    const int title_x = kSideInset;
    const int title_y = StatusBarHeight() + kTopGap;
    DrawTypographyText(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                       title_x, title_y, state.title_text, kTitleRole, design::color::kBlack);

    DrawTypographyText(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                       kSideInset, layout.version_y, state.version_text, kVersionRole,
                       design::color::kTextPrimary);

    DrawTypographyText(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                       kSideInset, layout.usage_heading_y, state.usage_heading_text,
                       kUsageHeadingRole, design::color::kBlack);
    int usage_y = layout.usage_lines_y;
    for (const std::string& line : state.usage_lines) {
        DrawTypographyText(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                           kSideInset, usage_y, line, kBodyRole, design::color::kTextPrimary);
        usage_y += LineHeight(kBodyRole) + kBodyLineGap;
    }

    const std::vector<std::string> body_lines =
        WrapTextToWidth(kBodyRole, std::string(state.acknowledgment_text), layout.page_width);
    int cursor_y = layout.body_y;
    const int body_line_height = LineHeight(kBodyRole);
    for (const std::string& line : body_lines) {
        DrawTypographyText(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                           kSideInset, cursor_y, line, kBodyRole, design::color::kTextPrimary);
        cursor_y += body_line_height + kBodyLineGap;
    }

    DrawButton(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
               layout.reset_usage.x, layout.reset_usage.y, state.reset_usage,
               {.width = layout.reset_usage.width, .center_label = true});
    DrawButton(framebuffer, raw_width, raw_height, portrait_width, portrait_height, layout.back.x,
              layout.back.y, state.back, {.width = layout.back.width, .center_label = true});

    DrawGlobalFooter(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                     footer_state);
}

}  // namespace epaper_ui
