// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/App.h"
#include "app/I18n.h"
#include "app/Ui.h"
#include "core/CrkwMaskManager.h"
#include <algorithm>

namespace sf {
void App::drawMhMaskManager() {
    if (!showMhMaskManager_) return;
    ImGui::SetNextWindowSize(ImVec2(390*uiScale_,0),ImGuiCond_FirstUseEver);
    if (ImGui::Begin(tr("CRKW MH Mask Manager"),&showMhMaskManager_,ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto role=doc_.activePartInfo().crkwMetaHuman["role"].asString();
        const bool valid=(role=="head"||role=="full")&&doc_.mesh.hasUvs();
        ImGui::TextUnformatted(doc_.activePartInfo().name.c_str());
        if (!valid) {
            ImGui::TextWrapped("%s", tr("Select a native MetaHuman head or full-body Part with its primary UVs."));
            if (ImGui::Button(tr("Select head##mhmask.head"),ImVec2(-1,0))) {
                for (int i=0;i<doc_.partCount();++i) {
                    const auto& ref=doc_.parts[size_t(i)].crkwMetaHuman;
                    if (ref["role"].asString()=="head"||ref["role"].asString()=="full") {
                        runCommand("part_select",{{"index",double(i)}});break;
                    }
                }
            }
        }
        auto command=[&](Args args) {
            runCommand("crkw_mh_mask",args);
            if (lastCommandOk_) {
                symmetry_.x=crkwMaskSettings(doc_.mesh)["side"].asString()=="BOTH";
                renderSettings_.showMask=true;
            }
        };
        ImGui::BeginDisabled(!valid);
        const auto retainedState=doc_.mesh.crkwMaskState;
        const Json& state=retainedState?*retainedState:crkwMaskSettings(doc_.mesh);
        if (valid && mhMaskUiState_!=doc_.mesh.crkwMaskState) {
            symmetry_.x=state["side"].asString()=="BOTH";
            mhMaskUiState_=doc_.mesh.crkwMaskState;
        }
        const std::string side=state["side"].asString(),selected=state["selected"].asString();
        ImGui::TextDisabled("%s", tr("Click: toggle  |  Ctrl+click: isolate"));
        for (const char* value:{"BOTH","R","L"}) {
            if (value!=std::string("BOTH")) ImGui::SameLine();
            if (ImGui::RadioButton(value==std::string("BOTH")?tr("Both##mhmask.both"):value==std::string("R")?tr("R##mhmask.right"):tr("L##mhmask.left"),side==value))
                command({{"action",std::string("settings")},{"side",std::string(value)}});
        }
        ImGui::Separator();
        std::vector<std::pair<std::string,std::string>> regions;
        for (const auto& preset:crkwMaskPresets()) regions.emplace_back(preset.name,preset.name);
        for (const auto& [key,value]:state["custom"].members()) regions.emplace_back(key,key.substr(4));
        const float width=(ImGui::GetContentRegionAvail().x-2*ImGui::GetStyle().ItemSpacing.x)/3;
        for (size_t i=0;i<regions.size();++i) {
            if (i%3) ImGui::SameLine();
            const auto& [key,label]=regions[i];
            const bool active=std::any_of(state["active"].items().begin(),state["active"].items().end(),[&](const Json& j){return j.asString()==key;});
            if (active) {
                const auto color=ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive);
                ImGui::PushStyleColor(ImGuiCol_Button,color);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,color);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,color);
            }
            if (ImGui::Button((label+"##mhmask."+key).c_str(),ImVec2(width,34*uiScale_)))
                command({{"action",std::string(ui::CtrlHeld()?"isolate":"toggle")},{"region",key}});
            if (active) ImGui::PopStyleColor(3);
        }
        ImGui::Separator();
        ImGui::Text(tr("Adjust: %s"),selected.starts_with("mhr_")?selected.substr(4).c_str():selected.c_str());
        int rings=int(state["regions"][selected]["rings"].asNumber()),feather=int(state["regions"][selected]["feather"].asNumber());
        ImGui::SetNextItemWidth(160*uiScale_);
        if (ImGui::InputInt(tr("Rings##mhmask.rings"),&rings,1,5))
            command({{"action",std::string("settings")},{"rings",double(std::clamp(rings,0,60))}});
        ImGui::SetNextItemWidth(160*uiScale_);
        if (ImGui::InputInt(tr("Feather##mhmask.feather"),&feather,1,5))
            command({{"action",std::string("settings")},{"feather",double(std::clamp(feather,0,20))}});
        for (const char* action:{"isolate","add","protect"}) {
            if (action!=std::string("isolate")) ImGui::SameLine();
            const std::string label=action==std::string("isolate")?"Isolate":action==std::string("add")?"Add":"Protect";
            if (ImGui::Button((label+"##mhmask."+action).c_str(),ImVec2(width,0))) command({{"action",std::string(action)}});
        }
        if (ImGui::Button(tr("Clear##mhmask.clear"),ImVec2(width,0))) command({{"action",std::string("clear")}});
        ImGui::SameLine();
        if (ImGui::Button(tr("Invert##mhmask.invert"),ImVec2(width,0))) command({{"action",std::string("invert")}});
        ImGui::SameLine();
        if (ImGui::Button(tr("Frame##mhmask.frame"),ImVec2(width,0))) {
            AABB bounds;
            for (uint32_t v=0;v<doc_.mesh.vertexCount();++v) if (doc_.mesh.maskAt(v)<.5f) bounds.expand(doc_.mesh.positions[v]);
            if (bounds.valid()) camera_.frame(bounds);else status_="No unmasked vertices to frame";
        }
        ImGui::Separator();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##mhmask.custom_name",mhMaskCustomName_,sizeof(mhMaskCustomName_));
        if (ImGui::Button(tr("Save custom region##mhmask.save"),ImVec2(-1,0)))
            command({{"action",std::string("save")},{"name",std::string(mhMaskCustomName_)}});
        ImGui::EndDisabled();
        ImGui::TextWrapped("%s",status_.c_str());
    }
    ImGui::End();
}
} // namespace sf
