// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/App.h"
#include "core/CrkwMetaHuman.h"
#include <algorithm>
#include <filesystem>
#include <cstdio>

namespace sf {
void App::initCrkwMetaHuman() {
#ifdef CRKW_SOURCE_DIR
    const std::filesystem::path source = std::filesystem::path(CRKW_SOURCE_DIR);
    const auto assets = (source.parent_path() / "MetaHuman Sculpt/metahuman_sculpt/assets").u8string();
    if (!crkwMhAssets_[0]) std::snprintf(crkwMhAssets_, sizeof crkwMhAssets_, "%s", std::string(assets.begin(), assets.end()).c_str());
    const auto folder = (source / "out/metahuman-export").u8string();
    if (!crkwMhExport_[0]) std::snprintf(crkwMhExport_, sizeof crkwMhExport_, "%s", std::string(folder.begin(), folder.end()).c_str());
#endif
}

void App::drawCrkwMetaHuman() {
    if (!showCrkwMetaHuman_) return;
    ImGui::SetNextWindowSize(ImVec2(480 * uiScale_, std::min(740 * uiScale_, ImGui::GetIO().DisplaySize.y - 50)), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("CRKW MetaHuman Sculpt", &showCrkwMetaHuman_)) {
        ImGui::TextUnformatted("Native template sculpting");
        ImGui::TextWrapped("Create a character, sculpt base proportions, then export its head, body, eyes and teeth.");
        ImGui::Separator();
        ImGui::TextUnformatted("Local template assets folder");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##crkw.mh_assets", crkwMhAssets_, sizeof crkwMhAssets_, ImGuiInputTextFlags_EnterReturnsTrue)) saveCkwWindow();
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##crkw.mh_layout", &crkwMhLayout_, "Full Body\0Head + Body\0Head Only\0");
        if (ImGui::Button("Create MetaHuman##crkw.mh_create", ImVec2(-1, 30 * uiScale_))) {
            static const char* layouts[] = {"FULL", "SEPARATE", "HEAD"};
            runCommand("crkw_mh_create", {{"assets", std::string(crkwMhAssets_)}, {"layout", std::string(layouts[crkwMhLayout_])}});
            if (lastCommandOk_) {
                setGizmoMode(GizmoMode::Off); paintMode_ = false;
                camera_.frame(doc_.mesh.bounds()); saveCkwWindow();
            }
        }
        ImGui::TextDisabled("Adds native SubTools; existing scene objects are kept.");
        const bool selected = doc_.activeSubtoolInfo().crkwMetaHuman.isObject();
        if (selected) {
            const auto mode = doc_.activeSubtoolInfo().crkwMetaHuman["preview"].asString();
            crkwMhTextures_ = !mode.empty() && mode != "OFF";
            if (mode != "OFF") crkwMhMap_ = mode == "BASE" ? 0 : mode == "ANNOTATED" ? 2 : 1;
        }
        ImGui::BeginDisabled(!selected);
        if (ImGui::Button("Start Sculpting##crkw.mh_start", ImVec2(-1, 0))) {
            doc_.sculptor.dyntopo.enabled = false; paintMode_ = false; paintRgb_ = false;
            setGizmoMode(GizmoMode::Off); brush_ = BrushType::Standard;
            status_ = "MetaHuman sculpting: keep base topology; remesh and Dyntopo break conform export";
        }
        if (ImGui::Button("Fix UVs##crkw.mh_fix", ImVec2(-1, 0))) {
            runCommand("crkw_mh_fix_uvs");
            if (lastCommandOk_) crkwMhTextures_ = false;
        }
        ImGui::EndDisabled();
        if (!selected) ImGui::TextWrapped("Select a MetaHuman SubTool to fix UVs or export its character.");
        ImGui::Separator();
        bool previewChanged = ImGui::Checkbox("Show UV Textures##crkw.mh_textures", &crkwMhTextures_);
        ImGui::SetNextItemWidth(-1);
        previewChanged |= ImGui::Combo("##crkw.mh_map", &crkwMhMap_, "Base Color\0UV Guides\0Annotated Guides\0");
        if (previewChanged) {
            static const char* maps[] = {"BASE", "GUIDES", "ANNOTATED"};
            runCommand("crkw_mh_preview", {{"mode", std::string(crkwMhTextures_ ? maps[crkwMhMap_] : "OFF")}});
            if (lastCommandOk_ && crkwMhTextures_) { renderSettings_.showPolypaint = true; renderSettings_.showTexture = true; }
            if (!lastCommandOk_) crkwMhTextures_ = false;
        }
        ImGui::TextWrapped("Guide preview leaves original UVs and painted textures unchanged. UV repair and export validate template topology.");
        ImGui::Separator();
        ImGui::TextUnformatted("Export package folder");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##crkw.mh_export_folder", crkwMhExport_, sizeof crkwMhExport_, ImGuiInputTextFlags_EnterReturnsTrue)) saveCkwWindow();
        ImGui::BeginDisabled(!selected);
        if (ImGui::Button("Export for MetaHuman##crkw.mh_export", ImVec2(-1, 30 * uiScale_))) {
            runCommand("crkw_mh_export", {{"folder", std::string(crkwMhExport_)}}); saveCkwWindow();
        }
        ImGui::EndDisabled();
        ImGui::TextWrapped("Export uses base-level positions and original UVs; subdivision fine detail is excluded. Matching role FBXs in this folder are replaced.");
        ImGui::TextWrapped("Unreal: From Template / Mesh Fit, Match Vertices by UVs. Conform and rig round-trip verification remains necessary.");
        ImGui::Separator();
        ImGui::TextWrapped("%s", status_.c_str());
    }
    ImGui::End();
}
} // namespace sf
