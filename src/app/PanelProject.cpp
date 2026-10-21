// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Project files in the app: Save / Save As (.sfproj), the unsaved-changes
// prompt (Save / Don't Save / Cancel) before new, open and quit, File > Open
// Recent and autosave with crash recovery. Prompts only appear in interactive
// sessions; --render, --record and demos run straight through.
#include "app/App.h"

#include "core/Matcap.h"
#include "core/Project.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace sf {

namespace {

const SDL_DialogFileFilter kProjectFilters[] = {{"SculptForge project", "sfproj"}, {"All files", "*"}};
constexpr const char* kUnsavedPopup = "Unsaved Changes";
constexpr const char* kRestorePopup = "Restore Autosave?";

std::filesystem::path fsPath(const std::string& p) { return std::filesystem::path(std::u8string(p.begin(), p.end())); }
std::string utf8(const std::filesystem::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}
std::string autosavePath(const std::string& prefDir) { return prefDir + "autosave.sfproj"; }
bool fileExists(const std::string& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(fsPath(p), ec);
}

Json vec3Json(const glm::vec3& v) {
    Json a = Json::array();
    for (int i = 0; i < 3; ++i) a.push(double(v[i]));
    return a;
}
void readVec3(const Json& a, glm::vec3& v) {
    if (a.size() != 3) return;
    for (int i = 0; i < 3; ++i) v[i] = float(a[size_t(i)].asNumber(v[i]));
}
void readFloat(const Json& j, const char* key, float& v) { v = float(j[key].asNumber(v)); }
void readBool(const Json& j, const char* key, bool& v) { v = j[key].asBool(v); }

// Save As dialog: a cancelled dialog is reported as an empty path so a pending
// "save, then quit/open/new" is dropped instead of firing on a later save.
void SDLCALL onProjectChosen(void* userdata, const char* const* files, int) {
    auto* req = static_cast<App::DialogRequest*>(userdata);
    req->app->queueFile(req->action, files && files[0] ? std::string(files[0]) : std::string());
}

} // namespace

bool App::interactive() const { return options_.renderPath.empty() && options_.recordPath.empty() && !demo_; }

void App::initProject() {
    if (!interactive()) return;
    if (char* pref = SDL_GetPrefPath("SculptForge", "SculptForge")) {
        prefDir_ = pref;
        SDL_free(pref);
    }
    if (prefDir_.empty()) return;
    std::ifstream f(fsPath(prefDir_ + "project.json"), std::ios::binary);
    Json prefs;
    std::string err;
    if (f && Json::parse(std::string(std::istreambuf_iterator<char>(f), {}), prefs, err)) {
        for (const auto& p : prefs["recent_files"].items())
            if (p.isString()) recentFiles_.push_back(p.asString());
        autosaveMinutes_ = std::clamp(int(prefs["autosave_minutes"].asNumber(autosaveMinutes_)), 0, 120);
    }
    recentFiles_ = pruneRecentFiles(std::move(recentFiles_), fileExists);

    // An autosave newer than the last clean exit means the previous session ended unexpectedly.
    std::error_code ec;
    const auto autosave = fsPath(autosavePath(prefDir_)), marker = fsPath(prefDir_ + "clean_exit");
    if (std::filesystem::is_regular_file(autosave, ec)) {
        const auto saved = std::filesystem::last_write_time(autosave, ec);
        std::error_code ec2;
        const auto exited = std::filesystem::last_write_time(marker, ec2);
        offerRestore_ = !ec && (ec2 || saved > exited);
    }
}

void App::finishProject() {
    if (!interactive() || prefDir_.empty()) return;
    saveProjectPrefs();
    std::ofstream(fsPath(prefDir_ + "clean_exit"), std::ios::binary | std::ios::trunc) << "SculptForge exited normally\n";
}

void App::saveProjectPrefs() {
    if (prefDir_.empty()) return;
    Json prefs = Json::object(), recent = Json::array();
    for (const auto& p : recentFiles_) recent.push(p);
    prefs.set("recent_files", std::move(recent));
    prefs.set("autosave_minutes", autosaveMinutes_);
    std::ofstream(fsPath(prefDir_ + "project.json"), std::ios::binary | std::ios::trunc) << prefs.dump(true);
}

