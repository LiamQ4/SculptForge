// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/BVH.h"
#include "core/Mesh.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace sf {

// One undoable step. Either a sparse set of per-vertex values of the stroke's
// target array (positions, layer base or a layer's offsets; see
// Mesh::layerTarget) or a full mesh snapshot (topology/layer-set changes).
struct UndoRecord {
    std::string label;
    int layer = -1;
    std::vector<uint32_t> indices;
    std::vector<glm::vec3> positions;
    std::unique_ptr<Mesh> mesh;

    bool empty() const { return indices.empty() && !mesh; }
    size_t approxBytes() const;
};

class UndoStack {
public:
    void push(UndoRecord record);
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    const std::string& undoLabel() const { return undo_.back().label; }
    const std::string& redoLabel() const { return redo_.back().label; }

    // Both rebuild/refit the BVH as needed.
    bool undo(Mesh& mesh, BVH& bvh);
    bool redo(Mesh& mesh, BVH& bvh);
    void clear();

    size_t byteBudget = size_t(1) << 30; // 1 GiB

private:
    static void apply(UndoRecord& r, Mesh& mesh, BVH& bvh);
    void enforceBudget();

    std::deque<UndoRecord> undo_, redo_;
};

} // namespace sf
