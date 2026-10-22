// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Colours and style of the ZBrush-like shell (docs/ui-design.md §10): dense,
// grey, with the SculptForge orange as the accent.
#pragma once

#include <imgui.h>

namespace sf::theme {

constexpr ImVec4 rgb(unsigned hex, float a = 1.0f) {
    return ImVec4(float((hex >> 16) & 0xff) / 255.0f, float((hex >> 8) & 0xff) / 255.0f, float(hex & 0xff) / 255.0f, a);
}

constexpr ImVec4 shellBg = rgb(0x1e1e20);   // shelves, trays, title strip, around the canvas
constexpr ImVec4 panelBg = rgb(0x252528);   // palette drop-downs, tray contents, pop-ups
constexpr ImVec4 header = rgb(0x2e2e32);    // palette and sub-palette headers
constexpr ImVec4 headerHover = rgb(0x3a3a3f);
constexpr ImVec4 frame = rgb(0x2b2b2f);     // slider and input frames
constexpr ImVec4 frameHover = rgb(0x36363b);
constexpr ImVec4 button = rgb(0x38383c);
constexpr ImVec4 buttonHover = rgb(0x47474c);
constexpr ImVec4 buttonPressed = rgb(0xe8812a);
constexpr ImVec4 accent = rgb(0xf0882e);    // toggled-on buttons, grabs, checks, selection, active tab
constexpr ImVec4 accentHover = rgb(0xff9a45);
constexpr ImVec4 text = rgb(0xd8d8d8);
constexpr ImVec4 textDim = rgb(0x8c8c90);
constexpr ImVec4 textOnAccent = rgb(0xffffff);
constexpr ImVec4 border = rgb(0x121214);    // 1 px between regions
constexpr ImVec4 danger = rgb(0xe5604a);    // errors
constexpr ImVec4 canvasBorder = rgb(0x0e0e10);
constexpr ImVec4 tile = rgb(0x2a2a2e);      // thumbnail placeholders

inline ImU32 u32(const ImVec4& c, float alpha = 1.0f) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alpha)); }

// Sets the colours, rounding and spacing (unscaled; call ImGuiStyle::ScaleAllSizes afterwards).
void applyStyle();

} // namespace sf::theme
