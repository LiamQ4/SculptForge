// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Undo.h"

#include <utility>

namespace sf {

size_t UndoRecord::approxBytes() const {
    size_t bytes = positions.bytes() + mask.bytes() + colors.bytes() + hidden.bytes() + polygroups.bytes();
    if (mesh)
        bytes += mesh->vertexCount() * ((2 + (mesh->base.empty() ? 0 : 1 + mesh->layers.size())) * sizeof(glm::vec3) +
                                        8 * sizeof(uint32_t)) +
                 mesh->triangleCount() * (sizeof(glm::uvec3) + sizeof(glm::vec3) + 4 * sizeof(uint32_t)) +
                 mesh->mask.size() * sizeof(float) + mesh->colors.size() * sizeof(glm::vec3) +
                 mesh->polygroups.size() * sizeof(uint32_t) + mesh->hidden.size() + mesh->uvs.size() * sizeof(glm::vec2);
    return bytes;
}

namespace {

// Records `get(i)` for the listed elements, or for all `count` when `which` is empty.
template <class T, class Get>
void captureChannel(UndoChannel<T>& ch, size_t count, std::span<const uint32_t> which, Get get) {
    if (which.empty()) {
        ch.indices.resize(count);
        ch.values.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            ch.indices[i] = i;
            ch.values[i] = get(i);
        }
        return;
    }
    for (uint32_t i : which) ch.add(i, get(i));
}

} // namespace

UndoRecord UndoRecord::captureMask(const Mesh& mesh, std::string label, std::span<const uint32_t> verts) {
    UndoRecord r;
    r.label = std::move(label);
    captureChannel(r.mask, mesh.vertexCount(), verts, [&](uint32_t v) { return mesh.maskAt(v); });
    return r;
}

UndoRecord UndoRecord::captureColors(const Mesh& mesh, std::string label, std::span<const uint32_t> verts) {
    UndoRecord r;
    r.label = std::move(label);
    captureChannel(r.colors, mesh.vertexCount(), verts, [&](uint32_t v) { return mesh.colorAt(v); });
    return r;
}

UndoRecord UndoRecord::captureHidden(const Mesh& mesh, std::string label, std::span<const uint32_t> tris) {
    UndoRecord r;
    r.label = std::move(label);
    captureChannel(r.hidden, mesh.triangleCount(), tris, [&](uint32_t t) { return uint8_t(mesh.triangleHidden(t)); });
    return r;
}

UndoRecord UndoRecord::capturePolygroups(const Mesh& mesh, std::string label, std::span<const uint32_t> tris) {
    UndoRecord r;
    r.label = std::move(label);
    captureChannel(r.polygroups, mesh.triangleCount(), tris, [&](uint32_t t) { return mesh.polygroupAt(t); });
    return r;
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
    if (!r.positions.empty()) {
        r.positions.swapWith(mesh.layerTarget(r.layer));
        mesh.evaluate(r.positions.indices);
        mesh.updateNormals(r.positions.indices);
        bvh.refit(mesh, r.positions.indices);
    }
    if (!r.mask.empty()) {
        r.mask.swapWith(mesh.ensureMask());
        for (uint32_t v : r.mask.indices) mesh.markDirty(v);
    }
    if (!r.colors.empty()) {
        r.colors.swapWith(mesh.ensureColors());
        for (uint32_t v : r.colors.indices) mesh.markDirty(v);
    }
    // Picking reads `hidden` directly, so the BVH needs no update.
    if (!r.hidden.empty()) {
        r.hidden.swapWith(mesh.ensureHidden());
        mesh.facesDirty = true;
    }
    if (!r.polygroups.empty()) {
        r.polygroups.swapWith(mesh.ensurePolygroups());
        mesh.facesDirty = true;
    }
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
