// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/GozCommands.h"

#include "core/SubTools.h"
#include "io/GozIO.h"
#include "io/StlIO.h"

#include <algorithm>
#include <filesystem>

namespace sf {

namespace {

std::string fileStem(const std::string& path) {
    auto stem = std::filesystem::path(std::u8string(path.begin(), path.end())).stem().u8string();
    return std::string(stem.begin(), stem.end());
}

std::string meshSummary(const Mesh& m) {
    std::string s = std::to_string(m.triangleCount()) + " triangles";
    if (m.hasPolygroups()) s += ", polygroups";
    if (m.hasUvs()) s += ", UVs";
    if (m.hasMask()) s += ", mask";
    if (m.hasColors()) s += ", polypaint";
    return s;
}

// The name ZBrush sees: the SubTool the mesh came from, else the document name.
std::string sendName(const Document& doc, const Args& a) {
    std::string name = argString(a, "name");
    if (name.empty()) name = doc.gozName;
    if (name.empty()) name = doc.assetName();
    return name;
}

// The GoZ name of SubTool `i` (active or not).
std::string& subtoolGozName(Document& doc, int i) {
    return i == doc.activeSubtool ? doc.gozName : doc.subtools[size_t(i)].gozName;
}

// Visible SubTools with triangles, as the GoZ objects to write: the name each came from ZBrush with,
// else its SubTool name. `indices` receives the SubTool of each object.
std::vector<MeshObjectRef> gozObjects(Document& doc, std::vector<int>& indices) {
    std::vector<MeshObjectRef> out;
    for (int i = 0; i < doc.subtoolCount(); ++i) {
        if (!doc.subtools[size_t(i)].visible || doc.subtoolMesh(i).triangleCount() == 0) continue;
        const std::string& goz = subtoolGozName(doc, i);
        out.push_back({goz.empty() ? doc.subtools[size_t(i)].name : goz, &doc.subtoolMesh(i)});
        indices.push_back(i);
    }
    return out;
}

} // namespace

void registerGozCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;

