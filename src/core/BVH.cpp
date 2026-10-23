// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/BVH.h"

#include "core/ThreadPool.h"

#include <algorithm>
#include <atomic>
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
    queryEpoch_ = 0;
    if (nt == 0) return;

    std::vector<glm::vec3> centroids(nt);
    poolFor(nt, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const auto& t = mesh.triangles[i];
            centroids[i] = (mesh.positions[t.x] + mesh.positions[t.y] + mesh.positions[t.z]) / 3.0f;
            triIndices_[i] = uint32_t(i);
        }
    }, 1 << 16);

    // Splits node `ni` of `nodes` at the median centroid along its longest axis (pushing its two children), or
    // leaves it a leaf. Only touches triIndices_ within the node's range, so subtrees can be split in parallel.
    auto split = [&](std::vector<Node>& nodes, uint32_t ni) -> bool {
        const uint32_t first = nodes[ni].first, count = nodes[ni].count;
        AABB cb;
        for (uint32_t i = first; i < first + count; ++i) cb.expand(centroids[triIndices_[i]]);
        const glm::vec3 ext = cb.extent();
        const int axis = ext.x > ext.y ? (ext.x > ext.z ? 0 : 2) : (ext.y > ext.z ? 1 : 2);
        if (count <= kLeafSize || ext[axis] <= 0.0f) return false;
        const uint32_t mid = first + count / 2;
        std::nth_element(triIndices_.begin() + first, triIndices_.begin() + mid, triIndices_.begin() + first + count,
                         [&](uint32_t a, uint32_t b) { return centroids[a][axis] < centroids[b][axis]; });
        const uint32_t left = static_cast<uint32_t>(nodes.size());
        nodes.push_back(Node{AABB{}, first, mid - first, ni});
        nodes.push_back(Node{AABB{}, mid, first + count - mid, ni});
        nodes[ni].first = left;
        nodes[ni].count = 0;
        return true;
    };
    // Splits `nodes` depth first from the nodes in `tasks`, until only leaves are left.
    auto buildFrom = [&](std::vector<Node>& nodes, std::vector<uint32_t> tasks) {
        while (!tasks.empty()) {
            const uint32_t ni = tasks.back();
            tasks.pop_back();
            if (!split(nodes, ni)) continue;
            tasks.push_back(nodes[ni].first);
            tasks.push_back(nodes[ni].first + 1);
        }
    };

    nodes_.reserve(2 * (nt / kLeafSize + 1));
    nodes_.push_back(Node{AABB{}, 0, static_cast<uint32_t>(nt), 0});
    if (nt < (size_t(1) << 18) || ThreadPool::shared().threads() == 1 || ThreadPool::shared().serial()) {
        buildFrom(nodes_, {0});
    } else {
        // Split the top breadth first until there is work for every thread, then build those subtrees in
        // parallel (each into its own node list) and append them. The splits are the same as a serial build's,
        // and children still follow their parents with the right child next to the left one.
        std::vector<uint32_t> open{0};
        const size_t want = size_t(ThreadPool::shared().threads()) * 8;
        while (open.size() < want) {
            std::vector<uint32_t> next;
            for (uint32_t ni : open)
                if (split(nodes_, ni)) {
                    next.push_back(nodes_[ni].first);
                    next.push_back(nodes_[ni].first + 1);
                }
            if (next.empty()) break;
            open = std::move(next);
        }
        std::vector<std::vector<Node>> sub(open.size());
        poolFor(open.size(), [&](size_t b, size_t e) {
            for (size_t k = b; k < e; ++k) {
                const Node& root = nodes_[open[k]];
                sub[k].push_back(Node{AABB{}, root.first, root.count, 0});
                if (root.count > 0) buildFrom(sub[k], {0});
            }
        }, 1);
        for (size_t k = 0; k < open.size(); ++k) {
            // Local node i >= 1 becomes base + i - 1; local 0 is the subtree's root, nodes_[open[k]].
            const uint32_t root = open[k], base = static_cast<uint32_t>(nodes_.size());
            auto global = [&](uint32_t i) { return i == 0 ? root : base + i - 1; };
            const std::vector<Node>& local = sub[k];
            nodes_[root].first = local[0].count > 0 ? local[0].first : global(local[0].first);
            nodes_[root].count = local[0].count;
            for (size_t i = 1; i < local.size(); ++i) {
                Node n = local[i];
                n.parent = global(n.parent);
                if (n.count == 0) n.first = global(n.first);
                nodes_.push_back(n);
            }
        }
    }

    // Leaf bounds (and each triangle's leaf), then inner bounds bottom-up (children have larger indices).
    poolFor(nodes_.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i)
            if (nodes_[i].count > 0) recomputeLeaf(mesh, nodes_[i]);
    }, 1 << 12);
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
    const bool skipHidden = mesh.hasHidden();
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
                if (skipHidden && mesh.triangleHidden(triIndices_[i])) continue;
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

