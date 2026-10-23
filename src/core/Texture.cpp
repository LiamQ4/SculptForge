// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Texture.h"

#include "core/Undo.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>

namespace sf {

namespace {

// --- Colour conversion: 8-bit sRGB <-> linear float through tables ---

struct ColorTables {
    std::array<float, 256> toLinear;
    static constexpr int kSteps = 4096;
    std::array<float, kSteps + 1> toSrgb; // linear 0..1 -> sRGB 0..1
    ColorTables() {
        for (int i = 0; i < 256; ++i) {
            const float x = float(i) / 255.0f;
            toLinear[size_t(i)] = x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
        }
        for (int i = 0; i <= kSteps; ++i) {
            const float x = float(i) / kSteps;
            toSrgb[size_t(i)] = x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
        }
    }
};
const ColorTables& tables() {
    static const ColorTables t;
    return t;
}
inline float linearOf(uint8_t c) { return tables().toLinear[c]; }
inline uint8_t srgb8(float linear) {
    const float x = std::clamp(linear, 0.0f, 1.0f) * float(ColorTables::kSteps);
    const int i = std::min(int(x), ColorTables::kSteps - 1);
    const float f = x - float(i);
    const auto& t = tables().toSrgb;
    return uint8_t(std::lround((t[size_t(i)] + (t[size_t(i) + 1] - t[size_t(i)]) * f) * 255.0f));
}
inline uint8_t unit8(float x) { return uint8_t(std::lround(std::clamp(x, 0.0f, 1.0f) * 255.0f)); }
inline glm::vec3 linearRgb(const uint8_t* p) { return {linearOf(p[0]), linearOf(p[1]), linearOf(p[2])}; }
inline void putLinear(uint8_t* p, const glm::vec3& c) {
    p[0] = srgb8(c.x);
    p[1] = srgb8(c.y);
    p[2] = srgb8(c.z);
}

// Runs fn(begin, end) over rows [0, n) on several threads for big jobs.
template <class Fn>
void parallelRows(int n, size_t work, Fn fn) {
    const int threads = work < 500000 ? 1 : int(std::clamp<unsigned>(std::thread::hardware_concurrency(), 1u, 16u));
    if (threads <= 1 || n < 2) {
        fn(0, n);
        return;
    }
    std::vector<std::thread> pool;
    const int chunk = (n + threads - 1) / threads;
    for (int t = 0; t < threads; ++t) {
        const int b = t * chunk, e = std::min(n, b + chunk);
        if (b < e) pool.emplace_back([=] { fn(b, e); });
    }
    for (auto& th : pool) th.join();
}

std::atomic<uint64_t> gTextureSerial{1};

// --- Fixed-point triangle setup (1/256 texel) shared by the rasteriser, the gutter and the brush ---

constexpr int64_t kSub = 256;

struct TriSetup {
    glm::i64vec2 p[3];
    int order[3] = {0, 1, 2}; // p[i] is corner order[i] of the caller's triangle
    int64_t area = 0;         // twice the area, > 0 (counter-clockwise after setup)
    bool topLeft[3] = {};     // edge opposite p[i] (p[i+1] -> p[i+2]) owns texels exactly on it
    glm::vec2 t[3];           // corners in texel units (float), caller order
    bool valid = false;

    TriSetup(const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2, int w, int h) {
        const glm::vec2 uv[3] = {uv0, uv1, uv2};
        for (int i = 0; i < 3; ++i) {
            if (!std::isfinite(uv[i].x) || !std::isfinite(uv[i].y)) return;
            t[i] = glm::vec2(uv[i].x * float(w), uv[i].y * float(h));
            // Clamp far-out UVs so the fixed-point maths can't overflow (they rasterise outside the texture anyway).
            const double x = std::clamp(double(uv[i].x), -4.0, 5.0) * w * kSub, y = std::clamp(double(uv[i].y), -4.0, 5.0) * h * kSub;
            p[i] = glm::i64vec2(std::llround(x), std::llround(y));
        }
        area = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[1].y - p[0].y) * (p[2].x - p[0].x);
        if (area == 0) return;
        if (area < 0) {
            std::swap(p[1], p[2]);
            std::swap(order[1], order[2]);
            area = -area;
        }
        for (int i = 0; i < 3; ++i) {
            const glm::i64vec2 a = p[(i + 1) % 3], b = p[(i + 2) % 3];
            const int64_t dx = b.x - a.x, dy = b.y - a.y;
            topLeft[i] = dy < 0 || (dy == 0 && dx < 0);
        }
        valid = true;
    }
    // Edge function of the edge opposite corner i at fixed-point point (X, Y): > 0 inside.
    int64_t edge(int i, int64_t X, int64_t Y) const {
        const glm::i64vec2 a = p[(i + 1) % 3], b = p[(i + 2) % 3];
        return (b.x - a.x) * (Y - a.y) - (b.y - a.y) * (X - a.x);
    }
    // Texel range covering the corners, grown by `pad` texels and clipped to [0, w) x [0, h).
    void bounds(int w, int h, int pad, int& x0, int& y0, int& x1, int& y1) const {
        const float minX = std::min({t[0].x, t[1].x, t[2].x}), maxX = std::max({t[0].x, t[1].x, t[2].x});
        const float minY = std::min({t[0].y, t[1].y, t[2].y}), maxY = std::max({t[0].y, t[1].y, t[2].y});
        x0 = std::max(0, int(std::floor(minX - 0.5f)) - pad);
        y0 = std::max(0, int(std::floor(minY - 0.5f)) - pad);
        x1 = std::min(w - 1, int(std::ceil(maxX - 0.5f)) + pad);
        y1 = std::min(h - 1, int(std::ceil(maxY - 0.5f)) + pad);
    }
};

// Closest point of triangle (a, b, c) to p in 2D, as barycentric weights (Ericson, Real-Time Collision Detection).
glm::vec3 closestBarycentric(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
    const glm::vec2 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return {1, 0, 0};
    const glm::vec2 bp = p - b;
    const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return {0, 1, 0};
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / (d1 - d3);
        return {1 - v, v, 0};
    }
    const glm::vec2 cp = p - c;
    const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return {0, 0, 1};
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / (d2 - d6);
        return {1 - w, 0, w};
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return {0, 1 - w, w};
    }
    const float denom = 1.0f / (va + vb + vc);
    const float v = vb * denom, w = vc * denom;
    return {1 - v - w, v, w};
}

