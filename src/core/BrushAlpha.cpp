// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/BrushAlpha.h"

#include "core/Commands.h"
#include "core/Sculpt.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace sf {

namespace {

constexpr int kBuiltinSize = 128;

float smooth01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

uint32_t hash2(int x, int y, uint32_t seed) {
    uint32_t h = uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u ^ seed * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

float valueNoise2(float x, float y, uint32_t seed) {
    const float fx = std::floor(x), fy = std::floor(y);
    const int ix = int(fx), iy = int(fy);
    const float tx = smooth01(x - fx), ty = smooth01(y - fy);
    auto at = [&](int dx, int dy) { return float(hash2(ix + dx, iy + dy, seed) & 0xffffu) / 65535.0f; };
    const float a = at(0, 0) + (at(1, 0) - at(0, 0)) * tx;
    const float b = at(0, 1) + (at(1, 1) - at(0, 1)) * tx;
    return a + (b - a) * ty;
}

// Fills a square alpha from f(u, v) with u, v in -1..1.
template <class F>
BrushAlpha generate(std::string name, F&& f) {
    BrushAlpha a;
    a.name = std::move(name);
    a.width = a.height = kBuiltinSize;
    a.values.resize(size_t(kBuiltinSize) * kBuiltinSize);
    for (int y = 0; y < kBuiltinSize; ++y)
        for (int x = 0; x < kBuiltinSize; ++x) {
            const float u = (float(x) + 0.5f) / kBuiltinSize * 2.0f - 1.0f;
            const float v = 1.0f - (float(y) + 0.5f) / kBuiltinSize * 2.0f;
            a.values[size_t(y) * kBuiltinSize + size_t(x)] = std::clamp(f(u, v), 0.0f, 1.0f);
        }
    return a;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

} // namespace

float BrushAlpha::sample(float u, float v) const {
    if (u < -1.0f || u > 1.0f || v < -1.0f || v > 1.0f) return 0.0f;
    if (empty()) return brushFalloff(std::sqrt(u * u + v * v), 1.0f);
    // Bilinear lookup; pixel centres span the -1..1 square.
    const float px = std::clamp((u * 0.5f + 0.5f) * float(width) - 0.5f, 0.0f, float(width - 1));
    const float py = std::clamp((0.5f - v * 0.5f) * float(height) - 0.5f, 0.0f, float(height - 1));
    const int x0 = int(px), y0 = int(py);
    const int x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
    const float tx = px - float(x0), ty = py - float(y0);
    auto at = [&](int x, int y) { return values[size_t(y) * size_t(width) + size_t(x)]; };
    const float a = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * tx;
    const float b = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * tx;
    return a + (b - a) * ty;
}

BrushAlpha BrushAlpha::round() { return BrushAlpha{}; }

BrushAlpha BrushAlpha::square() {
    // Flat plateau with soft edges over the outer fifth.
    return generate("Square", [](float u, float v) {
        return smooth01((1.0f - std::max(std::abs(u), std::abs(v))) / 0.2f);
    });
}

BrushAlpha BrushAlpha::noise(uint32_t seed) {
    // Three octaves of value noise inside the round falloff.
    return generate("Noise", [seed](float u, float v) {
        const float x = u * 4.0f + 8.0f, y = v * 4.0f + 8.0f;
        float n = valueNoise2(x, y, seed) * 0.57f + valueNoise2(x * 2.03f, y * 2.03f, seed + 1) * 0.29f +
                  valueNoise2(x * 4.01f, y * 4.01f, seed + 2) * 0.14f;
        n = std::clamp(n * 1.6f - 0.3f, 0.0f, 1.0f);
        return n * brushFalloff(std::sqrt(u * u + v * v), 1.0f);
    });
}

bool BrushAlpha::builtin(const std::string& name, BrushAlpha& out) {
    const std::string n = lower(name);
    if (n == "round" || n == "none" || n.empty()) out = round();
    else if (n == "square") out = square();
    else if (n == "noise") out = noise();
    else return false;
    return true;
}

BrushAlpha BrushAlpha::fromImage(const Image& image, std::string name, bool invert) {
    BrushAlpha a;
    a.name = std::move(name);
    if (image.empty()) return a;
    const Image img = image.downscaled(512);
    a.width = img.width;
    a.height = img.height;
    a.values.resize(size_t(img.width) * size_t(img.height));
    for (size_t i = 0; i < a.values.size(); ++i) {
        const uint8_t* p = &img.rgba[i * 4];
        float l = (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) / 255.0f;
        if (invert) l = 1.0f - l;
        a.values[i] = l * (float(p[3]) / 255.0f);
    }
    return a;
}

void registerBrushCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    registry.add({"brush_set_alpha",
                  "Set the brush alpha (grayscale stamp shaping the falloff of every brush): a built-in "
                  "(round = none, square, noise) or a grayscale image file.",
                  {{"name", T::String, "Built-in alpha: round, square or noise.", false},
                   {"path", T::String, "Image file (png, jpg, ...) used as the alpha; overrides name.", false},
                   {"invert", T::Bool, "Use the image's negative (white = no effect).", false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      const std::string path = argString(a, "path");
                      if (!path.empty()) {
                          Image img;
                          std::string err;
                          if (!Image::load(path, img, err)) return {false, err};
                          auto stem = std::filesystem::path(std::u8string(path.begin(), path.end())).stem().u8string();
                          doc.brushAlpha = BrushAlpha::fromImage(img, std::string(stem.begin(), stem.end()),
                                                                 argBool(a, "invert", false));
                          return {true, "Alpha '" + doc.brushAlpha.name + "' (" + std::to_string(doc.brushAlpha.width) +
                                            "x" + std::to_string(doc.brushAlpha.height) + ")"};
                      }
                      const std::string name = argString(a, "name", "round");
                      BrushAlpha alpha;
                      if (!BrushAlpha::builtin(name, alpha)) return {false, "Unknown alpha: " + name + " (round, square, noise)"};
                      doc.brushAlpha = std::move(alpha);
                      return {true, "Alpha '" + doc.brushAlpha.name + "'"};
                  }});
}

} // namespace sf
