// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase 0 stubs for the LightBox / thumbnails / pickers workstream, which replaces this file with
// LightBox.cpp, Thumbnails.cpp and Pickers.cpp.
#include "app/LightBox.h"
#include "app/Pickers.h"
#include "app/Thumbnails.h"

namespace sf {

void LightBox::draw(App&, const CanvasRect&) {}

ImTextureID ThumbnailCache::brush(int) { return ImTextureID(0); }
ImTextureID ThumbnailCache::recipe(const BrushRecipe&) { return ImTextureID(0); }
ImTextureID ThumbnailCache::alpha(const BrushAlpha&) { return ImTextureID(0); }
ImTextureID ThumbnailCache::material(int) { return ImTextureID(0); }
ImTextureID ThumbnailCache::file(const std::string&) { return ImTextureID(0); }
void ThumbnailCache::pump(Renderer&) {}

void openPicker(Picker, ImVec2) {}
void drawPickers(App&) {}
void drawBrushGrid(App&, GridMode) {}

} // namespace sf