// Visits the texels of one UV triangle inside [cx0, cx1] x [cy0, cy1]: fn(x, y, bary, inside). Inside texels
// follow the exact top-left rule; with pad > 0 the texels within `pad` texels of the triangle that no UV
// triangle covers (tex->coverage) are visited too, with the barycentrics of the nearest triangle point.
template <class Fn>
void visitTriangle(const TriSetup& s, int w, int h, int pad, const PaintTexture* cover, int cx0, int cy0, int cx1, int cy1,
                   Fn&& fn) {
    if (!s.valid) return;
    int x0, y0, x1, y1;
    s.bounds(w, h, pad, x0, y0, x1, y1);
    x0 = std::max(x0, cx0);
    y0 = std::max(y0, cy0);
    x1 = std::min(x1, cx1);
    y1 = std::min(y1, cy1);
    if (x0 > x1 || y0 > y1) return;
    const double inv = 1.0 / double(s.area);
    const float pad2 = float(pad) * float(pad);
    for (int y = y0; y <= y1; ++y) {
        const int64_t Y = int64_t(y) * kSub + kSub / 2;
        for (int x = x0; x <= x1; ++x) {
            const int64_t X = int64_t(x) * kSub + kSub / 2;
            int64_t e[3];
            bool inside = true;
            for (int i = 0; i < 3 && inside; ++i) {
                e[i] = s.edge(i, X, Y);
                inside = e[i] > 0 || (e[i] == 0 && s.topLeft[i]);
            }
            if (inside) {
                float b[3];
                for (int i = 0; i < 3; ++i) b[s.order[i]] = float(double(s.edge(i, X, Y)) * inv);
                fn(x, y, glm::vec3(b[0], b[1], b[2]), true);
                continue;
            }
            if (pad <= 0 || (cover && cover->covered(x, y))) continue;
            const glm::vec2 c(float(x) + 0.5f, float(y) + 0.5f);
            const glm::vec3 b = closestBarycentric(c, s.t[0], s.t[1], s.t[2]);
            const glm::vec2 q = s.t[0] * b.x + s.t[1] * b.y + s.t[2] * b.z;
            const glm::vec2 d = q - c;
            if (glm::dot(d, d) > pad2) continue;
            fn(x, y, b, false);
        }
    }
}

uint64_t uvFingerprint(const Mesh& m) {
    uint64_t h = 1469598103934665603ull ^ (uint64_t(m.uvs.size()) * 0x9E3779B97F4A7C15ull);
    if (m.uvs.empty()) return h | 1;
    const size_t n = m.uvs.size(), step = std::max<size_t>(1, n / 4096);
    for (size_t i = 0; i < n; i += step) {
        uint32_t bits[2];
        std::memcpy(bits, &m.uvs[i], sizeof bits);
        h = (h ^ bits[0]) * 1099511628211ull;
        h = (h ^ bits[1]) * 1099511628211ull;
    }
    return h | 1; // never 0 ("no coverage")
}

bool validTexLayer(const PaintTexture& t, int layer) { return layer >= -1 && layer < int(t.layers.size()); }

void ensureStack(PaintTexture& t) {
    if (!t.layers.empty()) return;
    t.base = t.pixels;
    t.activeLayer = -1;
}

void dropStackIfEmpty(PaintTexture& t) {
    if (!t.layers.empty()) return;
    if (t.base.size() == t.pixels.size()) t.pixels = t.base;
    std::vector<uint8_t>().swap(t.base);
    t.activeLayer = -1;
    t.markAllDirty();
}

// One texel of the composite: Base and the visible layers in display (sRGB) space, as compositePaintVertex.
inline void compositeTexel(const PaintTexture& t, size_t i, uint8_t* out) {
    const uint8_t* b = &t.base[i * 4];
    glm::vec3 c(0.0f);
    bool blended = false;
    for (const TextureLayer& l : t.layers) {
        if (!l.visible) continue;
        const uint8_t* s = &l.rgba[i * 4];
        const float a = std::min(float(s[3]) / 255.0f * l.opacity, 1.0f);
        if (a <= 0.0f) continue;
        if (!blended) c = glm::vec3(b[0], b[1], b[2]) / 255.0f;
        blended = true;
        const glm::vec3 src = glm::vec3(s[0], s[1], s[2]) / 255.0f;
        c = glm::mix(c, glm::clamp(blendColors(l.blend, c, src), 0.0f, 1.0f), a);
    }
    if (!blended) {
        out[0] = b[0], out[1] = b[1], out[2] = b[2];
    } else {
        out[0] = unit8(c.x), out[1] = unit8(c.y), out[2] = unit8(c.z);
    }
    out[3] = 255;
}

