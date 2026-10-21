// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Document.h"

#include <filesystem>
#include <memory>

namespace sf {

void Document::setMesh(Mesh m) {
    mesh = std::move(m);
    mesh.topologyDirty = true;
    bvh.build(mesh);
    undo.clear();
    modified = false;
    activeLayer = -1;
    bvhDirty = false;
    gozName.clear();
}

int Document::addBrush(BrushRecipe recipe) {
    recipe.clamp();
    for (size_t i = 0; i < brushes.size(); ++i) {
        if (brushes[i].name == recipe.name) {
            brushes[i] = std::move(recipe);
            return int(i);
        }
    }
    brushes.push_back(std::move(recipe));
    return int(brushes.size()) - 1;
}

std::string Document::assetName() const {
    if (path.empty()) return "Sculpt";
    auto stem = std::filesystem::path(std::u8string(path.begin(), path.end())).stem().u8string();
    return std::string(stem.begin(), stem.end());
}

void Document::replaceMeshUndoable(Mesh m, std::string label) {
    UndoRecord r;
    r.label = std::move(label);
    r.mesh = std::make_unique<Mesh>(std::move(mesh));
    mesh = std::move(m);
    mesh.topologyDirty = true;
    bvh.build(mesh);
    bvhDirty = false;
    undo.push(std::move(r));
    modified = true;
    if (activeLayer >= static_cast<int>(mesh.layers.size())) activeLayer = -1;
}

} // namespace sf
