// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/EnginePresets.h"

#include "core/Decimate.h"
#include "io/FbxIO.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace sf {

namespace {

// Unreal asset names: letters, digits and underscores.
std::string unrealName(const std::string& text) {
    std::string out;
    for (unsigned char c : text) out += std::isalnum(c) ? char(c) : '_';
    return out.empty() ? std::string("Sculpt") : out;
}

} // namespace

bool exportForUnreal(const Document& doc, const UnrealExportOptions& o, UnrealExportResult& r, std::string& error) {
    const Mesh& mesh = doc.mesh;
    if (mesh.triangleCount() == 0) {
        error = "The mesh is empty";
        return false;
    }
    // Morph targets only exist on skeletal meshes in Unreal; a skeletal mesh can't use an FBX LOD group
    // the same way, so layered sculpts export LOD0 only.
    r.skeletal = !mesh.layers.empty();
    r.assetName = !o.assetName.empty() ? unrealName(o.assetName) : (r.skeletal ? "SK_" : "SM_") + unrealName(doc.assetName());
    const float height = mesh.bounds().extent().y;
    r.scale = o.heightCm > 0.0f && height > 0.0f ? o.heightCm / height : 100.0f;

    std::vector<Mesh> lods;
    std::vector<const Mesh*> levels = {&mesh};
    if (!r.skeletal && o.lods > 1) {
        lods = generateLods(mesh, std::clamp(o.lods, 2, 8), std::clamp(o.ratio, 0.1f, 0.9f));
        levels.clear();
        for (const auto& l : lods) levels.push_back(&l);
    }
    r.lodCount = int(levels.size());

    std::filesystem::path folder(std::u8string(o.folder.begin(), o.folder.end()));
    if (folder.empty()) folder = std::filesystem::temp_directory_path();
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const auto file = (folder / std::u8string(reinterpret_cast<const char8_t*>(r.assetName.data()), r.assetName.size())).u8string() + u8".fbx";
    r.path.assign(file.begin(), file.end());

    FbxExportOptions fo;
    fo.scale = r.scale;
    return saveFbx(r.path, r.assetName, levels, fo, error);
}

} // namespace sf
