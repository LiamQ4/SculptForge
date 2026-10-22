// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ZBrush-style pop-up pickers: brush grid (B; type letters to choose), stroke,
// alpha and material (M) grids.
#pragma once

#include <imgui.h>

namespace sf {

class App;

enum class Picker { Brush, Stroke, Alpha, Material };
enum class GridMode { Popup, Palette };

void openPicker(Picker picker, ImVec2 anchor);
void drawPickers(App& app);
// The brush grid, reused by the Brush palette.
void drawBrushGrid(App& app, GridMode mode);

} // namespace sf
