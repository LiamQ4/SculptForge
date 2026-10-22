// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Thumbnails.h"

#include "app/AssetLibrary.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/PathStroke.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/Sculpt.h"
#include "render/Camera.h"
#include "render/GL.h"
#include "render/Renderer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <system_error>

namespace sf {

namespace {

constexpr size_t kMaxJobs = 3;       // worker threads at a time
constexpr int kRendersPerFrame = 2;  // GL renders per frame
constexpr double kStatSeconds = 2.0; // how long a file's size / time stamp is trusted

std::filesystem::path fsPath(const std::string& p) { return std::filesystem::path(std::u8string(p.begin(), p.end())); }

template <class T> bool ready(const std::future<T>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

ImTextureID texId(unsigned tex) { return ImTextureID(uintptr_t(tex)); }

// One mesh of every visible SubTool of `doc` (positions, triangles and polypaint), for the preview.
std::unique_ptr<Mesh> sceneMesh(Document& doc) {
    auto out = std::make_unique<Mesh>();
    bool colors = false;
    for (int i = 0; i < doc.subtoolCount(); ++i)
        colors |= doc.subtools[size_t(i)].visible &&
                  doc.subtoolMesh(i).colors.size() == doc.subtoolMesh(i).positions.size() &&
                  !doc.subtoolMesh(i).colors.empty();
    for (int i = 0; i < doc.subtoolCount(); ++i) {
        if (!doc.subtools[size_t(i)].visible) continue;
        const Mesh& m = doc.subtoolMesh(i);
        const uint32_t base = uint32_t(out->positions.size());
        out->positions.insert(out->positions.end(), m.positions.begin(), m.positions.end());
        if (colors) {
            if (m.colors.size() == m.positions.size())
                out->colors.insert(out->colors.end(), m.colors.begin(), m.colors.end());
            else
                out->colors.insert(out->colors.end(), m.positions.size(), glm::vec3(1.0f));
        }
        const bool hidden = m.hidden.size() == m.triangles.size();
        for (size_t t = 0; t < m.triangles.size(); ++t)
            if (!hidden || !m.hidden[t]) out->triangles.push_back(m.triangles[t] + glm::uvec3(base));
    }
    if (out->triangles.empty()) return nullptr;
    out->rebuildTopology();
    out->recomputeAllNormals();
    return out;
}

// Paths for brush previews: an S across a plane at y = 0 (seen from above), or across the front of a sphere.
std::vector<glm::vec3> planeCurve() {
    std::vector<glm::vec3> pts;
    for (int i = 0; i <= 8; ++i) {
        const float u = float(i) / 8.0f;
        pts.emplace_back(-0.66f + 1.32f * u, 1.0f, 0.28f * std::sin(u * 6.2831853f));
    }
    return pts;
}
std::vector<glm::vec3> line(glm::vec3 a, glm::vec3 b) { return {a, b}; }

void stroke(Document& d, const BrushRecipe* recipe, BrushType type, std::vector<glm::vec3> pts, glm::vec3 view,
            float radius, float strength, bool curve = true) {
    PathStroke st;
    st.points = std::move(pts);
    st.view = view;
    st.radius = radius;
    st.strength = strength;
    st.curve = curve;
    std::string err;
    strokePath(d, recipe, type, st, err);
}

// Sculpts the preview of a brush (a recipe when given, else `type`) with the same code real strokes use.
std::unique_ptr<Mesh> sculptBrushPreview(const BrushRecipe* recipe, int type, float& pitch, float& zoom,
                                         float& distance) {
    Document d;
    const glm::vec3 down(0.0f, -1.0f, 0.0f), front(0.0f, 0.0f, -1.0f);
    // A close-up of the stroke from above at an angle; the plane's edges stay outside the tile.
    pitch = -46.0f;
    distance = 2.15f;
    if (type == kPaintBrush) {
        // Polypaint: a coloured S on a clay sphere.
        Mesh m = makeIcosphere(5, 0.9f);
        m.ensureColors(glm::vec3(0.62f, 0.60f, 0.58f));
        for (uint32_t v = 0; v < m.vertexCount(); ++v) {
            const glm::vec3 p = m.positions[v];
            const float curve = 0.32f * std::sin(p.x * 3.4f);
            const float d2 = std::abs(p.y - curve);
            if (p.z > 0.0f && d2 < 0.16f) {
                const float t = std::clamp((0.16f - d2) / 0.05f, 0.0f, 1.0f);
                m.colors[v] = glm::mix(m.colors[v], glm::vec3(0.85f, 0.18f, 0.06f), t);
            }
        }
        pitch = -12.0f;
        distance = 0.0f;
        return std::make_unique<Mesh>(std::move(m));
    }
    const auto bt = BrushType(type);
    const bool sphere = !recipe && (bt == BrushType::Move || bt == BrushType::SnakeHook || bt == BrushType::Inflate);
    if (sphere) {
        d.setMesh(makeIcosphere(5, 0.9f));
        pitch = -12.0f;
        distance = 0.0f;
        if (bt == BrushType::Move)
            stroke(d, nullptr, bt, line({-0.1f, 0.0f, 2.0f}, {0.5f, 0.32f, 2.0f}), front, 0.6f, 1.0f, false);
        else if (bt == BrushType::SnakeHook) {
            // SnakeHook pulls out along the stroke; the path heads out of the surface to the side.
            stroke(d, nullptr, bt, line({0.15f, 0.15f, 2.0f}, {0.9f, 0.55f, 2.0f}), front, 0.28f, 1.0f, false);
        } else {
            std::vector<glm::vec3> pts;
            for (glm::vec3 p : planeCurve())
                pts.emplace_back(p.x * 0.8f, p.z * 1.4f, 2.0f);
            stroke(d, nullptr, bt, pts, front, 0.2f, 0.8f);
        }
        return sceneMesh(d);
    }
    d.setMesh(makePlane(220, 3.0f));
    const float r = 0.19f;
    if (recipe) {
        stroke(d, recipe, BrushType::Standard, planeCurve(), down, r, 0.9f);
        return sceneMesh(d);
    }
    switch (bt) {
    case BrushType::Smooth:
    case BrushType::Flatten:
    case BrushType::Trim:
    case BrushType::Nudge: {
        // These change existing form: raise a ridge first, then cross it with the brush.
        const auto ridge = line({-0.9f, 1.0f, 0.0f}, {0.9f, 1.0f, 0.0f});
        stroke(d, nullptr, BrushType::Standard, ridge, down, 0.26f, 1.0f, false);
        stroke(d, nullptr, BrushType::Clay, ridge, down, 0.26f, 1.0f, false);
        const auto across = bt == BrushType::Nudge ? line({-0.15f, 1.0f, -0.35f}, {0.15f, 1.0f, 0.35f})
                                                   : line({0.0f, 1.0f, -0.8f}, {0.0f, 1.0f, 0.8f});
        const float strength = bt == BrushType::Nudge ? 0.05f : bt == BrushType::Trim ? 0.5f : 1.0f;
        const int passes = bt == BrushType::Smooth ? 3 : 1;
        for (int pass = 0; pass < passes; ++pass)
            stroke(d, nullptr, bt, across, down, bt == BrushType::Nudge ? 0.26f : 0.22f, strength, false);
        break;
    }
    case BrushType::Pinch:
        stroke(d, nullptr, BrushType::Standard, planeCurve(), down, r * 1.2f, 0.9f);
        stroke(d, nullptr, bt, planeCurve(), down, r * 1.2f, 1.0f);
        break;
    case BrushType::ClayBuildup:
        stroke(d, nullptr, bt, planeCurve(), down, r, 0.12f);
        break;
    case BrushType::Layer:
        stroke(d, nullptr, bt, planeCurve(), down, 0.34f, 1.0f); // its height scales with the radius
        break;
    default:
        stroke(d, nullptr, bt, planeCurve(), down, r, 1.0f);
        break;
    }
    return sceneMesh(d);
}

} // namespace

ThumbnailCache::ThumbnailCache() = default;

ThumbnailCache::~ThumbnailCache() {
    // Textures die with the GL context (App::shutdown calls clear() while it is current).
    for (auto& j : jobs_)
        if (j.result.valid()) j.result.wait();
    for (auto& w : writes_)
        if (w.valid()) w.wait();
}

void ThumbnailCache::clear() {
    for (auto& [key, e] : entries_)
        if (e.tex) gl::DeleteTextures(1, &e.tex);
    entries_.clear();
    queue_.clear();
    renders_.clear();
}

void ThumbnailCache::setCacheDir(std::string dir) {
    cacheDir_ = std::move(dir);
    if (cacheDir_.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(fsPath(cacheDir_), ec);
    if (ec) {
        cacheDir_.clear();
        return;
    }
    // Keep the disk cache bounded: the least recently written thumbnails go first.
    writes_.push_back(std::async(std::launch::async, [dir = cacheDir_] {
        constexpr size_t kKeep = 2000;
        std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> files;
        std::error_code e;
        for (std::filesystem::directory_iterator it(fsPath(dir), e), end; !e && it != end; it.increment(e))
            if (it->path().extension() == ".png") files.emplace_back(it->last_write_time(e), it->path());
        if (files.size() <= kKeep) return;
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (size_t i = kKeep; i < files.size(); ++i)
            std::filesystem::remove(files[i].second, e);
    }));
}

bool ThumbnailCache::busy() const { return !queue_.empty() || !jobs_.empty() || !renders_.empty(); }

ImTextureID ThumbnailCache::request(const std::string& key, Producer produce, bool diskCache) {
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        it->second.used = frame_;
        return it->second.state == State::Ready ? texId(it->second.tex) : ImTextureID(0);
    }
    Entry& e = entries_[key];
    e.used = frame_;
    if (diskCache && !cacheDir_.empty()) {
        // Try the disk cache first; fall back to making it.
        const std::string cached = thumbnailCachePath(cacheDir_, key);
        produce = [cached, make = std::move(produce)]() {
            Result r;
            std::string err;
            std::error_code ec;
            if (std::filesystem::is_regular_file(fsPath(cached), ec) && Image::load(cached, r.image, err)) {
                r.store = false;
                return r;
            }
            return make();
        };
    } else if (!diskCache) {
        produce = [make = std::move(produce)]() {
            Result r = make();
            r.store = false;
            return r;
        };
    }
    queue_.emplace_front(key, std::move(produce));
    return ImTextureID(0);
}

ImTextureID ThumbnailCache::brush(int brushType) {
    const std::string key = "brush|" + std::to_string(brushType) + "|" + std::to_string(kThumbnailVersion);
    return request(
        hex64(fnv1a64(key)),
        [brushType] {
            Result r;
            r.mesh = sculptBrushPreview(nullptr, brushType, r.pitch, r.zoom, r.distance);
            r.label = "brush-" + std::to_string(brushType);
            r.yaw = 0.0f;
            r.polypaint = brushType == kPaintBrush;
            return r;
        },
        true);
}

ImTextureID ThumbnailCache::recipe(const BrushRecipe& recipe) {
    const std::string key = "recipe|" + recipe.toJson().dump() + "|" + std::to_string(kThumbnailVersion);
    return request(
        hex64(fnv1a64(key)),
        [recipe] {
            Result r;
            r.mesh = sculptBrushPreview(&recipe, 0, r.pitch, r.zoom, r.distance);
            r.label = "recipe-" + recipe.name;
            r.yaw = 0.0f;
            return r;
        },
        true);
}

ImTextureID ThumbnailCache::alpha(const BrushAlpha& alpha) {
    // Name, size and a sparse sample of the values: cheap enough to compute every frame.
    uint64_t h = fnv1a64(alpha.name + "|" + std::to_string(alpha.width) + "x" + std::to_string(alpha.height));
    const size_t n = alpha.values.size();
    for (size_t i = 0; i < 64 && n; ++i) {
        const float v = alpha.values[i * n / 64];
        h = fnv1a64(std::string_view(reinterpret_cast<const char*>(&v), sizeof v), h);
    }
    return request(
        "alpha|" + hex64(h),
        [alpha] {
            // The stamp as ZBrush shows alphas: white = full effect on black.
            Result r;
            constexpr int S = 128;
            r.image.width = r.image.height = S;
            r.image.rgba.resize(size_t(S) * S * 4);
            const BrushAlpha shown = alpha.empty() ? BrushAlpha::round() : alpha;
            for (int y = 0; y < S; ++y)
                for (int x = 0; x < S; ++x) {
                    const float u = (float(x) + 0.5f) / S * 2.0f - 1.0f, v = 1.0f - (float(y) + 0.5f) / S * 2.0f;
                    float g = shown.empty() ? brushFalloff(std::sqrt(u * u + v * v), 1.0f) : shown.sample(u, v);
                    const auto c = uint8_t(std::clamp(g, 0.0f, 1.0f) * 255.0f + 0.5f);
                    uint8_t* p = &r.image.rgba[(size_t(y) * S + size_t(x)) * 4];
                    p[0] = p[1] = p[2] = c;
                    p[3] = 255;
                }
            return r;
        },
        false);
}

ImTextureID ThumbnailCache::material(int matcap) {
    const std::string key = "material|" + std::to_string(matcap) + "|" + std::to_string(kThumbnailVersion);
    return request(
        hex64(fnv1a64(key)),
        [matcap] {
            Result r;
            r.mesh = std::make_unique<Mesh>(makeIcosphere(5, 1.0f));
            r.matcap = matcap;
            r.yaw = 0.0f;
            r.pitch = 0.0f;
            return r;
        },
        true);
}

ImTextureID ThumbnailCache::primitive(int primitive) {
    const std::string key = "primitive|" + std::to_string(primitive) + "|" + std::to_string(kThumbnailVersion);
    return request(
        hex64(fnv1a64(key)),
        [primitive] {
            Result r;
            switch (Primitive(primitive)) {
            case Primitive::Cube:
                r.mesh = std::make_unique<Mesh>(makeCube(24, 1.4f));
                break;
            case Primitive::Plane:
                r.mesh = std::make_unique<Mesh>(makePlane(32, 2.0f));
                r.pitch = -35.0f;
                break;
            default:
                r.mesh = std::make_unique<Mesh>(makeIcosphere(5, 1.0f));
                break;
            }
            return r;
        },
        true);
}

ImTextureID ThumbnailCache::file(const std::string& path) {
    Stat& s = stats_[path];
    const double now = ImGui::GetTime();
    if (now - s.at > kStatSeconds) {
        std::error_code ec;
        s.at = now;
        s.exists = std::filesystem::is_regular_file(fsPath(path), ec);
        s.size = s.exists ? uint64_t(std::filesystem::file_size(fsPath(path), ec)) : 0;
        s.mtime = modifiedTime(path);
    }
    if (!s.exists) return ImTextureID(0);
    const bool image = isImageFile(path), project = isProjectFile(path);
    const std::string kind = project ? "project" : image ? "image" : "mesh";
    const std::string key = fileThumbnailKey(kind, path, s.size, s.mtime, kPixels);
    return request(
        key,
        [path, image, project] {
            Result r;
            std::string err;
            if (image) {
                Image full;
                if (Image::load(path, full, err)) r.image = full.downscaled(kPixels);
                return r;
            }
            if (project && loadProjectThumbnail(path, r.image, err)) return r;
            // Meshes (and projects saved without a thumbnail): load on this worker, render on the main thread.
            Document scratch;
            if (CommandRegistry().execute(scratch, "open", {{"path", path}}).ok) r.mesh = sceneMesh(scratch);
            r.polypaint = true;
            return r;
        },
        true);
}

void ThumbnailCache::refresh(const std::string& path) {
    auto s = stats_.find(path);
    if (s == stats_.end()) return;
    const std::string kind = isProjectFile(path) ? "project" : isImageFile(path) ? "image" : "mesh";
    const std::string key = fileThumbnailKey(kind, path, s->second.size, s->second.mtime, kPixels);
    if (!cacheDir_.empty()) {
        std::error_code ec;
        std::filesystem::remove(fsPath(thumbnailCachePath(cacheDir_, key)), ec);
    }
    if (auto e = entries_.find(key); e != entries_.end() && e->second.state != State::Running) {
        if (e->second.tex) gl::DeleteTextures(1, &e->second.tex);
        entries_.erase(e);
    }
    stats_.erase(s);
}

void ThumbnailCache::upload(const std::string& key, const Image& image) {
    auto it = entries_.find(key);
    if (it == entries_.end()) return; // evicted meanwhile
    Entry& e = it->second;
    if (image.empty()) {
        e.state = State::Failed;
        return;
    }
    if (!e.tex) gl::GenTextures(1, &e.tex);
    gl::BindTexture(GL_TEXTURE_2D, e.tex);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_RGBA8), image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                   image.rgba.data());
    gl::BindTexture(GL_TEXTURE_2D, 0);
    e.state = State::Ready;
}

