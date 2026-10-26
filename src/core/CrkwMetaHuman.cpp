// Native CRKW MetaHuman template workflow. No Blender runtime dependency.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/CrkwMetaHuman.h"
#include "core/Image.h"
#include "core/Multires.h"
#include "core/Sha256.h"
#include "core/Texture.h"
#include "io/FbxIO.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>

namespace sf {
namespace {
using Path = std::filesystem::path;
Path path(const std::string& s) { return Path(std::u8string(s.begin(), s.end())); }
std::string utf8(const Path& p) { const auto s = p.u8string(); return {s.begin(), s.end()}; }
std::string topologyKey(const Mesh& m) {
    std::string bytes = std::to_string(m.vertexCount()) + ":";
    bytes.append(reinterpret_cast<const char*>(m.triangles.data()), m.triangles.size() * sizeof(glm::uvec3));
    bytes.append(reinterpret_cast<const char*>(m.quadPartners.data()), m.quadPartners.size() * sizeof(uint32_t));
    return sha256Hex(bytes);
}
std::string role(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (name.find("eyeleft") != std::string::npos) return "eyeLeft";
    if (name.find("eyeright") != std::string::npos) return "eyeRight";
    if (name.find("teeth") != std::string::npos) return "teeth";
    if (name.find("body") != std::string::npos) return name.find("full") != std::string::npos ? "full" : "body";
    if (name.find("head") != std::string::npos) return "head";
    throw std::runtime_error("Unknown MetaHuman template object: " + name);
}
std::vector<int> targets(Document& doc, bool all = false) {
    const auto& active = doc.activeSubtoolInfo().crkwMetaHuman;
    if (!all && !active.isObject()) throw std::runtime_error("Select a MetaHuman Sculpt SubTool first");
    std::vector<int> out;
    for (int i = 0; i < doc.subtoolCount(); ++i) {
        const auto& ref = doc.subtools[size_t(i)].crkwMetaHuman;
        if (ref.isObject() && (all || ref["session"].asString() == active["session"].asString())) out.push_back(i);
    }
    if (out.empty()) throw std::runtime_error("No MetaHuman Sculpt character in this scene");
    return out;
}
std::vector<glm::vec2> referenceUvs(const Json& ref, size_t corners) {
    const auto& values = ref["uvs"];
    if (!values.isArray() || values.size() != corners * 2) throw std::runtime_error("MetaHuman UV reference is missing or damaged");
    std::vector<glm::vec2> uv(corners);
    for (size_t i = 0; i < corners; ++i)
        for (size_t c = 0; c < 2; ++c) {
            if (!values[i * 2 + c].isNumber() || !std::isfinite(values[i * 2 + c].asNumber()))
                throw std::runtime_error("MetaHuman UV reference is invalid");
            uv[i][int(c)] = float(values[i * 2 + c].asNumber());
        }
    return uv;
}
Mesh checkedBase(Document& doc, int index) {
    const auto& mesh = doc.subtoolMesh(index);
    Mesh base = subdivLevelCount(mesh) ? subdivLevelMesh(mesh, 0) : mesh;
    const auto& ref = doc.subtools[size_t(index)].crkwMetaHuman;
    const std::string r = ref["role"].asString();
    if (r != "head" && r != "body" && r != "full" && r != "eyeLeft" && r != "eyeRight" && r != "teeth")
        throw std::runtime_error("Invalid MetaHuman part identity");
    if (topologyKey(base) != ref["topology"].asString())
        throw std::runtime_error(doc.subtools[size_t(index)].name + ": topology changed; remeshing, reordered vertices and applied subdivision cannot be repaired");
    if (!base.layers.empty()) throw std::runtime_error("MetaHuman base export requires no sculpt layers; bake your intended base shape first");
    base.uvs = referenceUvs(ref, base.triangleCount() * 3);
    if (r == "full") {
        if (ref["parts"].size() != base.triangleCount()) throw std::runtime_error("Missing full-body part reference");
        for (const auto& part : ref["parts"].items())
            if (!part.isNumber() || (part.asNumber() != 0 && part.asNumber() != 1)) throw std::runtime_error("Invalid full-body part reference");
    }
    base.previewTexture.reset(); base.previewUvs.clear();
    return base;
}
Image mapImage(const Path& folder, const std::string& part, const std::string& mode) {
    const bool skin = part == "Head" || part == "Body";
    const std::string suffix = skin && mode != "BASE" ? (part == "Head" && mode == "ANNOTATED" ? "_Guides_Annotated" : "_Guides") : "";
    Path file = folder / ("T_Gray_" + part + "_D" + suffix + ".PNG");
    if (!std::filesystem::exists(file)) file.replace_extension(".png");
    Image image;
    std::string error;
    if (!Image::load(utf8(file), image, error)) throw std::runtime_error(error);
    return image.downscaled(2048);
}
void preview(Document& doc, const std::vector<int>& indices, const std::string& mode, bool remember = true) {
    struct Display { int index; std::shared_ptr<PaintTexture> texture; std::vector<glm::vec2> uv; };
    std::vector<Display> pending;
    std::map<std::string, std::shared_ptr<PaintTexture>> cache;
    for (int index : indices) {
        const auto& ref = doc.subtools[size_t(index)].crkwMetaHuman;
        auto& mesh = doc.subtoolMesh(index);
        Display display{index, {}, {}};
        if (mode != "OFF") {
            checkedBase(doc, index); // validate the base independently of UV edits and display state
            const std::string r = ref["role"].asString(), folder = ref["assets"].asString();
            const std::string key = folder + ":" + r + ":" + mode;
            if (!cache.contains(key)) {
                Image image;
                if (r == "full") {
                    const Image body = mapImage(path(folder), "Body", mode), head = mapImage(path(folder), "Head", mode);
                    constexpr int tile = 2048;
                    image.width = tile * 2; image.height = tile;
                    image.rgba.resize(size_t(image.width) * image.height * 4);
                    for (int which = 0; which < 2; ++which) {
                        const Image& source = which ? head : body;
                        for (int y = 0; y < tile; ++y) for (int x = 0; x < tile; ++x) {
                            const size_t src = (size_t(y * source.height / tile) * source.width + x * source.width / tile) * 4;
                            const size_t dst = (size_t(y) * image.width + which * tile + x) * 4;
                            std::copy_n(source.rgba.data() + src, 4, image.rgba.data() + dst);
                        }
                    }
                } else image = mapImage(path(folder), r == "head" ? "Head" : r == "body" ? "Body" : r == "teeth" ? "Teeth" : "Eyes", mode);
                auto texture = makeTexture(image.width, image.height);
                std::string error;
                if (!loadTextureImage(*texture, image, error)) throw std::runtime_error(error);
                cache[key] = std::move(texture);
            }
            display.texture = cache[key];
            if (r == "full") {
                if (!mesh.hasUvs()) throw std::runtime_error("Fix base UVs before displaying full-body texture guides");
                display.uv = mesh.uvs;
                for (size_t t = 0; t < mesh.triangleCount(); ++t) {
                    const int part = subdivLevelCount(mesh) ? int(mesh.polygroupAt(uint32_t(t))) : int(ref["parts"][t].asNumber());
                    for (int c = 0; c < 3; ++c) display.uv[t * 3 + c].x = (display.uv[t * 3 + c].x + float(part == 1)) * .5f;
                }
            }
        }
        pending.push_back(std::move(display));
    }
    for (auto& display : pending) {
        auto& mesh = doc.subtoolMesh(display.index);
        mesh.previewTexture = std::move(display.texture);
        mesh.previewUvs = std::move(display.uv);
        mesh.facesDirty = true;
        if (remember) doc.subtools[size_t(display.index)].crkwMetaHuman.set("preview", mode);
    }
}
Mesh partMesh(const Mesh& src, const Json& ref, int part) {
    if (!ref["parts"].isArray() || ref["parts"].size() != src.triangleCount()) throw std::runtime_error("Missing full-body part reference");
    Mesh out;
    std::vector<uint32_t> mapping(src.vertexCount(), Mesh::kNoQuad), triangleMap(src.triangleCount(), Mesh::kNoQuad);
    for (size_t t = 0; t < src.triangleCount(); ++t) if (int(ref["parts"][t].asNumber(-1)) == part)
        for (int c = 0; c < 3; ++c) mapping[src.triangles[t][c]] = 0;
    for (size_t v = 0; v < mapping.size(); ++v) if (mapping[v] == 0) { mapping[v] = uint32_t(out.vertexCount()); out.positions.push_back(src.positions[v]); }
    for (uint32_t t = 0; t < src.triangleCount(); ++t) if (int(ref["parts"][size_t(t)].asNumber(-1)) == part) {
        triangleMap[t] = uint32_t(out.triangleCount());
        const auto tri = src.triangles[t];
        out.triangles.push_back({mapping[tri.x], mapping[tri.y], mapping[tri.z]});
        out.uvs.insert(out.uvs.end(), src.uvs.begin() + size_t(t) * 3, src.uvs.begin() + size_t(t) * 3 + 3);
    }
    if (out.triangles.empty()) throw std::runtime_error("The full-body template must contain body material 0 and head material 1");
    remapQuads(src, triangleMap, out);
    out.rebuildTopology(); out.recomputeAllNormals();
    return out;
}
}

void registerCrkwMetaHumanCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    auto add = [&](const char* name, const char* description, std::vector<ParamSpec> params, auto op) {
        registry.add({name, description, std::move(params), [op](Document& doc, const Args& args) -> CommandResult {
            try { return op(doc, args); } catch (const std::exception& e) { return {false, e.what()}; }
        }});
    };
    add("crkw_mh_create", "Add a native MetaHuman template character, with persistent topology/UV references.",
        {{"assets", T::String, "Folder containing your local MetaHuman Sculpt template FBXs and PNGs", false},
         {"layout", T::String, "FULL (joined), SEPARATE (head+body) or HEAD", false}},
        [](Document& doc, const Args& args) -> CommandResult {
            std::string folder = argString(args, "assets");
            if (folder.empty()) if (const char* env = std::getenv("CRKW_METAHUMAN_ASSETS")) folder = env;
            if (folder.empty()) throw std::runtime_error("Choose your local MetaHuman template assets folder");
            const std::string layout = argString(args, "layout", "FULL");
            if (layout != "FULL" && layout != "SEPARATE" && layout != "HEAD") throw std::runtime_error("Unknown MetaHuman layout");
            std::vector<MeshObject> objects;
            for (const auto& file : layout == "FULL" ? std::vector<std::string>{"FullBody.fbx"} : layout == "HEAD" ? std::vector<std::string>{"head.fbx"} : std::vector<std::string>{"head.fbx", "body.fbx"}) {
                std::vector<MeshObject> loaded; std::string error;
                if (!loadFbxTemplate(utf8(path(folder) / file), loaded, error)) throw std::runtime_error(error);
                for (auto& object : loaded) objects.push_back(std::move(object));
            }
            std::set<std::string> roles;
            for (const auto& object : objects) {
                if (!object.mesh.hasUvs()) throw std::runtime_error("Template has no UVs: " + object.name);
                if (!roles.insert(role(object.name)).second) throw std::runtime_error("Duplicate MetaHuman template role");
            }
            for (const auto& required : {layout == "FULL" ? "full" : "head", "eyeLeft", "eyeRight", "teeth"})
                if (!roles.contains(required)) throw std::runtime_error(std::string("Template is missing ") + required);
            if (layout == "SEPARATE" && !roles.contains("body")) throw std::runtime_error("Template is missing body");
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            int primary = -1;
            for (auto& object : objects) {
                const std::string r = role(object.name);
                Json ref = Json::object(), uvs = Json::array(), parts = Json::array();
                ref.set("session", std::to_string(stamp)); ref.set("role", r); ref.set("assets", folder);
                ref.set("topology", topologyKey(object.mesh)); ref.set("preview", "OFF");
                for (const auto& uv : object.mesh.uvs) { uvs.push(double(uv.x)); uvs.push(double(uv.y)); }
                for (size_t t = 0; t < object.mesh.triangleCount(); ++t) parts.push(double(object.mesh.polygroupAt(uint32_t(t))));
                ref.set("uvs", std::move(uvs)); ref.set("parts", std::move(parts));
                const int index = doc.addSubtool(std::move(object.mesh), "MH " + r, false);
                doc.subtools[size_t(index)].crkwMetaHuman = std::move(ref);
                if (r == "full" || r == "head") primary = index;
            }
            doc.selectSubtool(primary); doc.sculptor.dyntopo.enabled = false;
            return {true, "MetaHuman created: " + std::to_string(objects.size()) + " native SubTools (" + layout + ")"};
        });
    add("crkw_mh_fix_uvs", "Restore the selected character's original base UVs; reject changed topology.", {},
        [](Document& doc, const Args&) -> CommandResult {
            const auto indices = targets(doc);
            std::vector<Mesh> checked;
            for (int i : indices) {
                if (subdivLevelCount(doc.subtoolMesh(i)) && subdivCurrentLevel(doc.subtoolMesh(i)) != 0)
                    throw std::runtime_error("Return to subdivision level 1 before fixing base UVs");
                checked.push_back(checkedBase(doc, i));
            }
            for (size_t k = 0; k < indices.size(); ++k) {
                auto& mesh = doc.subtoolMesh(indices[k]);
                UndoRecord record; record.label = "MetaHuman Fix UVs"; record.mesh = std::make_unique<Mesh>(mesh);
                record.mesh->previewTexture.reset(); record.mesh->previewUvs.clear();
                (indices[k] == doc.activeSubtool ? doc.undo : doc.subtools[size_t(indices[k])].undo).push(std::move(record));
                mesh.uvs = std::move(checked[k].uvs);
                mesh.previewUvs.clear(); mesh.previewTexture.reset(); mesh.facesDirty = true;
                doc.subtools[size_t(indices[k])].crkwMetaHuman.set("preview", "OFF");
            }
            doc.modified = true;
            return {true, "MetaHuman original base UVs restored; sculpt positions unchanged"};
        });
    add("crkw_mh_preview", "Show display-only MetaHuman base/guide maps without modifying UVs or painted materials.",
        {{"mode", T::String, "OFF, BASE, GUIDES or ANNOTATED", false}},
        [](Document& doc, const Args& args) -> CommandResult {
            const std::string mode = argString(args, "mode", "GUIDES");
            if (mode != "OFF" && mode != "BASE" && mode != "GUIDES" && mode != "ANNOTATED") throw std::runtime_error("Unknown MetaHuman map mode");
            preview(doc, targets(doc, true), mode); doc.modified = true;
            return {true, "MetaHuman texture preview: " + mode};
        });
    add("crkw_mh_export", "Export the selected character as separate base head/body/eyes/teeth FBXs with reference UVs.",
        {{"folder", T::String, "Output package folder; matching role FBXs are replaced", true}},
        [](Document& doc, const Args& args) -> CommandResult {
            std::map<std::string, Mesh> meshes;
            for (int i : targets(doc)) {
                Mesh base = checkedBase(doc, i);
                const auto& ref = doc.subtools[size_t(i)].crkwMetaHuman;
                const std::string r = ref["role"].asString();
                if (r == "full") { meshes["body"] = partMesh(base, ref, 0); meshes["head"] = partMesh(base, ref, 1); }
                else meshes[r] = std::move(base);
            }
            const Path folder = path(argString(args, "folder"));
            if (folder.empty()) throw std::runtime_error("Choose an export folder");
            std::filesystem::create_directories(folder);
            const Path staging = folder / (".crkw-mh-stage-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directory(staging);
            std::vector<Path> staged;
            try {
                for (auto& [name, mesh] : meshes) {
                    // Conform packages contain geometry/UVs, not painted texture sidecars.
                    mesh.texture.reset();
                    FbxExportOptions options; options.scale = 100; options.layers = false;
                    const Path file = staging / (name + ".fbx"); std::string error;
                    staged.push_back(file);
                    if (!saveFbx(utf8(file), name, {&mesh}, options, error)) throw std::runtime_error(error);
                }
                for (const auto& file : staged) std::filesystem::copy_file(file, folder / file.filename(), std::filesystem::copy_options::overwrite_existing);
            } catch (...) {
                for (const auto& file : staged) std::filesystem::remove(file);
                std::error_code ec; std::filesystem::remove(staging, ec); throw;
            }
            for (const auto& file : staged) std::filesystem::remove(file);
            std::filesystem::remove(staging);
            return {true, "MetaHuman exported: " + std::to_string(meshes.size()) + " base FBXs; use From Template / Match Vertices by UVs"};
        });
}

void restoreCrkwMetaHumanPreviews(Document& doc) {
    for (int i = 0; i < doc.subtoolCount(); ++i) {
        const auto& ref = doc.subtools[size_t(i)].crkwMetaHuman;
        const std::string mode = ref["preview"].asString();
        if (!ref.isObject() || mode.empty() || mode == "OFF") continue;
        try { preview(doc, {i}, mode, false); } catch (...) {
            auto& mesh = doc.subtoolMesh(i); mesh.previewTexture.reset(); mesh.previewUvs.clear(); mesh.facesDirty = true;
        }
    }
}
} // namespace sf
