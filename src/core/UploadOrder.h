// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The order a renderer keeps a mesh's vertices in on the GPU, and which parts of it a change needs re-sent.
//
// Subdivided meshes number their vertices level by level (the cage's, then each level's edge and face
// points), so the vertices under one brush dab are scattered over the whole index range, and re-sending the
// changed index ranges meant re-sending ten times what changed. Their triangles though stay grouped by the
// face they came from, at every level, so numbering vertices by first use in the triangle list keeps the
// vertices of any region close together. The renderer stores vertex arrays in that order (and remaps the
// indices); dirtyRuns() turns the vertices the mesh marked dirty (Mesh::markDirty) into a few short GPU index
// ranges to gather and upload.
#pragma once

#include "core/Mesh.h"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace sf {

class UploadOrder {
public:
    static constexpr uint32_t kBlockShift = 9, kBlock = 1u << kBlockShift; // GPU upload granularity: 512 vertices

    // Numbers the vertices by first use in the triangle list (unused vertices last).
    void build(const Mesh& mesh);
    size_t size() const { return fromGpu_.size(); }
    uint32_t toGpu(uint32_t v) const { return toGpu_[v]; }
    // fromGpu()[i] = the mesh vertex at GPU index i.
    std::span<const uint32_t> fromGpu() const { return fromGpu_; }

    // Sorted, disjoint GPU index ranges [begin, end) holding every vertex marked dirty, in whole blocks (runs
    // one block apart are joined). Everything, as one range, when most of the mesh is dirty.
    void dirtyRuns(const Mesh& mesh, std::vector<std::pair<uint32_t, uint32_t>>& out);

    size_t memoryBytes() const;

private:
    std::vector<uint32_t> toGpu_, fromGpu_;
    std::vector<uint8_t> marked_;  // per GPU block, while dirtyRuns collects (all zero between calls)
    std::vector<uint32_t> blocks_; // the marked blocks
};

} // namespace sf
