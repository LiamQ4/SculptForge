// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AutoUv.h"

#include "core/Mesh.h"
#include "core/Texture.h"

#include <xatlas.h>

#include <algorithm>
#include <atomic>
#include <memory>

namespace sf {

namespace {

struct ProgressState {
    const AutoUvProgress* fn = nullptr;
    std::atomic<bool> cancelled{false};
};

bool onProgress(xatlas::ProgressCategory category, int percent, void* user) {
    auto* s = static_cast<ProgressState*>(user);
    if (!s->fn || !*s->fn) return true;
    // Rough share of the time each stage takes.
    float from = 0.0f, span = 0.05f;
    switch (category) {
    case xatlas::ProgressCategory::AddMesh: from = 0.0f, span = 0.05f; break;
    case xatlas::ProgressCategory::ComputeCharts: from = 0.05f, span = 0.6f; break;
    case xatlas::ProgressCategory::PackCharts: from = 0.65f, span = 0.3f; break;
    case xatlas::ProgressCategory::BuildOutputMeshes: from = 0.95f, span = 0.05f; break;
    }
    if (!(*s->fn)(from + span * float(std::clamp(percent, 0, 100)) / 100.0f)) s->cancelled = true;
    return !s->cancelled;
}

} // namespace

bool computeAutoUv(const std::vector<glm::vec3>& positions, const std::vector<glm::uvec3>& triangles, int resolution,
                   AutoUvResult& out, std::string& error, const AutoUvProgress& progress) {
    out = AutoUvResult();
    if (positions.empty() || triangles.empty()) {
        error = "There is no mesh to unwrap";
        return false;
    }
    std::unique_ptr<xatlas::Atlas, void (*)(xatlas::Atlas*)> atlas(xatlas::Create(), xatlas::Destroy);
    ProgressState state;
    state.fn = &progress;
    xatlas::SetProgressCallback(atlas.get(), onProgress, &state);
    xatlas::MeshDecl decl;
    decl.vertexPositionData = positions.data();
    decl.vertexPositionStride = sizeof(glm::vec3);
    decl.vertexCount = uint32_t(positions.size());
    decl.indexData = triangles.data();
    decl.indexCount = uint32_t(triangles.size() * 3);
    decl.indexFormat = xatlas::IndexFormat::UInt32;
    const xatlas::AddMeshError added = xatlas::AddMesh(atlas.get(), decl, 1);
    if (added != xatlas::AddMeshError::Success) {
        error = std::string("Auto UV failed: ") + xatlas::StringForEnum(added);
        return false;
    }
    xatlas::AddMeshJoin(atlas.get());
    if (state.cancelled) {
        error = "Auto UV was cancelled";
        return false;
    }
    xatlas::ChartOptions charts;
    xatlas::PackOptions pack;
    pack.resolution = uint32_t(std::clamp(resolution, 256, 8192));
    pack.padding = uint32_t(kTextureGutter); // the painter pads islands by this much
    pack.bilinear = true;
    pack.blockAlign = true; // faster packing on big meshes
    xatlas::Generate(atlas.get(), charts, pack);
    if (state.cancelled) {
        error = "Auto UV was cancelled";
        return false;
    }
    if (atlas->meshCount != 1 || atlas->width == 0 || atlas->height == 0) {
        error = "Auto UV produced no charts";
        return false;
    }
    const xatlas::Mesh& m = atlas->meshes[0];
    if (m.indexCount != triangles.size() * 3) {
        error = "Auto UV changed the triangle count";
        return false;
    }
    const float sx = 1.0f / float(atlas->width), sy = 1.0f / float(atlas->height);
    out.uvs.resize(m.indexCount);
    for (uint32_t i = 0; i < m.indexCount; ++i) {
        const xatlas::Vertex& v = m.vertexArray[m.indexArray[i]];
        out.uvs[i] = glm::clamp(glm::vec2(v.uv[0] * sx, v.uv[1] * sy), glm::vec2(0.0f), glm::vec2(1.0f));
    }
    out.charts = int(m.chartCount);
    return true;
}

bool computeAutoUv(const Mesh& mesh, AutoUvResult& out, std::string& error, const AutoUvProgress& progress) {
    const int res = mesh.texture ? std::max(mesh.texture->width, mesh.texture->height) : kDefaultTextureSize;
    return computeAutoUv(mesh.positions, mesh.triangles, res, out, error, progress);
}

} // namespace sf
