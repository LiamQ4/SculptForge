// CRKW shelf camera and canvas controls.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/App.h"
#include "app/I18n.h"
#include "app/NativeDialog.h"
#include <IconsLucide.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <mutex>

namespace sf {
namespace {
std::mutex exportMutex;
std::vector<std::string> exportPaths;
void SDLCALL cameraExportChosen(void*, const char* const* files, int) {
    if (!files || !files[0]) return;
    std::string file = files[0];
    if (std::filesystem::path(std::u8string(file.begin(), file.end())).extension().empty()) file += ".png";
    std::lock_guard lock(exportMutex);
    exportPaths.push_back(std::move(file));
}
}

CanvasRect App::fitCameraCanvas(CanvasRect rect) const {
    if (!crkwCanvasFixed_ || rect.w <= 0 || rect.h <= 0) return rect;
    const float aspect = float(crkwCanvasWidth_) / float(crkwCanvasHeight_);
    const float w = std::min(rect.w, rect.h * aspect), h = w / aspect;
    rect.x += (rect.w - w) * .5f; rect.y += (rect.h - h) * .5f;
    rect.w = w; rect.h = h;
    return rect;
}

void App::initCameraController() {
    actions_.add({"crkw.camera", "Camera", ICON_LC_CAMERA,
        "Camera and canvas: perspective / orthographic, lens, field of view and output size.",
        "Render", "", "", [this] { showCameraController_ = !showCameraController_; },
        [this] { return showCameraController_; }, nullptr});
    using T = ParamSpec::Type;
    commands_.add({"crkw_camera", "Set CRKW camera projection, lens and fitted canvas output resolution.",
        {{"perspective", T::Bool, "Perspective on; false for orthographic", false},
         {"fov", T::Number, "Vertical field of view in degrees (5-120)", false},
         {"mm", T::Number, "Lens in mm (7-270), 24 mm sensor height; use fov or mm", false},
         {"fixed", T::Bool, "Fit the requested canvas aspect inside the viewport", false},
         {"width", T::Number, "Canvas width in pixels (64-8192)", false},
         {"height", T::Number, "Canvas height in pixels (64-8192); at most 16 megapixels", false}},
        [this](Document& doc, const Args& args) -> CommandResult {
            const double w = argNumber(args, "width", crkwCanvasWidth_), h = argNumber(args, "height", crkwCanvasHeight_);
            const double fov = argNumber(args, "fov", glm::degrees(camera_.fovY)), mm = argNumber(args, "mm", 50);
            if (!std::isfinite(w) || !std::isfinite(h) || w < 64 || h < 64 || w > 8192 || h > 8192 ||
                std::floor(w) != w || std::floor(h) != h || w * h > 16777216)
                return {false, "Canvas: use 64-8192 pixels per side and at most 16 megapixels"};
            if (args.contains("mm") && args.contains("fov")) return {false, "Set either mm or fov"};
            if (!std::isfinite(fov) || (args.contains("fov") && (fov < 5 || fov > 120)) ||
                !std::isfinite(mm) || (args.contains("mm") && (mm < 7 || mm > 270)))
                return {false, "Use FOV 5-120 degrees or lens 7-270 mm"};
            if (args.contains("mm")) camera_.setFocalLengthMm(float(mm));
            else if (args.contains("fov")) camera_.fovY = glm::radians(float(fov));
            camera_.ortho = !argBool(args, "perspective", !camera_.ortho);
            crkwCanvasWidth_ = int(w); crkwCanvasHeight_ = int(h);
            crkwCanvasDraft_[0] = int(w); crkwCanvasDraft_[1] = int(h);
            crkwCanvasFixed_ = argBool(args, "fixed", crkwCanvasFixed_);
            updateCameraAspect(); doc.modified = true;
            return {true, std::string(camera_.ortho ? "Orthographic" : "Perspective") + " camera, canvas " +
                std::to_string(int(w)) + "x" + std::to_string(int(h))};
        }});
    commands_.add({"crkw_camera_export", "Render the current camera to a PNG at the selected canvas pixel resolution.",
        {{"path", T::String, "Output PNG path", true}},
        [this](Document&, const Args& args) -> CommandResult {
            syncScene();
            Camera camera = camera_;
            camera.aspect = float(crkwCanvasWidth_) / float(crkwCanvasHeight_);
            Image image; image.width = crkwCanvasWidth_; image.height = crkwCanvasHeight_;
            std::string error;
            RenderSettings settings = renderSettings_;
            settings.motionProxy = false;
            if (!renderer_.renderToImage(camera, image.width, image.height, settings, image.rgba, error) ||
                !image.savePng(argString(args, "path"), error)) return {false, error};
            return {true, "Saved canvas " + std::to_string(image.width) + "x" + std::to_string(image.height)};
        }});
}

void App::drawCameraController() {
    std::vector<std::string> paths;
    { std::lock_guard lock(exportMutex); paths.swap(exportPaths); }
    for (const auto& path : paths) runCommand("crkw_camera_export", {{"path", path}});
    if (!showCameraController_) return;
    ImGui::SetNextWindowSize(ImVec2(310 * uiScale_, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(shell_.rightShelf.x - 320 * uiScale_, shell_.canvas.y + 12 * uiScale_), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(tr("CRKW Camera"), &showCameraController_, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::RadioButton(tr("Persp##camera.persp"), !camera_.ortho)) runCommand("crkw_camera", {{"perspective", true}});
        ImGui::SameLine();
        if (ImGui::RadioButton(tr("Ortho##camera.ortho"), camera_.ortho)) runCommand("crkw_camera", {{"perspective", false}});
        ImGui::Separator();
        ImGui::BeginDisabled(camera_.ortho);
        float mm = camera_.focalLengthMm(), fov = glm::degrees(camera_.fovY);
        ImGui::SetNextItemWidth(130 * uiScale_);
        if (ImGui::InputFloat(tr("Lens mm##camera.mm"), &mm, 0, 0, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue))
            runCommand("crkw_camera", {{"mm", double(mm)}});
        ImGui::SetNextItemWidth(130 * uiScale_);
        if (ImGui::InputFloat(tr("FOV vertical##camera.fov"), &fov, 0, 0, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue))
            runCommand("crkw_camera", {{"fov", double(fov)}});
        ImGui::TextDisabled("%s", tr("Full frame: 24 mm sensor height"));
        ImGui::EndDisabled();
        if (camera_.ortho) ImGui::TextDisabled("%s", tr("Lens retained for perspective mode"));
        if (ImGui::Button(tr("Frame model##camera.frame"), ImVec2(-1, 0))) camera_.frame(sceneBounds());
        ImGui::Separator();
        bool fixed = crkwCanvasFixed_;
        if (ImGui::Checkbox(tr("Fixed canvas##camera.fixed"), &fixed)) runCommand("crkw_camera", {{"fixed", fixed}});
        ImGui::TextUnformatted(tr("Canvas pixels (W / H)"));
        ImGui::SetNextItemWidth(-1);
        ImGui::InputInt2("##camera.pixels", crkwCanvasDraft_);
        if (ImGui::Button(tr("Apply size##camera.apply"), ImVec2(-1, 0)))
            runCommand("crkw_camera", {{"width", double(crkwCanvasDraft_[0])}, {"height", double(crkwCanvasDraft_[1])}, {"fixed", true}});
        if (ImGui::Button(tr("1K square##camera.square"))) runCommand("crkw_camera", {{"width", 1024.0}, {"height", 1024.0}, {"fixed", true}});
        ImGui::SameLine();
        if (ImGui::Button("1080p##camera.hd")) runCommand("crkw_camera", {{"width", 1920.0}, {"height", 1080.0}, {"fixed", true}});
        ImGui::SameLine();
        if (ImGui::Button("4K##camera.4k")) runCommand("crkw_camera", {{"width", 3840.0}, {"height", 2160.0}, {"fixed", true}});
        if (ImGui::Button(tr("Swap W / H##camera.swap"), ImVec2(-1, 0)))
            runCommand("crkw_camera", {{"width", double(crkwCanvasHeight_)}, {"height", double(crkwCanvasWidth_)}, {"fixed", true}});
        ImGui::TextDisabled(tr("%dx%d pixels - %s"), crkwCanvasWidth_, crkwCanvasHeight_, crkwCanvasFixed_ ? "fit preview" : "export size");
        if (ImGui::Button(tr("Export canvas PNG...##camera.export"), ImVec2(-1, 0))) {
            static const SDL_DialogFileFilter png[] = {{"PNG image", "png"}};
            sf::dialogs::saveFile(cameraExportChosen, nullptr, window_, png, 1, nullptr);
        }
        if (ImGui::Button(tr("Reset camera settings##camera.reset"), ImVec2(-1, 0)))
            runCommand("crkw_camera", {{"perspective", true}, {"fov", 35.0}, {"fixed", false}, {"width", 1920.0}, {"height", 1080.0}});
        ImGui::TextWrapped("%s", status_.c_str());
    }
    ImGui::End();
}
} // namespace sf