void BVH::queryTriangles(const Mesh& mesh, const glm::vec3& center, float radius, std::vector<uint32_t>& out) const {
    out.clear();
    if (nodes_.empty()) return;
    const float r2 = radius * radius;
    stack_.clear();
    stack_.push_back(0);
    while (!stack_.empty()) {
        const Node& n = nodes_[stack_.back()];
        stack_.pop_back();
        if (!sphereBox(n.box, center, r2)) continue;
        if (n.count > 0) {
            for (uint32_t i = n.first; i < n.first + n.count; ++i) {
                const auto& t = mesh.triangles[triIndices_[i]];
                AABB b;
                b.expand(mesh.positions[t.x]);
                b.expand(mesh.positions[t.y]);
                b.expand(mesh.positions[t.z]);
                if (sphereBox(b, center, r2)) out.push_back(triIndices_[i]);
            }
        } else {
            stack_.push_back(n.first);
            stack_.push_back(n.first + 1);
        }
    }
}

void BVH::queryVertices(const Mesh& mesh, const glm::vec3& center, float radius, std::vector<uint32_t>& out) const {
    out.clear();
    if (nodes_.empty()) return;
    const float r2 = radius * radius;
    // vertStamp_ holds (epoch << 16) | code: kDone once a vertex is output in this query; while splitting
    // over threads, the code of the earliest subtree (in traversal order) holding it, kTaskCode - task.
    constexpr uint32_t kDone = 0xFFFF, kTaskCode = 0xFFFE;
    if (++queryEpoch_ == 0x10000) {
        std::fill(vertStamp_.begin(), vertStamp_.end(), 0);
        queryEpoch_ = 1;
    }
    const uint32_t epoch = queryEpoch_ << 16, done = epoch | kDone;
    auto inside = [&](uint32_t v) {
        const glm::vec3 d = mesh.positions[v] - center;
        return glm::dot(d, d) <= r2;
    };

    // The subtrees the sphere touches at a fixed depth, in the order a depth-first traversal reaches them.
    constexpr int kTaskDepth = 9;
    std::vector<uint32_t>& tasks = queryTasks_;
    tasks.clear();
    std::vector<std::pair<uint32_t, int>> stack{{0u, 0}};
    while (!stack.empty()) {
        const auto [ni, depth] = stack.back();
        stack.pop_back();
        const Node& n = nodes_[ni];
        if (!sphereBox(n.box, center, r2)) continue;
        if (n.count > 0 || depth == kTaskDepth) {
            tasks.push_back(ni);
            continue;
        }
        stack.push_back({n.first, depth + 1});
        stack.push_back({n.first + 1, depth + 1});
    }
    // Visits the vertices of the triangles under node `root` that the sphere's boxes touch, depth first.
    auto walk = [&](uint32_t root, std::vector<uint32_t>& nodeStack, auto&& visit) {
        nodeStack.assign(1, root);
        while (!nodeStack.empty()) {
            const Node& n = nodes_[nodeStack.back()];
            nodeStack.pop_back();
            if (!sphereBox(n.box, center, r2)) continue;
            if (n.count == 0) {
                nodeStack.push_back(n.first);
                nodeStack.push_back(n.first + 1);
                continue;
            }
            for (uint32_t i = n.first; i < n.first + n.count; ++i) {
                const auto& t = mesh.triangles[triIndices_[i]];
                visit(t.x);
                visit(t.y);
                visit(t.z);
            }
        }
    };

    if (tasks.size() < 8 || tasks.size() > kTaskCode || ThreadPool::shared().serial()) {
        for (uint32_t root : tasks)
            walk(root, stack_, [&](uint32_t v) {
                if (vertStamp_[v] == done) return;
                vertStamp_[v] = done;
                if (inside(v)) out.push_back(v);
            });
        return;
    }
    // In parallel, with the serial order: each subtree lists the vertices inside the sphere and claims them
    // (the earliest subtree's code wins: an atomic max); then each lists the ones it owns, in the order it
    // first met them, and the lists are joined in subtree order.
    if (queryParts_.size() < tasks.size()) queryParts_.resize(tasks.size());
    poolFor(tasks.size(), [&](size_t b, size_t e) {
        std::vector<uint32_t> nodeStack;
        for (size_t k = b; k < e; ++k) {
            const uint32_t mine = epoch | (kTaskCode - uint32_t(k));
            std::vector<uint32_t>& found = queryParts_[k];
            found.clear();
            walk(tasks[k], nodeStack, [&](uint32_t v) {
                std::atomic_ref<uint32_t> owner(vertStamp_[v]);
                if (owner.load(std::memory_order_relaxed) == mine || !inside(v)) return;
                found.push_back(v);
                for (uint32_t cur = owner.load(std::memory_order_relaxed);
                     cur < mine && !owner.compare_exchange_weak(cur, mine, std::memory_order_relaxed);) {
                }
            });
        }
    }, 1, 2);
    poolFor(tasks.size(), [&](size_t b, size_t e) {
        for (size_t k = b; k < e; ++k) {
            const uint32_t mine = epoch | (kTaskCode - uint32_t(k));
            std::vector<uint32_t>& found = queryParts_[k];
            size_t kept = 0;
            for (uint32_t v : found) {
                std::atomic_ref<uint32_t> owner(vertStamp_[v]);
                if (owner.load(std::memory_order_relaxed) != mine) continue;
                owner.store(done, std::memory_order_relaxed);
                found[kept++] = v;
            }
            found.resize(kept);
        }
    }, 1, 2);
    for (size_t k = 0; k < tasks.size(); ++k) out.insert(out.end(), queryParts_[k].begin(), queryParts_[k].end());
}

