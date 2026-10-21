// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Commands.h"

#include "core/Decimate.h"
#include "core/EnginePresets.h"
#include "core/ImageTo3D.h"
#include "core/Primitives.h"
#include "core/Splats.h"
#include "core/Voxel.h"
#include "io/FbxIO.h"
#include "io/GltfIO.h"
#include "io/ObjIO.h"
#include "io/PlyIO.h"
#include "io/UsdIO.h"

#include <algorithm>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <sstream>

namespace sf {

double argNumber(const Args& args, const std::string& key, double fallback) {
    auto it = args.find(key);
    if (it == args.end()) return fallback;
    if (auto* d = std::get_if<double>(&it->second)) return *d;
    if (auto* b = std::get_if<bool>(&it->second)) return *b ? 1.0 : 0.0;
    return fallback;
}

bool argBool(const Args& args, const std::string& key, bool fallback) {
    auto it = args.find(key);
    if (it == args.end()) return fallback;
    if (auto* b = std::get_if<bool>(&it->second)) return *b;
    if (auto* d = std::get_if<double>(&it->second)) return *d != 0.0;
    return fallback;
}

std::string argString(const Args& args, const std::string& key, const std::string& fallback) {
    auto it = args.find(key);
    if (it == args.end()) return fallback;
    if (auto* s = std::get_if<std::string>(&it->second)) return *s;
    return fallback;
}

namespace {

constexpr size_t kMaxSubdivideTriangles = 50'000'000;

std::string lowerExtension(const std::string& path) {
    std::string ext = std::filesystem::path(std::u8string(path.begin(), path.end())).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        default: out += c;
        }
    }
    return out;
}

} // namespace

