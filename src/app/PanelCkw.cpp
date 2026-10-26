// CKW CrokedWillo workspace and sculpt helper.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/App.h"
#include "app/Ui.h"
#include <IconsLucide.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace sf {
namespace {
std::filesystem::path windowSettingsPath() {
    std::filesystem::path path;
    if (char* pref = SDL_GetPrefPath("SculptForge", "SculptForge")) {
        path = std::filesystem::u8path(pref) / "ckw-window.json";
        SDL_free(pref);
    }
    return path;
}
std::string folded(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}

void App::initCkwWindow() {
    std::ifstream input(windowSettingsPath());
    if (input) {
        const std::string text((std::istreambuf_iterator<char>(input)), {});
        Json settings;
        std::string error;
        if (Json::parse(text, settings, error)) {
            ckwBorderless_ = settings["borderless"].asBool(true);
            ckwStartHuion_ = settings["startHuion"].asBool(true);
            if (settings["display"].isString()) ckwDisplay_ = settings["display"].asString();
        }
    }
    applyCkwWindow(false);
    if (ckwStartHuion_) startCkwHuion();
}

void App::saveCkwWindow() {
    if (uiConfigDeterministic_ || !options_.uiTest.empty()) return;
    Json settings = Json::object();
    settings.set("borderless", ckwBorderless_);
    settings.set("startHuion", ckwStartHuion_);
    settings.set("display", ckwDisplay_);
    const auto path = windowSettingsPath();
    if (path.empty()) return;
    std::ofstream output(path);
    output << settings.dump(true) << '\n';
    if (!output) status_ = "CKW: could not save window preferences";
}

void App::applyCkwWindow(bool save) {
    if (!options_.uiTest.empty() || !options_.recordPath.empty() || !options_.renderPath.empty()) return;
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    SDL_DisplayID target = SDL_GetPrimaryDisplay();
    bool preferredFound = false;
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetDisplayName(displays[i]);
        if (name && folded(name) == folded(ckwDisplay_)) {
            target = displays[i]; preferredFound = true; break;
        }
    }
    if (!preferredFound && folded(ckwDisplay_).find("kamvas") != std::string::npos) {
        for (int i = 0; i < count; ++i) {
            const char* name = SDL_GetDisplayName(displays[i]);
            if (name && (folded(name).find("kamvas") != std::string::npos || folded(name).find("huion") != std::string::npos)) {
                target = displays[i]; preferredFound = true; break;
            }
        }
    }
    SDL_free(displays);
    SDL_Rect bounds{};
    if (!SDL_GetDisplayBounds(target, &bounds)) { status_ = "CKW: display is unavailable"; return; }
    // A regular borderless desktop-sized window, never an exclusive display-mode switch.
    SDL_SetWindowFullscreen(window_, false);
    SDL_RestoreWindow(window_);
    SDL_SetWindowBordered(window_, !ckwBorderless_);
    const int width = ckwBorderless_ ? bounds.w : std::min(1600, std::max(640, bounds.w - 80));
    const int height = ckwBorderless_ ? bounds.h : std::min(960, std::max(480, bounds.h - 100));
    SDL_SetWindowPosition(window_, bounds.x + (bounds.w - width) / 2, bounds.y + (bounds.h - height) / 2);
    SDL_SetWindowSize(window_, width, height);
    SDL_SyncWindow(window_);
    int x = 0, y = 0, actualWidth = 0, actualHeight = 0;
    SDL_GetWindowPosition(window_, &x, &y);
    SDL_GetWindowSize(window_, &actualWidth, &actualHeight);
    SDL_Log("CKW window: display=%s; borderless=%d; exclusive=%d; bounds=%d,%d %dx%d",
        SDL_GetDisplayName(SDL_GetDisplayForWindow(window_)), int(ckwBorderless_),
        int((SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0), x, y, actualWidth, actualHeight);
    if (!preferredFound) SDL_Log("CKW: preferred display %s disconnected; using primary", ckwDisplay_.c_str());
    if (save) saveCkwWindow();
}

void App::registerCkwActions() {
    actions_.add({"ckw.clay_polish", "CKW Clay Polish", ICON_LC_WAND_SPARKLES,
        "Smooth the visible unmasked surface, then sharpen curvature. One undo step; topology is preserved.",
        "CrokedWillo", "", "ckw_clay_polish", [this] {
            runCommand("ckw_clay_polish", {{"softness", double(ckwSoftness_)}, {"sharpness", double(ckwSharpness_)},
                {"soft_passes", double(ckwSoftPasses_)}, {"sharp_passes", double(ckwSharpPasses_)},
                {"preserve_shape", double(ckwPreserve_)}, {"edge_smoothing", double(ckwEdgeSmoothing_)}});
        }, nullptr, [this] { return doc_.mesh.vertexCount() > 0 && !doc_.sculptor.dyntopo.enabled; }});
}

void App::drawPalette_Ckw() {
    ImGui::TextUnformatted("CKW Sculpt Helper");
    ImGui::Separator();
    ui::ActionButton(*this, "ckw.clay_polish", ImVec2(-1, 30 * uiScale_));
    ImGui::PushItemWidth(-1);
    ui::SliderInside("##ckw.softness", &ckwSoftness_, 0, 1, "Softness %.2f");
    ImGui::SliderInt("##ckw.softpasses", &ckwSoftPasses_, 0, 20, "Soft Passes %d");
    ui::SliderInside("##ckw.sharpness", &ckwSharpness_, 0, 1, "Sharpness %.2f");
    ImGui::SliderInt("##ckw.sharppasses", &ckwSharpPasses_, 0, 20, "Sharp Passes %d");
    ui::SliderInside("##ckw.preserve", &ckwPreserve_, 0, 1, "Preserve Shape %.2f");
    ui::SliderInside("##ckw.edges", &ckwEdgeSmoothing_, 0, 1, "Edge Smoothing %.2f");
    ImGui::TextWrapped("Respects masks and hidden vertices. Ctrl+Z undoes both stages.");
    if (doc_.sculptor.dyntopo.enabled) ImGui::TextWrapped("Disable Dynamic topology to use Clay Polish.");
    ImGui::Separator();
    ImGui::TextUnformatted("CKW Window");
    if (ImGui::Checkbox("Windowed fullscreen (borderless)##ckw", &ckwBorderless_)) applyCkwWindow();
    ImGui::TextDisabled("F11 toggles windowed fullscreen");
    ImGui::TextUnformatted("Open on display");
    if (ImGui::BeginCombo("##ckw.display", ckwDisplay_.c_str())) {
        int count = 0;
        SDL_DisplayID* displays = SDL_GetDisplays(&count);
        for (int i = 0; i < count; ++i) {
            const char* name = SDL_GetDisplayName(displays[i]);
            if (!name) continue;
            ImGui::PushID(int(displays[i]));
            if (ImGui::Selectable(name, ckwDisplay_ == name)) { ckwDisplay_ = name; applyCkwWindow(); }
            ImGui::PopID();
        }
        SDL_free(displays);
        ImGui::EndCombo();
    }
    if (ImGui::Button("Move to saved display##ckw", ImVec2(-1, 0))) applyCkwWindow();
#if defined(_WIN32)
    if (ImGui::Checkbox("Start HuionTablet with CKW##ckw", &ckwStartHuion_)) {
        saveCkwWindow();
        if (ckwStartHuion_ && !uiConfigDeterministic_) startCkwHuion();
    }
    if (ImGui::Button("Open HuionTablet##ckw", ImVec2(-1, 0)) && !uiConfigDeterministic_) startCkwHuion();
    ImGui::TextWrapped("Huion may request administrator consent; approve its Windows prompt if shown.");
#endif
    ImGui::TextWrapped("Display and window mode save automatically. If the display is disconnected, use the primary screen.");
    ImGui::PopItemWidth();
}
} // namespace sf
