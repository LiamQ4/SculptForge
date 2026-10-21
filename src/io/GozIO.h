// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// GoZ: ZBrush's bridge format (.GoZ binary meshes plus a shared "GoZ folder"
// with an object list). The binary layout follows the open-source
// Blender add-on GoB (github.com/JoseConseco/GoB, GPL); see docs/goz.md.
//
// .GoZ carries positions, tri/quad faces, per-face-corner UVs, polygroups,
// mask and polypaint. Quads are triangulated on import as (a,b,c) + (a,c,d)
// in consecutive triangles, and export pairs such triangles back into quads.
// ZBrush's own .ZTL/.ZPR files are proprietary and cannot be read.
#pragma once

#include "core/Mesh.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sf {

// Extra GoZ information that has no place in Mesh.
struct GozInfo {
    std::string name;            // object (SubTool) name without the "GoZMesh_" prefix
    int subdivisionLevel = 0;    // as recorded by ZBrush (informational)
    std::string diffuseMap, displacementMap, normalMap; // texture paths, if any
    size_t quads = 0, triangles = 0; // GoZ faces read
};

struct GozWriteOptions {
    bool quads = true; // pair (a,b,c)+(a,c,d) triangles back into quads
};

// Parses an in-memory .GoZ file. Positions are converted from ZBrush's
// Y-down axes to SculptForge's Y-up ones (a 180 degree turn about X).
bool parseGoz(std::span<const uint8_t> data, Mesh& out, std::string& error, GozInfo* info = nullptr);
std::vector<uint8_t> writeGoz(const Mesh& mesh, const std::string& name, const GozWriteOptions& options = {});

// Paths are UTF-8.
bool loadGoz(const std::string& path, Mesh& out, std::string& error, GozInfo* info = nullptr);
bool saveGoz(const std::string& path, const Mesh& mesh, const std::string& name, std::string& error,
             const GozWriteOptions& options = {});

// GoZ object names: ASCII letters, digits, '_' and '-' (GoB's escaping).
std::string gozObjectName(const std::string& name);

// ---------------------------------------------------------------------------
// The GoZ bridge (ZBrush on Windows and macOS)
// ---------------------------------------------------------------------------

// The shared GoZ root: %PUBLIC%\Pixologic on Windows, /Users/Shared/Pixologic
// on macOS, empty elsewhere (ZBrush does not run on Linux).
std::string gozDefaultRoot();
// Message for platforms without ZBrush.
std::string gozUnavailableMessage();
// Where SculptForge registers itself as a GoZ application (GoZApps/SculptForge/GoZ_Config.txt).
void setGozApplicationPath(std::string executable);

struct GozSendOptions {
    std::string root;       // GoZ root; empty = gozDefaultRoot()
    std::string name;       // object name; empty = "SculptForge"
    bool launch = true;     // notify ZBrush (GoZBrushFromApp) or start it
    std::string zbrushPath; // ZBrush executable to start if GoZBrushFromApp is missing (optional)
};

// Writes the mesh to <root>/GoZProjects/Default/<name>.GoZ, lists it in
// GoZBrush/GoZ_ObjectList.txt, names SculptForge in GoZ_Application.txt and
// (optionally) tells ZBrush to import it. `message` describes what happened.
bool gozSend(const Mesh& mesh, const GozSendOptions& options, std::string& message);

// Reads every object in <root>/GoZBrush/GoZ_ObjectList.txt (what ZBrush last
// sent) into one mesh. Several SubTools are merged.
bool gozReceive(const std::string& root, Mesh& out, std::string& message, std::vector<std::string>* names = nullptr);

// Path of the object list ZBrush rewrites on every send (for polling).
std::string gozObjectListPath(const std::string& root);

// Error text for ZBrush's proprietary files (.ztl, .zpr, .zbr).
std::string zbrushProprietaryMessage(const std::string& extension);

} // namespace sf