// Writes paint of weight `w` into texel `px` of the target (Base / plain when `layer` < 0), starting from `orig`.
inline void paintTexel(uint8_t* px, const uint8_t* orig, bool layer, const glm::vec3& color, float w, bool erase) {
    if (!layer) {
        putLinear(px, glm::mix(linearRgb(orig), erase ? glm::vec3(1.0f) : color, w));
        px[3] = 255;
        return;
    }
    const float a = float(orig[3]) / 255.0f;
    if (erase) {
        std::memcpy(px, orig, 3);
        px[3] = unit8(a * (1.0f - w));
        return;
    }
    // "Source over" onto the layer's own coverage (as paintVertex).
    const float na = a + w * (1.0f - a);
    putLinear(px, na > 0.0f ? (linearRgb(orig) * (a * (1.0f - w)) + color * w) / na : color);
    px[3] = unit8(na);
}

} // namespace

// --- PaintTexture ---

bool validTextureSize(int size) { return size == 1024 || size == 2048 || size == 4096 || size == 8192; }

void PaintTexture::markDirty(int x0, int y0, int x1, int y1) {
    x0 = std::max(x0, 0), y0 = std::max(y0, 0), x1 = std::min(x1, width), y1 = std::min(y1, height);
    if (x0 >= x1 || y0 >= y1) return;
    if (dirtyMin.x >= dirtyMax.x) {
        dirtyMin = {x0, y0};
        dirtyMax = {x1, y1};
    } else {
        dirtyMin = glm::min(dirtyMin, glm::ivec2(x0, y0));
        dirtyMax = glm::max(dirtyMax, glm::ivec2(x1, y1));
    }
    ++revision;
}

void PaintTexture::markAllDirty() {
    ++revision;
    dirtyFrom = revision; // older uploads are stale: views re-upload everything
    dirtyMin = {0, 0};
    dirtyMax = {width, height};
}

std::vector<uint8_t>& PaintTexture::target(int layer) {
    if (layers.empty()) return pixels;
    if (layer < 0 || layer >= int(layers.size())) return base;
    return layers[size_t(layer)].rgba;
}

size_t PaintTexture::bytes() const {
    size_t n = pixels.size() + base.size() + coverage.size() + sizeof(PaintTexture);
    for (const auto& l : layers) n += l.rgba.size() + sizeof(TextureLayer);
    return n;
}

std::shared_ptr<PaintTexture> makeTexture(int width, int height) {
    auto t = std::make_shared<PaintTexture>();
    t->width = std::max(width, 1);
    t->height = std::max(height, 1);
    t->serial = gTextureSerial++;
    t->pixels.assign(t->texelCount() * 4, 255);
    t->markAllDirty();
    return t;
}

std::shared_ptr<PaintTexture> cloneTexture(const PaintTexture& tex) {
    auto t = std::make_shared<PaintTexture>(tex);
    t->serial = gTextureSerial++;
    t->markAllDirty();
    return t;
}

bool hasTexture(const Mesh& m) {
    return m.texture && m.texture->width > 0 && m.hasUvs() && m.uvs.size() == m.triangleCount() * 3 &&
           m.texture->pixels.size() == m.texture->texelCount() * 4;
}

bool paintsTexture(const Mesh& m) { return m.paintToTexture && hasTexture(m); }

PaintTexture& mutableTexture(Mesh& m) {
    if (m.texture.use_count() > 1) m.texture = cloneTexture(*m.texture);
    return *m.texture;
}

void ensureCoverage(const Mesh& m, PaintTexture& tex, bool force) {
    const uint64_t fp = uvFingerprint(m);
    if (!force && tex.coverageUvs == fp && tex.coverage.size() == (tex.texelCount() + 7) / 8) return;
    tex.coverage.assign((tex.texelCount() + 7) / 8, 0);
    tex.coverageUvs = fp;
    if (m.uvs.size() != m.triangleCount() * 3) return;
    const int w = tex.width, h = tex.height;
    for (size_t t = 0; t < m.triangleCount(); ++t) {
        const TriSetup s(m.uvs[t * 3], m.uvs[t * 3 + 1], m.uvs[t * 3 + 2], w, h);
        visitTriangle(s, w, h, 0, nullptr, 0, 0, w - 1, h - 1, [&](int x, int y, const glm::vec3&, bool) {
            const size_t i = size_t(y) * size_t(w) + size_t(x);
            tex.coverage[i >> 3] |= uint8_t(1u << (i & 7));
        });
    }
}

void rasterizeUvTriangle(const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2, int width, int height,
                         const TexelFn& fn) {
    if (width <= 0 || height <= 0) return;
    const TriSetup s(uv0, uv1, uv2, width, height);
    visitTriangle(s, width, height, 0, nullptr, 0, 0, width - 1, height - 1,
                  [&](int x, int y, const glm::vec3& b, bool) { fn(x, y, b.x, b.y, b.z); });
}

// --- Compositing and sampling ---

void compositeTexture(PaintTexture& t, int x0, int y0, int x1, int y1) {
    x0 = std::max(x0, 0), y0 = std::max(y0, 0), x1 = std::min(x1, t.width), y1 = std::min(y1, t.height);
    if (x0 >= x1 || y0 >= y1) return;
    if (!t.layers.empty() && t.base.size() == t.pixels.size()) {
        parallelRows(y1 - y0, size_t(x1 - x0) * size_t(y1 - y0) * (t.layers.size() + 1), [&](int b, int e) {
            for (int y = y0 + b; y < y0 + e; ++y)
                for (int x = x0; x < x1; ++x) {
                    const size_t i = size_t(y) * size_t(t.width) + size_t(x);
                    compositeTexel(t, i, &t.pixels[i * 4]);
                }
        });
    }
    t.markDirty(x0, y0, x1, y1);
}

