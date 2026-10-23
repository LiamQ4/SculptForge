// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Performance work (docs/performance.md): the thread pool, brushes / normals / BVH refits that run on it give
// the same result as running serially, partial GPU uploads (core/UploadOrder.h) cover every changed vertex, the
// parallel BVH build answers like the serial one, undo after a big stroke is exact, and the benchmark runs.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/BrushRecipe.h"
#include "core/Primitives.h"
#include "core/ThreadPool.h"
#include "core/UploadOrder.h"

#include <atomic>
#include <cmath>
#include <thread>

using namespace sf;

namespace {

// Runs `fn` with the shared pool forced serial, then restores it.
template <class F>
void serially(F&& fn) {
    ThreadPool::shared().setSerial(true);
    fn();
    ThreadPool::shared().setSerial(false);
}

bool sameVec3(const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) return false;
    return true;
}

// A stroke with every built-in brush type plus a recipe, mask and paint, big enough (a few blocks of vertices
// per dab) to run in parallel.
void bigStroke(Document& d) {
    const float radius = 0.4f;
    for (int t = 0; t < int(BrushType::Count); ++t) {
        const BrushType type = BrushType(t);
        d.sculptor.beginStroke(d.mesh, brushName(type));
        for (int i = 0; i < 4; ++i) {
            DabParams p;
            p.center = glm::normalize(glm::vec3(0.3f + 0.05f * float(i), 0.2f, 1.0f));
            p.radius = radius;
            p.strength = 0.6f;
            p.strokeDelta = glm::vec3(0.01f, 0.0f, 0.0f);
            if (type == BrushType::Move) {
                if (i == 0) d.sculptor.beginGrab(d.mesh, d.bvh, p, Symmetry{});
                else d.sculptor.grab(d.mesh, d.bvh, glm::vec3(0.01f, 0.005f, 0.0f));
            } else {
                d.sculptor.dab(d.mesh, d.bvh, type, p, Symmetry{});
            }
        }
        d.undo.push(d.sculptor.endStroke());
    }
    const std::vector<BrushRecipe> recipes = builtinBrushRecipes();
    for (const BrushRecipe& r : recipes) {
        d.sculptor.beginStroke(d.mesh, r.name);
        DabParams p;
        p.center = glm::normalize(glm::vec3(-0.2f, 0.4f, 1.0f));
        p.radius = radius;
        p.strokeDelta = glm::vec3(0.0f, 0.01f, 0.0f);
        d.sculptor.dabRecipe(d.mesh, d.bvh, r, p, Symmetry{});
        d.undo.push(d.sculptor.endStroke());
    }
    d.sculptor.beginStroke(d.mesh, "Mask");
    DabParams p;
    p.center = glm::normalize(glm::vec3(0.0f, -0.3f, 1.0f));
    p.radius = radius;
    d.sculptor.dabMask(d.mesh, d.bvh, p, Symmetry{});
    d.undo.push(d.sculptor.endStroke());
}

Document sphereDoc(int subdivisions) { return makeDoc(makeIcosphere(subdivisions)); }

} // namespace

TEST(thread_pool_runs_every_job_once) {
    ThreadPool& pool = ThreadPool::shared();
    CHECK(pool.threads() >= 1);
    std::vector<std::atomic<int>> hits(100000);
    poolFor(hits.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) hits[i].fetch_add(1);
    }, 1000);
    bool once = true;
    for (auto& h : hits) once = once && h.load() == 1;
    CHECK(once);

    // Nested use runs inline; two threads using the pool at once both finish.
    std::atomic<size_t> nested{0};
    poolFor(64, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) poolFor(100, [&](size_t b2, size_t e2) { nested += e2 - b2; }, 10);
    }, 1);
    CHECK(nested.load() == 6400);
    std::atomic<size_t> a{0}, c{0};
    std::thread other([&] {
        for (int k = 0; k < 50; ++k) poolFor(10000, [&](size_t b, size_t e) { a += e - b; }, 100);
    });
    for (int k = 0; k < 50; ++k) poolFor(10000, [&](size_t b, size_t e) { c += e - b; }, 100);
    other.join();
    CHECK(a.load() == 500000 && c.load() == 500000);

    // One block runs on the calling thread, like a plain loop.
    std::thread::id where;
    poolFor(10, [&](size_t, size_t) { where = std::this_thread::get_id(); }, 4096);
    CHECK(where == std::this_thread::get_id());
}

