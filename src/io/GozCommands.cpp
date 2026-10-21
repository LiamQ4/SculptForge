// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/GozCommands.h"

#include "io/GozIO.h"
#include "io/StlIO.h"

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
                 doc.setMesh(std::move(m));
                 doc.path.clear();
                 doc.gozName = info.name.empty() ? fileStem(path) : info.name;
                 std::string msg = "Opened GoZ " + doc.gozName + " (" + meshSummary(doc.mesh) + ")";
                 if (!err.empty()) msg += "; " + err;
                 return {true, msg};
             }});

    reg.add({"goz_export",
             "Export the mesh as a ZBrush GoZ file (.GoZ) with polygroups, UVs, mask and polypaint.",
             {{"path", T::String, "Output .GoZ path (UTF-8).", true},
              {"name", T::String, "SubTool name. Default: the name it came from ZBrush with, else the document name.", false},
              {"quads", T::Bool, "Rebuild quads from triangle pairs made by GoZ import. Default true.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string path = argString(a, "path");
                 GozWriteOptions o;
                 o.quads = argBool(a, "quads", true);
                 std::string err;
                 const std::string name = argString(a, "name", doc.gozName.empty() ? fileStem(path) : doc.gozName);
                 if (!saveGoz(path, doc.mesh, name, err, o)) return {false, err};
                 return {true, "Exported " + path};
             }});

    reg.add({"goz_send",
             "Send the mesh to ZBrush through the GoZ bridge (Windows/macOS): writes it to the shared GoZ folder, "
             "lists it in GoZ_ObjectList.txt and notifies or starts ZBrush. Sending a mesh received from ZBrush "
             "updates that SubTool.",
             {{"name", T::String, "SubTool name. Default: the name it came from ZBrush with, else the document name.", false},
              {"folder", T::String, "GoZ root folder (contains GoZBrush/ and GoZProjects/). Default: the public Pixologic folder.", false},
              {"launch", T::Bool, "Notify or start ZBrush. Default true.", false},
              {"zbrush", T::String, "ZBrush executable to start if GoZ's notifier is missing (optional).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 GozSendOptions o;
                 o.root = argString(a, "folder");
                 o.name = sendName(doc, a);
                 o.launch = argBool(a, "launch", true);
                 o.zbrushPath = argString(a, "zbrush");
                 std::string msg;
                 const bool ok = gozSend(doc.mesh, o, msg);
                 if (ok) doc.gozName = gozObjectName(o.name);
                 return {ok, msg};
             }});

    reg.add({"goz_receive",
             "Receive what ZBrush last sent with GoZ (the objects in GoZ_ObjectList.txt), replacing the mesh as an "
             "undoable step. Several SubTools are merged into one mesh.",
             {{"folder", T::String, "GoZ root folder. Default: the public Pixologic folder.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Mesh m;
                 std::string msg;
                 std::vector<std::string> names;
                 if (!gozReceive(argString(a, "folder"), m, msg, &names)) return {false, msg};
                 if (doc.mesh.triangleCount()) doc.replaceMeshUndoable(std::move(m), "Receive from ZBrush");
                 else doc.setMesh(std::move(m));
                 doc.path.clear();
                 doc.gozName = names.size() == 1 ? names.front() : std::string();
                 doc.modified = true;
                 return {true, msg};
             }});

    reg.add({"export_stl",
             "Export the mesh as binary STL (for 3D printing).",
             {{"path", T::String, "Output .stl path (UTF-8).", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string path = argString(a, "path");
                 std::string err;
                 if (!saveStl(path, doc.mesh, err)) return {false, err};
                 return {true, "Exported " + path + " (" + std::to_string(doc.mesh.triangleCount()) + " triangles)"};
             }});
}

} // namespace sf