void compositeTextureAll(PaintTexture& t) {
    compositeTexture(t, 0, 0, t.width, t.height);
    t.markAllDirty();
}

glm::vec3 sampleTexture(const PaintTexture& t, const glm::vec2& uv) {
    if (t.width <= 0 || t.pixels.size() != t.texelCount() * 4) return glm::vec3(1.0f);
    const float fx = std::clamp(uv.x, 0.0f, 1.0f) * float(t.width) - 0.5f;
    const float fy = std::clamp(uv.y, 0.0f, 1.0f) * float(t.height) - 0.5f;
    const int x0 = std::clamp(int(std::floor(fx)), 0, t.width - 1), y0 = std::clamp(int(std::floor(fy)), 0, t.height - 1);
    const int x1 = std::min(x0 + 1, t.width - 1), y1 = std::min(y0 + 1, t.height - 1);
    const float ax = std::clamp(fx - float(x0), 0.0f, 1.0f), ay = std::clamp(fy - float(y0), 0.0f, 1.0f);
    auto at = [&](int x, int y) { return linearRgb(&t.pixels[(size_t(y) * size_t(t.width) + size_t(x)) * 4]); };
    return glm::mix(glm::mix(at(x0, y0), at(x1, y0), ax), glm::mix(at(x0, y1), at(x1, y1), ax), ay);
}

glm::vec3 sampleTextureAt(const Mesh& m, uint32_t tri, const glm::vec3& point) {
    if (!hasTexture(m) || tri >= m.triangleCount()) return glm::vec3(1.0f);
    const glm::uvec3& t = m.triangles[tri];
    const glm::vec3 a = m.positions[t.x], b = m.positions[t.y], c = m.positions[t.z];
    const glm::vec3 v0 = b - a, v1 = c - a, v2 = point - a;
    const float d00 = glm::dot(v0, v0), d01 = glm::dot(v0, v1), d11 = glm::dot(v1, v1);
    const float d20 = glm::dot(v2, v0), d21 = glm::dot(v2, v1);
    const float den = d00 * d11 - d01 * d01;
    float v = 0.0f, w = 0.0f;
    if (std::abs(den) > 1e-20f) {
        v = (d11 * d20 - d01 * d21) / den;
        w = (d00 * d21 - d01 * d20) / den;
    }
    const glm::vec2 uv = m.uvs[size_t(tri) * 3] * (1.0f - v - w) + m.uvs[size_t(tri) * 3 + 1] * v + m.uvs[size_t(tri) * 3 + 2] * w;
    return sampleTexture(*m.texture, uv);
}

// --- Brush ---

bool TexturePainter::begin(Mesh& mesh) {
    active_ = false;
    tiles_.clear();
    tileSlot_.clear();
    dab_ = 0;
    if (!paintsTexture(mesh)) return false;
    const PaintTexture& t = *mesh.texture;
    layer_ = t.layers.empty() ? -1 : std::clamp(t.activeLayer, -1, int(t.layers.size()) - 1);
    if (!textureLayerEditable(t, layer_)) return false;
    PaintTexture& tex = mutableTexture(mesh);
    ensureCoverage(mesh, tex);
    tilesX_ = (tex.width + kTextureTile - 1) / kTextureTile;
    tilesY_ = (tex.height + kTextureTile - 1) / kTextureTile;
    tileSlot_.assign(size_t(tilesX_) * size_t(tilesY_), -1);
    active_ = true;
    return true;
}

TexturePainter::Tile& TexturePainter::tile(uint32_t index, const std::vector<uint8_t>& target, int width, int height) {
    int32_t& slot = tileSlot_[index];
    if (slot >= 0) return tiles_[size_t(slot)];
    slot = int32_t(tiles_.size());
    Tile& t = tiles_.emplace_back();
    t.index = index;
    constexpr size_t n = size_t(kTextureTile) * kTextureTile;
    t.original.assign(n * 4, 0);
    t.coverage.assign(n, 0.0f);
    t.stamp.assign(n, 0);
    const int tx = int(index % uint32_t(tilesX_)) * kTextureTile, ty = int(index / uint32_t(tilesX_)) * kTextureTile;
    const int cw = std::min(kTextureTile, width - tx), ch = std::min(kTextureTile, height - ty);
    for (int y = 0; y < ch; ++y)
        std::memcpy(&t.original[size_t(y) * kTextureTile * 4], &target[(size_t(ty + y) * size_t(width) + size_t(tx)) * 4],
                    size_t(cw) * 4);
    return t;
}