TEST(parallel_brushes_match_serial) {
    // icosphere(7): 164k vertices, so each dab covers several blocks (> 4096 vertices) and runs in parallel.
    Document par = sphereDoc(7), ser = sphereDoc(7);
    bigStroke(par);
    serially([&] { bigStroke(ser); });
    CHECK(sameVec3(par.mesh.positions, ser.mesh.positions));
    CHECK(sameVec3(par.mesh.normals, ser.mesh.normals));
    CHECK(par.mesh.mask == ser.mesh.mask);
    CHECK(par.undo.undoCount() == ser.undo.undoCount());
    // Undo every stroke: back to the sphere exactly.
    const Mesh fresh = makeIcosphere(7);
    while (par.undo.canUndo()) par.undo.undo(par.mesh, par.bvh);
    CHECK(sameVec3(par.mesh.positions, fresh.positions));
    bool maskClear = true;
    for (float m : par.mesh.mask) maskClear = maskClear && m == 0.0f;
    CHECK(maskClear);
}

TEST(parallel_normals_match_full_recompute) {
    Mesh m = makeIcosphere(6);
    std::vector<uint32_t> moved;
    for (uint32_t v = 0; v < m.vertexCount(); v += 3) {
        m.positions[v] *= 1.0f + 0.05f * std::sin(float(v));
        moved.push_back(v);
    }
    m.updateNormals(moved);
    const std::vector<glm::vec3> partial = m.normals;
    m.recomputeAllNormals();
    float worst = 0.0f;
    for (size_t v = 0; v < partial.size(); ++v) worst = std::max(worst, glm::length(partial[v] - m.normals[v]));
    CHECK(worst < 1e-5f);
}

TEST(upload_runs_cover_changed_vertices) {
    // A subdivided mesh, whose vertex numbering scatters any region over the index range.
    Mesh m = makeBenchSphere(200000);
    BVH bvh;
    bvh.build(m);
    UploadOrder order;
    order.build(m);
    // A permutation: every vertex once.
    std::vector<uint8_t> seen(m.vertexCount(), 0);
    bool perm = order.size() == m.vertexCount();
    for (uint32_t i = 0; perm && i < order.size(); ++i) {
        const uint32_t v = order.fromGpu()[i];
        perm = v < m.vertexCount() && !seen[v] && order.toGpu(v) == i;
        seen[v] = 1;
    }
    CHECK(perm);

    m.clearDirty();
    std::vector<std::pair<uint32_t, uint32_t>> runs;
    order.dirtyRuns(m, runs);
    CHECK(runs.empty());
    CHECK(!m.vertexDirty(0));
    // A brush dab with symmetry: every vertex whose normal changed is in a run, and the runs hold not much
    // more than the dab touched.
    Sculptor s;
    s.beginStroke(m, "Standard");
    const std::vector<glm::vec3> before = m.normals;
    DabParams p;
    p.center = glm::normalize(glm::vec3(0.5f, 0.5f, 0.7f));
    p.radius = 0.2f;
    s.dab(m, bvh, BrushType::Standard, p, Symmetry{});
    order.dirtyRuns(m, runs);
    std::vector<uint8_t> inRun(m.vertexCount(), 0);
    size_t uploaded = 0;
    for (size_t i = 0; i < runs.size(); ++i) {
        CHECK(runs[i].first < runs[i].second && runs[i].second <= m.vertexCount());
        if (i) CHECK(runs[i - 1].second < runs[i].first);
        uploaded += runs[i].second - runs[i].first;
        for (uint32_t g = runs[i].first; g < runs[i].second; ++g) inRun[order.fromGpu()[g]] = 1;
    }
    bool allCovered = true;
    size_t changed = 0, dirty = 0;
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        dirty += m.vertexDirty(v);
        if (m.normals[v] != before[v]) {
            ++changed;
            allCovered = allCovered && inRun[v] && m.vertexDirty(v);
        }
    }
    CHECK(changed > 1000);
    CHECK(allCovered);
    // In mesh order, the 2048-vertex blocks holding them would be several times more.
    size_t meshOrder = 0;
    for (size_t b = 0; b < m.dirtyBlocks.size(); ++b) meshOrder += m.dirtyBlocks[b] ? Mesh::kDirtyBlock : 0;
    CHECK(uploaded * 3 < meshOrder);
    m.clearDirty();
    order.dirtyRuns(m, runs);
    CHECK(runs.empty());
    CHECK(!m.vertexDirty(order.fromGpu()[0]));
    // Ranges mark per vertex too; most of the mesh dirty uploads everything as one range.
    m.markDirtyRange(70, 200);
    CHECK(!m.vertexDirty(69) && m.vertexDirty(70) && m.vertexDirty(199) && !m.vertexDirty(200));
    m.markAllDirty();
    order.dirtyRuns(m, runs);
    CHECK(runs.size() == 1 && runs[0].first == 0 && runs[0].second == m.vertexCount());
}

