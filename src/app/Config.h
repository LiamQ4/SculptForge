// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// UI preferences and the ZBrush-style configuration workflow: Store Config
// (Ctrl+Shift+I) writes <prefs>/ui.json with the layout, shortcut overrides and
// these preferences; nothing is saved automatically. Restore Standard UI goes
// back to the built-in layout, Restore Custom UI to the stored one, and Save UI /
// Load UI (Ctrl+L) share a layout as a file.
#pragma once

#include "core/Json.h"

#include <string>

namespace sf {

class App;

struct UiPrefs {
    bool popupInfo = true;      // Ctrl+hover shows an item's description (ZBrush "Popup Info")
    float helpOpacity = 0.97f;  // Ctrl-help box opacity (ZBrush "Quick Info Opacity")
    float tooltipDelay = 0.5f;  // plain hover tooltips on icon-only buttons and clipped labels
    bool oneSubpalette = true;  // opening a sub-palette closes the others (Shift+click opens more)
    bool autoCollapseLeft = true, autoCollapseRight = false; // trays collapse palettes that would overflow
    float uiScale = 0.0f;       // 0 = follow the display; otherwise applied at the next start
    bool lightboxAtStartup = true;
    std::string libraryFolder;  // "" = Documents/SculptForge
    int cameraNotch = 0;        // keep the UI out of a laptop's camera notch: 0 automatic, 1 always, 2 never
};

Json toJson(const UiPrefs& prefs);
void fromJson(const Json& j, UiPrefs& prefs);

namespace config {
// Preferences palette sub-palettes (the shell's Preferences palette calls these).
void drawConfigSection(App& app);    // Enable Customize, Store / Restore / Save / Load UI
void drawInterfaceSection(App& app); // UI scale, Popup Info, tooltips, sub-palettes, left-handed layout
void drawHotkeysSection(App& app);   // every action's shortcut, click to reassign
void drawLightBoxSection(App& app);  // show at startup, rows, placement, library folder
} // namespace config

} // namespace sf
