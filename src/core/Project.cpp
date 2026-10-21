// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Project.h"

#include "io/Zip.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <map>
#include <system_error>

#ifndef SCULPTFORGE_VERSION
#define SCULPTFORGE_VERSION "unknown"
#endif

namespace sf {

namespace {

constexpr const char* kFormatName = "SculptForge project";
constexpr const char* kManifestName = "manifest.json";
// Top-level manifest keys owned by the core; every other key belongs to
// Document::projectSettings (camera, render, brush, symmetry, ...).
const char* const kCoreKeys[] = {"format", "version", "app_version", "mesh", "layers", "active_layer",
                                 "brushes", "active_brush", "splats"};

std::filesystem::path fsPath(const std::string& p) { return std::filesystem::path(std::u8string(p.begin(), p.end())); }

bool isCoreKey(const std::string& key) {
    return std::any_of(std::begin(kCoreKeys), std::end(kCoreKeys), [&](const char* k) { return key == k; });
}

// --- Blobs: little-endian arrays of f32 / u32 / u8 with N components per element ---

enum class Scalar { F32, U32, U8 };

const char* scalarName(Scalar s) { return s == Scalar::F32 ? "f32" : s == Scalar::U32 ? "u32" : "u8"; }
size_t scalarSize(Scalar s) { return s == Scalar::U8 ? 1 : 4; }

void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
}
uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

class Writer {
public:
    // Adds a blob and returns its manifest descriptor.
    Json add(const std::string& file, Scalar type, int components, size_t count, std::vector<uint8_t> data) {
        Json d = Json::object();
        d.set("file", file);
        d.set("type", scalarName(type));
        d.set("components", components);
        d.set("count", double(count));
        entries.push_back({file, std::move(data)});
        return d;
    }
    Json floats(const std::string& file, const float* v, size_t count, int components) {
        std::vector<uint8_t> data;
        data.reserve(count * size_t(components) * 4);
        for (size_t i = 0; i < count * size_t(components); ++i) put32(data, std::bit_cast<uint32_t>(v[i]));
        return add(file, Scalar::F32, components, count, std::move(data));
    }
    Json vec3s(const std::string& file, const std::vector<glm::vec3>& v) {
        return floats(file, v.empty() ? nullptr : &v[0].x, v.size(), 3);
    }
    Json uints(const std::string& file, const uint32_t* v, size_t count, int components) {
        std::vector<uint8_t> data;
        data.reserve(count * size_t(components) * 4);
        for (size_t i = 0; i < count * size_t(components); ++i) put32(data, v[i]);
        return add(file, Scalar::U32, components, count, std::move(data));
    }
    Json bytes(const std::string& file, const std::vector<uint8_t>& v) { return add(file, Scalar::U8, 1, v.size(), v); }

    std::vector<ZipEntry> entries;
};

class Reader {
public:
    std::map<std::string, const std::vector<uint8_t>*> files;
    std::string error;

    // Validates `desc` against the expected layout and returns its bytes (nullptr + error on mismatch).
    const uint8_t* blob(const Json& desc, const char* what, Scalar type, int components, size_t count) {
        if (!desc.isObject()) return fail(what, "is missing");
        if (desc["type"].asString() != scalarName(type) || int(desc["components"].asNumber()) != components)
            return fail(what, "has an unexpected layout");
        if (desc["count"].asNumber(-1) != double(count)) return fail(what, "has the wrong size");
        auto it = files.find(desc["file"].asString());
        if (it == files.end()) return fail(what, "data is missing from the archive");
        if (it->second->size() != count * size_t(components) * scalarSize(type)) return fail(what, "data is truncated");
        static const uint8_t kEmpty = 0;
        return it->second->empty() ? &kEmpty : it->second->data();
    }
    bool floats(const Json& desc, const char* what, size_t count, int components, float* out) {
        const uint8_t* p = blob(desc, what, Scalar::F32, components, count);
        if (!p) return false;
        for (size_t i = 0; i < count * size_t(components); ++i) out[i] = std::bit_cast<float>(get32(p + 4 * i));
        return true;
    }
    bool vec3s(const Json& desc, const char* what, size_t count, std::vector<glm::vec3>& out) {
        out.resize(count);
        return floats(desc, what, count, 3, count ? &out[0].x : nullptr);
    }
    bool uints(const Json& desc, const char* what, size_t count, int components, uint32_t* out) {
        const uint8_t* p = blob(desc, what, Scalar::U32, components, count);
        if (!p) return false;
        for (size_t i = 0; i < count * size_t(components); ++i) out[i] = get32(p + 4 * i);
        return true;
    }

private:
    const uint8_t* fail(const char* what, const char* problem) {
        if (error.empty()) error = std::string("Project ") + what + " " + problem;
        return nullptr;
    }
};

Json matrixJson(const glm::mat4& m) {
    Json a = Json::array();
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) a.push(double(m[c][r]));
    return a;
}

