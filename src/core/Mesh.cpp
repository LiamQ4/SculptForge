// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Mesh.h"

#include <algorithm>
#include <unordered_map>

namespace sf {

static glm::vec3 faceNormal(const std::vector<glm::vec3>& p, const glm::uvec3& t) {
    // Unnormalised: magnitude is twice the triangle area, giving area weighting.
    return glm::cross(p[t.y] - p[t.x], p[t.z] - p[t.x]);
}

void Mesh::rebuildTopology() {
    const size_t nv = positions.size();

    vtOffsets_.assign(nv + 1, 0);
    for (const auto& t : triangles)
        for (int c = 0; c < 3; ++c) ++vtOffsets_[t[c] + 1];
    for (size_t i = 0; i < nv; ++i) vtOffsets_[i + 1] += vtOffsets_[i];
    vtList_.resize(vtOffsets_[nv]);
    {
        std::vector<uint32_t> cursor(vtOffsets_.begin(), vtOffsets_.end() - 1);
        for (uint32_t ti = 0; ti < triangles.size(); ++ti)
            for (int c = 0; c < 3; ++c) vtList_[cursor[triangles[ti][c]]++] = ti;
    }

    adjOffsets_.assign(nv + 1, 0);
    adjList_.clear();
    adjList_.reserve(vtList_.size() * 2);
    std::vector<uint32_t> ring;
    for (uint32_t v = 0; v < nv; ++v) {
        ring.clear();
        for (uint32_t ti : vertexTriangles(v))
            for (int c = 0; c < 3; ++c)
                if (triangles[ti][c] != v) ring.push_back(triangles[ti][c]);
        std::sort(ring.begin(), ring.end());
        ring.erase(std::unique(ring.begin(), ring.end()), ring.end());
        adjList_.insert(adjList_.end(), ring.begin(), ring.end());
        adjOffsets_[v + 1] = static_cast<uint32_t>(adjList_.size());
    }

    faceNormals_.resize(triangles.size());
    triStamp_.assign(triangles.size(), 0);
    vertStamp_.assign(nv, 0);
    stamp_ = 0;
    normals.resize(nv);
    topologyDirty = true;
}

uint32_t Mesh::nextStamp() {
    if (++stamp_ == 0) {
        std::fill(triStamp_.begin(), triStamp_.end(), 0);
        std::fill(vertStamp_.begin(), vertStamp_.end(), 0);
        stamp_ = 1;
    }
    return stamp_;
}

void Mesh::recomputeAllNormals() {
    faceNormals_.resize(triangles.size());
    for (size_t i = 0; i < triangles.size(); ++i) faceNormals_[i] = faceNormal(positions, triangles[i]);
    normals.assign(positions.size(), glm::vec3(0.0f));
    for (size_t i = 0; i < triangles.size(); ++i)
        for (int c = 0; c < 3; ++c) normals[triangles[i][c]] += faceNormals_[i];
    for (auto& n : normals) {
        float len = glm::length(n);
        n = len > 1e-20f ? n / len : glm::vec3(0, 1, 0);
    }
    if (!positions.empty()) {
        dirtyBegin = 0;
        dirtyEnd = static_cast<uint32_t>(positions.size());
    }
}

void Mesh::updateNormals(std::span<const uint32_t> verts) {
    const uint32_t s = nextStamp();

    scratchTris_.clear();
    for (uint32_t v : verts) {
        markDirty(v);
        for (uint32_t ti : vertexTriangles(v)) {
            if (triStamp_[ti] != s) {
                triStamp_[ti] = s;
                scratchTris_.push_back(ti);
            }
        }
    }
    for (uint32_t ti : scratchTris_) faceNormals_[ti] = faceNormal(positions, triangles[ti]);

    scratchVerts_.clear();
    for (uint32_t ti : scratchTris_) {
        for (int c = 0; c < 3; ++c) {
            uint32_t v = triangles[ti][c];
            if (vertStamp_[v] != s) {
                vertStamp_[v] = s;
                scratchVerts_.push_back(v);
            }
        }
    }
    for (uint32_t v : scratchVerts_) {
        glm::vec3 n(0.0f);
        for (uint32_t ti : vertexTriangles(v)) n += faceNormals_[ti];
        float len = glm::length(n);
        if (len > 1e-20f) normals[v] = n / len;
        markDirty(v);
    }
}

int Mesh::addLayer(std::string name) {
    if (layers.empty()) base = positions;
    layers.push_back({std::move(name), std::vector<glm::vec3>(positions.size(), glm::vec3(0.0f)), 1.0f});
    return static_cast<int>(layers.size()) - 1;
}

void Mesh::removeLayer(int index) {
    if (index < 0 || index >= static_cast<int>(layers.size())) return;
    layers.erase(layers.begin() + index);
    if (layers.empty()) {
        positions = base;
        base.clear();
    } else {
        evaluateAll();
    }
    recomputeAllNormals();
}

void Mesh::setLayerWeight(int index, float weight) {
    if (index < 0 || index >= static_cast<int>(layers.size())) return;
    layers[index].weight = weight;
    evaluateAll();
    recomputeAllNormals();
}

std::vector<glm::vec3>& Mesh::layerTarget(int layer) {
    if (layers.empty()) return positions;
    if (layer < 0 || layer >= static_cast<int>(layers.size())) return base;
    return layers[layer].delta;
}

float Mesh::layerTargetWeight(int layer) const {
    if (layers.empty() || layer < 0 || layer >= static_cast<int>(layers.size())) return 1.0f;
    return layers[layer].weight;
}

void Mesh::evaluate(std::span<const uint32_t> verts) {
    if (layers.empty()) return;
    for (uint32_t v : verts) {
        glm::vec3 p = base[v];
        for (const auto& l : layers) p += l.weight * l.delta[v];
        positions[v] = p;
    }
}

void Mesh::evaluateAll() {
    if (layers.empty()) return;
    for (size_t v = 0; v < positions.size(); ++v) {
        glm::vec3 p = base[v];
        for (const auto& l : layers) p += l.weight * l.delta[v];
        positions[v] = p;
    }
}

AABB Mesh::bounds() const {
    AABB b;
    for (const auto& p : positions) b.expand(p);
    return b;
}

static void laplacianStep(Mesh& m, float lambda, std::vector<glm::vec3>& next) {
    next.resize(m.vertexCount());
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        auto ring = m.neighbors(v);
        if (ring.empty()) {
            next[v] = m.positions[v];
            continue;
        }
        glm::vec3 avg(0.0f);
        for (uint32_t n : ring) avg += m.positions[n];
        avg /= float(ring.size());
        next[v] = glm::mix(m.positions[v], avg, lambda);
    }
    m.positions.swap(next);
}