TEST(parallel_bvh_build_answers_like_serial) {
    // icosphere(7): 328k triangles, above the size the build splits over threads.
    const Mesh m = makeIcosphere(7);
    BVH par, ser;
    par.build(m);
    serially([&] { ser.build(m); });
    CHECK(par.nodeCount() == ser.nodeCount());
    std::vector<uint32_t> a, b;
    bool same = true;
    for (int i = 0; i < 50; ++i) {
        const glm::vec3 c = glm::normalize(glm::vec3(std::sin(float(i)), std::cos(float(i) * 1.3f), std::sin(float(i) * 0.7f)));
        par.queryVertices(m, c, 0.1f, a);
        ser.queryVertices(m, c, 0.1f, b);
        same = same && a == b;
        par.queryTriangles(m, c, 0.05f, a);
        ser.queryTriangles(m, c, 0.05f, b);
        same = same && a == b;
        RayHit h1, h2;
        const bool r1 = par.raycast(m, c * 3.0f, -c, h1), r2 = ser.raycast(m, c * 3.0f, -c, h2);
        same = same && r1 && r2 && h1.triangle == h2.triangle && h1.t == h2.t;
    }
    CHECK(same);
}

TEST(bvh_refit_after_big_stroke_finds_everything) {
    Document d = sphereDoc(7);
    bigStroke(d);
    // Brute force vs. the refitted tree.
    std::vector<uint32_t> found;
    bool ok = true;
    for (int i = 0; i < 20; ++i) {
        const glm::vec3 c = glm::normalize(glm::vec3(0.3f, 0.2f - 0.03f * float(i), 1.0f));
        d.bvh.queryVertices(d.mesh, c, 0.2f, found);
        size_t brute = 0;
        for (const glm::vec3& p : d.mesh.positions) brute += glm::distance(p, c) <= 0.2f;
        ok = ok && found.size() == brute;
    }
    CHECK(ok);
}

TEST(first_paint_undo_is_cheap_and_exact) {
    // The app's first polypaint stroke on an unpainted mesh: the undo step holds the paint stack without
    // colours (not a copy of the mesh), and undo removes the attribute again.
    Document d = sphereDoc(5);
    CHECK(!d.mesh.hasColors());
    auto snapshot = std::make_unique<PaintStackUndo>(PaintStackUndo::capture(d.mesh));
    d.sculptor.beginStroke(d.mesh, "Paint");
    d.mesh.ensureColors();
    for (uint32_t v = 0; v < 100; ++v) {
        d.sculptor.recordColor(d.mesh, v);
        d.mesh.colors[v] = glm::vec3(1, 0, 0);
    }
    UndoRecord r = d.sculptor.endStroke();
    r.colors = {};
    r.paintAlpha = {};
    r.paint = std::move(snapshot);
    CHECK(r.approxBytes() < 1000);
    d.undo.push(std::move(r));
    d.undo.undo(d.mesh, d.bvh);
    CHECK(!d.mesh.hasColors());
    d.undo.redo(d.mesh, d.bvh);
    CHECK(d.mesh.hasColors() && d.mesh.colors[5] == glm::vec3(1, 0, 0) && d.mesh.colors[500] == glm::vec3(1.0f));
}

TEST(mesh_memory_has_no_face_normal_cache) {
    // Bytes per triangle of a sculpting mesh (positions, normals, triangles, both adjacency lists and stamps):
    // about 54 on a closed triangle mesh (it was 78 with a per-triangle face normal cache, its stamps and the
    // adjacency list's spare capacity).
    const Mesh m = makeIcosphere(6);
    const double perTriangle = double(m.memoryBytes()) / double(m.triangleCount());
    CHECK(perTriangle < 60.0);
}

TEST(benchmark_smoke) {
    BenchOptions o;
    o.sizesMillions = {0.02};
    o.dabsPerStroke = 3;
    o.projectDir = std::filesystem::temp_directory_path().string();
    const std::string report = runBenchmark(o);
    CHECK(report.find("| Standard |") != std::string::npos);
    CHECK(report.find("| Paint |") != std::string::npos);
    CHECK(report.find("Open project") != std::string::npos);
    CHECK(report.find("failed") == std::string::npos);
    const Mesh m = makeBenchSphere(50000);
    CHECK(m.triangleCount() > 40000 && m.triangleCount() < 60000);
}