void BVH::refit(const Mesh& mesh, std::span<const uint32_t> movedVerts) {
    if (nodes_.empty()) return;
    const uint32_t s = nextStamp(leafStamp_, leafStampValue_);
    scratchLeaves_.clear();
    constexpr size_t kBlock = 4096;
    if (movedVerts.size() <= kBlock) {
        for (uint32_t v : movedVerts) {
            for (uint32_t ti : mesh.vertexTriangles(v)) {
                uint32_t leaf = triLeaf_[ti];
                if (leafStamp_[leaf] != s) {
                    leafStamp_[leaf] = s;
                    scratchLeaves_.push_back(leaf);
                }
            }
        }
    } else {
        // Threads claim leaves with an atomic exchange of the stamp (the set, and so the result, is the same).
        const size_t parts = (movedVerts.size() + kBlock - 1) / kBlock;
        if (scratchParts_.size() < parts) scratchParts_.resize(parts);
        poolFor(movedVerts.size(), [&](size_t b, size_t e) {
            std::vector<uint32_t>& out = scratchParts_[b / kBlock];
            out.clear();
            for (size_t i = b; i < e; ++i)
                for (uint32_t ti : mesh.vertexTriangles(movedVerts[i])) {
                    const uint32_t leaf = triLeaf_[ti];
                    if (std::atomic_ref<uint32_t>(leafStamp_[leaf]).exchange(s, std::memory_order_relaxed) != s)
                        out.push_back(leaf);
                }
        }, kBlock);
        for (size_t p = 0; p < parts; ++p)
            scratchLeaves_.insert(scratchLeaves_.end(), scratchParts_[p].begin(), scratchParts_[p].end());
    }
    poolFor(scratchLeaves_.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) recomputeLeaf(mesh, nodes_[scratchLeaves_[i]]);
    }, 512);
    // Grow ancestors until one already contains the new bounds (a union, so the order doesn't matter).
    for (uint32_t li : scratchLeaves_) {
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

size_t BVH::memoryBytes() const {
    auto bytes = [](const auto& v) { return v.capacity() * sizeof(v[0]); };
    return bytes(nodes_) + bytes(triIndices_) + bytes(triLeaf_) + bytes(vertStamp_) + bytes(leafStamp_) +
           bytes(scratchLeaves_) + bytes(stack_);
}

void BVH::refitAll(const Mesh& mesh) {
    for (size_t i = nodes_.size(); i-- > 0;) { // children always have larger indices
        Node& n = nodes_[i];
        if (n.count > 0) {
            recomputeLeaf(mesh, n);
        } else {
            n.box = nodes_[n.first].box;
            n.box.expand(nodes_[n.first + 1].box);
        }
    }
}

} // namespace sf