glm::mat4 matrixFromJson(const Json& a) {
    glm::mat4 m(1.0f);
    if (a.size() != 16) return m;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) m[c][r] = float(a[size_t(c * 4 + r)].asNumber(c == r ? 1.0 : 0.0));
    return m;
}

size_t countOf(const Json& j) {
    const double n = j.asNumber(-1);
    return n >= 0 && n < 4.0e9 ? size_t(n) : size_t(-1);
}

} // namespace

bool isProjectPath(const std::string& path) {
    std::string ext = fsPath(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext == kProjectExtension;
}

bool saveProject(const std::string& path, const Document& doc, std::string& error) {
    const Mesh& m = doc.mesh;
    Writer w;
    Json manifest = Json::object();
    manifest.set("format", kFormatName);
    manifest.set("version", kProjectVersion);
    manifest.set("app_version", SCULPTFORGE_VERSION);

    Json mesh = Json::object();
    mesh.set("vertex_count", double(m.vertexCount()));
    mesh.set("triangle_count", double(m.triangleCount()));
    mesh.set("positions", w.vec3s("mesh/positions.bin", m.positions));
    mesh.set("triangles", w.uints("mesh/triangles.bin", m.triangles.empty() ? nullptr : &m.triangles[0].x,
                                  m.triangleCount(), 3));
    if (!m.layers.empty()) mesh.set("base", w.vec3s("mesh/base.bin", m.base));
    if (m.hasMask()) mesh.set("mask", w.floats("mesh/mask.bin", m.mask.data(), m.mask.size(), 1));
    if (m.hasColors()) mesh.set("colors", w.vec3s("mesh/colors.bin", m.colors));
    if (m.hasPolygroups()) mesh.set("polygroups", w.uints("mesh/polygroups.bin", m.polygroups.data(), m.polygroups.size(), 1));
    if (m.hasHidden()) mesh.set("hidden", w.bytes("mesh/hidden.bin", m.hidden));
    if (m.hasUvs()) mesh.set("uvs", w.floats("mesh/uvs.bin", &m.uvs[0].x, m.uvs.size(), 2));
    manifest.set("mesh", std::move(mesh));

    Json layers = Json::array();
    for (size_t i = 0; i < m.layers.size(); ++i) {
        Json l = Json::object();
        l.set("name", m.layers[i].name);
        l.set("weight", double(m.layers[i].weight));
        l.set("delta", w.vec3s("layers/" + std::to_string(i) + ".bin", m.layers[i].delta));
        layers.push(std::move(l));
    }
    manifest.set("layers", std::move(layers));
    manifest.set("active_layer", doc.activeLayer);

    Json brushes = Json::array();
    for (const auto& b : doc.brushes) brushes.push(b.toJson());
    manifest.set("brushes", std::move(brushes));
    if (doc.activeBrush >= 0 && doc.activeBrush < int(doc.brushes.size()))
        manifest.set("active_brush", doc.brushes[size_t(doc.activeBrush)].name);

    if (!doc.splats.empty()) {
        const SplatCloud& s = doc.splats;
        if (std::string v = s.validate(); !v.empty()) {
            error = "Cannot save splats: " + v;
            return false;
        }
        Json splats = Json::object();
        splats.set("count", double(s.size()));
        splats.set("sh_degree", s.shDegree);
        splats.set("name", s.name);
        splats.set("transform", matrixJson(s.transform));
        splats.set("positions", w.vec3s("splats/positions.bin", s.positions));
        std::vector<float> rot;
        rot.reserve(s.size() * 4);
        for (const auto& q : s.rotations) rot.insert(rot.end(), {q.x, q.y, q.z, q.w});
        splats.set("rotations", w.floats("splats/rotations.bin", rot.data(), s.size(), 4));
        splats.set("scales", w.vec3s("splats/scales.bin", s.scales));
        splats.set("opacities", w.floats("splats/opacities.bin", s.opacities.data(), s.size(), 1));
        splats.set("sh", w.vec3s("splats/sh.bin", s.sh));
        manifest.set("splats", std::move(splats));
    }

    if (doc.projectSettings.isObject())
        for (const auto& [key, value] : doc.projectSettings.members())
            if (!isCoreKey(key)) manifest.set(key, value);

    const std::string text = manifest.dump(true);
    w.entries.insert(w.entries.begin(), ZipEntry{kManifestName, std::vector<uint8_t>(text.begin(), text.end())});

    // Write next to the target and rename, so a failed save never destroys the previous file.
    const std::string tmp = path + ".tmp";
    if (!writeStoredZip(tmp, w.entries, 64, error)) return false;
    std::error_code ec;
    std::filesystem::rename(fsPath(tmp), fsPath(path), ec);
    if (ec) {
        std::filesystem::remove(fsPath(tmp), ec);
        error = "Cannot write " + path;
        return false;
    }
    return true;
}

bool loadProject(const std::string& path, Document& doc, std::string& error) {
    std::vector<ZipEntry> entries;
    if (!readStoredZip(path, entries, error)) return false;
    Reader r;
    for (const auto& e : entries) r.files[e.name] = &e.data;
    auto mf = r.files.find(kManifestName);
    if (mf == r.files.end()) {
        error = path + " is not a SculptForge project (no manifest)";
        return false;
    }
    Json manifest;
    if (!Json::parse(std::string(mf->second->begin(), mf->second->end()), manifest, error) || !manifest.isObject() ||
        manifest["format"].asString() != kFormatName) {
        error = path + " is not a SculptForge project" + (error.empty() ? "" : " (" + error + ")");
        return false;
    }
    const double version = manifest["version"].asNumber(0);
    if (version > kProjectVersion) {
        error = path + " was saved by a newer SculptForge (" + manifest["app_version"].asString() +
                ", project format " + std::to_string(int(version)) + "); this version reads format " +
                std::to_string(kProjectVersion) + ". Please update SculptForge to open it.";
        return false;
    }
    if (version < 1) {
        error = path + " has an invalid project version";
        return false;
    }

    // Mesh
    const Json& mj = manifest["mesh"];
    const size_t nv = countOf(mj["vertex_count"]), nt = countOf(mj["triangle_count"]);
    if (nv == size_t(-1) || nt == size_t(-1)) {
        error = "Project mesh has no valid vertex/triangle count";
        return false;
    }
    Mesh m;
    m.triangles.resize(nt);
    if (!r.vec3s(mj["positions"], "positions", nv, m.positions) ||
        !r.uints(mj["triangles"], "triangles", nt, 3, nt ? &m.triangles[0].x : nullptr)) {
        error = r.error;
        return false;
    }
    for (const auto& t : m.triangles)
        if (t.x >= nv || t.y >= nv || t.z >= nv) {
            error = "Project triangles reference missing vertices";
            return false;
        }
    if (mj.has("mask")) {
        m.mask.resize(nv);
        r.floats(mj["mask"], "mask", nv, 1, m.mask.data());
    }
    if (mj.has("colors")) r.vec3s(mj["colors"], "colors", nv, m.colors);
    if (mj.has("polygroups")) {
        m.polygroups.resize(nt);
        r.uints(mj["polygroups"], "polygroups", nt, 1, m.polygroups.data());
    }
    if (mj.has("hidden")) {
        if (const uint8_t* p = r.blob(mj["hidden"], "hidden flags", Scalar::U8, 1, nt)) m.hidden.assign(p, p + nt);
    }
    if (mj.has("uvs")) {
        m.uvs.resize(nt * 3);
        r.floats(mj["uvs"], "uvs", nt * 3, 2, nt ? &m.uvs[0].x : nullptr);
    }
    const Json& lj = manifest["layers"];
    if (lj.size()) {
        r.vec3s(mj["base"], "base positions", nv, m.base);
        for (size_t i = 0; i < lj.size(); ++i) {
            SculptLayer layer;
            layer.name = lj[i]["name"].asString();
            layer.weight = float(lj[i]["weight"].asNumber(1.0));
            r.vec3s(lj[i]["delta"], "layer offsets", nv, layer.delta);
            m.layers.push_back(std::move(layer));
        }
    }
    if (!r.error.empty()) {
        error = r.error;
        return false;
    }
    m.rebuildTopology();
    if (!m.layers.empty()) m.evaluateAll();
    m.recomputeAllNormals();

    // Splats
    SplatCloud splats;
    if (const Json& sj = manifest["splats"]; sj.isObject()) {
        const size_t n = countOf(sj["count"]);
        const int degree = int(sj["sh_degree"].asNumber(0));
        if (n == size_t(-1) || degree < 0 || degree > 3) {
            error = "Project splats have an invalid size";
            return false;
        }
        splats.resize(n, degree);
        splats.name = sj["name"].isString() ? sj["name"].asString() : splats.name;
        splats.transform = matrixFromJson(sj["transform"]);
        std::vector<float> rot(n * 4);
        r.vec3s(sj["positions"], "splat positions", n, splats.positions);
        r.floats(sj["rotations"], "splat rotations", n, 4, rot.data());
        r.vec3s(sj["scales"], "splat scales", n, splats.scales);
        r.floats(sj["opacities"], "splat opacities", n, 1, splats.opacities.data());
        r.vec3s(sj["sh"], "splat colours", n * size_t(splats.shCount()), splats.sh);
        if (!r.error.empty()) {
            error = r.error;
            return false;
        }
        for (size_t i = 0; i < n; ++i) splats.rotations[i] = glm::quat(rot[i * 4 + 3], rot[i * 4], rot[i * 4 + 1], rot[i * 4 + 2]);
    }

    // Everything parsed: replace the scene.
    doc.setMesh(std::move(m));
    doc.splats = std::move(splats);
    doc.splatsChanged();
    const int active = int(manifest["active_layer"].asNumber(-1));
    doc.activeLayer = active >= 0 && active < int(doc.mesh.layers.size()) ? active : -1;
    for (const auto& b : manifest["brushes"].items())
        if (b.isObject()) doc.addBrush(BrushRecipe::fromJson(b));
    doc.activeBrush = -1;
    const std::string& activeBrush = manifest["active_brush"].asString();
    for (size_t i = 0; i < doc.brushes.size() && !activeBrush.empty(); ++i)
        if (doc.brushes[i].name == activeBrush) doc.activeBrush = int(i);
    doc.projectSettings = Json::object();
    for (const auto& [key, value] : manifest.members())
        if (!isCoreKey(key)) doc.projectSettings.set(key, value);
    doc.path = path;
    doc.markClean();
    return true;
}

CommandResult openProjectCommand(Document& doc, const std::string& path) {
    std::string err;
    if (!loadProject(path, doc, err)) return {false, err};
    std::string msg = "Opened project " + path + " (" + std::to_string(doc.mesh.triangleCount()) + " triangles";
    if (!doc.mesh.layers.empty()) msg += ", " + std::to_string(doc.mesh.layers.size()) + " layers";
    if (!doc.splats.empty()) msg += ", " + std::to_string(doc.splats.size()) + " splats";
    return {true, msg + ")"};
}

void registerProjectCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    registry.add({"save_project",
                  "Save everything (mesh, mask, polypaint, polygroups, visibility, UVs, sculpt layers, splats, brushes "
                  "and view settings) as a SculptForge project (.sfproj).",
                  {{"path", T::String, "Output .sfproj path (UTF-8). Defaults to the current project path.", false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      std::string path = argString(a, "path", isProjectPath(doc.path) ? doc.path : std::string());
                      if (path.empty()) return {false, "No path given"};
                      // Any other extension too: "head.obj" must not overwrite an OBJ with a ZIP
                      // that open, isProjectPath and Ctrl+S would not recognise.
                      if (!isProjectPath(path)) path += kProjectExtension;
                      std::string err;
                      if (!saveProject(path, doc, err)) return {false, err};
                      doc.path = path;
                      doc.markClean();
                      return {true, "Saved project " + path};
                  }});
    registry.add({"open_project", "Open a SculptForge project (.sfproj), replacing the current scene.",
                  {{"path", T::String, ".sfproj path (UTF-8).", true}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      return openProjectCommand(doc, argString(a, "path"));
                  }});
}

// ---------------------------------------------------------------------------
// Recent files
// ---------------------------------------------------------------------------

std::vector<std::string> addRecentFile(std::vector<std::string> list, const std::string& path, size_t limit) {
    if (path.empty()) return list;
    list.erase(std::remove(list.begin(), list.end(), path), list.end());
    list.insert(list.begin(), path);
    if (list.size() > limit) list.resize(limit);
    return list;
}

std::vector<std::string> pruneRecentFiles(std::vector<std::string> list,
                                          const std::function<bool(const std::string&)>& exists) {
    std::vector<std::string> out;
    for (auto& p : list)
        if (!p.empty() && std::find(out.begin(), out.end(), p) == out.end() && exists(p)) out.push_back(std::move(p));
    return out;
}

} // namespace sf