size_t TexturePainter::dab(Mesh& mesh, const BVH& bvh, const DabParams& p, const glm::vec3& color, Symmetry sym) {
    if (!active_ || p.radius <= 0.0f || !hasTexture(mesh)) return 0;
    PaintTexture& tex = mutableTexture(mesh);
    if (!textureLayerEditable(tex, layer_) || (layer_ >= 0 && layer_ >= int(tex.layers.size()))) return 0;
    std::vector<uint8_t>& target = tex.target(layer_);
    const bool onLayer = layer_ >= 0;
    const int w = tex.width, h = tex.height;
    const float s = std::clamp(p.strength, 0.0f, 1.0f) * std::clamp(p.pressure, 0.0f, 1.0f);
    if (s <= 0.0f) return 0;
    const bool useAlpha = p.alpha && !p.alpha->empty();
    const bool masked = mesh.hasMask();
    const bool normals = mesh.normals.size() == mesh.vertexCount();
    glm::ivec2 changedMin(w, h), changedMax(0, 0);
    size_t changed = 0;

    for (const glm::vec3& m : Sculptor::mirrors(sym)) {
        const glm::vec3 center = p.center * m, viewDir = p.viewDir * m;
        // The alpha's frame on the view plane (as recipe brushes orient it).
        glm::vec3 aUp = p.alphaUp * m;
        aUp -= viewDir * glm::dot(aUp, viewDir);
        aUp = glm::length(aUp) > 1e-6f ? glm::normalize(aUp) : glm::vec3(0, 1, 0);
        const glm::vec3 aRight = glm::cross(aUp, -viewDir);
        const float r = p.radius, r2 = r * r;
        ++dab_;
        bvh.queryTriangles(mesh, center, r, tris_);
        for (uint32_t ti : tris_) {
            if (mesh.triangleHidden(ti)) continue;
            const glm::uvec3& tv = mesh.triangles[ti];
            const TriSetup su(mesh.uvs[size_t(ti) * 3], mesh.uvs[size_t(ti) * 3 + 1], mesh.uvs[size_t(ti) * 3 + 2], w, h);
            if (!su.valid) continue;
            const glm::vec3 P0 = mesh.positions[tv.x], P1 = mesh.positions[tv.y], P2 = mesh.positions[tv.z];
            // Only the texels whose 3D point can be inside the sphere: the affine texel -> 3D map turns the
            // sphere into an ellipse in texel space; clip to its bounding box.
            const glm::vec2 e1 = su.t[1] - su.t[0], e2 = su.t[2] - su.t[0];
            const float det = e1.x * e2.y - e1.y * e2.x;
            if (std::abs(det) < 1e-12f) continue;
            // A = [P1-P0, P2-P0] * inverse([e1 e2]): columns are d(3D)/dx and d(3D)/dy.
            const glm::vec3 q1 = P1 - P0, q2 = P2 - P0;
            const glm::vec3 Ax = (q1 * e2.y - q2 * e1.y) / det, Ay = (q2 * e1.x - q1 * e2.x) / det;
            const float mxx = glm::dot(Ax, Ax), mxy = glm::dot(Ax, Ay), myy = glm::dot(Ay, Ay);
            const float mdet = mxx * myy - mxy * mxy;
            int cx0 = 0, cy0 = 0, cx1 = w - 1, cy1 = h - 1;
            if (mdet > 1e-30f) {
                const glm::vec3 rel = center - P0;
                const float bx = glm::dot(Ax, rel), by = glm::dot(Ay, rel);
                const float dx = (myy * bx - mxy * by) / mdet, dy = (mxx * by - mxy * bx) / mdet;
                const glm::vec3 closest = P0 + Ax * dx + Ay * dy;
                const float k = r2 - glm::dot(closest - center, closest - center);
                if (k < 0.0f) continue;
                const float hx = std::sqrt(k * myy / mdet), hy = std::sqrt(k * mxx / mdet);
                const glm::vec2 c = su.t[0] + glm::vec2(dx, dy);
                cx0 = int(std::floor(c.x - hx)) - kTextureGutter - 1;
                cx1 = int(std::ceil(c.x + hx)) + kTextureGutter + 1;
                cy0 = int(std::floor(c.y - hy)) - kTextureGutter - 1;
                cy1 = int(std::ceil(c.y + hy)) + kTextureGutter + 1;
            }
            const float m0 = masked ? mesh.mask[tv.x] : 0.0f, m1 = masked ? mesh.mask[tv.y] : 0.0f,
                        m2 = masked ? mesh.mask[tv.z] : 0.0f;
            const glm::vec3 N0 = normals ? mesh.normals[tv.x] : glm::vec3(0), N1 = normals ? mesh.normals[tv.y] : glm::vec3(0),
                            N2 = normals ? mesh.normals[tv.z] : glm::vec3(0);
            const glm::vec3 faceN = glm::cross(q1, q2);
            visitTriangle(su, w, h, kTextureGutter, &tex, cx0, cy0, cx1, cy1, [&](int x, int y, const glm::vec3& b, bool) {
                const glm::vec3 P = P0 * b.x + P1 * b.y + P2 * b.z;
                const glm::vec3 o = P - center;
                const float d2 = glm::dot(o, o);
                if (d2 >= r2) return;
                // Front faces only: never paint through to the far side.
                const glm::vec3 N = normals ? N0 * b.x + N1 * b.y + N2 * b.z : faceN;
                if (glm::dot(N, viewDir) > 0.0f) return;
                const float f = useAlpha ? p.alpha->sample(glm::dot(o, aRight) / r, glm::dot(o, aUp) / r)
                                         : brushFalloff(std::sqrt(d2), r);
                const float wgt = f * s * (1.0f - std::clamp(m0 * b.x + m1 * b.y + m2 * b.z, 0.0f, 1.0f));
                if (wgt <= 0.0f) return;
                const uint32_t ti2 = uint32_t(y / kTextureTile) * uint32_t(tilesX_) + uint32_t(x / kTextureTile);
                Tile& t = tile(ti2, target, w, h);
                const size_t li = size_t(y % kTextureTile) * kTextureTile + size_t(x % kTextureTile);
                if (t.stamp[li] == dab_) return; // one write per texel per dab (shared edges, overlapping UVs)
                t.stamp[li] = dab_;
                float& acc = t.coverage[li];
                acc += std::min(wgt, 1.0f) * (1.0f - acc);
                paintTexel(&target[(size_t(y) * size_t(w) + size_t(x)) * 4], &t.original[li * 4], onLayer, color, acc, p.invert);
                changedMin = glm::min(changedMin, glm::ivec2(x, y));
                changedMax = glm::max(changedMax, glm::ivec2(x + 1, y + 1));
                ++changed;
            });
        }
    }
    if (changed) {
        if (!tex.layers.empty()) compositeTexture(tex, changedMin.x, changedMin.y, changedMax.x, changedMax.y);
        else tex.markDirty(changedMin.x, changedMin.y, changedMax.x, changedMax.y);
    }
    return changed;
}

