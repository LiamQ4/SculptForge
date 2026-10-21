// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Undo.h"

#include <utility>

namespace sf {

size_t UndoRecord::approxBytes() const {
    size_t bytes = indices.size() * sizeof(uint32_t) + positions.size() * sizeof(glm::vec3);
    if (mesh)
        bytes += mesh->vertexCount() * ((2 + (mesh->base.empty() ? 0 : 1 + mesh->layers.size())) * sizeof(glm::vec3) +
                                        8 * sizeof(uint32_t)) +
                 mesh->triangleCount() * (sizeof(glm::uvec3) + sizeof(glm::vec3) + 4 * sizeof(uint32_t));
    return bytes;
}

void UndoStack::push(UndoRecord record) {
    if (record.empty()) return;
    undo_.push_back(std::move(record));
    redo_.clear();
    enforceBudget();
}

// Swaps the record's state with the mesh, so the record now holds the inverse.
void UndoStack::apply(UndoRecord& r, Mesh& mesh, BVH& bvh) {
    if (r.mesh) {
        std::swap(*r.mesh, mesh);
        mesh.topologyDirty = true;
        bvh.build(mesh);
        return;
    }
    auto& target = mesh.layerTarget(r.layer);
    for (size_t i = 0; i < r.indices.size(); ++i) std::swap(target[r.indices[i]], r.positions[i]);
    mesh.evaluate(r.indices);
    mesh.updateNormals(r.indices);
    bvh.refit(mesh, r.indices);
}

bool UndoStack::undo(Mesh& mesh, BVH& bvh) {
    if (undo_.empty()) return false;
    UndoRecord r = std::move(undo_.back());
    undo_.pop_back();
    apply(r, mesh, bvh);
    redo_.push_back(std::move(r));
    return true;
}

bool UndoStack::redo(Mesh& mesh, BVH& bvh) {
    if (redo_.empty()) return false;
    UndoRecord r = std::move(redo_.back());
    redo_.pop_back();
    apply(r, mesh, bvh);
    undo_.push_back(std::move(r));
    return true;
}

void UndoStack::clear() {
    undo_.clear();
    redo_.clear();
}

void UndoStack::enforceBudget() {
    size_t total = 0;
    for (const auto& r : undo_) total += r.approxBytes();
    while (undo_.size() > 1 && total > byteBudget) {
        total -= undo_.front().approxBytes();
        undo_.pop_front();
    }
}

} // namespace sf