void App::rememberRecent(const std::string& path) {
    if (path.empty() || prefDir_.empty()) return;
    std::error_code ec;
    std::string full = utf8(std::filesystem::absolute(fsPath(path), ec).lexically_normal());
    if (ec) full = path;
    if (full == utf8(fsPath(autosavePath(prefDir_)).lexically_normal())) return;
    recentFiles_ = addRecentFile(std::move(recentFiles_), full);
    saveProjectPrefs();
}

void App::tickProject(float dt) {
    updateTitle();
    if (!interactive() || prefDir_.empty()) return;
    if (!doc_.dirty()) {
        autosaveTimer_ = 0.0f;
        if (autosaveWritten_) { // nothing left to recover
            std::error_code ec;
            std::filesystem::remove(fsPath(autosavePath(prefDir_)), ec);
            autosaveWritten_ = false;
            autosaveRevision_ = ~uint64_t(0);
        }
        return;
    }
    autosaveTimer_ += dt;
    const uint64_t revision = doc_.undo.revision() * 1000003u + doc_.splatsVersion;
    // Edits only `modified` tracks (layer weights, Python setters) leave the revision alone, so
    // while it is set autosave every interval rather than trusting an unchanged revision.
    if (autosaveMinutes_ <= 0 || autosaveTimer_ < float(autosaveMinutes_) * 60.0f || doc_.sculptor.strokeActive() ||
        (revision == autosaveRevision_ && !doc_.modified))
        return;
    autosaveTimer_ = 0.0f;
    storeProjectSettings();
    std::string err;
    if (saveProject(autosavePath(prefDir_), doc_, err)) {
        autosaveRevision_ = revision;
        autosaveWritten_ = true;
        SDL_Log("Autosaved to %s", autosavePath(prefDir_).c_str());
    } else {
        status_ = "Autosave failed: " + err;
    }
}

void App::saveProjectAs(bool forceDialog) {
    if (!forceDialog && isProjectPath(doc_.path)) {
        saveProjectTo(doc_.path);
        return;
    }
    std::string suggestion;
    if (!doc_.path.empty()) suggestion = utf8(fsPath(doc_.path).replace_extension(kProjectExtension));
    SDL_ShowSaveFileDialog(onProjectChosen, &dialogRequests_[int(FileAction::SaveProject)], window_, kProjectFilters, 2,
                           suggestion.empty() ? nullptr : suggestion.c_str());
}

void App::saveProjectTo(const std::string& path) {
    if (path.empty()) { // dialog cancelled
        continueAfterSave_ = false;
        discardAction_ = nullptr;
        return;
    }
    runCommand("save_project", {{"path", path}});
    if (continueAfterSave_) {
        continueAfterSave_ = false;
        auto action = std::move(discardAction_);
        discardAction_ = nullptr;
        if (!doc_.dirty() && action) action(); // only once the work is really saved
    }
}

void App::storeProjectSettings() {
    Json& s = doc_.projectSettings;
    if (!s.isObject()) s = Json::object();
    Json cam = Json::object();
    cam.set("target", vec3Json(camera_.target));
    cam.set("distance", double(camera_.distance));
    cam.set("yaw", double(camera_.yaw));
    cam.set("pitch", double(camera_.pitch));
    cam.set("fov_y", double(camera_.fovY));
    s.set("camera", std::move(cam));

    Json render = Json::object();
    render.set("wireframe", renderSettings_.wireframe);
    render.set("base_color", vec3Json(renderSettings_.baseColor));
    render.set("show_polypaint", renderSettings_.showPolypaint);
    render.set("show_mask", renderSettings_.showMask);
    render.set("show_polygroups", renderSettings_.showPolygroups);
    render.set("show_mesh", renderSettings_.showMesh);
    render.set("show_splats", renderSettings_.showSplats);
    render.set("splat_opacity", double(renderSettings_.splatOpacity));
    render.set("matcap", renderSettings_.matcap < 0 ? std::string() : std::string(matcapName(renderSettings_.matcap)));
    s.set("render", std::move(render));

    Json brush = Json::object();
    brush.set("type", brushName(brush_));
    brush.set("radius_px", double(radiusPx_));
    brush.set("strength", double(strength_));
    brush.set("invert", invertToggle_);
    brush.set("front_faces_only", frontFacesOnly_);
    brush.set("pressure_strength", pressureStrength_);
    brush.set("pressure_size", pressureSize_);
    brush.set("paint_color", vec3Json(doc_.paintColor));
    s.set("brush", std::move(brush));

    Json sym = Json::object();
    sym.set("x", symmetry_.x);
    sym.set("y", symmetry_.y);
    sym.set("z", symmetry_.z);
    s.set("symmetry", std::move(sym));
}

