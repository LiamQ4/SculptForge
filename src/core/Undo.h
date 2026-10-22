// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/BVH.h"
#include "core/Mesh.h"

#include <deque>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace sf {

// Sparse old values of one mesh array: values[i] belongs to element indices[i].
template <class T>
struct UndoChannel {
    std::vector<uint32_t> indices;
    std::vector<T> values;

    bool empty() const { return indices.empty(); }
    void add(uint32_t index, const T& value) {
        indices.push_back(index);
        values.push_back(value);
    }
    // Exchanges the stored values with `target`, so the channel then holds the inverse.
    void swapWith(std::vector<T>& target) {
        for (size_t i = 0; i < indices.size(); ++i) std::swap(target[indices[i]], values[i]);
    }
    size_t bytes() const { return indices.size() * (sizeof(uint32_t) + sizeof(T)); }
};

// One undoable step: any mix of sparse channels, or a full mesh snapshot
// (topology/layer-set changes; the snapshot holds every attribute).
//  - positions: the stroke's target array (positions, layer base or a layer's
//    offsets; see Mesh::layerTarget), selected by `layer`
//  - mask / colors: per-vertex mask and polypaint
//  - hidden / polygroups: per-triangle visibility and group ids
// Record old values before changing them (ensureX() the attribute first);
// undo allocates a missing attribute so values can always be restored.
struct UndoRecord {
    std::string label;
    int layer = -1;
    UndoChannel<glm::vec3> positions;
    UndoChannel<float> mask;
    UndoChannel<glm::vec3> colors;
    UndoChannel<uint8_t> hidden;
    UndoChannel<uint32_t> polygroups;
    std::unique_ptr<Mesh> mesh;

    bool empty() const {
        return positions.empty() && mask.empty() && colors.empty() && hidden.empty() && polygroups.empty() && !mesh;
    }
    size_t approxBytes() const;

    // Convenience snapshots of whole attributes (or of `elements` only), e.g.
    // before "mask all", "invert mask", "fill color" or "hide group".
    static UndoRecord captureMask(const Mesh& mesh, std::string label, std::span<const uint32_t> verts = {});
    static UndoRecord captureColors(const Mesh& mesh, std::string label, std::span<const uint32_t> verts = {});
    static UndoRecord captureHidden(const Mesh& mesh, std::string label, std::span<const uint32_t> tris = {});
    static UndoRecord capturePolygroups(const Mesh& mesh, std::string label, std::span<const uint32_t> tris = {});
};

class UndoStack {
public:
    UndoStack() = default;
    UndoStack(UndoStack&&) = default; // move-only (records own mesh snapshots)
    UndoStack& operator=(UndoStack&&) = default;
    UndoStack(const UndoStack&) = delete;
    UndoStack& operator=(const UndoStack&) = delete;
    void push(UndoRecord record);
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    const std::string& undoLabel() const { return undo_.back().label; }
    const std::string& redoLabel() const { return redo_.back().label; }

    // Both rebuild/refit the BVH as needed.
    bool undo(Mesh& mesh, BVH& bvh);
    bool redo(Mesh& mesh, BVH& bvh);
    void clear();
    // Bumped by every push, undo and redo (unsaved-changes tracking).
    uint64_t revision() const { return revision_; }

    size_t byteBudget = size_t(1) << 30; // 1 GiB

private:
    static void apply(UndoRecord& r, Mesh& mesh, BVH& bvh);
    void enforceBudget();

    std::deque<UndoRecord> undo_, redo_;
    uint64_t revision_ = 0;
};

} // namespace sf