void ThumbnailCache::evict() {
    size_t live = 0;
    for (const auto& [k, e] : entries_)
        live += e.tex != 0;
    if (live <= kMaxTextures) return;
    std::vector<std::pair<uint64_t, std::string>> byAge;
    for (const auto& [k, e] : entries_)
        if (e.tex && e.used + 2 < frame_) byAge.emplace_back(e.used, k);
    std::sort(byAge.begin(), byAge.end());
    for (size_t i = 0; i < byAge.size() && live > kMaxTextures; ++i, --live) {
        auto it = entries_.find(byAge[i].second);
        gl::DeleteTextures(1, &it->second.tex);
        entries_.erase(it); // made again (from the disk cache) when shown again
    }
}

void ThumbnailCache::pump(Renderer& renderer) {
    ++frame_;
    // Requests not seen for a while (scrolled away, picker closed) leave the queue.
    for (auto it = queue_.begin(); it != queue_.end();) {
        auto e = entries_.find(it->first);
        if (e == entries_.end() || e->second.used + 30 < frame_) {
            if (e != entries_.end()) entries_.erase(e);
            it = queue_.erase(it);
        } else {
            ++it;
        }
    }
    while (jobs_.size() < kMaxJobs && !queue_.empty()) {
        auto [key, produce] = std::move(queue_.front());
        queue_.pop_front();
        entries_[key].state = State::Running;
        jobs_.push_back({key, std::async(std::launch::async, std::move(produce))});
    }
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        if (!ready(it->result)) {
            ++it;
            continue;
        }
        Result r = it->result.get();
        if (r.mesh) {
            if (auto e = entries_.find(it->key); e != entries_.end()) e->second.state = State::Rendering;
            renders_.emplace_back(it->key, std::move(r));
        } else {
            upload(it->key, r.image);
            if (r.store && !r.image.empty() && !cacheDir_.empty())
                writes_.push_back(
                    std::async(std::launch::async, [path = thumbnailCachePath(cacheDir_, it->key), img = r.image] {
                        std::string err;
                        img.savePng(path, err);
                    }));
        }
        it = jobs_.erase(it);
    }
    for (int n = 0; n < kRendersPerFrame && !renders_.empty(); ++n) {
        auto [key, r] = std::move(renders_.front());
        renders_.pop_front();
        Camera cam;
        cam.aspect = 1.0f;
        cam.yaw = glm::radians(r.yaw);
        cam.pitch = glm::radians(r.pitch);
        cam.frame(r.mesh->bounds());
        cam.distance *= r.zoom;
        if (r.distance > 0.0f) {
            cam.target = glm::vec3(0.0f);
            cam.distance = r.distance;
        }
        RenderSettings settings;
        settings.matcap = r.matcap;
        settings.showPolypaint = r.polypaint;
        settings.showMask = settings.showPolygroups = false;
        settings.dimInactive = false;
        Image img;
        img.width = img.height = kPixels;
        std::string err;
        if (!renderer.renderMeshThumbnail(*r.mesh, cam, kPixels, settings, img.rgba, err)) img = Image();
        upload(key, img);
        if (!r.label.empty() && !img.empty() && SDL_getenv("SCULPTFORGE_THUMBNAIL_CACHE") && !cacheDir_.empty()) {
            std::string e;
            img.savePng(cacheDir_ + "/" + r.label + ".png", e);
        }
        if (r.store && !img.empty() && !cacheDir_.empty())
            writes_.push_back(std::async(std::launch::async, [path = thumbnailCachePath(cacheDir_, key), img] {
                std::string e;
                img.savePng(path, e);
            }));
    }
    writes_.erase(std::remove_if(writes_.begin(), writes_.end(), [](const std::future<void>& f) { return ready(f); }),
                  writes_.end());
    evict();
}

} // namespace sf