    reg.add({"goz_import",
             "Open a ZBrush GoZ file (.GoZ), replacing the scene. Polygroups, UVs, mask and polypaint are kept; "
             "quads are split into triangle pairs (exported back as quads).",
             {{"path", T::String, "Input .GoZ path (UTF-8).", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string path = argString(a, "path");
                 Mesh m;
                 GozInfo info;
                 std::string err;
                 if (!loadGoz(path, m, err, &info)) return {false, err};
                 const std::string name = info.name.empty() ? fileStem(path) : info.name;
                 doc.setMesh(std::move(m), name);
                 doc.path.clear();
                 doc.gozName = name;
                 std::string msg = "Opened GoZ " + doc.gozName + " (" + meshSummary(doc.mesh) + ")";
                 if (!err.empty()) msg += "; " + err;
                 return {true, msg};
             }});

    reg.add({"goz_export",
             "Export the mesh as a ZBrush GoZ file (.GoZ) with polygroups, UVs, mask and polypaint. With several "
             "visible SubTools, each is written next to `path` as <name>_<SubTool>.GoZ.",
             {{"path", T::String, "Output .GoZ path (UTF-8).", true},
              {"name", T::String, "SubTool name. Default: the name it came from ZBrush with, else the document name.", false},
              {"quads", T::Bool, "Rebuild quads from triangle pairs made by GoZ import. Default true.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string path = argString(a, "path");
                 GozWriteOptions o;
                 o.quads = argBool(a, "quads", true);
                 std::string err;
                 std::vector<int> indices;
                 const auto objects = gozObjects(doc, indices);
                 if (doc.subtoolCount() == 1 || objects.size() == 1) {
                     const int i = objects.size() == 1 ? indices[0] : doc.activeSubtool;
                     const std::string& goz = subtoolGozName(doc, i);
                     const std::string name = argString(a, "name", goz.empty() ? fileStem(path) : goz);
                     if (!saveGoz(path, doc.subtoolMesh(i), name, err, o)) return {false, err};
                     return {true, "Exported " + path};
                 }
                 if (objects.empty()) return {false, "Nothing visible to export"};
                 // One file per SubTool beside `path`: <stem>_<object>.GoZ.
                 const std::filesystem::path base(std::u8string(path.begin(), path.end()));
                 std::vector<std::string> used;
                 for (const auto& obj : objects) {
                     std::string name = gozObjectName(obj.name);
                     const std::string wanted = name;
                     for (int k = 2; std::find(used.begin(), used.end(), name) != used.end(); ++k) name = wanted + "_" + std::to_string(k);
                     used.push_back(name);
                     const std::string file = fileStem(path) + "_" + name + ".GoZ";
                     const auto out = (base.parent_path() / std::u8string(file.begin(), file.end())).u8string();
                     if (!saveGoz(std::string(out.begin(), out.end()), *obj.mesh, name, err, o)) return {false, err};
                 }
                 return {true, "Exported " + std::to_string(objects.size()) + " SubTools as " + fileStem(path) + "_*.GoZ"};
             }});

    reg.add({"goz_send",
             "Send the visible SubTools to ZBrush through the GoZ bridge (Windows/macOS): writes each to the shared "
             "GoZ folder, lists them in GoZ_ObjectList.txt and notifies or starts ZBrush. Sending meshes received "
             "from ZBrush updates those SubTools.",
             {{"name", T::String, "SubTool name (single SubTool only). Default: the name it came from ZBrush with, else the document name.", false},
              {"folder", T::String, "GoZ root folder (contains GoZBrush/ and GoZProjects/). Default: the public Pixologic folder.", false},
              {"launch", T::Bool, "Notify or start ZBrush. Default true.", false},
              {"zbrush", T::String, "ZBrush executable to start if GoZ's notifier is missing (optional).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 GozSendOptions o;
                 o.root = argString(a, "folder");
                 o.launch = argBool(a, "launch", true);
                 o.zbrushPath = argString(a, "zbrush");
                 std::string msg;
                 if (doc.subtoolCount() == 1) {
                     o.name = sendName(doc, a);
                     const bool ok = gozSend(doc.mesh, o, msg);
                     if (ok) doc.gozName = gozObjectName(o.name);
                     return {ok, msg};
                 }
                 std::vector<int> indices;
                 auto objects = gozObjects(doc, indices);
                 if (objects.size() == 1 && a.contains("name")) objects[0].name = argString(a, "name");
                 std::vector<std::string> sent;
                 const bool ok = gozSend(objects, o, msg, &sent);
                 // Remember the names, so ZBrush's next send updates these SubTools.
                 if (ok)
                     for (size_t k = 0; k < sent.size() && k < indices.size(); ++k) subtoolGozName(doc, indices[k]) = sent[k];
                 return {ok, msg};
             }});

    reg.add({"goz_receive",
             "Receive what ZBrush last sent with GoZ (the objects in GoZ_ObjectList.txt). Each object updates the "
             "SubTool it was sent from (an undoable step on that SubTool), or becomes a new SubTool; a single object "
             "replaces a scene of one SubTool that didn't come from ZBrush.",
             {{"folder", T::String, "GoZ root folder. Default: the public Pixologic folder.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 std::vector<MeshObject> objects;
                 std::string msg;
                 if (!gozReceive(argString(a, "folder"), objects, msg)) return {false, msg};
                 const int original = doc.activeSubtool; // new SubTools go to the end, so this index stays valid
                 auto findGoz = [&](const std::string& name) {
                     for (int i = 0; i < doc.subtoolCount(); ++i)
                         if (!name.empty() && subtoolGozName(doc, i) == name) return i;
                     return -1;
                 };
                 for (size_t k = 0; k < objects.size(); ++k) {
                     MeshObject& obj = objects[k];
                     int target = findGoz(obj.name);
                     // The first object sent into a fresh scene (one SubTool, not from ZBrush) replaces it.
                     const bool fresh = target < 0 && k == 0 && doc.subtoolCount() == 1 && doc.gozName.empty();
                     if (fresh) target = 0;
                     if (target < 0) {
                         const int i = doc.addSubtool(std::move(obj.mesh), obj.name, false);
                         subtoolGozName(doc, i) = obj.name;
                         continue;
                     }
                     doc.selectSubtool(target);
                     if (doc.mesh.triangleCount()) doc.replaceMeshUndoable(std::move(obj.mesh), "Receive from ZBrush");
                     else doc.replaceActiveMesh(std::move(obj.mesh));
                     doc.gozName = obj.name;
                     if (fresh && objects.size() > 1) doc.activeSubtoolInfo().name = doc.uniqueSubtoolName(obj.name);
                 }
                 doc.selectSubtool(original);
                 doc.path.clear();
                 doc.modified = true;
                 return {true, msg};
             }});

    reg.add({"export_stl",
             "Export the visible SubTools as one binary STL mesh (for 3D printing).",
             {{"path", T::String, "Output .stl path (UTF-8).", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string path = argString(a, "path");
                 std::string err;
                 Mesh merged; // no copy for a single SubTool
                 const Mesh& mesh = doc.subtoolCount() == 1 ? doc.mesh : (merged = mergedVisibleMesh(doc));
                 if (!saveStl(path, mesh, err)) return {false, err};
                 return {true, "Exported " + path + " (" + std::to_string(mesh.triangleCount()) + " triangles)"};
             }});
}

} // namespace sf
