// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/BVH.h"

#include <algorithm>
#include <cmath>

namespace sf {

namespace {

constexpr uint32_t kLeafSize = 16;

bool rayBox(const AABB& b, const glm::vec3& o, const glm::vec3& invDir, float tMax) {
    glm::vec3 t0 = (b.min - o) * invDir;
    glm::vec3 t1 = (b.max - o) * invDir;
    glm::vec3 tmin = glm::min(t0, t1), tmax = glm::max(t0, t1);
    float enter = std::max(std::max(tmin.x, tmin.y), std::max(tmin.z, 0.0f));
    float exit = std::min(std::min(tmax.x, tmax.y), std::min(tmax.z, tMax));
    return enter <= exit;
}

bool sphereBox(const AABB& b, const glm::vec3& c, float r2) {
    glm::vec3 d = glm::max(glm::max(b.min - c, c - b.max), glm::vec3(0.0f));
    return glm::dot(d, d) <= r2;
}

// Two-sided Moller-Trumbore.
bool rayTriangle(const glm::vec3& o, const glm::vec3& d, const glm::vec3& a, const glm::vec3& b,
                 const glm::vec3& c, float& t) {
    glm::vec3 e1 = b - a, e2 = c - a;
    glm::vec3 p = glm::cross(d, e2);
    float det = glm::dot(e1, p);
    if (std::abs(det) < 1e-12f) return false;
    float inv = 1.0f / det;
    glm::vec3 s = o - a;
    float u = glm::dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    glm::vec3 q = glm::cross(s, e1);
    float v = glm::dot(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    t = glm::dot(e2, q) * inv;
    return t > 0.0f;
}

} // namespace

void BVH::build(const Mesh& mesh) {
    const size_t nt = mesh.triangleCount();
    nodes_.clear();
    triIndices_.resize(nt);
    triLeaf_.assign(nt, 0);
    vertStamp_.assign(mesh.vertexCount(), 0);
    vertStampValue_ = 0;
    if (nt == 0) return;

    std::vector<glm::vec3> centroids(nt);
    for (uint32_t i = 0; i < nt; ++i) {
        const auto& t = mesh.triangles[i];
        centroids[i] = (mesh.positions[t.x] + mesh.positions[t.y] + mesh.positions[t.z]) / 3.0f;
        triIndices_[i] = i;
    }

    nodes_.reserve(2 * (nt / kLeafSize + 1));
    nodes_.push_back(Node{AABB{}, 0, static_cast<uint32_t>(nt), 0});

    struct Task { uint32_t node; };
    std::vector<Task> tasks{{0}};
    while (!tasks.empty()) {
        uint32_t ni = tasks.back().node;
        tasks.pop_back();
        uint32_t first = nodes_[ni].first, count = nodes_[ni].count;

        AABB cb;
        for (uint32_t i = first; i < first + count; ++i) cb.expand(centroids[triIndices_[i]]);

        glm::vec3 ext = cb.extent();
        int axis = ext.x > ext.y ? (ext.x > ext.z ? 0 : 2) : (ext.y > ext.z ? 1 : 2);
        if (count <= kLeafSize || ext[axis] <= 0.0f) {
            recomputeLeaf(mesh, nodes_[ni]);
            continue;
        }

        uint32_t mid = first + count / 2;
        std::nth_element(triIndices_.begin() + first, triIndices_.begin() + mid, triIndices_.begin() + first + count,
                         [&](uint32_t a, uint32_t b) { return centroids[a][axis] < centroids[b][axis]; });

        uint32_t left = static_cast<uint32_t>(nodes_.size());
        nodes_.push_back(Node{AABB{}, first, mid - first, ni});
        nodes_.push_back(Node{AABB{}, mid, first + count - mid, ni});
        nodes_[ni].first = left;
        nodes_[ni].count = 0;
        tasks.push_back({left});
        tasks.push_back({left + 1});
    }

    // Inner node bounds, bottom-up (children always have larger indices).
    for (size_t i = nodes_.size(); i-- > 0;) {
        Node& n = nodes_[i];
        if (n.count == 0) {
            n.box = nodes_[n.first].box;
            n.box.expand(nodes_[n.first + 1].box);
        }
    }
    leafStamp_.assign(nodes_.size(), 0);
    leafStampValue_ = 0;
}

void BVH::recomputeLeaf(const Mesh& mesh, Node& n) {
    n.box = AABB{};
    for (uint32_t i = n.first; i < n.first + n.count; ++i) {
        const auto& t = mesh.triangles[triIndices_[i]];
        n.box.expand(mesh.positions[t.x]);
        n.box.expand(mesh.positions[t.y]);
        n.box.expand(mesh.positions[t.z]);
        triLeaf_[triIndices_[i]] = static_cast<uint32_t>(&n - nodes_.data());
    }
}

uint32_t BVH::nextStamp(std::vector<uint32_t>& stamps, uint32_t& stamp) const {
    if (++stamp == 0) {
        std::fill(stamps.begin(), stamps.end(), 0);
        stamp = 1;
    }
    return stamp;
}

bool BVH::raycast(const Mesh& mesh, const glm::vec3& origin, const glm::vec3& dir, RayHit& hit) const {
    if (nodes_.empty()) return false;
    // Avoid 0 * inf = NaN in the slab test for axis-aligned rays.
    glm::vec3 safeDir = dir;
    for (int i = 0; i < 3; ++i)
        if (std::abs(safeDir[i]) < 1e-20f) safeDir[i] = std::copysign(1e-20f, safeDir[i]);
    glm::vec3 invDir = 1.0f / safeDir;
    float best = std::numeric_limits<float>::max();
    uint32_t bestTri = 0;
    bool found = false;

    stack_.clear();
    stack_.push_back(0);
    while (!stack_.empty()) {
        const Node& n = nodes_[stack_.back()];
        stack_.pop_back();
        if (!rayBox(n.box, origin, invDir, best)) continue;
        if (n.count > 0) {
            for (uint32_t i = n.first; i < n.first + n.count; ++i) {
                const auto& t = mesh.triangles[triIndices_[i]];
                float tt;
                if (rayTriangle(origin, dir, mesh.positions[t.x], mesh.positions[t.y], mesh.positions[t.z], tt) &&
                    tt < best) {
                    best = tt;
                    bestTri = triIndices_[i];
                    found = true;
                }
            }
        } else {
            stack_.push_back(n.first);
            stack_.push_back(n.first + 1);
        }
    }
    if (!found) return false;

    const auto& t = mesh.triangles[bestTri];
    hit.t = best;
    hit.triangle = bestTri;
    hit.position = origin + dir * best;
    glm::vec3 n = glm::cross(mesh.positions[t.y] - mesh.positions[t.x], mesh.positions[t.z] - mesh.positions[t.x]);
    float len = glm::length(n);
    hit.normal = len > 0.0f ? n / len : -dir;
    return true;
}

void BVH::queryVertices(const Mesh& mesh, const glm::vec3& center, float radius, std::vector<uint32_t>& out) const {
    out.clear();
    if (nodes_.empty()) return;
    const float r2 = radius * radius;
    const uint32_t s = nextStamp(vertStamp_, vertStampValue_);

    stack_.clear();
    stack_.push_back(0);
    while (!stack_.empty()) {
        const Node& n = nodes_[stack_.back()];
        stack_.pop_back();
        if (!sphereBox(n.box, center, r2)) continue;
        if (n.count > 0) {
            for (uint32_t i = n.first; i < n.first + n.count; ++i) {
                const auto& t = mesh.triangles[triIndices_[i]];
                for (int c = 0; c < 3; ++c) {
                    uint32_t v = t[c];
                    if (vertStamp_[v] == s) continue;
                    vertStamp_[v] = s;
                    glm::vec3 d = mesh.positions[v] - center;
                    if (glm::dot(d, d) <= r2) out.push_back(v);
                }
            }
        } else {
            stack_.push_back(n.first);
            stack_.push_back(n.first + 1);
        }
    }
}

void BVH::refit(const Mesh& mesh, std::span<const uint32_t> movedVerts) {
    if (nodes_.empty()) return;
    const uint32_t s = nextStamp(leafStamp_, leafStampValue_);
    scratchLeaves_.clear();
    for (uint32_t v : movedVerts) {
        for (uint32_t ti : mesh.vertexTriangles(v)) {
            uint32_t leaf = triLeaf_[ti];
            if (leafStamp_[leaf] != s) {
                leafStamp_[leaf] = s;
                scratchLeaves_.push_back(leaf);
            }
        }
    }
    for (uint32_t li : scratchLeaves_) {
        recomputeLeaf(mesh, nodes_[li]);
        // Grow ancestors until one already contains the new bounds.
        AABB box = nodes_[li].box;
        uint32_t ni = li;
        while (ni != 0) {
            ni = nodes_[ni].parent;
            AABB& pb = nodes_[ni].box;
            if (glm::all(glm::lessThanEqual(pb.min, box.min)) && glm::all(glm::greaterThanEqual(pb.max, box.max)))
                break;
            pb.expand(box);
            box = pb;
        }
    }
}

} // namespace sf
