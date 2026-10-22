// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Widgets every panel uses, so Ctrl-hover help and UI customization come for free.
#pragma once

#include <imgui.h>

#include <string>

namespace sf {
class App;
}

namespace sf::ui {

enum class Look { Label, Icon, IconLabel, Tile };

// Runs the action on click; toggles draw with the accent when checked(); shows Ctrl-hover help;
// is a Ctrl+Alt drag source in customize mode. Ctrl+Alt+click (outside customize mode) assigns
// a shortcut, as in ZBrush. Returns true when the action ran.
bool ActionButton(App& app, const char* actionId, ImVec2 size = {}, Look look = Look::Label);
// Ctrl-hover help for the last item.
void HelpText(const char* title, const char* shortcut, const char* description, const char* path = nullptr);
void HelpAction(App& app, const char* actionId);
// ZBrush-style slider with its label drawn inside the frame ("Draw Size 60") and a thin accent grab.
bool SliderInside(const char* id, float* v, float lo, float hi, const char* format, ImGuiSliderFlags flags = 0);

// Customize mode: shelves this payload ("SF_UIITEM") is dragged from and dropped on.
enum Shelf { ShelfNone = -1, ShelfTop, ShelfLeft, ShelfRight, ShelfBottom };
struct UiItemPayload {
    char id[96] = "";
    int fromShelf = ShelfNone;
    int fromIndex = -1;
};
inline constexpr const char* kUiItemPayload = "SF_UIITEM";
// Where the next ActionButton / UiItemDragSource sits (shelves call this before drawing an item).
void SetNextItemOrigin(int shelf, int index);
// Makes the last item a Ctrl+Alt drag source in customize mode (for widgets that aren't ActionButtons).
bool UiItemDragSource(App& app, const char* itemId, const char* label);

// Keyboard keycaps for a chord ("Ctrl+Shift+I", "F3 / Ctrl+Shift+P"), drawn inline as an item.
void Keycaps(const std::string& chord);
float KeycapsWidth(const std::string& chord);
float KeycapsAt(ImDrawList* drawList, ImVec2 pos, const std::string& chord); // no layout; returns the width
// A plain tooltip after the hover delay (icon-only buttons, clipped labels); suppressed while Ctrl is held.
void DelayedTooltip(const char* text);
bool CtrlHeld();
bool AltHeld();

// Draws this frame's Ctrl-help box; call once, after every other window.
void DrawHelp(App& app);
// What the Ctrl-help box showed in the last frame ("" if none), for UI tests.
const std::string& HelpShown();

// Theme tokens shared by the help, palette and console (ui-spec section 10).
namespace color {
inline constexpr ImU32 accent = IM_COL32(0xf0, 0x88, 0x2e, 0xff);
inline constexpr ImU32 accentHover = IM_COL32(0xff, 0x9a, 0x45, 0xff);
inline constexpr ImU32 text = IM_COL32(0xd8, 0xd8, 0xd8, 0xff);
inline constexpr ImU32 textDim = IM_COL32(0x8c, 0x8c, 0x90, 0xff);
inline constexpr ImU32 danger = IM_COL32(0xe5, 0x60, 0x4a, 0xff);
inline constexpr ImU32 panelBg = IM_COL32(0x25, 0x25, 0x28, 0xff);
inline constexpr ImU32 shellBg = IM_COL32(0x1e, 0x1e, 0x20, 0xff);
inline constexpr ImU32 header = IM_COL32(0x2e, 0x2e, 0x32, 0xff);
inline constexpr ImU32 border = IM_COL32(0x12, 0x12, 0x14, 0xff);
} // namespace color

} // namespace sf::ui
