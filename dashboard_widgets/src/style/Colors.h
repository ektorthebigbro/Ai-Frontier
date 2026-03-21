#ifndef COLORS_H
#define COLORS_H
#include <QColor>
namespace Theme {
    // Backgrounds
    constexpr const char* BgDeep      = "#080c14";
    constexpr const char* BgPrimary   = "#0d1117";
    constexpr const char* BgCard      = "#161b22";
    constexpr const char* BgCardHover = "#1c2333";
    constexpr const char* BgSidebar   = "#0a1018";
    constexpr const char* BgInput     = "#0d1520";
    constexpr const char* BgChrome    = "#080e1a";

    // Borders
    constexpr const char* Border      = "#1e2d3d";
    constexpr const char* BorderLight = "#2a3a4e";
    constexpr const char* BorderFocus = "#3b82f6";

    // Text
    constexpr const char* TextPrimary = "#e6edf3";
    constexpr const char* TextSecondary = "#8b949e";
    constexpr const char* TextMuted   = "#5a6a7e";
    constexpr const char* TextDim     = "#3d4f63";

    // Accent colors (matching reference)
    constexpr const char* AccentGreen  = "#3fb950";
    constexpr const char* AccentBlue   = "#58a6ff";
    constexpr const char* AccentCyan   = "#39d2c0";
    constexpr const char* AccentPurple = "#bc8cff";
    constexpr const char* AccentPink   = "#f778ba";
    constexpr const char* AccentOrange = "#f0883e";
    constexpr const char* AccentRed    = "#f85149";
    constexpr const char* AccentYellow = "#e3b341";

    // Hero metric badge colors (from reference screenshots)
    constexpr const char* BadgeGreen   = "#22c55e";
    constexpr const char* BadgeBlue    = "#3b82f6";
    constexpr const char* BadgeYellow  = "#eab308";
    constexpr const char* BadgeRed     = "#ef4444";

    // Gradients (start, end) for progress bar
    constexpr const char* GradStart    = "#3b82f6";
    constexpr const char* GradEnd      = "#ec4899";

    // Chart colors
    constexpr const char* ChartPink    = "#ec4899";
    constexpr const char* ChartPurple  = "#a855f7";
    constexpr const char* ChartCyan    = "#06b6d4";
    constexpr const char* ChartGreen   = "#22c55e";

    // Status
    constexpr const char* StatusOk     = "#22c55e";
    constexpr const char* StatusWarn   = "#f59e0b";
    constexpr const char* StatusError  = "#ef4444";
    constexpr const char* StatusInfo   = "#3b82f6";
}
#endif