void taubinSmooth(Mesh& m, int iterations) {
    std::vector<glm::vec3> next;
    for (int it = 0; it < iterations; ++it) {
        laplacianStep(m, 0.5f, next);
        laplacianStep(m, -0.53f, next);
    }
}

Mesh weldImported(const Mesh& m, const std::vector<SculptLayer>& layers) {
    AABB box = m.bounds();
    const float tol = box.valid() ? std::max(glm::length(box.extent()) * 1e-6f, 1e-12f) : 1.0f;
    struct KeyHash {
        size_t operator()(const glm::ivec3& k) const {
            return size_t(k.x) * 73856093u ^ size_t(k.y) * 19349663u ^ size_t(k.z) * 83492791u;
        }
    };
    std::unordered_map<glm::ivec3, uint32_t, KeyHash> unique;
    unique.reserve(m.positions.size());
    std::vector<uint32_t> remap(m.positions.size());
    std::vector<uint32_t> kept;
    for (uint32_t v = 0; v < m.positions.size(); ++v) {
        glm::ivec3 key(glm::round(m.positions[v] / tol));
        auto [it, inserted] = unique.try_emplace(key, uint32_t(kept.size()));
        if (inserted) kept.push_back(v);
        remap[v] = it->second;
    }
    Mesh welded;
    for (uint32_t v : kept) welded.positions.push_back(m.positions[v]);
    for (const auto& t : m.triangles) {
        glm::uvec3 nt(remap[t.x], remap[t.y], remap[t.z]);
        if (nt.x != nt.y && nt.y != nt.z && nt.x != nt.z) welded.triangles.push_back(nt);
    }
    if (!layers.empty()) {
        welded.base = welded.positions;
        for (const auto& src : layers) {
            SculptLayer layer{src.name, {}, src.weight};
            layer.delta.reserve(kept.size());
            for (uint32_t v : kept) layer.delta.push_back(v < src.delta.size() ? src.delta[v] : glm::vec3(0.0f));
            welded.layers.push_back(std::move(layer));
        }
        welded.evaluateAll();
    }
    welded.rebuildTopology();
    welded.recomputeAllNormals();
    return welded;
}

} // namespace sf