void TexturePainter::end(UndoRecord& record) {
    if (active_ && !tiles_.empty()) {
        auto u = std::make_unique<TextureTileUndo>();
        u->layer = layer_;
        std::sort(tiles_.begin(), tiles_.end(), [](const Tile& a, const Tile& b) { return a.index < b.index; });
        constexpr size_t n = size_t(kTextureTile) * kTextureTile * 4;
        u->tiles.reserve(tiles_.size());
        u->data.resize(tiles_.size() * n);
        for (size_t i = 0; i < tiles_.size(); ++i) {
            u->tiles.push_back(tiles_[i].index);
            std::memcpy(&u->data[i * n], tiles_[i].original.data(), n);
        }
        record.textureTiles = std::move(u);
    }
    active_ = false;
    tiles_.clear();
    tileSlot_.clear();
    std::vector<uint32_t>().swap(tris_);
}

void TextureTileUndo::swapWith(Mesh& mesh) {
    if (!mesh.texture) return;
    PaintTexture& t = mutableTexture(mesh);
    if (!validTexLayer(t, layer) || (layer >= 0 && t.layers.empty())) return;
    std::vector<uint8_t>& target = t.target(layer);
    if (target.size() != t.texelCount() * 4) return;
    const int tilesX = (t.width + kTextureTile - 1) / kTextureTile;
    constexpr size_t n = size_t(kTextureTile) * kTextureTile * 4;
    for (size_t i = 0; i < tiles.size(); ++i) {
        const int tx = int(tiles[i] % uint32_t(tilesX)) * kTextureTile, ty = int(tiles[i] / uint32_t(tilesX)) * kTextureTile;
        const int cw = std::min(kTextureTile, t.width - tx), ch = std::min(kTextureTile, t.height - ty);
        if (cw <= 0 || ch <= 0) continue;
        uint8_t* saved = &data[i * n];
        for (int y = 0; y < ch; ++y)
            std::swap_ranges(saved + size_t(y) * kTextureTile * 4, saved + size_t(y) * kTextureTile * 4 + size_t(cw) * 4,
                             &target[(size_t(ty + y) * size_t(t.width) + size_t(tx)) * 4]);
        if (!t.layers.empty()) compositeTexture(t, tx, ty, tx + cw, ty + ch);
        else t.markDirty(tx, ty, tx + cw, ty + ch);
    }
}

void TextureStackUndo::swapWith(Mesh& mesh) {
    std::swap(texture, mesh.texture);
    std::swap(paintToTexture, mesh.paintToTexture);
    mesh.facesDirty = true; // the renderer keeps per-triangle UVs only while there is a texture
}

size_t TextureStackUndo::bytes() const { return texture ? texture->bytes() : 0; }

UndoRecord captureTexture(Mesh& mesh, std::string label) {
    UndoRecord r;
    r.label = std::move(label);
    r.texture = std::make_unique<TextureStackUndo>();
    r.texture->texture = mesh.texture;
    r.texture->paintToTexture = mesh.paintToTexture;
    if (mesh.texture) mesh.texture = cloneTexture(*mesh.texture);
    return r;
}

// --- Whole-texture operations ---

void bakeTexels(const Mesh& m, PaintTexture& tex, const BakeFn& fn, bool skipHidden) {
    if (m.uvs.size() != m.triangleCount() * 3 || tex.width <= 0) return;
    ensureCoverage(m, tex);
    const int w = tex.width, h = tex.height;
    std::vector<uint8_t> written((tex.texelCount() + 7) / 8, 0);
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        if (skipHidden && m.triangleHidden(t)) continue;
        const TriSetup s(m.uvs[size_t(t) * 3], m.uvs[size_t(t) * 3 + 1], m.uvs[size_t(t) * 3 + 2], w, h);
        visitTriangle(s, w, h, kTextureGutter, &tex, 0, 0, w - 1, h - 1, [&](int x, int y, const glm::vec3& b, bool) {
            const size_t i = size_t(y) * size_t(w) + size_t(x);
            if (written[i >> 3] & (1u << (i & 7))) return;
            written[i >> 3] |= uint8_t(1u << (i & 7));
            fn(t, b.x, b.y, b.z, x, y);
        });
    }
}

