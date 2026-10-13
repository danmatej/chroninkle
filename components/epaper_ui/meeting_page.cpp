#include "epaper_ui/meeting_page.h"

#include <algorithm>
#include <string>
#include <vector>

#include "render_utils.h"

namespace epaper_ui {
namespace {

constexpr auto kTitleRole = design::TypographyRole::kHeadingH1;
constexpr auto kStatusRole = design::TypographyRole::kLabelLargeBold;
constexpr auto kElapsedRole = design::TypographyRole::kDisplay;
constexpr auto kInfoRole = design::TypographyRole::kLabelMedium;
constexpr auto kBodyRole = design::TypographyRole::kBody;
constexpr auto kHintRole = design::TypographyRole::kLabelMedium;
constexpr int kSideInset = design::spacing::k16;
constexpr int kTopGap = design::spacing::k24;
constexpr int kSectionGap = design::spacing::k16;
constexpr int kLineGap = design::spacing::k4;
constexpr int kDotSize = 16;
constexpr int kDividerThickness = 2;

}  // namespace

void DrawMeetingPage(uint8_t* framebuffer,
                     int raw_width,
                     int raw_height,
                     int portrait_width,
                     int portrait_height,
                     const MeetingPageState& state,
                     const StatusBarState& status_bar_state)
{
    if (framebuffer == nullptr) {
        return;
    }

    FillPortraitRect(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                     {0, 0, portrait_width, portrait_height}, design::color::kWhite);
    DrawStatusBar(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                  status_bar_state);

    const int page_width = std::max(0, portrait_width - (2 * kSideInset));
    int y = StatusBarHeight() + kTopGap;
    DrawTypographyText(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                       kSideInset, y, state.title_text, kTitleRole, design::color::kBlack);
    y += LineHeight(kTitleRole) + kSectionGap;

    int status_x = kSideInset;
    const int status_height = LineHeight(kStatusRole);
    if (state.recording) {
        FillRoundedPortraitRect(framebuffer, raw_width, raw_height, portrait_width,
                                portrait_height,
                                {kSideInset, y + CenterOffset(status_height, kDotSize), kDotSize,
                                 kDotSize},
                                kDotSize / 2, design::color::kBlack);
        status_x += kDotSize + design::spacing::k8;
    }
    DrawTypographyText(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                       status_x, y,
                       FitLabelText(kStatusRole, state.status_text,
                                    page_width - (status_x - kSideInset)),
                       kStatusRole, design::color::kBlack);
    y += status_height + kSectionGap;

    if (!state.elapsed_text.empty()) {
        DrawTypographyText(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                           kSideInset, y, state.elapsed_text, kElapsedRole,
                           design::color::kBlack);
        y += LineHeight(kElapsedRole) + kSectionGap;
    }

    for (const std::string* line : {&state.progress_text, &state.detail_text}) {
        if (line->empty()) {
            continue;
        }
        for (const std::string& wrapped : WrapTextToWidth(kInfoRole, *line, page_width)) {
            DrawTypographyText(framebuffer, raw_width, raw_height, portrait_width,
                               portrait_height, kSideInset, y, wrapped, kInfoRole,
                               design::color::kTextPrimary);
            y += LineHeight(kInfoRole) + kLineGap;
        }
    }
    y += kSectionGap - kLineGap;

    // The hint is pinned to the bottom; the transcript tail fills the space between.
    const int hint_height = LineHeight(kHintRole);
    const int hint_y = portrait_height - design::spacing::k24 - hint_height;
    if (!state.hint_text.empty()) {
        DrawTypographyText(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                           kSideInset, hint_y,
                           FitLabelText(kHintRole, state.hint_text, page_width), kHintRole,
                           design::color::kTextSecondary);
    }

    if (state.transcript_tail.empty()) {
        return;
    }
    FillPortraitRect(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                     {kSideInset, y, page_width, kDividerThickness}, design::color::kGrayLight);
    y += kDividerThickness + kSectionGap;

    const int body_line = LineHeight(kBodyRole) + kLineGap;
    const int bottom = hint_y - kSectionGap;
    const int max_lines = body_line > 0 ? std::max(0, (bottom - y) / body_line) : 0;
    const std::vector<std::string> lines =
        WrapTextToWidth(kBodyRole, state.transcript_tail, page_width);
    const size_t first =
        lines.size() > static_cast<size_t>(max_lines) ? lines.size() - max_lines : 0;
    for (size_t i = first; i < lines.size(); ++i) {
        DrawTypographyText(framebuffer, raw_width, raw_height, portrait_width, portrait_height,
                           kSideInset, y, lines[i], kBodyRole, design::color::kTextPrimary);
        y += body_line;
    }
}

}  // namespace epaper_ui
