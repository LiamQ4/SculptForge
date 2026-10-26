// CRKW CrokedWillo workspace and sculpt helper.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/App.h"
#include "app/Ui.h"
#include <IconsLucide.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace sf {
namespace {
std::filesystem::path windowSettingsPath() {
    std::filesystem::path path;
    if (char* pref = SDL_GetPrefPath("SculptForge", "SculptForge")) {
        const std::string utf8(pref);
        path = std::filesystem::path(std::u8string(utf8.begin(), utf8.end())) / "crkw-workspace.json";
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
    if (!input) {
        input.clear();
        input.open(windowSettingsPath().parent_path() / "ckw-window.json"); // migrate existing CKW settings
    }
    if (input) {
        const std::string text((std::istreambuf_iterator<char>(input)), {});
        Json settings;
        std::string error;
        if (Json::parse(text, settings, error)) {
            ckwBorderless_ = settings["borderless"].asBool(true);
            ckwStartHuion_ = settings["startHuion"].asBool(true);
            if (settings["display"].isString()) ckwDisplay_ = settings["display"].asString();
            auto color = [&](const char* key, glm::vec3& value) {
                const auto& c = settings[key];
                if (!c.isArray() || c.items().size() != 3) return;
                for (int i = 0; i < 3; ++i) {
                    const double v = c[size_t(i)].asNumber(value[i]);
                    if (std::isfinite(v)) value[i] = float(std::clamp(v, 0.0, 1.0));
                }
            };
            color("backgroundTop", renderSettings_.backgroundTop);
            color("backgroundBottom", renderSettings_.backgroundBottom);
            renderSettings_.backgroundGradient = settings["backgroundGradient"].asBool(true);
            crkwSilhouette_ = settings["silhouette"].asBool(true);
            crkwSilhouetteSize_ = std::clamp(int(settings["silhouetteSize"].asNumber(144)), 96, 224);
            crkwSilhouetteCorner_ = std::clamp(int(settings["silhouetteCorner"].asNumber(0)), 0, 1);
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
    auto color = [](const glm::vec3& c) { Json a = Json::array(); for (int i = 0; i < 3; ++i) a.push(double(c[i])); return a; };
    settings.set("backgroundTop", color(renderSettings_.backgroundTop));
    settings.set("backgroundBottom", color(renderSettings_.backgroundBottom));
    settings.set("backgroundGradient", renderSettings_.backgroundGradient);
    settings.set("silhouette", crkwSilhouette_);
    settings.set("silhouetteSize", crkwSilhouetteSize_);
    settings.set("silhouetteCorner", crkwSilhouetteCorner_);
    const auto path = windowSettingsPath();
    if (path.empty()) return;
    std::ofstream output(path);
    output << settings.dump(true) << '\n';
    if (!output) status_ = "CRKW: could not save window preferences";
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
    if (!SDL_GetDisplayBounds(target, &bounds)) { status_ = "CRKW: display is unavailable"; return; }
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
    SDL_Log("CRKW window: display=%s; borderless=%d; exclusive=%d; bounds=%d,%d %dx%d",
        SDL_GetDisplayName(SDL_GetDisplayForWindow(window_)), int(ckwBorderless_),
        int((SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0), x, y, actualWidth, actualHeight);
    if (!preferredFound) SDL_Log("CRKW: preferred display %s disconnected; using primary", ckwDisplay_.c_str());
    if (save) saveCkwWindow();
}

void App::registerCkwActions() {
    actions_.add({"ckw.clay_polish", "CRKW Clay Polish", ICON_LC_WAND_SPARKLES,
        "Smooth the visible unmasked surface, then sharpen curvature. One undo step; topology is preserved.",
        "CrokedWillo", "", "crkw_clay_polish", [this] {
            runCommand("crkw_clay_polish", {{"softness", double(ckwSoftness_)}, {"sharpness", double(ckwSharpness_)},
                {"soft_passes", double(ckwSoftPasses_)}, {"sharp_passes", double(ckwSharpPasses_)},
                {"preserve_shape", double(ckwPreserve_)}, {"edge_smoothing", double(ckwEdgeSmoothing_)}});
        }, nullptr, [this] { return doc_.mesh.vertexCount() > 0 && !doc_.sculptor.dyntopo.enabled; }});
    using T = ParamSpec::Type;
    commands_.add({"crkw_background", "Set CRKW viewport background colours (0-1 RGB); settings save automatically.",
        {{"red", T::Number, "Top/solid red", false}, {"green", T::Number, "Top/solid green", false},
         {"blue", T::Number, "Top/solid blue", false}, {"bottom_red", T::Number, "Bottom red", false},
         {"bottom_green", T::Number, "Bottom green", false}, {"bottom_blue", T::Number, "Bottom blue", false},
         {"gradient", T::Bool, "Enable gradient", false}},
        [this](Document&, const Args& args) -> CommandResult {
            auto set = [&](const char* key, float& value) {
                const double number = argNumber(args, key, value);
                if (std::isfinite(number)) value = float(std::clamp(number, 0.0, 1.0));
            };
            set("red", renderSettings_.backgroundTop.x); set("green", renderSettings_.backgroundTop.y); set("blue", renderSettings_.backgroundTop.z);
            set("bottom_red", renderSettings_.backgroundBottom.x); set("bottom_green", renderSettings_.backgroundBottom.y); set("bottom_blue", renderSettings_.backgroundBottom.z);
            renderSettings_.backgroundGradient = argBool(args, "gradient", renderSettings_.backgroundGradient);
            saveCkwWindow();
            return {true, "CRKW background updated"};
        }});
}

void App::drawPalette_Ckw() {
    ImGui::TextUnformatted("CRKW Sculpt Helper");
    ImGui::Separator();
    ui::ActionButton(*this, "ckw.clay_polish", ImVec2(-1, 30 * uiScale_));
    ImGui::PushItemWidth(-1);
    ui::SliderInside("##ckw.softness", &ckwSoftness_, 0, 1, "Softness %.2f");
    ImGui::SliderInt("##ckw.softpasses", &ckwSoftPasses_, 0, 20, "Soft Passes %d");
    ui::SliderInside("##ckw.sharpness", &ckwSharpness_, 0, 1, "Sharpness %.2f");
    ImGui::SliderInt("##ckw.sharppasses", &ckwSharpPasses_, 0, 20, "Sharp Passes %d");
    ui::SliderInside("##ckw.preserve", &ckwPreserve_, 0, 1, "Preserve Shape %.2f");
    ui::SliderInside("##ckw.edges", &ckwEdgeSmoothing_, 0, 1, "Edge Smoothing %.2f");
    if (ImGui::Button("Reset Clay Polish##crkw.reset_polish", ImVec2(-1, 0))) {
        ckwSoftness_ = .3f; ckwSharpness_ = .4f;
        ckwSoftPasses_ = ckwSharpPasses_ = 2;
        ckwPreserve_ = .7f; ckwEdgeSmoothing_ = .35f;
    }
    ImGui::TextWrapped("Respects masks and hidden vertices. Ctrl+Z undoes both stages.");
    if (doc_.sculptor.dyntopo.enabled) ImGui::TextWrapped("Disable Dynamic topology to use Clay Polish.");
    ImGui::Separator();
    ImGui::TextUnformatted("Background");
    bool appearanceChanged = ImGui::Checkbox("Gradient##crkw.background_gradient", &renderSettings_.backgroundGradient);
    appearanceChanged |= ImGui::ColorEdit3(renderSettings_.backgroundGradient ? "Top colour##crkw.background_top" : "Colour##crkw.background_top",
        &renderSettings_.backgroundTop.x, ImGuiColorEditFlags_NoInputs);
    if (renderSettings_.backgroundGradient)
        appearanceChanged |= ImGui::ColorEdit3("Bottom colour##crkw.background_bottom", &renderSettings_.backgroundBottom.x, ImGuiColorEditFlags_NoInputs);
    if (ImGui::Button("Reset background##crkw.reset_background", ImVec2(-1, 0))) {
        renderSettings_.backgroundTop = {.20f, .21f, .24f};
        renderSettings_.backgroundBottom = {.08f, .08f, .09f};
        renderSettings_.backgroundGradient = true;
        appearanceChanged = true;
    }
    ImGui::Separator();
    appearanceChanged |= ImGui::Checkbox("Silhouette preview##crkw.silhouette", &crkwSilhouette_);
    if (crkwSilhouette_) {
        appearanceChanged |= ImGui::SliderInt("##crkw.preview_size", &crkwSilhouetteSize_, 96, 224, "Preview size %d");
        appearanceChanged |= ImGui::Combo("##crkw.preview_corner", &crkwSilhouetteCorner_, "Top right\0Top left\0");
    }
    if (appearanceChanged) { crkwSilhouetteNext_ = 0; saveCkwWindow(); }
    ImGui::Separator();
    if (ImGui::Button("Settings##crkw.settings", ImVec2(-1, 0))) ImGui::OpenPopup("##crkw.settings_menu");
    ImGui::SetNextWindowSize(ImVec2(320 * uiScale_, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##crkw.settings_menu")) {
        ImGui::PushItemWidth(-1);
        ImGui::TextUnformatted("CRKW Settings");
        ImGui::Separator();
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
        if (ImGui::Checkbox("Start HuionTablet with CRKW##ckw", &ckwStartHuion_)) {
            saveCkwWindow();
            if (ckwStartHuion_ && !uiConfigDeterministic_) startCkwHuion();
        }
        if (ImGui::Button("Open HuionTablet##ckw", ImVec2(-1, 0)) && !uiConfigDeterministic_) startCkwHuion();
        ImGui::TextWrapped("Huion may request administrator consent; approve its Windows prompt if shown.");
    #endif
        ImGui::TextWrapped("Display and window mode save automatically. If the display is disconnected, use the primary screen.");
        ImGui::PopItemWidth();
        ImGui::EndPopup();
    }
    ImGui::PopItemWidth();
}

void App::drawCrkwSilhouette() {
    if (!crkwSilhouette_ || !options_.renderPath.empty() || !options_.recordPath.empty()) return;
    const double now = ImGui::GetTime();
    if (!crkwSilhouetteTexture_ || now >= crkwSilhouetteNext_) {
        crkwSilhouetteTexture_ = renderer_.silhouetteTexture(camera_, crkwSilhouetteSize_);
        crkwSilhouetteNext_ = now + .15; // modest GPU cost on dense sculpts; no CPU mesh copies
    }
    const CanvasRect canvas = canvasRect();
    const float size = float(crkwSilhouetteSize_) * uiScale_;
    const float width = size + 16 * uiScale_, height = size + 36 * uiScale_;
    if (canvas.w < width + 24 || canvas.h < height + 24) return;
    const float x = crkwSilhouetteCorner_ == 0 ? canvas.x + canvas.w - width - 12 * uiScale_ : canvas.x + 12 * uiScale_;
    ImGui::SetNextWindowPos(ImVec2(x, canvas.y + 12 * uiScale_));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    if (ImGui::Begin("CRKW Silhouette##crkw.preview", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::TextUnformatted("Silhouette");
        if (crkwSilhouetteTexture_)
            ImGui::Image(ImTextureID(uintptr_t(crkwSilhouetteTexture_)), ImVec2(size, size), ImVec2(0, 1), ImVec2(1, 0));
    }
    ImGui::End();
}
} // namespace sf