size_t textureFill(Mesh& m, const glm::vec3& color, const std::vector<float>& vw) {
    if (!hasTexture(m) || vw.size() != m.vertexCount()) return 0;
    PaintTexture& tex = mutableTexture(m);
    const int layer = tex.layers.empty() ? -1 : tex.activeLayer;
    if (!textureLayerEditable(tex, layer)) return 0;
    std::vector<uint8_t>& target = tex.target(layer);
    const bool onLayer = layer >= 0;
    size_t changed = 0;
    bakeTexels(m, tex, [&](uint32_t t, float b0, float b1, float b2, int x, int y) {
        const glm::uvec3& tv = m.triangles[t];
        const float wgt = std::clamp(vw[tv.x] * b0 + vw[tv.y] * b1 + vw[tv.z] * b2, 0.0f, 1.0f);
        if (wgt <= 0.0f) return;
        uint8_t* px = &target[(size_t(y) * size_t(tex.width) + size_t(x)) * 4];
        uint8_t orig[4];
        std::memcpy(orig, px, 4);
        paintTexel(px, orig, onLayer, color, wgt, false);
        ++changed;
    });
    compositeTextureAll(tex);
    return changed;
}

void polypaintToTexture(Mesh& m) {
    if (!hasTexture(m)) return;
    PaintTexture& tex = mutableTexture(m);
    const int layer = tex.layers.empty() ? -1 : tex.activeLayer;
    std::vector<uint8_t>& target = tex.target(layer);
    bakeTexels(m, tex, [&](uint32_t t, float b0, float b1, float b2, int x, int y) {
        const glm::uvec3& tv = m.triangles[t];
        const glm::vec3 c = m.colorAt(tv.x) * b0 + m.colorAt(tv.y) * b1 + m.colorAt(tv.z) * b2;
        uint8_t* px = &target[(size_t(y) * size_t(tex.width) + size_t(x)) * 4];
        putLinear(px, c);
        px[3] = 255;
    }, false);
    compositeTextureAll(tex);
}

void textureToPolypaint(Mesh& m) {
    if (!hasTexture(m)) return;
    const size_t nv = m.vertexCount();
    std::vector<glm::vec3> sum(nv, glm::vec3(0.0f));
    std::vector<uint32_t> count(nv, 0);
    for (size_t t = 0; t < m.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) {
            const uint32_t v = m.triangles[t][c];
            sum[v] += sampleTexture(*m.texture, m.uvs[t * 3 + size_t(c)]);
            ++count[v];
        }
    m.ensureColors();
    for (uint32_t v = 0; v < nv; ++v)
        if (count[v]) paintVertex(m, v, sum[v] / float(count[v]), 1.0f);
    m.markAllDirty();
}

Image textureImage(const PaintTexture& t) {
    Image img;
    if (t.width <= 0 || t.pixels.size() != t.texelCount() * 4) return img;
    img.width = t.width;
    img.height = t.height;
    img.rgba.resize(t.pixels.size());
    const size_t row = size_t(t.width) * 4;
    for (int y = 0; y < t.height; ++y) // bottom-up texture rows -> top-down image rows
        std::memcpy(&img.rgba[size_t(t.height - 1 - y) * row], &t.pixels[size_t(y) * row], row);
    return img;
}

bool loadTextureImage(PaintTexture& t, const Image& img, std::string& error) {
    if (img.empty() || img.rgba.size() != size_t(img.width) * size_t(img.height) * 4) {
        error = "The image is empty";
        return false;
    }
    if (img.width > 16384 || img.height > 16384) {
        error = "The image is larger than 16384 pixels";
        return false;
    }
    t.width = img.width;
    t.height = img.height;
    t.layers.clear();
    t.base.clear();
    t.activeLayer = -1;
    t.coverage.clear();
    t.coverageUvs = 0;
    t.pixels.resize(img.rgba.size());
    const size_t row = size_t(t.width) * 4;
    for (int y = 0; y < t.height; ++y) std::memcpy(&t.pixels[size_t(y) * row], &img.rgba[size_t(t.height - 1 - y) * row], row);
    for (size_t i = 3; i < t.pixels.size(); i += 4) t.pixels[i] = 255; // the paint is opaque
    t.markAllDirty();
    return true;
}

// --- Layer stack ---

bool textureLayerEditable(const PaintTexture& t, int layer) {
    if (layer == -1) return true;
    if (!validTexLayer(t, layer)) return false;
    const TextureLayer& l = t.layers[size_t(layer)];
    return !l.locked && l.visible;
}

std::string uniqueTextureLayerName(const PaintTexture& t, const std::string& wanted) {
    const std::string stem = wanted.empty() ? "Layer" : wanted;
    auto used = [&](const std::string& n) { return findTextureLayer(t, n) != -2; };
    if (!wanted.empty() && !used(stem)) return stem;
    for (int i = 2;; ++i) {
        std::string n = stem + " " + std::to_string(i);
        if (!used(n)) return n;
    }
}

int findTextureLayer(const PaintTexture& t, const std::string& name) {
    for (size_t i = 0; i < t.layers.size(); ++i)
        if (t.layers[i].name == name) return int(i);
    return name == "Base" ? -1 : -2;
}

int addTextureLayer(PaintTexture& t, std::string name, int above) {
    ensureStack(t);
    TextureLayer l;
    l.name = uniqueTextureLayerName(t, name.empty() ? "Layer " + std::to_string(t.layers.size() + 1) : name);
    l.rgba.resize(t.texelCount() * 4);
    for (size_t i = 0; i < l.rgba.size(); i += 4) l.rgba[i] = l.rgba[i + 1] = l.rgba[i + 2] = 255, l.rgba[i + 3] = 0;
    if (above == -2) above = int(t.layers.size()) - 1;
    above = std::clamp(above, -1, int(t.layers.size()) - 1);
    t.layers.insert(t.layers.begin() + (above + 1), std::move(l));
    t.activeLayer = above + 1;
    return above + 1;
}

