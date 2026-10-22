// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// LightBox (comma key): a thumbnail strip over the canvas for projects, models,
// brushes, alphas, materials, images, splats, QuickSaves and recent files.
#pragma once

#include "app/UiLayout.h"

namespace sf {

class App;

class LightBox {
public:
    void draw(App& app, const CanvasRect& canvas);
    bool open() const { return open_; }
    void setOpen(bool open) { open_ = open; }
    void toggle() { open_ = !open_; }

private:
    bool open_ = false;
};

} // namespace sf