// Lenient: anything missing keeps its current value.
void App::applyProjectSettings() {
    const Json& s = doc_.projectSettings;
    if (const Json& cam = s["camera"]; cam.isObject()) {
        readVec3(cam["target"], camera_.target);
        readFloat(cam, "distance", camera_.distance);
        readFloat(cam, "yaw", camera_.yaw);
        readFloat(cam, "pitch", camera_.pitch);
        readFloat(cam, "fov_y", camera_.fovY);
        camera_.distance = std::max(camera_.distance, 1e-4f);
        camera_.fovY = std::clamp(camera_.fovY, 0.05f, 3.0f);
    }
    if (const Json& r = s["render"]; r.isObject()) {
        readBool(r, "wireframe", renderSettings_.wireframe);
        readVec3(r["base_color"], renderSettings_.baseColor);
        readBool(r, "show_polypaint", renderSettings_.showPolypaint);
        readBool(r, "show_mask", renderSettings_.showMask);
        readBool(r, "show_polygroups", renderSettings_.showPolygroups);
        readBool(r, "show_mesh", renderSettings_.showMesh);
        readBool(r, "show_splats", renderSettings_.showSplats);
        readFloat(r, "splat_opacity", renderSettings_.splatOpacity);
        if (r["matcap"].isString()) {
            renderSettings_.matcap = -1; // unknown or empty name: studio lighting
            for (int i = 0; i < matcapCount(); ++i)
                if (r["matcap"].asString() == matcapName(i)) renderSettings_.matcap = i;
        }
    }
    if (const Json& b = s["brush"]; b.isObject()) {
        for (int i = 0; i < int(BrushType::Count); ++i)
            if (b["type"].asString() == brushName(BrushType(i))) brush_ = BrushType(i);
        readFloat(b, "radius_px", radiusPx_);
        readFloat(b, "strength", strength_);
        radiusPx_ = std::clamp(radiusPx_, 2.0f, 800.0f);
        strength_ = std::clamp(strength_, 0.0f, 1.0f);
        readBool(b, "invert", invertToggle_);
        readBool(b, "front_faces_only", frontFacesOnly_);
        readBool(b, "pressure_strength", pressureStrength_);
        readVec3(b["paint_color"], doc_.paintColor);
        readBool(b, "pressure_size", pressureSize_);
    }
    if (const Json& sym = s["symmetry"]; sym.isObject()) {
        readBool(sym, "x", symmetry_.x);
        readBool(sym, "y", symmetry_.y);
        readBool(sym, "z", symmetry_.z);
    }
}

void App::afterCommand(const std::string& name, const Args& args, const CommandResult& r) {
    if (!r.ok) return;
    const std::string path = argString(args, "path", doc_.path);
    const bool project = name == "open_project" || (name == "open" && isProjectPath(path));
    if (project) {
        if (!doc_.projectSettings["camera"].isObject()) camera_.frame(sceneBounds()); // e.g. saved by the CLI
        applyProjectSettings(); // after runCommand framed the camera, so the saved view wins
    }
    if (project || name == "open" || name == "open_obj" || name == "save_project" || name == "save_obj")
        rememberRecent(path);
}

