// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/UploadOrder.h"

#include <algorithm>
#include <bit>

namespace sf {

void UploadOrder::build(const Mesh& mesh) {
    const size_t nv = mesh.vertexCount();
    constexpr uint32_t kUnset = ~0u;
    toGpu_.assign(nv, kUnset);
    fromGpu_.clear();
    fromGpu_.reserve(nv);
    for (const glm::uvec3& t : mesh.triangles)
        for (int c = 0; c < 3; ++c)
            if (t[c] < nv && toGpu_[t[c]] == kUnset) {
                toGpu_[t[c]] = uint32_t(fromGpu_.size());
                fromGpu_.push_back(t[c]);
            }
    for (uint32_t v = 0; v < nv; ++v)
        if (toGpu_[v] == kUnset) {
            toGpu_[v] = uint32_t(fromGpu_.size());
            fromGpu_.push_back(v);
        }
    marked_.assign((nv + kBlock - 1) / kBlock, 0);
    blocks_.clear();
}

void UploadOrder::dirtyRuns(const Mesh& mesh, std::vector<std::pair<uint32_t, uint32_t>>& out) {
    out.clear();
    const uint32_t nv = uint32_t(std::min(mesh.vertexCount(), fromGpu_.size()));
    const uint32_t end = std::min(mesh.dirtyEnd, nv);
    if (mesh.dirtyBegin >= end) return;
    const size_t firstBlock = mesh.dirtyBegin >> Mesh::kDirtyBlockShift;
    const size_t lastBlock = std::min(mesh.dirtyBlocks.size(), size_t((end - 1) >> Mesh::kDirtyBlockShift) + 1);
    constexpr size_t kWordsPerBlock = Mesh::kDirtyBlock / 64;
    size_t dirty = 0;
    for (size_t b = firstBlock; b < lastBlock; ++b)
        if (mesh.dirtyBlocks[b])
            for (size_t w = b * kWordsPerBlock; w < (b + 1) * kWordsPerBlock && w < mesh.dirtyBits.size(); ++w)
                dirty += size_t(std::popcount(mesh.dirtyBits[w]));
    if (dirty * 4 > nv || mesh.dirtyBits.size() * 64 < nv) {
        out.emplace_back(0, nv); // a good part of the mesh (or no per-vertex marks): everything, in one go
        return;
    }
    // The GPU blocks of every marked vertex.
    for (size_t b = firstBlock; b < lastBlock; ++b) {
        if (!mesh.dirtyBlocks[b]) continue;
        for (size_t w = b * kWordsPerBlock; w < (b + 1) * kWordsPerBlock && w < mesh.dirtyBits.size(); ++w) {
            for (uint64_t bits = mesh.dirtyBits[w]; bits; bits &= bits - 1) {
                const size_t v = w * 64 + size_t(std::countr_zero(bits));
                if (v >= nv) break;
                const uint32_t g = toGpu_[v] >> kBlockShift;
                if (!marked_[g]) {
                    marked_[g] = 1;
                    blocks_.push_back(g);
                }
            }
        }
    }
    std::sort(blocks_.begin(), blocks_.end());
    for (uint32_t g : blocks_) {
        marked_[g] = 0;
        const uint32_t lo = g << kBlockShift, hi = std::min(nv, (g + 1) << kBlockShift);
        if (!out.empty() && lo - out.back().second <= kBlock) out.back().second = hi;
        else out.emplace_back(lo, hi);
    }
    blocks_.clear();
}

size_t UploadOrder::memoryBytes() const {
    return (toGpu_.capacity() + fromGpu_.capacity() + blocks_.capacity()) * sizeof(uint32_t) + marked_.capacity();
}

} // namespace sf