CommandRegistry::CommandRegistry() {
    using T = ParamSpec::Type;

    add({"new_sphere",
         "Replace the scene with a new sphere ready for sculpting.",
         {{"subdivisions", T::Number, "Icosphere subdivision level (0-8). Default 6.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             int level = std::clamp(int(argNumber(a, "subdivisions", 6)), 0, 8);
             doc.setMesh(makeIcosphere(level));
             doc.path.clear();
             return {true, "Created sphere with " + std::to_string(doc.mesh.triangleCount()) + " triangles"};
         }});

    add({"new_plane",
         "Replace the scene with a flat grid plane.",
         {{"resolution", T::Number, "Grid cells per side (1-2000). Default 128.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             int res = std::clamp(int(argNumber(a, "resolution", 128)), 1, 2000);
             doc.setMesh(makePlane(res));
             doc.path.clear();
             return {true, "Created plane with " + std::to_string(doc.mesh.triangleCount()) + " triangles"};
         }});

    add({"open_obj",
         "Load a Wavefront OBJ file, replacing the current scene.",
         {{"path", T::String, "File path (UTF-8).", true}},
         [](Document& doc, const Args& a) -> CommandResult {
             std::string path = argString(a, "path");
             Mesh m;
             std::string err;
             if (!loadObj(path, m, err)) return {false, err};
             doc.setMesh(std::move(m));
             doc.path = path;
             return {true, "Loaded " + path};
         }});

    add({"open",
         "Open a file: models (.obj, .fbx, .glb, .gltf, .ply, .usda, .usdz) replace the scene; Gaussian splats "
         "(.ply/.splat captures, KHR_gaussian_splatting .glb, USD ParticleField) load as a reference next to the sculpt. "
         "glTF morph targets and FBX/USD blend shapes become sculpt layers.",
         {{"path", T::String, "File path (UTF-8).", true}},
         [](Document& doc, const Args& a) -> CommandResult {
             std::string path = argString(a, "path");
             const std::string ext = lowerExtension(path);
             Mesh m;
             SplatCloud splats;
             std::string err;
             bool ok;
             if (ext == ".glb" || ext == ".gltf") ok = loadGltf(path, m, err, &splats);
             else if (ext == ".usda" || ext == ".usd" || ext == ".usdz") ok = loadUsd(path, m, splats, err);
             else if (ext == ".splat") ok = loadDotSplat(path, splats, err);
             else if (ext == ".fbx") ok = loadFbx(path, m, err);
             else if (ext == ".ply") {
                 PlyContent content;
                 ok = loadPly(path, m, splats, content, err);
             } else ok = loadObj(path, m, err);
             if (!ok) return {false, err};
             std::string msg = "Opened " + path;
             if (m.triangleCount()) {
                 doc.setMesh(std::move(m));
                 doc.path = ext == ".obj" ? path : std::string(); // other formats are exported explicitly
                 msg += " (" + std::to_string(doc.mesh.triangleCount()) + " triangles" +
                        (doc.mesh.layers.empty() ? "" : ", " + std::to_string(doc.mesh.layers.size()) + " layers") + ")";
             }
             if (!splats.empty()) {
                 doc.splats = std::move(splats);
                 doc.splatsChanged();
                 msg += " (" + std::to_string(doc.splats.size()) + " Gaussian splats, SH degree " +
                        std::to_string(doc.splats.shDegree) + ")";
             }
             return {true, msg};
         }});

    add({"remesh",
         "Voxel remesh (like DynaMesh): rebuild the sculpt as an even, watertight surface, merging overlapping "
         "parts and fixing broken scans or generated models. Sculpt layers are baked in.",
         {{"resolution", T::Number, "Voxels across the longest side (32-512). Default 200; higher keeps more detail.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             std::string err;
             Mesh m = voxelRemesh(doc.mesh, std::clamp(int(argNumber(a, "resolution", 200)), 32, 512), &err);
             if (m.triangleCount() == 0) return {false, err};
             doc.replaceMeshUndoable(std::move(m), "Remesh");
             return {true, "Remeshed to " + std::to_string(doc.mesh.triangleCount()) + " triangles"};
         }});

    add({"image_relief",
         "Turn an image into a sculpted relief: light areas rise, dark areas stay low (logos, coins, emblems, "
         "height maps, texture photos). Replaces the scene.",
         {{"path", T::String, "Image file (png, jpg, bmp, tga...).", true},
          {"depth", T::Number, "Relief height as a fraction of the width (0.01-1). Default 0.12.", false},
          {"resolution", T::Number, "Detail: grid cells along the long side (32-2048). Default 384.", false},
          {"invert", T::Bool, "Make dark areas high instead.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             Image img;
             std::string err;
             if (!Image::load(argString(a, "path"), img, err)) return {false, err};
             ReliefOptions o;
             o.depth = float(std::clamp(argNumber(a, "depth", o.depth), 0.01, 1.0));
             o.resolution = int(std::clamp(argNumber(a, "resolution", o.resolution), 32.0, 2048.0));
             o.invert = argBool(a, "invert", false);
             doc.replaceMeshUndoable(makeImageRelief(img, o), "Image Relief");
             doc.path.clear();
             return {true, "Relief with " + std::to_string(doc.mesh.triangleCount()) + " triangles"};
         }});

    add({"image_inflate",
         "Inflate a sketch or silhouette (transparent background or plain backdrop) into a rounded, closed 3D base "
         "mesh ready for sculpting. Replaces the scene.",
         {{"path", T::String, "Image file.", true},
          {"thickness", T::Number, "1 = round, lower = flatter (0.1-3). Default 1.", false},
          {"resolution", T::Number, "Detail along the long side (32-512). Default 160.", false},
          {"subdivisions", T::Number, "Smoothing subdivision passes (0-3). Default 1.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             Image img;
             std::string err;
             if (!Image::load(argString(a, "path"), img, err)) return {false, err};
             InflateOptions o;
             o.thickness = float(std::clamp(argNumber(a, "thickness", 1.0), 0.1, 3.0));
             o.resolution = int(std::clamp(argNumber(a, "resolution", o.resolution), 32.0, 512.0));
             o.subdivisions = int(std::clamp(argNumber(a, "subdivisions", 1.0), 0.0, 3.0));
             Mesh m = inflateSilhouette(img, o, &err);
             if (m.triangleCount() == 0) return {false, err};
             doc.replaceMeshUndoable(std::move(m), "Inflate Sketch");
             doc.path.clear();
             return {true, "Inflated sketch into " + std::to_string(doc.mesh.triangleCount()) + " triangles"};
         }});

    add({"image_project",
         "Stamp an image onto the current model as surface detail, projected from the front (+Z). Light = raised. "
         "Goes into the active sculpt layer.",
         {{"path", T::String, "Image file.", true},
          {"depth", T::Number, "Displacement at full brightness, as a fraction of the model size (0.001-0.5). Default 0.03.", false},
          {"invert", T::Bool, "Make dark areas raised instead.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             Image img;
             std::string err;
             if (!Image::load(argString(a, "path"), img, err)) return {false, err};
             AABB b = doc.mesh.bounds();
             if (!b.valid()) return {false, "Nothing to project onto"};
             const float size = glm::length(b.extent());
             const glm::vec3 eye = b.center() + glm::vec3(0, 0, size * 2.0f);
             const float halfH = std::max(b.extent().x / std::max(img.aspect(), 1e-3f), b.extent().y) * 0.5f;
             const float halfW = halfH * img.aspect();
             glm::mat4 proj = glm::ortho(-halfW, halfW, -halfH, halfH, 0.01f, size * 4.0f);
             glm::mat4 view = glm::lookAt(eye, b.center(), glm::vec3(0, 1, 0));
             ProjectOptions o;
             o.depth = float(std::clamp(argNumber(a, "depth", 0.03), 0.001, 0.5)) * size;
             o.invert = argBool(a, "invert", false);
             o.coverage = 1.0f;
             Mesh m = doc.mesh;
             size_t n = projectImage(m, doc.activeLayer, img, proj * view, img.aspect(), eye, o);
             if (n == 0) return {false, "The image did not land on any front-facing surface"};
             doc.replaceMeshUndoable(std::move(m), "Project Image");
             return {true, "Projected onto " + std::to_string(n) + " vertices"};
         }});

    add({"save_obj",
         "Save the mesh as a Wavefront OBJ file.",
         {{"path", T::String, "File path (UTF-8). Defaults to the current document path.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             std::string path = argString(a, "path", doc.path);
             if (path.empty()) return {false, "No path given"};
             std::string err;
             if (!saveObj(path, doc.mesh, err)) return {false, err};
             doc.path = path;
             doc.modified = false;
             return {true, "Saved " + path};
         }});

    add({"subdivide",
         "Smoothly subdivide the mesh (Loop subdivision). Each level multiplies the triangle count by 4.",
         {{"levels", T::Number, "Number of subdivision levels to apply (1-4). Default 1.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             int levels = std::clamp(int(argNumber(a, "levels", 1)), 1, 4);
             size_t projected = doc.mesh.triangleCount();
             for (int i = 0; i < levels; ++i) projected *= 4;
             if (projected > kMaxSubdivideTriangles)
                 return {false, "Subdividing would exceed " + std::to_string(kMaxSubdivideTriangles) + " triangles"};
             Mesh m = loopSubdivide(doc.mesh);
             for (int i = 1; i < levels; ++i) m = loopSubdivide(m);
             doc.replaceMeshUndoable(std::move(m), "Subdivide");
             return {true, "Subdivided to " + std::to_string(doc.mesh.triangleCount()) + " triangles"};
         }});

    add({"layer_add",
         "Add a sculpt layer (blend shape / morph target). New strokes go into it until another layer is made active.",
         {{"name", T::String, "Layer name, e.g. 'Smile' or 'BlinkL'.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             Mesh m = doc.mesh;
             int idx = m.addLayer(argString(a, "name", "Layer " + std::to_string(doc.mesh.layers.size() + 1)));
             doc.replaceMeshUndoable(std::move(m), "Add Layer");
             doc.activeLayer = idx;
             return {true, "Added layer '" + doc.mesh.layers[idx].name + "' (index " + std::to_string(idx) + ")"};
         }});

    add({"layer_remove",
         "Delete a sculpt layer and its offsets.",
         {{"index", T::Number, "Layer index (0-based).", true}},
         [](Document& doc, const Args& a) -> CommandResult {
             int idx = int(argNumber(a, "index", -1));
             if (idx < 0 || idx >= int(doc.mesh.layers.size())) return {false, "No layer " + std::to_string(idx)};
             Mesh m = doc.mesh;
             m.removeLayer(idx);
             doc.replaceMeshUndoable(std::move(m), "Remove Layer");
             doc.activeLayer = -1;
             return {true, "Removed layer " + std::to_string(idx)};
         }});

    add({"layer_set_weight",
         "Set how strongly a sculpt layer is applied (0 = off, 1 = as sculpted; negative or >1 exaggerates).",
         {{"index", T::Number, "Layer index (0-based).", true},
          {"weight", T::Number, "Blend weight, typically 0-1.", true}},
         [](Document& doc, const Args& a) -> CommandResult {
             int idx = int(argNumber(a, "index", -1));
             if (idx < 0 || idx >= int(doc.mesh.layers.size())) return {false, "No layer " + std::to_string(idx)};
             doc.mesh.setLayerWeight(idx, float(argNumber(a, "weight", 1.0)));
             doc.bvhDirty = true;
             doc.modified = true;
             return {true, "Layer " + std::to_string(idx) + " weight set"};
         }});

    add({"layer_set_active",
         "Choose which sculpt layer new strokes go into (-1 = base mesh).",
         {{"index", T::Number, "Layer index, or -1 for the base mesh.", true}},
         [](Document& doc, const Args& a) -> CommandResult {
             int idx = int(argNumber(a, "index", -1));
             if (idx < -1 || idx >= int(doc.mesh.layers.size())) return {false, "No layer " + std::to_string(idx)};
             doc.activeLayer = idx;
             return {true, idx < 0 ? "Sculpting on base mesh" : "Sculpting on layer " + doc.mesh.layers[idx].name};
         }});

    add({"decimate",
         "Reduce triangle count while preserving shape (quadric edge collapse). Sculpt layers are kept.",
         {{"ratio", T::Number, "Fraction of triangles to keep (0.01-1). Default 0.5.", false},
          {"triangles", T::Number, "Exact target triangle count (overrides ratio).", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             size_t target = a.contains("triangles")
                                 ? size_t(std::max(4.0, argNumber(a, "triangles", 4)))
                                 : size_t(double(doc.mesh.triangleCount()) *
                                          std::clamp(argNumber(a, "ratio", 0.5), 0.01, 1.0));
             doc.replaceMeshUndoable(decimate(doc.mesh, target), "Decimate");
             return {true, "Decimated to " + std::to_string(doc.mesh.triangleCount()) + " triangles"};
         }});

    add({"export_glb",
         "Export a game-ready binary glTF (.glb). Sculpt layers become morph targets (blend shapes).",
         {{"path", T::String, "Output .glb path (UTF-8).", true}},
         [](Document& doc, const Args& a) -> CommandResult {
             std::string path = argString(a, "path");
             std::string err;
             if (!saveGlb(path, doc.mesh, doc.assetName(), err)) return {false, err};
             return {true, "Exported " + path};
         }});

    add({"export_usd",
         "Export OpenUSD (.usda text, or .usdz package for AR Quick Look / Vision Pro). Sculpt layers become "
         "UsdSkel blend shapes with their current weights; loaded Gaussian splats are included as a "
         "ParticleField3DGaussianSplat prim.",
         {{"path", T::String, "Output .usda, .usd or .usdz path (UTF-8).", true},
          {"splats", T::Bool, "Include the loaded Gaussian splats. Default true.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             std::string path = argString(a, "path");
             std::string err;
             const SplatCloud* splats = argBool(a, "splats", true) && !doc.splats.empty() ? &doc.splats : nullptr;
             if (!saveUsd(path, &doc.mesh, splats, doc.assetName(), err)) return {false, err};
             return {true, "Exported " + path + (splats ? " (mesh + " + std::to_string(splats->size()) + " splats)" : "")};
         }});

    add({"export_fbx",
         "Export binary FBX: sculpt layers become blend shapes; with lods > 1 the LOD chain is written as an FBX "
         "LOD group in the same file.",
         {{"path", T::String, "Output .fbx path (UTF-8).", true},
          {"lods", T::Number, "LOD levels including LOD0 (1-8). Default 1.", false},
          {"ratio", T::Number, "Triangle ratio between LODs (0.1-0.9). Default 0.5.", false},
          {"scale", T::Number, "Multiply positions (FBX units are centimetres). Default 100 (1 unit = 1 m).", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             const int levels = std::clamp(int(argNumber(a, "lods", 1)), 1, 8);
             std::vector<Mesh> lods;
             std::vector<const Mesh*> meshes = {&doc.mesh};
             if (levels > 1) {
                 lods = generateLods(doc.mesh, levels, float(std::clamp(argNumber(a, "ratio", 0.5), 0.1, 0.9)));
                 meshes.clear();
                 for (const auto& l : lods) meshes.push_back(&l);
             }
             FbxExportOptions o;
             o.scale = float(argNumber(a, "scale", 100.0));
             std::string err;
             const std::string path = argString(a, "path");
             if (!saveFbx(path, doc.assetName(), meshes, o, err)) return {false, err};
             return {true, "Exported " + path};
         }});

    add({"export_unreal",
         "Export an Unreal Engine-ready FBX: centimetres, SM_/SK_ naming, LOD chain packed for automatic LOD "
         "import; a sculpt with layers becomes a skeletal mesh with morph targets.",
         {{"folder", T::String, "Output folder (UTF-8). The file is named after the asset.", true},
          {"name", T::String, "Asset name (default SM_<document> or SK_<document>).", false},
          {"height_cm", T::Number, "Model height in Unreal, in cm. Default: 1 unit = 1 m.", false},
          {"lods", T::Number, "LOD levels for static meshes (1-8). Default 4.", false},
          {"ratio", T::Number, "Triangle ratio between LODs (0.1-0.9). Default 0.5.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             UnrealExportOptions o;
             o.folder = argString(a, "folder");
             o.assetName = argString(a, "name");
             o.heightCm = float(argNumber(a, "height_cm", 0.0));
             o.lods = int(argNumber(a, "lods", 4));
             o.ratio = float(argNumber(a, "ratio", 0.5));
             UnrealExportResult r;
             std::string err;
             if (!exportForUnreal(doc, o, r, err)) return {false, err};
             return {true, "Exported " + r.path + " (" + (r.skeletal ? "skeletal mesh, " + std::to_string(doc.mesh.layers.size()) + " morph targets"
                                                                     : "static mesh, " + std::to_string(r.lodCount) + " LODs") + ")"};
         }});

    add({"export_lods",
         "Generate a LOD chain and export each level as <name>_LOD<n>.glb next to `path`. Blend shapes are kept on every LOD.",
         {{"path", T::String, "Base output path, e.g. /assets/rock.glb.", true},
          {"levels", T::Number, "Number of LODs including LOD0 (1-8). Default 4.", false},
          {"ratio", T::Number, "Triangle ratio between consecutive LODs (0.1-0.9). Default 0.5.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             const std::string pathArg = argString(a, "path");
             std::filesystem::path base(std::u8string(pathArg.begin(), pathArg.end()));
             int levels = std::clamp(int(argNumber(a, "levels", 4)), 1, 8);
             float ratio = float(std::clamp(argNumber(a, "ratio", 0.5), 0.1, 0.9));
             auto lods = generateLods(doc.mesh, levels, ratio);
             auto stemU8 = base.stem().u8string();
             std::string stem(stemU8.begin(), stemU8.end());
             std::ostringstream msg;
             msg << "Exported";
             for (size_t i = 0; i < lods.size(); ++i) {
                 std::string fileName = stem + "_LOD" + std::to_string(i) + ".glb";
                 auto out = (base.parent_path() / std::u8string(fileName.begin(), fileName.end())).u8string();
                 std::string outPath(out.begin(), out.end());
                 std::string err;
                 if (!saveGlb(outPath, lods[i], stem + "_LOD" + std::to_string(i), err)) return {false, err};
                 msg << " LOD" << i << " (" << lods[i].triangleCount() << " tris)";
             }
             return {true, msg.str()};
         }});

    {
        // brush_create takes every recipe parameter, so AI tool calls and scripts can design brushes.
        std::vector<ParamSpec> params = {{"name", T::String, "Brush name.", true},
                                         {"description", T::String, "What the brush is for.", false}};
        for (const auto& rp : recipeParams()) {
            T type = rp.kind == RecipeParam::Kind::Float ? T::Number
                     : rp.kind == RecipeParam::Kind::Bool ? T::Bool
                                                          : T::String;
            params.push_back({rp.key, type, rp.help, false});
        }
        add({"brush_create",
             "Create (or update, by name) a custom sculpting brush from a recipe and make it the active brush. "
             "Omitted parameters use neutral defaults.",
             params, [](Document& doc, const Args& a) -> CommandResult {
                 Json j = Json::object();
                 for (const auto& [key, value] : a) {
                     if (auto* d = std::get_if<double>(&value)) j.set(key, *d);
                     else if (auto* b = std::get_if<bool>(&value)) j.set(key, *b);
                     else j.set(key, std::get<std::string>(value));
                 }
                 doc.activeBrush = doc.addBrush(BrushRecipe::fromJson(j));
                 return {true, "Brush '" + doc.brushes[doc.activeBrush].name + "' ready"};
             }});
    }

    add({"brush_list", "List the custom brushes in the library.", {},
         [](Document& doc, const Args&) -> CommandResult {
             std::string out;
             for (size_t i = 0; i < doc.brushes.size(); ++i)
                 out += (i ? "\n" : "") + std::to_string(i) + ": " + doc.brushes[i].name + " - " +
                        doc.brushes[i].description;
             return {true, out};
         }});

    add({"brush_save", "Save a custom brush as a shareable .sfbrush file.",
         {{"path", T::String, "Output .sfbrush path.", true},
          {"name", T::String, "Brush name (defaults to the active brush).", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             std::string name = argString(a, "name");
             const BrushRecipe* r = nullptr;
             for (const auto& b : doc.brushes)
                 if (b.name == name) r = &b;
             if (!r && name.empty() && doc.activeBrush >= 0) r = &doc.brushes[doc.activeBrush];
             if (!r) return {false, "No such brush: " + name};
             std::string err;
             if (!r->save(argString(a, "path"), err)) return {false, err};
             return {true, "Saved brush '" + r->name + "'"};
         }});

    add({"brush_load", "Load a .sfbrush file into the library and make it active.",
         {{"path", T::String, ".sfbrush path.", true}},
         [](Document& doc, const Args& a) -> CommandResult {
             BrushRecipe r;
             std::string err;
             if (!BrushRecipe::load(argString(a, "path"), r, err)) return {false, err};
             doc.activeBrush = doc.addBrush(std::move(r));
             return {true, "Loaded brush '" + doc.brushes[doc.activeBrush].name + "'"};
         }});

    add({"splat_info", "Report the loaded Gaussian splats.", {},
         [](Document& doc, const Args&) -> CommandResult {
             if (doc.splats.empty()) return {true, "No splats loaded"};
             const AABB b = doc.splats.bounds(0.98f);
             std::ostringstream s;
             s << doc.splats.size() << " splats, SH degree " << doc.splats.shDegree << ", main body ("
               << b.min.x << ", " << b.min.y << ", " << b.min.z << ") - (" << b.max.x << ", " << b.max.y << ", " << b.max.z << ")";
             return {true, s.str()};
         }});

    add({"splat_save",
         "Save the loaded Gaussian splats: .ply (standard 3DGS layout, e.g. for SuperSplat/Postshot), .splat (compact web viewers), .glb "
         "(glTF KHR_gaussian_splatting) or .usda/.usdz (OpenUSD ParticleField3DGaussianSplat).",
         {{"path", T::String, "Output path (UTF-8).", true}},
         [](Document& doc, const Args& a) -> CommandResult {
             if (doc.splats.empty()) return {false, "No splats loaded"};
             const std::string path = argString(a, "path");
             const std::string ext = lowerExtension(path);
             std::string err;
             bool ok = ext == ".ply"     ? saveSplatPly(path, doc.splats, err)
                       : ext == ".splat" ? saveDotSplat(path, doc.splats, err)
                       : ext == ".glb" ? saveSplatGlb(path, doc.splats, err)
                       : (ext == ".usda" || ext == ".usd" || ext == ".usdz")
                           ? saveUsd(path, nullptr, &doc.splats, doc.assetName(), err)
                           : (err = "Use a .ply, .splat, .glb, .usda or .usdz file name", false);
             if (!ok) return {false, err};
             return {true, "Saved " + std::to_string(doc.splats.size()) + " splats to " + path};
         }});

    add({"splat_clean", "Remove faint splats and outlying floaters from a capture.",
         {{"min_opacity", T::Number, "Remove splats fainter than this (0-1). Default 0.05.", false},
          {"keep", T::Number, "Fraction of the capture to keep per axis, trimming outliers (0.5-1). Default 0.98.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             if (doc.splats.empty()) return {false, "No splats loaded"};
             const size_t removed = cleanSplats(doc.splats, float(std::clamp(argNumber(a, "min_opacity", 0.05), 0.0, 1.0)),
                                                float(std::clamp(argNumber(a, "keep", 0.98), 0.5, 1.0)));
             doc.splatsChanged();
             return {true, "Removed " + std::to_string(removed) + " splats, " + std::to_string(doc.splats.size()) + " left"};
         }});

    add({"splat_transform",
         "Turn and place the splats (captures are often upside down or off-centre). Rotations are in degrees "
         "about the capture's centre, applied X then Y then Z.",
         {{"rotate_x", T::Number, "Degrees about X (180 flips an upside-down capture).", false},
          {"rotate_y", T::Number, "Degrees about Y.", false},
          {"rotate_z", T::Number, "Degrees about Z.", false},
          {"fit", T::Bool, "Centre at the origin and scale to the size of a new sphere.", false},
          {"reset", T::Bool, "Undo all placement first.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             if (doc.splats.empty()) return {false, "No splats loaded"};
             if (argBool(a, "reset", false)) doc.splats.transform = glm::mat4(1.0f);
             rotateSplats(doc.splats, glm::vec3(float(argNumber(a, "rotate_x", 0)), float(argNumber(a, "rotate_y", 0)),
                                                float(argNumber(a, "rotate_z", 0))));
             if (argBool(a, "fit", false)) fitSplats(doc.splats);
             doc.splatsChanged();
             return {true, "Splats placed"};
         }});

    add({"splat_to_mesh",
         "Convert a Gaussian-splat capture into a watertight, sculptable mesh (replaces the sculpt; the splats "
         "stay as a reference). Trim the capture first so only the subject remains.",
         {{"resolution", T::Number, "Voxels across the subject (32-512). Default 192.", false},
          {"min_opacity", T::Number, "Ignore splats fainter than this (0-1). Default 0.25.", false},
          {"keep", T::Number, "Fraction of splats to keep per axis, trimming floaters (0.5-1). Default 0.97.", false},
          {"close", T::Number, "Seal holes up to this many voxels (0-8). Default 2.", false},
          {"smooth", T::Number, "Smoothing passes (0-50). Default 6.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             if (doc.splats.empty()) return {false, "No splats loaded"};
             SplatToMeshOptions o;
             o.resolution = int(argNumber(a, "resolution", o.resolution));
             o.minOpacity = float(argNumber(a, "min_opacity", o.minOpacity));
             o.keepFraction = float(argNumber(a, "keep", o.keepFraction));
             o.closeRadius = int(argNumber(a, "close", o.closeRadius));
             o.smoothIterations = int(argNumber(a, "smooth", o.smoothIterations));
             std::string err;
             Mesh m = splatsToMesh(doc.splats, o, &err);
             if (m.triangleCount() == 0) return {false, err};
             doc.replaceMeshUndoable(std::move(m), "Splats to Mesh");
             return {true, "Built a " + std::to_string(doc.mesh.triangleCount()) + "-triangle mesh from the splats"};
         }});

    add({"mesh_to_splats",
         "Turn the sculpt into Gaussian splats (flat oriented surfels with baked clay shading), replacing any "
         "loaded splats - for splat viewers, web scenes and engines that render 3DGS.",
         {{"count", T::Number, "Number of splats (1000-5000000). Default 250000.", false},
          {"shading", T::Bool, "Bake soft lighting and cavity darkening into the colour. Default true.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             if (doc.mesh.triangleCount() == 0) return {false, "The mesh is empty"};
             MeshToSplatsOptions o;
             o.count = size_t(std::clamp(argNumber(a, "count", 250000), 1000.0, 5000000.0));
             o.shading = argBool(a, "shading", true);
             doc.splats = meshToSplats(doc.mesh, o);
             doc.splats.name = doc.assetName();
             doc.splatsChanged();
             return {true, "Made " + std::to_string(doc.splats.size()) + " splats from the sculpt"};
         }});

    add({"splat_clear", "Remove the loaded Gaussian splats.", {},
         [](Document& doc, const Args&) -> CommandResult {
             doc.splats.clear();
             doc.splatsChanged();
             return {true, "Splats cleared"};
         }});

    add({"undo", "Undo the last action.", {},
         [](Document& doc, const Args&) -> CommandResult {
             if (!doc.undo.undo(doc.mesh, doc.bvh)) return {false, "Nothing to undo"};
             doc.modified = true;
             return {true, "Undone"};
         }});

    add({"redo", "Redo the last undone action.", {},
         [](Document& doc, const Args&) -> CommandResult {
             if (!doc.undo.redo(doc.mesh, doc.bvh)) return {false, "Nothing to redo"};
             doc.modified = true;
             return {true, "Redone"};
         }});

    add({"mesh_info", "Report vertex/triangle counts and bounding box of the mesh.", {},
         [](Document& doc, const Args&) -> CommandResult {
             AABB b = doc.mesh.bounds();
             std::ostringstream s;
             s << doc.mesh.vertexCount() << " vertices, " << doc.mesh.triangleCount() << " triangles";
             if (b.valid())
                 s << ", bounds (" << b.min.x << ", " << b.min.y << ", " << b.min.z << ") - (" << b.max.x << ", "
                   << b.max.y << ", " << b.max.z << ")";
             return {true, s.str()};
         }});
}

void CommandRegistry::add(Command cmd) { commands_.push_back(std::move(cmd)); }

const Command* CommandRegistry::find(const std::string& name) const {
    for (const auto& c : commands_)
        if (c.name == name) return &c;
    return nullptr;
}

CommandResult CommandRegistry::execute(Document& doc, const std::string& name, const Args& args) const {
    const Command* cmd = find(name);
    if (!cmd) return {false, "Unknown command: " + name};
    for (const auto& p : cmd->params)
        if (p.required && !args.contains(p.name)) return {false, "Missing required argument: " + p.name};
    return cmd->run(doc, args);
}

std::string CommandRegistry::toolSchemaJson() const {
    std::ostringstream s;
    s << "[";
    for (size_t i = 0; i < commands_.size(); ++i) {
        const auto& c = commands_[i];
        s << (i ? "," : "") << "{\"name\":\"" << jsonEscape(c.name) << "\",\"description\":\""
          << jsonEscape(c.description) << "\",\"input_schema\":{\"type\":\"object\",\"properties\":{";
        std::vector<std::string> required;
        for (size_t j = 0; j < c.params.size(); ++j) {
            const auto& p = c.params[j];
            const char* type = p.type == ParamSpec::Type::Bool ? "boolean"
                               : p.type == ParamSpec::Type::Number ? "number"
                                                                   : "string";
            s << (j ? "," : "") << "\"" << jsonEscape(p.name) << "\":{\"type\":\"" << type
              << "\",\"description\":\"" << jsonEscape(p.description) << "\"}";
            if (p.required) required.push_back(p.name);
        }
        s << "},\"required\":[";
        for (size_t j = 0; j < required.size(); ++j) s << (j ? "," : "") << "\"" << jsonEscape(required[j]) << "\"";
        s << "]}}";
    }
    s << "]";
    return s.str();
}

} // namespace sf