void App::confirmDiscard(std::function<void()> action) {
    if (!interactive() || !doc_.dirty()) {
        action();
        return;
    }
    discardAction_ = std::move(action);
    continueAfterSave_ = false;
    openUnsavedPopup_ = true;
}

void App::requestQuit() {
    confirmDiscard([this] { running_ = false; });
}

void App::openPathConfirmed(const std::string& path) {
    // Images go to the Image to 3D panel and brushes into the library: no scene change.
    std::string ext = fsPath(path).extension().string();
    for (auto& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (Image::isImagePath(path) || ext == ".sfbrush") openPath(path);
    else confirmDiscard([this, path] { openPath(path); });
}

void App::drawFileMenuProjectItems() {
    if (ImGui::BeginMenu("Open Recent", !recentFiles_.empty())) {
        const size_t before = recentFiles_.size();
        recentFiles_ = pruneRecentFiles(std::move(recentFiles_), fileExists);
        if (recentFiles_.size() != before) saveProjectPrefs();
        std::string chosen;
        for (size_t i = 0; i < recentFiles_.size(); ++i) {
            const std::string label = utf8(fsPath(recentFiles_[i]).filename()) + "##recent" + std::to_string(i);
            if (ImGui::MenuItem(label.c_str())) chosen = recentFiles_[i];
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", recentFiles_[i].c_str());
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Clear Recent")) {
            recentFiles_.clear();
            saveProjectPrefs();
        }
        ImGui::EndMenu();
        if (!chosen.empty()) openPathConfirmed(chosen);
    }
    if (ImGui::MenuItem("Save", "Ctrl+S")) saveProjectAs(false);
    if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) saveProjectAs(true);
    ImGui::SetNextItemWidth(120.0f * uiScale_);
    ImGui::SliderInt("Autosave (min)", &autosaveMinutes_, 0, 60, autosaveMinutes_ ? "%d" : "off");
    if (ImGui::IsItemDeactivatedAfterEdit()) saveProjectPrefs();
}

void App::drawProjectPopups() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    if (openUnsavedPopup_) {
        ImGui::OpenPopup(kUnsavedPopup);
        openUnsavedPopup_ = false;
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(kUnsavedPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::string name = doc_.path.empty() ? std::string("Untitled") : utf8(fsPath(doc_.path).filename());
        ImGui::Text("Save changes to %s before continuing?", name.c_str());
        ImGui::TextDisabled("Your changes will be lost if you don't save them.");
        ImGui::Spacing();
        const float w = 110.0f * uiScale_;
        if (ImGui::Button("Save", ImVec2(w, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            ImGui::CloseCurrentPopup();
            if (isProjectPath(doc_.path)) {
                saveProjectTo(doc_.path);
                auto action = std::move(discardAction_);
                discardAction_ = nullptr;
                if (!doc_.dirty() && action) action();
            } else {
                continueAfterSave_ = true;
                saveProjectAs(true);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(w, 0))) {
            ImGui::CloseCurrentPopup();
            auto action = std::move(discardAction_);
            discardAction_ = nullptr;
            if (action) action();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(w, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
            discardAction_ = nullptr;
        }
        ImGui::EndPopup();
    }

    if (offerRestore_) {
        ImGui::OpenPopup(kRestorePopup);
        offerRestore_ = false;
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(kRestorePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("SculptForge did not close normally last time.");
        ImGui::TextUnformatted("Restore the autosaved work?");
        ImGui::Spacing();
        const float w = 110.0f * uiScale_;
        const std::string autosave = autosavePath(prefDir_);
        if (ImGui::Button("Restore", ImVec2(w, 0))) {
            ImGui::CloseCurrentPopup();
            runCommand("open_project", {{"path", autosave}});
            if (doc_.path == autosave) {
                doc_.path.clear();     // Save asks where to put it
                doc_.modified = true;  // still unsaved work
                autosaveWritten_ = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(w, 0))) {
            ImGui::CloseCurrentPopup();
            std::error_code ec;
            std::filesystem::remove(fsPath(autosave), ec);
        }
        ImGui::EndPopup();
    }
}

} // namespace sf