int duplicateTextureLayer(PaintTexture& t, int layer) {
    if (!validTexLayer(t, layer)) return -2;
    ensureStack(t);
    TextureLayer copy;
    if (layer < 0) {
        copy.name = "Base";
        copy.rgba = t.base;
        for (size_t i = 3; i < copy.rgba.size(); i += 4) copy.rgba[i] = 255;
    } else {
        copy = t.layers[size_t(layer)];
        copy.locked = false;
    }
    copy.name = uniqueTextureLayerName(t, copy.name + " copy");
    t.layers.insert(t.layers.begin() + (layer + 1), std::move(copy));
    t.activeLayer = layer + 1;
    compositeTextureAll(t);
    return layer + 1;
}

bool removeTextureLayer(PaintTexture& t, int layer) {
    if (layer < 0 || layer >= int(t.layers.size())) return false;
    t.layers.erase(t.layers.begin() + layer);
    t.activeLayer = std::max(std::min(layer, int(t.layers.size()) - 1), -1);
    compositeTextureAll(t);
    dropStackIfEmpty(t);
    return true;
}

bool moveTextureLayer(PaintTexture& t, int layer, int direction) {
    const int to = layer + (direction > 0 ? 1 : -1);
    if (layer < 0 || layer >= int(t.layers.size()) || to < 0 || to >= int(t.layers.size())) return false;
    std::swap(t.layers[size_t(layer)], t.layers[size_t(to)]);
    if (t.activeLayer == layer) t.activeLayer = to;
    else if (t.activeLayer == to) t.activeLayer = layer;
    compositeTextureAll(t);
    return true;
}

bool mergeTextureLayerDown(PaintTexture& t, int layer, std::string& error) {
    if (layer < 0 || layer >= int(t.layers.size())) {
        error = "Choose a layer above Base to merge down";
        return false;
    }
    TextureLayer& up = t.layers[size_t(layer)];
    if (up.locked) {
        error = "Layer '" + up.name + "' is locked";
        return false;
    }
    const size_t n = t.texelCount();
    if (layer == 0) {
        if (up.visible)
            parallelRows(t.height, n, [&](int b, int e) {
                for (size_t i = size_t(b) * size_t(t.width); i < size_t(e) * size_t(t.width); ++i) {
                    const uint8_t* s = &up.rgba[i * 4];
                    const float a = std::min(float(s[3]) / 255.0f * up.opacity, 1.0f);
                    if (a <= 0.0f) continue;
                    uint8_t* d = &t.base[i * 4];
                    const glm::vec3 c = glm::vec3(d[0], d[1], d[2]) / 255.0f, src = glm::vec3(s[0], s[1], s[2]) / 255.0f;
                    const glm::vec3 o = glm::mix(c, glm::clamp(blendColors(up.blend, c, src), 0.0f, 1.0f), a);
                    d[0] = unit8(o.x), d[1] = unit8(o.y), d[2] = unit8(o.z);
                }
            });
    } else {
        TextureLayer& low = t.layers[size_t(layer - 1)];
        if (low.locked) {
            error = "Layer '" + low.name + "' is locked";
            return false;
        }
        if (!low.visible) {
            error = "Show layer '" + low.name + "' before merging into it";
            return false;
        }
        parallelRows(t.height, n, [&](int b, int e) {
            for (size_t i = size_t(b) * size_t(t.width); i < size_t(e) * size_t(t.width); ++i) {
                uint8_t* d = &low.rgba[i * 4];
                const uint8_t* s = &up.rgba[i * 4];
                const float ab = std::min(float(d[3]) / 255.0f * low.opacity, 1.0f);
                const float as = up.visible ? std::min(float(s[3]) / 255.0f * up.opacity, 1.0f) : 0.0f;
                if (as <= 0.0f) {
                    d[3] = unit8(ab);
                    continue;
                }
                const glm::vec3 cb = glm::vec3(d[0], d[1], d[2]) / 255.0f, cs = glm::vec3(s[0], s[1], s[2]) / 255.0f;
                const glm::vec3 mixed = glm::clamp(blendColors(up.blend, cb, cs), 0.0f, 1.0f);
                const float ao = as + ab * (1.0f - as);
                const glm::vec3 co = (as * (1.0f - ab) * cs + as * ab * mixed + (1.0f - as) * ab * cb) / ao;
                d[0] = unit8(co.x), d[1] = unit8(co.y), d[2] = unit8(co.z), d[3] = unit8(ao);
            }
        });
        low.opacity = 1.0f;
    }
    t.layers.erase(t.layers.begin() + layer);
    t.activeLayer = layer - 1;
    compositeTextureAll(t);
    dropStackIfEmpty(t);
    return true;
}

void flattenTextureLayers(PaintTexture& t) {
    if (t.layers.empty()) return;
    compositeTextureAll(t);
    t.layers.clear();
    t.base = t.pixels;
    dropStackIfEmpty(t);
}

void clearTextureLayer(PaintTexture& t, int layer) {
    if (t.layers.empty()) {
        std::fill(t.pixels.begin(), t.pixels.end(), uint8_t(255));
        t.markAllDirty();
        return;
    }
    if (layer < 0) std::fill(t.base.begin(), t.base.end(), uint8_t(255));
    else if (layer < int(t.layers.size())) {
        std::vector<uint8_t>& px = t.layers[size_t(layer)].rgba;
        for (size_t i = 0; i < px.size(); i += 4) px[i] = px[i + 1] = px[i + 2] = 255, px[i + 3] = 0;
    }
    compositeTextureAll(t);
}

} // namespace sf
