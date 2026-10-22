// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Thumbnails for brushes, alphas, materials and files, rendered offscreen and
// cached in memory and on disk. Calls never block: they return 0 until the
// image is ready, and pump() does a little work each frame.
#pragma once

#include "core/BrushAlpha.h"
#include "core/BrushRecipe.h"

#include <imgui.h>

#include <string>

namespace sf {

class Renderer;

class ThumbnailCache {
public:
    ImTextureID brush(int brushType);
    ImTextureID recipe(const BrushRecipe& recipe);
    ImTextureID alpha(const BrushAlpha& alpha);
    ImTextureID material(int matcap);
    ImTextureID file(const std::string& path);
    void pump(Renderer& renderer);
};

} // namespace sf
