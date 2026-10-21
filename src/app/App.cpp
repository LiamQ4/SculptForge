// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/App.h"

#include "ai/AiCommands.h"
#include "app/Demo.h"

#include <imgui_internal.h>
#include "ai/Generator3D.h"
#include "core/EnginePresets.h"
#include "link/UnrealLink.h"
#include "core/Decimate.h"
#include "core/ImageTo3D.h"
#include "core/Script.h"
#include "render/GL.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <memory>
#include <thread>

namespace sf {

namespace assets {
extern const unsigned char fontRegular[];
extern const unsigned long long fontRegular_size;
extern const unsigned char fontSemiBold[];
extern const unsigned long long fontSemiBold_size;
extern const unsigned char appIcon[];
extern const unsigned long long appIcon_size;
} // namespace assets

namespace {

const SDL_DialogFileFilter kObjFilters[] = {{"Wavefront OBJ", "obj"}, {"All files", "*"}};
const SDL_DialogFileFilter kGlbFilters[] = {{"glTF binary", "glb"}, {"All files", "*"}};
const SDL_DialogFileFilter kBrushFilters[] = {{"SculptForge brush", "sfbrush"}, {"All files", "*"}};
const SDL_DialogFileFilter kModelFilters[] = {{"Models and splats", "obj;fbx;glb;gltf;ply;splat;usda;usd;usdz"}, {"All files", "*"}};
const SDL_DialogFileFilter kFbxFilters[] = {{"FBX", "fbx"}, {"All files", "*"}};
const SDL_DialogFileFilter kUsdFilters[] = {{"USDZ package", "usdz"}, {"USD text", "usda"}};
const SDL_DialogFileFilter kSplatFilters[] = {{"Gaussian splats (PLY)", "ply"}, {"glTF splats", "glb"}, {"OpenUSD", "usda;usdz"}};
const SDL_DialogFileFilter kImageFilters[] = {{"Images", "png;jpg;jpeg;bmp;tga;psd;gif"}, {"All files", "*"}};

const char* kExamplePrompts[] = {
    "Sharp wrinkles that fade out at the ends",
    "Overlapping dragon scales",
    "Weathered, chipped stone edges",
    "Dry cracked mud",
    "Soft clay buildup like a thumb smear",
};

bool ctrlDown(SDL_Keymod mod) { return (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0; }

void applyStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 6.0f;
    s.FrameRounding = 4.0f;
    s.GrabRounding = 4.0f;
    s.WindowBorderSize = 0.0f;
    s.FramePadding = ImVec2(8, 5);
    s.ItemSpacing = ImVec2(8, 6);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.11f, 0.12f, 0.94f);
    c[ImGuiCol_Button] = ImVec4(0.22f, 0.22f, 0.25f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.85f, 0.50f, 0.25f, 0.85f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.95f, 0.55f, 0.25f, 1.0f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.90f, 0.55f, 0.25f, 1.0f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.62f, 0.30f, 1.0f);
    c[ImGuiCol_CheckMark] = ImVec4(1.00f, 0.62f, 0.30f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.85f, 0.50f, 0.25f, 0.45f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.85f, 0.50f, 0.25f, 0.70f);
    c[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.20f, 1.0f);
}

} // namespace

App::App() = default;
App::~App() = default;

bool App::init(const AppOptions& options) {
    options_ = options;
    const bool recording = !options.recordPath.empty();
    const bool headless = !options.renderPath.empty() || recording;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    const SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                  (headless ? SDL_WINDOW_HIDDEN : 0);
    const int winW = recording ? options.width : 1600, winH = recording ? options.height : 960;
    window_ = SDL_CreateWindow("SculptForge", winW, winH, flags);
    if (!window_) {
        // Retry without multisampling for drivers that refuse it.
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        window_ = SDL_CreateWindow("SculptForge", winW, winH, flags);
    }
    if (!window_) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    {
        Image icon;
        std::string iconErr;
        if (Image::decode(assets::appIcon, size_t(assets::appIcon_size), icon, iconErr)) {
            if (SDL_Surface* surface = SDL_CreateSurfaceFrom(icon.width, icon.height, SDL_PIXELFORMAT_RGBA32, icon.rgba.data(),
                                                             icon.width * 4)) {
                SDL_SetWindowIcon(window_, surface);
                SDL_DestroySurface(surface);
            }
        }
    }
    glContext_ = SDL_GL_CreateContext(window_);
    if (!glContext_) {
        SDL_Log("OpenGL 3.3 context creation failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(window_, glContext_);
    SDL_GL_SetSwapInterval(1);

    if (const char* missing = gl::load([](const char* n) { return reinterpret_cast<gl::GenericProc>(SDL_GL_GetProcAddress(n)); })) {
        SDL_Log("Missing OpenGL function: %s", missing);
        return false;
    }
    SDL_Log("OpenGL %s on %s", reinterpret_cast<const char*>(gl::GetString(GL_VERSION)),
            reinterpret_cast<const char*>(gl::GetString(GL_RENDERER)));

    std::string err;
    if (!renderer_.init(err)) {
        SDL_Log("%s", err.c_str());
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    if (char* pref = SDL_GetPrefPath("SculptForge", "SculptForge")) {
        iniPath_ = std::string(pref) + "imgui.ini";
        io.IniFilename = iniPath_.c_str();
        aiSettingsPath_ = std::string(pref) + "ai.json";
        aiSettings_ = ai::AiSettings::load(aiSettingsPath_);
        SDL_free(pref);
    } else {
        io.IniFilename = nullptr;
    }
    ai::registerAiCommands(commands_, [this] { return aiSettings_; });
    std::snprintf(falKey_, sizeof falKey_, "%s", aiSettings_.falKey.c_str());
    std::snprintf(falModel_, sizeof falModel_, "%s", aiSettings_.falModel.c_str());
    std::snprintf(meshyKey_, sizeof meshyKey_, "%s", aiSettings_.meshyKey.c_str());
    std::snprintf(hfToken_, sizeof hfToken_, "%s", aiSettings_.hfToken.c_str());
    // Pixel density (Retina / Wayland scaling) is handled by the backend via the
    // framebuffer scale; only scale the UI for any additional content scaling.
    float density = SDL_GetWindowPixelDensity(window_);
    float scale = SDL_GetWindowDisplayScale(window_) / (density > 0.0f ? density : 1.0f);
    if (scale <= 0.0f) scale = 1.0f;
    if (recording) scale = float(options.height) / 900.0f; // UI sized for the video, not this screen
    uiScale_ = scale;
    applyStyle();
    ImGui::GetStyle().ScaleAllSizes(scale);
    // Inter (SIL OFL), embedded in the binary. Rasterised at the final size for crisp text.
    ImFontConfig fontConfig;
    fontConfig.FontDataOwnedByAtlas = false;
    fontConfig.OversampleH = 2;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontRegular), int(assets::fontRegular_size),
                                   15.0f * scale, &fontConfig);
    if (recording) {
        fontCaption_ = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontSemiBold),
                                                      int(assets::fontSemiBold_size), float(options.height) * 0.042f,
                                                      &fontConfig);
        fontCaptionSub_ = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontRegular),
                                                         int(assets::fontRegular_size), float(options.height) * 0.024f,
                                                         &fontConfig);
        ImGui::GetCurrentContext()->TestEngineHookItems = true; // lets the demo find buttons by label
        io.IniFilename = nullptr;                               // fixed, reproducible layout
    }
    ImGui_ImplSDL3_InitForOpenGL(window_, glContext_);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    for (int i = 0; i < int(FileAction::Count); ++i) dialogRequests_[i] = {this, FileAction(i)};
    runCommand("new_sphere");
    if (!options.openPath.empty()) openPath(options.openPath);
    if (!options.script.empty()) {
        std::vector<ScriptStatement> statements;
        std::string err;
        if (!parseScript(options.script, statements, err)) {
            SDL_Log("Script error: %s", err.c_str());
            return false;
        }
        for (const auto& st : statements) {
            CommandResult r = commands_.execute(doc_, st.command, st.args);
            SDL_Log("%s: %s", st.command.c_str(), r.message.c_str());
            if (!r.ok && headless) return false;
        }
        camera_.frame(sceneBounds());
        updateTitle();
    }
    if (!aiSettings_.generatorCommand.empty())
        std::snprintf(generatorCommand_, sizeof generatorCommand_, "%s", aiSettings_.generatorCommand.c_str());
    else if (const char* base = SDL_GetBasePath()) {
#if defined(_WIN32)
        const char* cliName = "sculptforge-cli.exe";
#else
        const char* cliName = "sculptforge-cli";
#endif
        std::snprintf(generatorCommand_, sizeof generatorCommand_, "%s",
                      ai::defaultGeneratorCommand(std::string(base) + cliName).c_str());
    }
    return true;
}

void App::shutdown() {
    if (refTexture_) gl::DeleteTextures(1, &refTexture_);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    renderer_.shutdown();
    if (glContext_) SDL_GL_DestroyContext(glContext_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

int App::renderHeadless() {
    const int frames = std::max(1, options_.turntable);
    camera_.frame(options_.hideMesh && !doc_.splats.empty() ? doc_.splats.bounds(0.9f) : sceneBounds());
    camera_.aspect = float(options_.width) / float(std::max(options_.height, 1));
    camera_.pitch = glm::radians(options_.pitchDegrees);
    RenderSettings settings = renderSettings_;
    settings.wireframe = options_.wireframe;
    settings.showMesh = !options_.hideMesh;
    renderer_.sync(doc_.mesh);
    renderer_.syncSplats(doc_.splats, doc_.splatsVersion);
    const std::filesystem::path out(std::u8string(options_.renderPath.begin(), options_.renderPath.end()));
    for (int f = 0; f < frames; ++f) {
        camera_.yaw = glm::radians(options_.yawDegrees + 360.0f * float(f) / float(frames));
        Image img;
        img.width = options_.width;
        img.height = options_.height;
        std::string err;
        if (!renderer_.renderToImage(camera_, img.width, img.height, settings, img.rgba, err)) {
            SDL_Log("Render failed: %s", err.c_str());
            return 1;
        }
        std::string path = options_.renderPath;
        if (options_.turntable > 0) {
            char suffix[16];
            std::snprintf(suffix, sizeof suffix, "_%03d.png", f);
            auto stem = (out.parent_path() / out.stem()).u8string();
            path = std::string(stem.begin(), stem.end()) + suffix;
        }
        if (!img.savePng(path, err)) {
            SDL_Log("%s", err.c_str());
            return 1;
        }
        SDL_Log("Rendered %s", path.c_str());
    }
    return 0;
}

int App::runRecording() {
    demo_ = std::make_unique<Demo>(*this);
    std::string err;
    if (!demo_->load(options_.demo.empty() ? "sculpt" : options_.demo, err)) {
        SDL_Log("%s", err.c_str());
        return 1;
    }
    const int w = options_.width, h = options_.height, fps = std::max(options_.fps, 1);
    // Frames go straight to ffmpeg as raw RGBA.
    std::string cmd = "ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgba -s " + std::to_string(w) + "x" + std::to_string(h) +
                      " -r " + std::to_string(fps) +
                      " -i - -c:v libx264 -preset slow -crf 20 -pix_fmt yuv420p -movflags +faststart \"" +
                      options_.recordPath + "\"";
#if defined(_WIN32)
    FILE* ff = _popen(cmd.c_str(), "wb");
#else
    FILE* ff = popen(cmd.c_str(), "w");
#endif
    if (!ff) {
        SDL_Log("Cannot start ffmpeg");
        return 1;
    }
    std::vector<uint8_t> rgba;
    const float dt = 1.0f / float(fps);
    int frames = 0, written = 0;
    while (!demo_->finished() && frames < fps * 60 * 30) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
        } // the demo provides all input
        demo_->update(dt);
        processPendingFiles();
        animateLayers(dt);
        pollBrushRequest();
        pollUnreal();
        camera_.aspect = float(w) / float(h);

        Demo::beginFrame();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(float(w), float(h));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        io.DeltaTime = dt;
        ImGui::NewFrame();
        drawUi();
        demo_->drawOverlay();
        ImGui::Render();

        Mesh& shown = displayedMesh();
        if (&shown != lastDisplayed_) shown.topologyDirty = true;
        lastDisplayed_ = &shown;
        renderer_.sync(shown);
        renderer_.syncSplats(doc_.splats, doc_.splatsVersion);
        ++frames;
        // While waiting on a model, keep only every Nth frame (marked on screen as sped up).
        if ((frames % demo_->frameStride()) != 0) continue;
        if (!renderer_.beginCapture(w, h, err)) {
            SDL_Log("%s", err.c_str());
            break;
        }
        renderer_.draw(camera_, w, h, renderSettings_);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        renderer_.endCapture(rgba);
        fwrite(rgba.data(), 1, rgba.size(), ff);
        ++written;
    }
#if defined(_WIN32)
    const int status = _pclose(ff);
#else
    const int status = pclose(ff);
#endif
    if (!status_.empty()) SDL_Log("Last status: %s", status_.c_str());
    SDL_Log("Recorded %d frames (%.1f s) to %s", written, float(written) / float(fps), options_.recordPath.c_str());
    return status == 0 ? 0 : 1;
}

int App::run() {
    if (!options_.renderPath.empty()) return renderHeadless();
    if (!options_.recordPath.empty()) return runRecording();
    Uint64 lastTicks = SDL_GetTicksNS();
    while (running_) {
        Uint64 now = SDL_GetTicksNS();
        float dt = float(double(now - lastTicks) * 1e-9);
        lastTicks = now;
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e);
        processPendingFiles();
        animateLayers(dt);
        pollBrushRequest();
        pollGenerator();
        pollUnreal();

        int fbW = 0, fbH = 0, winW = 0, winH = 0;
        SDL_GetWindowSizeInPixels(window_, &fbW, &fbH);
        SDL_GetWindowSize(window_, &winW, &winH);
        if (fbW <= 0 || fbH <= 0) {
            SDL_Delay(16);
            continue;
        }
        camera_.aspect = float(fbW) / float(fbH);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        drawUi();
        ImGui::Render();

        Mesh& shown = displayedMesh();
        if (&shown != lastDisplayed_) shown.topologyDirty = true;
        lastDisplayed_ = &shown;
        renderer_.sync(shown);
        renderer_.syncSplats(doc_.splats, doc_.splatsVersion);
        renderer_.draw(camera_, fbW, fbH, renderSettings_);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window_);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void App::handleEvent(const SDL_Event& e) {
    ImGui_ImplSDL3_ProcessEvent(&e);
    const ImGuiIO& io = ImGui::GetIO();

    switch (e.type) {
    case SDL_EVENT_QUIT:
        running_ = false;
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        if (e.window.windowID == SDL_GetWindowID(window_)) running_ = false;
        break;
    case SDL_EVENT_PEN_PROXIMITY_IN:
        penSeen_ = true;
        break;
    case SDL_EVENT_PEN_AXIS:
        if (e.paxis.axis == SDL_PEN_AXIS_PRESSURE) penPressure_ = e.paxis.value;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        lastInputPen_ = e.button.which == SDL_PEN_MOUSEID;
        if (!io.WantCaptureMouse) onMouseDown(e.button.x, e.button.y, e.button.button);
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        onMouseUp();
        break;
    case SDL_EVENT_MOUSE_MOTION:
        onMouseMove(e.motion.x, e.motion.y, e.motion.xrel, e.motion.yrel, e.motion.which == SDL_PEN_MOUSEID);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (!io.WantCaptureMouse) camera_.zoom(e.wheel.y);
        break;
    case SDL_EVENT_KEY_DOWN:
        if (!io.WantCaptureKeyboard && !e.key.repeat) onKey(e.key.key, e.key.mod);
        else if (!io.WantCaptureKeyboard && (e.key.key == SDLK_LEFTBRACKET || e.key.key == SDLK_RIGHTBRACKET))
            onKey(e.key.key, e.key.mod);
        break;
    case SDL_EVENT_DROP_FILE:
        if (e.drop.data) openPath(e.drop.data);
        break;
    default:
        break;
    }
}

bool App::pick(float x, float y, RayHit& hit) const {
    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    glm::vec3 origin, dir;
    camera_.screenRay(x, y, float(w), float(h), origin, dir);
    return doc_.bvh.raycast(doc_.mesh, origin, dir, hit);
}

BrushType App::activeBrush() const {
    // ZBrush convention: holding Shift temporarily switches to Smooth.
    return (SDL_GetModState() & SDL_KMOD_SHIFT) ? BrushType::Smooth : brush_;
}

void App::onMouseDown(float x, float y, Uint8 button) {
    mouseX_ = x;
    mouseY_ = y;
    const SDL_Keymod mod = SDL_GetModState();
    doc_.syncBvh();
    if (button == SDL_BUTTON_LEFT) {
        if (polygroupMouseDown(x, y, mod)) return;
        RayHit hit;
        const bool maskMod = ctrlDown(mod) && !(mod & SDL_KMOD_SHIFT);
        if (previewLod_ < 0 && pick(x, y, hit)) {
            if (maskMod) beginMaskStroke(x, y);
            else beginStroke(x, y, hit);
        } else if (maskMod && !(mod & SDL_KMOD_ALT) && previewLod_ < 0) {
            drag_ = Drag::MaskCanvas; // click inverts, drag clears (or zooms without a mask)
            maskCanvasX_ = x;
            maskCanvasY_ = y;
            maskCanvasMoved_ = false;
        } else {
            // Dragging on empty space navigates (ZBrush style).
            drag_ = (mod & SDL_KMOD_ALT) ? Drag::Pan : ctrlDown(mod) ? Drag::Zoom : Drag::Orbit;
        }
    } else if (button == SDL_BUTTON_RIGHT) {
        drag_ = (mod & SDL_KMOD_SHIFT) ? Drag::Pan : Drag::Orbit;
    } else if (button == SDL_BUTTON_MIDDLE) {
        drag_ = (mod & SDL_KMOD_CTRL) ? Drag::Zoom : Drag::Pan;
    }
}

void App::onMouseMove(float x, float y, float dx, float dy, bool fromPen) {
    mouseX_ = x;
    mouseY_ = y;
    lastInputPen_ = fromPen;
    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    switch (drag_) {
    case Drag::Stroke: strokeTo(x, y); break;
    case Drag::Orbit: camera_.orbit(dx, dy); break;
    case Drag::Pan: camera_.pan(dx, dy, float(h)); break;
    case Drag::Zoom: camera_.zoom(-dy * 0.05f); break;
    case Drag::MaskCanvas: maskCanvasMove(x, y); break;
    case Drag::None: break;
    }
}

void App::onMouseUp() {
    if (hideRectActive_) polygroupMouseUp();
    if (drag_ == Drag::Stroke) endStroke();
    if (drag_ == Drag::MaskCanvas) maskCanvasRelease();
    drag_ = Drag::None;
}

void App::onKey(SDL_Keycode key, SDL_Keymod mod) {
    const bool ctrl = ctrlDown(mod);
    const bool shift = (mod & SDL_KMOD_SHIFT) != 0;
    if (ctrl) {
        switch (key) {
        case SDLK_Z: runCommand(shift ? "redo" : "undo"); break;
        case SDLK_Y: runCommand("redo"); break;
        case SDLK_D: runCommand("subdivide"); break;
        case SDLK_O: openFileDialog(); break;
        case SDLK_S:
            if (shift || doc_.path.empty()) saveFileDialog(FileAction::SaveObj);
            else runCommand("save_obj");
            break;
        case SDLK_N: runCommand("new_sphere"); break;
        case SDLK_E: saveFileDialog(FileAction::ExportGlb); break;
        case SDLK_U: sendToUnreal(); break;
        default: break;
        }
        return;
    }
    switch (key) {
    case SDLK_LEFTBRACKET: radiusPx_ = std::max(2.0f, radiusPx_ / 1.1f); break;
    case SDLK_RIGHTBRACKET: radiusPx_ = std::min(800.0f, radiusPx_ * 1.1f); break;
    case SDLK_X: symmetry_.x = !symmetry_.x; break;
    case SDLK_W: renderSettings_.wireframe = !renderSettings_.wireframe; break;
    case SDLK_F: camera_.frame(sceneBounds()); break;
    case SDLK_F1: showHelp_ = !showHelp_; break;
    default:
        if (key >= SDLK_1 && key < SDLK_1 + int(BrushType::Count)) brush_ = BrushType(key - SDLK_1);
        break;
    }
}

// ---------------------------------------------------------------------------
// Strokes
// ---------------------------------------------------------------------------

void App::beginStroke(float x, float y, const RayHit& hit) {
    strokeBrush_ = activeBrush();
    strokeInvert_ = invertToggle_ != ((SDL_GetModState() & SDL_KMOD_ALT) != 0);
    const BrushRecipe* recipe = strokeBrush_ == BrushType::Smooth ? nullptr : activeRecipe();
    haveLastDab_ = false;
    if (!doc_.sculptor.beginStroke(doc_.mesh, recipe ? recipe->name : brushName(strokeBrush_), doc_.activeLayer)) {
        status_ = "Active layer weight is 0 - raise it to sculpt into the layer";
        drag_ = Drag::Orbit;
        return;
    }
    drag_ = Drag::Stroke;
    playingLayer_ = -1;
    clearLods();
    lastX_ = x;
    lastY_ = y;
    residual_ = 0.0f;

    if (strokeBrush_ == BrushType::Move && !recipe) {
        int w = 0, h = 0;
        SDL_GetWindowSize(window_, &w, &h);
        float pressure = lastInputPen_ && pressureSize_ ? penPressure_ : 1.0f;
        DabParams p;
        p.center = hit.position;
        p.viewDir = camera_.forward();
        p.radius = radiusPx_ * pressure * camera_.worldPerPixel(hit.position, float(h));
        p.strength = strength_;
        doc_.sculptor.beginGrab(doc_.mesh, doc_.bvh, p, symmetry_);
        grabPlanePoint_ = grabLast_ = hit.position;
        return;
    }
    dabAt(x, y);
}

void App::strokeTo(float x, float y) {
    const BrushRecipe* recipe = strokeBrush_ == BrushType::Smooth || strokeMask_ ? nullptr : activeRecipe();
    if (strokeBrush_ == BrushType::Move && !recipe) {
        int w = 0, h = 0;
        SDL_GetWindowSize(window_, &w, &h);
        glm::vec3 origin, dir;
        camera_.screenRay(x, y, float(w), float(h), origin, dir);
        glm::vec3 n = camera_.forward();
        float denom = glm::dot(dir, n);
        if (std::abs(denom) < 1e-6f) return;
        float t = glm::dot(grabPlanePoint_ - origin, n) / denom;
        glm::vec3 p = origin + dir * t;
        doc_.sculptor.grab(doc_.mesh, doc_.bvh, p - grabLast_);
        grabLast_ = p;
        return;
    }

    // Place dabs at even screen-space spacing along the cursor path.
    const float spacing = std::max(1.0f, radiusPx_ * (recipe ? recipe->spacing : 0.15f));
    glm::vec2 from(lastX_, lastY_), to(x, y);
    float len = glm::length(to - from);
    float travelled = spacing - residual_;
    while (travelled <= len) {
        glm::vec2 p = from + (to - from) * (travelled / len);
        dabAt(p.x, p.y);
        travelled += spacing;
    }
    residual_ = len - (travelled - spacing);
    lastX_ = x;
    lastY_ = y;
}

void App::dabAt(float x, float y) {
    const BrushRecipe* recipe = strokeBrush_ == BrushType::Smooth || strokeMask_ ? nullptr : activeRecipe();
    if (recipe && recipe->jitter > 0.0f) {
        // Deterministic per-stroke scatter (xorshift).
        auto rnd = [this] {
            jitterSeed_ ^= jitterSeed_ << 13;
            jitterSeed_ ^= jitterSeed_ >> 17;
            jitterSeed_ ^= jitterSeed_ << 5;
            return float(jitterSeed_ & 0xffff) / 65535.0f * 2.0f - 1.0f;
        };
        x += rnd() * recipe->jitter * radiusPx_;
        y += rnd() * recipe->jitter * radiusPx_;
    }
    RayHit hit;
    if (!pick(x, y, hit)) return;
    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    const float pressure = lastInputPen_ ? std::clamp(penPressure_, 0.0f, 1.0f) : 1.0f;

    DabParams p;
    p.center = hit.position;
    p.viewDir = camera_.forward();
    p.radius = radiusPx_ * (pressureSize_ ? std::max(pressure, 0.05f) : 1.0f) *
               camera_.worldPerPixel(hit.position, float(h));
    p.strength = strength_;
    p.pressure = pressureStrength_ ? pressure : 1.0f;
    p.invert = strokeInvert_;
    p.frontFacesOnly = frontFacesOnly_;
    if (strokeMask_) {
        p.strength = maskStrength_;
        doc_.sculptor.dabMask(doc_.mesh, doc_.bvh, p, symmetry_);
        return;
    }
    if (recipe) {
        p.strokeDelta = haveLastDab_ ? hit.position - lastDabHit_ : glm::vec3(0.0f);
        lastDabHit_ = hit.position;
        haveLastDab_ = true;
        doc_.sculptor.dabRecipe(doc_.mesh, doc_.bvh, *recipe, p, symmetry_);
        return;
    }
    doc_.sculptor.dab(doc_.mesh, doc_.bvh, strokeBrush_, p, symmetry_);
}

void App::endStroke() {
    strokeMask_ = false;
    UndoRecord r = doc_.sculptor.endStroke();
    if (!r.empty()) {
        doc_.undo.push(std::move(r));
        doc_.modified = true;
        updateTitle();
    }
}

// ---------------------------------------------------------------------------
// Commands & files
// ---------------------------------------------------------------------------

void App::runCommand(const std::string& name, const Args& args) {
    if (doc_.sculptor.strokeActive()) endStroke();
    CommandResult r = commands_.execute(doc_, name, args);
    status_ = r.message;
    if (r.ok && name != "export_glb" && name != "export_lods" && name != "export_usd" && name != "export_fbx" &&
        name != "export_unreal" && name.rfind("unreal_", 0) != 0 && name != "save_obj" &&
        name != "mesh_info" && name.rfind("brush_", 0) != 0 && name.rfind("splat_", 0) != 0 && name != "mesh_to_splats") {
        clearLods();
        playingLayer_ = -1;
    }
    if (r.ok && name == "save_obj" && !doc_.mesh.layers.empty())
        status_ += " (OBJ stores the current blend; export GLB to keep layers as blend shapes)";
    if (!r.ok) SDL_Log("%s failed: %s", name.c_str(), r.message.c_str());
    if (r.ok && (name == "new_sphere" || name == "new_plane" || name == "open_obj" || name == "open" ||
                 name == "image_relief" || name == "image_inflate" || name == "splat_to_mesh" ||
                 (name == "splat_transform" && args.contains("fit"))))
        camera_.frame(sceneBounds());
    updateTitle();
}

namespace {

void SDLCALL onFileChosen(void* userdata, const char* const* files, int) {
    if (!files || !files[0]) return;
    auto* req = static_cast<App::DialogRequest*>(userdata);
    req->app->queueFile(req->action, files[0]);
}

} // namespace

void App::queueFile(FileAction action, std::string path) {
    if (action != FileAction::OpenObj && action != FileAction::LoadBrush && action != FileAction::OpenImage &&
        std::filesystem::path(std::u8string(path.begin(), path.end())).extension().empty())
        path += action == FileAction::SaveObj      ? ".obj"
                : action == FileAction::SaveBrush  ? ".sfbrush"
                : action == FileAction::ExportUsd  ? ".usdz"
                : action == FileAction::SaveSplats ? ".ply"
                : action == FileAction::ExportUnreal ? ".fbx"
                                                   : ".glb";
    std::lock_guard lock(pendingMutex_);
    pendingFiles_.push_back({action, std::move(path)});
}

void App::openFileDialog() {
    SDL_ShowOpenFileDialog(onFileChosen, &dialogRequests_[int(FileAction::OpenObj)], window_, kModelFilters, 2,
                           nullptr, false);
}

// Opens models, brushes or reference images based on the file type.
void App::openPath(const std::string& path) {
    std::string ext = std::filesystem::path(std::u8string(path.begin(), path.end())).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (Image::isImagePath(path)) {
        if (loadReferenceImage(path)) showImageTo3D_ = true;
    } else if (ext == ".sfbrush") {
        runCommand("brush_load", {{"path", path}});
    } else {
        runCommand("open", {{"path", path}});
    }
}

void App::saveFileDialog(FileAction action) {
    const bool obj = action == FileAction::SaveObj;
    const SDL_DialogFileFilter* filters = obj                                  ? kObjFilters
                                          : action == FileAction::SaveBrush   ? kBrushFilters
                                          : action == FileAction::ExportUsd   ? kUsdFilters
                                          : action == FileAction::SaveSplats  ? kSplatFilters
                                          : action == FileAction::ExportUnreal ? kFbxFilters
                                                                              : kGlbFilters;
    const int count = action == FileAction::SaveSplats ? 3 : 2;
    SDL_ShowSaveFileDialog(onFileChosen, &dialogRequests_[int(action)], window_, filters, count,
                           obj && !doc_.path.empty() ? doc_.path.c_str() : nullptr);
}

AABB App::sceneBounds() const {
    AABB b = doc_.mesh.bounds();
    if (!doc_.splats.empty() && renderSettings_.showSplats) b.expand(doc_.splats.bounds(0.9f));
    return b;
}

void App::processPendingFiles() {
    std::vector<PendingFile> files;
    {
        std::lock_guard lock(pendingMutex_);
        files.swap(pendingFiles_);
    }
    for (const auto& f : files) {
        switch (f.action) {
        case FileAction::OpenObj: openPath(f.path); break;
        case FileAction::OpenImage: openPath(f.path); break;
        case FileAction::SaveObj: runCommand("save_obj", {{"path", f.path}}); break;
        case FileAction::ExportGlb: runCommand("export_glb", {{"path", f.path}}); break;
        case FileAction::ExportLods:
            runCommand("export_lods", {{"path", f.path}, {"levels", double(lodLevels_)}, {"ratio", double(lodRatio_)}});
            break;
        case FileAction::SaveBrush: runCommand("brush_save", {{"path", f.path}}); break;
        case FileAction::LoadBrush: runCommand("brush_load", {{"path", f.path}}); break;
        case FileAction::ExportUsd: runCommand("export_usd", {{"path", f.path}}); break;
        case FileAction::SaveSplats: runCommand("splat_save", {{"path", f.path}}); break;
        case FileAction::ExportUnreal: {
            auto p = std::filesystem::path(std::u8string(f.path.begin(), f.path.end()));
            auto folder = p.parent_path().u8string();
            auto stem = p.stem().u8string();
            runCommand("export_unreal", {{"folder", std::string(folder.begin(), folder.end())},
                                         {"name", std::string(stem.begin(), stem.end())},
                                         {"height_cm", double(unrealHeightCm_)},
                                         {"lods", double(lodLevels_)},
                                         {"ratio", double(lodRatio_)}});
            break;
        }
        case FileAction::Count: break;
        }
    }
}

// ---------------------------------------------------------------------------
// Sculpt layers & LODs
// ---------------------------------------------------------------------------

Mesh& App::displayedMesh() {
    return previewLod_ >= 0 && previewLod_ < int(lods_.size()) ? lods_[previewLod_] : doc_.mesh;
}

void App::setLayerWeight(int index, float weight) {
    doc_.mesh.setLayerWeight(index, weight);
    doc_.bvhDirty = true;
    doc_.modified = true;
    for (auto& lod : lods_) lod.setLayerWeight(index, weight); // LODs share the blend shapes
}

void App::animateLayers(float dt) {
    if (playingLayer_ < 0 || playingLayer_ >= int(doc_.mesh.layers.size())) {
        playingLayer_ = -1;
        return;
    }
    playTime_ += dt;
    setLayerWeight(playingLayer_, 0.5f - 0.5f * std::cos(playTime_ * 3.0f));
}

void App::generateLods() {
    Uint64 start = SDL_GetTicksNS();
    lods_ = sf::generateLods(doc_.mesh, lodLevels_, lodRatio_);
    lastDisplayed_ = nullptr;
    previewLod_ = int(lods_.size()) - 1;
    status_ = "Generated " + std::to_string(lods_.size()) + " LODs in " +
              std::to_string(int(double(SDL_GetTicksNS() - start) * 1e-6)) + " ms";
}

void App::clearLods() {
    lods_.clear();
    previewLod_ = -1;
    lastDisplayed_ = nullptr;
}

void App::updateTitle() {
    std::string name = doc_.path.empty()
                           ? std::string("Untitled")
                           : std::filesystem::path(std::u8string(doc_.path.begin(), doc_.path.end())).filename().string();
    std::string title = (doc_.modified ? "* " : "") + name + " - SculptForge";
    SDL_SetWindowTitle(window_, title.c_str());
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void App::drawUi() {
    if (hideUi_) return; // demos: clean beauty shots
    drawMenuBar();
    drawToolPanel();
    if (showLayersPanel_) drawLayersPanel();
    if (showGamePanel_) drawGameExportPanel();
    drawSplatSection();
    if (showBrushLab_) drawBrushLab();
    if (showImageTo3D_) drawImageTo3D();
    if (previewLod_ >= 0) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        std::string banner = "Previewing LOD" + std::to_string(previewLod_) + "  -  " +
                             std::to_string(lods_[previewLod_].triangleCount()) + " tris  (sculpting paused)";
        ImVec2 size = ImGui::CalcTextSize(banner.c_str());
        ImGui::GetForegroundDrawList()->AddText(
            ImVec2(vp->Pos.x + (vp->Size.x - size.x) * 0.5f, vp->Pos.y + vp->Size.y - size.y - 16.0f),
            IM_COL32(255, 190, 120, 255), banner.c_str());
    }
    if (!ImGui::GetIO().WantCaptureMouse || drag_ == Drag::Stroke) drawBrushCursor();
    drawPolygroupOverlay();

    if (showAbout_) {
        ImGui::SetNextWindowSize(ImVec2(460 * uiScale_, 0), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin("About SculptForge", &showAbout_, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
            ImGui::Text("SculptForge %s", SCULPTFORGE_VERSION);
            ImGui::TextWrapped("Free and open-source digital sculpting.");
            ImGui::Spacing();
            ImGui::TextWrapped("Copyright (C) 2026 The SculptForge contributors. This program is free software: you can "
                               "redistribute it and/or modify it under the terms of the GNU General Public License, "
                               "version 3 or later. It comes with ABSOLUTELY NO WARRANTY.");
            ImGui::Spacing();
            ImGui::TextWrapped("Built with SDL 3 (zlib), Dear ImGui (MIT), GLM (MIT), stb (MIT/public domain), ufbx "
                               "(MIT/public domain) and the Inter typeface (SIL OFL 1.1). Uses libcurl (curl license) "
                               "where available.");
            ImGui::Spacing();
            if (ImGui::Button("License")) SDL_OpenURL("https://github.com/islandmed/SculptForge/blob/main/LICENSE");
            ImGui::SameLine();
            if (ImGui::Button("Third-party notices"))
                SDL_OpenURL("https://github.com/islandmed/SculptForge/blob/main/THIRD_PARTY_NOTICES.md");
            ImGui::SameLine();
            if (ImGui::Button("AI services & privacy"))
                SDL_OpenURL("https://github.com/islandmed/SculptForge/blob/main/docs/ai-services.md");
        }
        ImGui::End();
    }
    if (showHelp_) {
        ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Controls", &showHelp_)) {
            ImGui::TextUnformatted(
                "Sculpt            Left drag on the model\n"
                "Invert (subtract) Hold Alt while sculpting\n"
                "Smooth            Hold Shift while sculpting\n"
                "Rotate            Left drag on background / right drag\n"
                "Pan               Alt + left drag background / middle drag\n"
                "Zoom              Mouse wheel / Ctrl + middle drag\n"
                "                  (Ctrl + left drag background when nothing is masked)\n"
                "Paint mask        Hold Ctrl while stroking (Ctrl+Alt erases)\n"
                "Invert mask       Ctrl + click background\n"
                "Clear mask        Ctrl + drag background\n"
                "Isolate polygroup Ctrl+Shift+click (again: invert)\n"
                "Hide polygroup    Ctrl+Shift+Alt+click\n"
                "Show all          Ctrl+Shift+click background\n"
                "Hide outside rect Ctrl+Shift+drag (Alt: inside)\n"
                "Brush size        [ and ]\n"
                "Brushes           1-8\n"
                "Symmetry X        X\n"
                "Wireframe         W\n"
                "Frame model       F\n"
                "Subdivide         Ctrl+D\n"
                "Undo / Redo       Ctrl+Z / Ctrl+Shift+Z\n"
                "Open / Save       Ctrl+O / Ctrl+S\n"
                "Tablet pressure is used automatically when a pen is detected.\n"
                "Drop a model (.obj/.glb), brush (.sfbrush) or image onto the window.");
        }
        ImGui::End();
    }
}

void App::drawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Sphere", "Ctrl+N")) runCommand("new_sphere");
        if (ImGui::MenuItem("New Plane")) runCommand("new_plane");
        ImGui::Separator();
        if (ImGui::MenuItem("Open Model...", "Ctrl+O")) openFileDialog();
        if (ImGui::MenuItem("Generate 3D..."))
            SDL_ShowOpenFileDialog(onFileChosen, &dialogRequests_[int(FileAction::OpenImage)], window_, kImageFilters, 2,
                                   nullptr, false);
        if (ImGui::MenuItem("Save", "Ctrl+S", false, !doc_.path.empty())) runCommand("save_obj");
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) saveFileDialog(FileAction::SaveObj);
        ImGui::Separator();
        if (ImGui::MenuItem("Export GLB (blend shapes)...", "Ctrl+E")) saveFileDialog(FileAction::ExportGlb);
        if (ImGui::MenuItem("Export LOD Chain...")) saveFileDialog(FileAction::ExportLods);
        if (ImGui::MenuItem("Export USD / USDZ...")) saveFileDialog(FileAction::ExportUsd);
        if (ImGui::MenuItem("Export FBX for Unreal...")) saveFileDialog(FileAction::ExportUnreal);
        if (ImGui::MenuItem("Send to Unreal", "Ctrl+U", false, !pendingUnreal_.valid())) sendToUnreal();
        if (ImGui::MenuItem("Save Splats...", nullptr, false, !doc_.splats.empty())) saveFileDialog(FileAction::SaveSplats);
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) running_ = false;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        std::string undoText = doc_.undo.canUndo() ? "Undo " + doc_.undo.undoLabel() : "Undo";
        std::string redoText = doc_.undo.canRedo() ? "Redo " + doc_.undo.redoLabel() : "Redo";
        if (ImGui::MenuItem(undoText.c_str(), "Ctrl+Z", false, doc_.undo.canUndo())) runCommand("undo");
        if (ImGui::MenuItem(redoText.c_str(), "Ctrl+Shift+Z", false, doc_.undo.canRedo())) runCommand("redo");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Mesh")) {
        if (ImGui::MenuItem("Subdivide", "Ctrl+D")) runCommand("subdivide");
        if (ImGui::MenuItem("Voxel Remesh")) runCommand("remesh", {{"resolution", double(voxelResolution_)}});
        if (ImGui::MenuItem("Sculpt to Splats")) runCommand("mesh_to_splats");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Wireframe", "W", &renderSettings_.wireframe);
        ImGui::MenuItem("Show Mesh", nullptr, &renderSettings_.showMesh);
        ImGui::MenuItem("Show Polypaint", nullptr, &renderSettings_.showPolypaint);
        ImGui::MenuItem("Show Mask", nullptr, &renderSettings_.showMask);
        ImGui::MenuItem("Show Polygroups", nullptr, &renderSettings_.showPolygroups);
        ImGui::Separator();
        ImGui::MenuItem("Layers Panel", nullptr, &showLayersPanel_);
        ImGui::MenuItem("Game Ready Panel", nullptr, &showGamePanel_);
        ImGui::MenuItem("Gaussian Splats", nullptr, &showSplatsPanel_);
        ImGui::MenuItem("Brush Lab", nullptr, &showBrushLab_);
        ImGui::MenuItem("Generate 3D", nullptr, &showImageTo3D_);
        ImGui::MenuItem("Show Splats", nullptr, &renderSettings_.showSplats, !doc_.splats.empty());
        if (ImGui::MenuItem("Frame Model", "F")) camera_.frame(sceneBounds());
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("Controls", "F1", &showHelp_);
        ImGui::MenuItem("About SculptForge", nullptr, &showAbout_);
        if (ImGui::MenuItem("Website & documentation")) SDL_OpenURL("https://islandmed.github.io/SculptForge/");
        if (ImGui::MenuItem("Report a problem")) SDL_OpenURL("https://github.com/islandmed/SculptForge/issues");
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void App::drawLayersPanel() {
    const float scale = uiScale_;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 290 * scale, vp->Pos.y + 34 * scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(280 * scale, 0), ImVec2(280 * scale, FLT_MAX));
    if (!ImGui::Begin("Layers / Blend Shapes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("Sculpt expressions & correctives;\nexported as glTF morph targets.");

    if (ImGui::RadioButton("Base mesh", doc_.activeLayer < 0)) runCommand("layer_set_active", {{"index", -1.0}});

    int removeIndex = -1;
    for (int i = 0; i < int(doc_.mesh.layers.size()); ++i) {
        SculptLayer& layer = doc_.mesh.layers[i];
        ImGui::PushID(i);
        ImGui::Separator();
        if (ImGui::RadioButton(layer.name.c_str(), doc_.activeLayer == i))
            runCommand("layer_set_active", {{"index", double(i)}});
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 52 * scale);
        const bool playing = playingLayer_ == i;
        if (ImGui::SmallButton(playing ? "Stop" : "Play")) {
            playingLayer_ = playing ? -1 : i;
            playTime_ = 0.0f;
            if (playing) setLayerWeight(i, 1.0f);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Animate this blend shape 0 -> 1 -> 0");
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) removeIndex = i;
        float w = layer.weight;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##weight", &w, 0.0f, 1.0f, "weight %.2f")) {
            if (playingLayer_ == i) playingLayer_ = -1;
            setLayerWeight(i, w);
        }
        ImGui::PopID();
    }
    if (removeIndex >= 0) {
        if (playingLayer_ == removeIndex) playingLayer_ = -1;
        runCommand("layer_remove", {{"index", double(removeIndex)}});
    }

    ImGui::Separator();
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize("+ Add").x - ImGui::GetStyle().FramePadding.x * 2 -
                            ImGui::GetStyle().ItemSpacing.x);
    bool enter = ImGui::InputText("##newlayer", newLayerName_, sizeof newLayerName_,
                                  ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("+ Add") || enter) runCommand("layer_add", {{"name", std::string(newLayerName_)}});
    ImGui::End();
}

void App::sendToUnreal() {
    if (pendingUnreal_.valid()) return;
    UnrealExportOptions o;
    o.heightCm = unrealHeightCm_;
    o.lods = lodLevels_;
    o.ratio = lodRatio_;
    o.folder = (std::filesystem::temp_directory_path() / "sculptforge_unreal").string();
    UnrealExportResult r;
    std::string err;
    if (!exportForUnreal(doc_, o, r, err)) {
        status_ = "Export failed: " + err;
        return;
    }
    link::UnrealImport import;
    import.fbxPath = r.path;
    import.destination = unrealDestination_;
    import.assetName = r.assetName;
    import.skeletal = r.skeletal;
    import.lods = r.lodCount > 1;
    status_ = "Sending " + r.assetName + " to Unreal...";
    auto promise = std::make_shared<std::promise<std::string>>();
    pendingUnreal_ = promise->get_future();
    std::thread([promise, import, network = unrealNetwork_] {
        std::string message;
        link::sendToUnreal(network ? link::UnrealEndpoint::network() : link::UnrealEndpoint(), import, message);
        promise->set_value(message);
    }).detach();
}

void App::pollUnreal() {
    if (!pendingUnreal_.valid() || pendingUnreal_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    status_ = pendingUnreal_.get();
}

void App::drawGameExportPanel() {
    const float scale = uiScale_;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 290 * scale, vp->Pos.y + 330 * scale),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(280 * scale, 0), ImVec2(280 * scale, FLT_MAX));
    if (!ImGui::Begin("Game Ready", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    ImGui::PushItemWidth(-ImGui::CalcTextSize("Reduction").x - ImGui::GetStyle().ItemInnerSpacing.x);
    if (ImGui::SliderInt("LODs", &lodLevels_, 2, 6)) clearLods();
    if (ImGui::SliderFloat("Reduction", &lodRatio_, 0.1f, 0.9f, "x%.2f / level")) clearLods();
    ImGui::PopItemWidth();
    if (ImGui::Button(lods_.empty() ? "Generate LOD chain" : "Regenerate", ImVec2(-1, 0))) generateLods();

    if (!lods_.empty()) {
        if (ImGui::BeginTable("lods", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Level");
            ImGui::TableSetupColumn("Triangles");
            ImGui::TableSetupColumn("Budget");
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable("Sculpt", previewLod_ < 0, ImGuiSelectableFlags_SpanAllColumns)) {
                previewLod_ = -1;
            }
            ImGui::TableNextColumn();
            ImGui::Text("%zu", doc_.mesh.triangleCount());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("editable");
            for (int i = 0; i < int(lods_.size()); ++i) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                std::string label = "LOD" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), previewLod_ == i, ImGuiSelectableFlags_SpanAllColumns)) {
                    previewLod_ = i;
                    renderSettings_.wireframe = true;
                }
                ImGui::TableNextColumn();
                ImGui::Text("%zu", lods_[i].triangleCount());
                ImGui::TableNextColumn();
                ImGui::Text("%.1f%%", 100.0 * double(lods_[i].triangleCount()) / double(lods_[0].triangleCount()));
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Click a level to inspect it in the viewport.");
    }

    ImGui::SeparatorText("Unreal Engine");
    ImGui::PushItemWidth(-ImGui::CalcTextSize("Folder").x - ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::InputText("Folder", unrealDestination_, sizeof unrealDestination_);
    if (unrealHeightCm_ <= 0.0f) unrealHeightCm_ = std::max(1.0f, doc_.mesh.bounds().extent().y * 100.0f);
    ImGui::DragFloat("Height", &unrealHeightCm_, 1.0f, 1.0f, 100000.0f, "%.0f cm");
    ImGui::PopItemWidth();
    ImGui::Checkbox("Unreal is on another computer", &unrealNetwork_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("In Unreal: Project Settings > Plugins > Python > Advanced:\nMulticast Time-To-Live = 1, "
                          "Multicast Bind Address = 0.0.0.0");
    if (pendingUnreal_.valid()) {
        ImGui::ProgressBar(-1.0f * float(ImGui::GetTime()), ImVec2(-1, 0), "Sending to Unreal...");
    } else if (ImGui::Button("Send to Unreal", ImVec2(-1, 0))) {
        sendToUnreal();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Imports (or updates) the asset in the open Unreal Editor.\nNeeds the Python Editor Script "
                          "Plugin and Project Settings > Python > Enable Remote Execution.");
    if (ImGui::Button("Export FBX for Unreal...", ImVec2(-1, 0))) saveFileDialog(FileAction::ExportUnreal);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (doc_.mesh.layers.empty()) ImGui::TextWrapped("Static mesh with %d LODs.", lodLevels_);
    else ImGui::TextWrapped("Skeletal mesh with %zu morph targets.", doc_.mesh.layers.size());
    ImGui::PopStyleColor();

    ImGui::SeparatorText("Export");
    if (ImGui::Button("GLB + blend shapes", ImVec2(-1, 0))) saveFileDialog(FileAction::ExportGlb);
    if (ImGui::Button("LOD chain (GLB per level)", ImVec2(-1, 0))) saveFileDialog(FileAction::ExportLods);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Unreal, Unity, Godot, Blender, three.js");
    ImGui::PopStyleColor();
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Brush Lab: describe a brush in words, get a recipe, tweak it with sliders
// ---------------------------------------------------------------------------

const BrushRecipe* App::activeRecipe() const {
    return doc_.activeBrush >= 0 && doc_.activeBrush < int(doc_.brushes.size()) ? &doc_.brushes[doc_.activeBrush]
                                                                                  : nullptr;
}

void App::sendBrushRequest(const std::string& text) {
    if (text.empty() || pendingBrush_.valid()) return;
    chat_.push_back(ai::composeUserMessage(text, refineCurrent_ ? activeRecipe() : nullptr));
    chatLog_.push_back({ChatLine::Kind::User, text});
    scrollChat_ = true;
    busyTime_ = 0.0f;

    // Detached worker with its own copies, so quitting never waits on the network.
    auto promise = std::make_shared<std::promise<BrushReply>>();
    pendingBrush_ = promise->get_future();
    std::thread([promise, settings = aiSettings_, history = chat_] {
        BrushReply reply;
        reply.suggestion = ai::requestBrush(settings, history, &reply.raw);
        promise->set_value(std::move(reply));
    }).detach();
}

void App::pollBrushRequest() {
    if (!pendingBrush_.valid() || pendingBrush_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    BrushReply reply = pendingBrush_.get();
    scrollChat_ = true;
    if (!reply.suggestion.ok) {
        chat_.pop_back(); // keep the model's history to successful turns
        chatLog_.push_back({ChatLine::Kind::Error, reply.suggestion.error});
        return;
    }
    chat_.push_back({ai::ChatMessage::Role::Assistant, reply.raw, reply.suggestion.reply});
    doc_.activeBrush = doc_.addBrush(reply.suggestion.recipe);
    chatLog_.push_back({ChatLine::Kind::Assistant, "[" + reply.suggestion.recipe.name + "]  " + reply.suggestion.reply});
    status_ = "Brush '" + reply.suggestion.recipe.name + "' is ready - try it on the model";
}

void App::drawRecipeEditor(BrushRecipe& recipe) {
    char name[64];
    std::snprintf(name, sizeof name, "%s", recipe.name.c_str());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##name", name, sizeof name, ImGuiInputTextFlags_EnterReturnsTrue) && name[0])
        recipe.name = name;
    ImGui::PushItemWidth(-ImGui::CalcTextSize("Pressure curve").x - ImGui::GetStyle().ItemInnerSpacing.x);
    for (const auto& p : recipeParams()) {
        switch (p.kind) {
        case RecipeParam::Kind::Float:
            ImGui::SliderFloat(p.label, recipe.field(p.key), p.min, p.max, "%.2f");
            break;
        case RecipeParam::Kind::Bool: ImGui::Checkbox(p.label, &recipe.frontFacesOnly); break;
        case RecipeParam::Kind::Enum: {
            int idx = recipe.enumIndex(p.key);
            if (ImGui::Combo(p.label, &idx, p.options.data(), int(p.options.size()))) recipe.setEnumIndex(p.key, idx);
            break;
        }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", p.help);
    }
    ImGui::PopItemWidth();
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Save .sfbrush...", ImVec2(half, 0))) saveFileDialog(FileAction::SaveBrush);
    ImGui::SameLine();
    if (ImGui::Button("Load .sfbrush...", ImVec2(half, 0)))
        SDL_ShowOpenFileDialog(onFileChosen, &dialogRequests_[int(FileAction::LoadBrush)], window_, kBrushFilters, 2,
                               nullptr, false);
}

void App::drawAiSettings() {
    using P = ai::AiSettings::Provider;
    const char* providers[] = {"Anthropic (Claude)", "OpenAI-compatible (OpenAI, Ollama, LM Studio...)"};
    int provider = int(aiSettings_.provider);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##provider", &provider, providers, 2)) {
        std::string key = aiSettings_.apiKey;
        aiSettings_ = ai::AiSettings::defaults(P(provider));
        aiSettings_.apiKey = key;
    }
    auto textField = [](const char* label, std::string& value, ImGuiInputTextFlags flags = 0) {
        char buf[512];
        std::snprintf(buf, sizeof buf, "%s", value.c_str());
        ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Endpoint").x - ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::InputText(label, buf, sizeof buf, flags)) value = buf;
    };
    textField("Endpoint", aiSettings_.endpoint);
    textField("Model", aiSettings_.model);
    textField("API key", aiSettings_.apiKey, ImGuiInputTextFlags_Password);
    if (aiSettings_.provider == P::Anthropic) {
        const char* efforts[] = {"low", "medium", "high"};
        int e = aiSettings_.effort == "high" ? 2 : aiSettings_.effort == "medium" ? 1 : 0;
        ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Endpoint").x - ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::Combo("Effort", &e, efforts, 3)) aiSettings_.effort = efforts[e];
    }
    if (ImGui::Button("Save settings", ImVec2(-1, 0))) {
        std::string err;
        status_ = aiSettingsPath_.empty() || !aiSettings_.save(aiSettingsPath_, err) ? "Could not save AI settings " + err
                                                                                     : "AI settings saved";
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Opt-in and private: requests go only to the endpoint above. Keys are stored on this "
                       "computer only (owner-readable ai.json); leave empty to use ANTHROPIC_API_KEY / "
                       "OPENAI_API_KEY. For free, offline use, run Ollama and pick OpenAI-compatible.");
    ImGui::PopStyleColor();
}

void App::drawBrushLab() {
    const float scale = uiScale_;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // First use: bottom of the window, right of the Tools panel.
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + 270 * scale, std::max(vp->Pos.y + 34 * scale,
                                                                     vp->Pos.y + vp->Size.y - 470 * scale)),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(420 * scale, 0), ImVec2(420 * scale, FLT_MAX));
    if (!ImGui::Begin("Brush Lab", &showBrushLab_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("Describe a brush the way you'd describe it to another sculptor.");

    // Conversation
    if (ImGui::BeginChild("chat", ImVec2(-1, 150 * scale), ImGuiChildFlags_Borders)) {
        if (chatLog_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("Try one of the ideas below, or ask for changes to the selected brush, e.g. "
                               "\"sharper\", \"less buildup\", \"make it grittier\".");
            ImGui::PopStyleColor();
        }
        for (const auto& line : chatLog_) {
            ImVec4 col = line.kind == ChatLine::Kind::User    ? ImVec4(1.0f, 0.70f, 0.45f, 1.0f)
                         : line.kind == ChatLine::Kind::Error ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f)
                                                              : ImVec4(0.85f, 0.88f, 0.95f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextWrapped("%s %s", line.kind == ChatLine::Kind::User ? ">" : " ", line.text.c_str());
            ImGui::PopStyleColor();
        }
        if (pendingBrush_.valid()) {
            busyTime_ += ImGui::GetIO().DeltaTime;
            int dots = int(busyTime_ * 3.0f) % 4;
            ImGui::TextDisabled("  Designing brush%.*s", dots, "...");
        }
        if (scrollChat_) {
            ImGui::SetScrollHereY(1.0f);
            scrollChat_ = false;
        }
    }
    ImGui::EndChild();

    // Starter ideas
    for (int i = 0; i < int(std::size(kExamplePrompts)); ++i) {
        float w = ImGui::CalcTextSize(kExamplePrompts[i]).x + ImGui::GetStyle().FramePadding.x * 2;
        if (i > 0 && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + w <
                         ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
            ImGui::SameLine();
        ImGui::BeginDisabled(pendingBrush_.valid());
        if (ImGui::SmallButton(kExamplePrompts[i])) sendBrushRequest(kExamplePrompts[i]);
        ImGui::EndDisabled();
    }

    // Input
    const char* sendLabel = "Create";
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize(sendLabel).x - ImGui::GetStyle().FramePadding.x * 2 -
                            ImGui::GetStyle().ItemSpacing.x);
    bool submit = ImGui::InputTextWithHint("##prompt", "e.g. bark of an old oak tree", chatInput_, sizeof chatInput_,
                                           ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::BeginDisabled(pendingBrush_.valid());
    submit |= ImGui::Button(sendLabel);
    ImGui::EndDisabled();
    if (submit && chatInput_[0] && !pendingBrush_.valid()) {
        sendBrushRequest(chatInput_);
        chatInput_[0] = '\0';
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::BeginDisabled(!activeRecipe());
    ImGui::Checkbox("Refine the selected brush", &refineCurrent_);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear chat")) {
        chat_.clear();
        chatLog_.clear();
    }

    if (BrushRecipe* recipe = doc_.activeBrush >= 0 ? &doc_.brushes[doc_.activeBrush] : nullptr) {
        if (ImGui::CollapsingHeader(("Recipe: " + recipe->name + "###recipe").c_str())) drawRecipeEditor(*recipe);
    }
    if (ImGui::CollapsingHeader("AI provider")) drawAiSettings();
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Image to 3D
// ---------------------------------------------------------------------------

bool App::loadReferenceImage(const std::string& path) {
    Image img;
    std::string err;
    if (!Image::load(path, img, err)) {
        status_ = err;
        return false;
    }
    refImage_ = std::move(img);
    refImagePath_ = path;
    if (!refTexture_) gl::GenTextures(1, &refTexture_);
    gl::BindTexture(GL_TEXTURE_2D, refTexture_);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_RGBA8), refImage_.width, refImage_.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                   refImage_.rgba.data());
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_LINEAR));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(GL_LINEAR));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(GL_CLAMP_TO_EDGE));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLint(GL_CLAMP_TO_EDGE));
    gl::BindTexture(GL_TEXTURE_2D, 0);
    status_ = "Loaded image " + std::to_string(refImage_.width) + "x" + std::to_string(refImage_.height);
    return true;
}

void App::projectImageFromView() {
    if (refImage_.empty()) return;
    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    const AABB b = doc_.mesh.bounds();
    ProjectOptions o;
    o.depth = projectDepth_ * glm::length(b.extent());
    o.invert = projectInvert_;
    Mesh m = doc_.mesh;
    size_t n = projectImage(m, doc_.activeLayer, refImage_, camera_.projection() * camera_.view(),
                            float(w) / float(std::max(h, 1)), camera_.position(), o);
    if (n == 0) {
        status_ = "The image didn't land on the model - aim the view at it first";
        return;
    }
    doc_.replaceMeshUndoable(std::move(m), "Project Image");
    clearLods();
    status_ = "Stamped image onto " + std::to_string(n) + " vertices (Ctrl+Z to undo)";
    updateTitle();
}

void App::startGenerator() {
    if (pendingGenerator_.valid()) return;
    if (textMode_ ? textPrompt_[0] == '\0' : refImagePath_.empty()) return;
    aiSettings_.generatorCommand = generatorCommand_;
    aiSettings_.falKey = falKey_;
    aiSettings_.falModel = falModel_;
    aiSettings_.meshyKey = meshyKey_;
    aiSettings_.hfToken = hfToken_;
    auto stem = (std::filesystem::temp_directory_path() / ("sculptforge_generated_" + std::to_string(SDL_GetTicksNS()))).u8string();
    generatorTime_ = 0.0f;
    generatorProgress_ = std::make_shared<ai::GenerationProgress>();
    generatorProgress_->setStatus("Starting...");
    auto promise = std::make_shared<std::promise<ai::GeneratorResult>>();
    pendingGenerator_ = promise->get_future();
    std::thread([promise, settings = aiSettings_, in = refImagePath_, text = textMode_ ? std::string(textPrompt_) : std::string(),
                 out = std::string(stem.begin(), stem.end()), progress = generatorProgress_] {
        promise->set_value(text.empty() ? ai::generate3D(settings, in, out, progress.get())
                                        : ai::generate3DFromText(settings, text, out, progress.get()));
    }).detach();
}

void App::pollGenerator() {
    if (!pendingGenerator_.valid() || pendingGenerator_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    ai::GeneratorResult r = pendingGenerator_.get();
    if (!r.ok) {
        status_ = "Image to 3D failed: " + r.error;
        return;
    }
    runCommand("open", {{"path", r.outputPath}});
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(std::u8string(r.outputPath.begin(), r.outputPath.end())), ec);
    const size_t raw = doc_.mesh.triangleCount();
    if (remeshGenerated_) runCommand("remesh", {{"resolution", double(remeshResolution_)}});
    doc_.path.clear();
    status_ = "Generated model imported (" + std::to_string(raw) + " tris" +
              (remeshGenerated_ ? ", remeshed to " + std::to_string(doc_.mesh.triangleCount()) : std::string()) +
              ") - ready to sculpt";
    updateTitle();
}

void App::drawImageTo3D() {
    const float scale = uiScale_;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 590 * scale, vp->Pos.y + 34 * scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(290 * scale, 0), ImVec2(290 * scale, FLT_MAX));
    if (!ImGui::Begin("Generate 3D", &showImageTo3D_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("##source")) {
        if (ImGui::BeginTabItem("From image")) {
            textMode_ = false;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("From text")) {
            textMode_ = true;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (textMode_) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextMultiline("##textprompt", textPrompt_, sizeof textPrompt_, ImVec2(-1, ImGui::GetTextLineHeight() * 3.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Describe one object: \"a stylised dragon head with curled horns\", \"a cartoon fox sitting\".");
        ImGui::PopStyleColor();
    } else {
        if (ImGui::Button(refImage_.empty() ? "Choose image..." : "Change image...", ImVec2(-1, 0)))
            SDL_ShowOpenFileDialog(onFileChosen, &dialogRequests_[int(FileAction::OpenImage)], window_, kImageFilters, 2,
                                   nullptr, false);
        if (refImage_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("Or drop a sketch, concept, logo, photo or height map onto the window.");
            ImGui::PopStyleColor();
            ImGui::End();
            return;
        }
        const float thumbW = ImGui::GetContentRegionAvail().x;
        const float thumbH = std::min(thumbW / refImage_.aspect(), 180.0f * scale);
        const float drawW = thumbH * refImage_.aspect();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (thumbW - drawW) * 0.5f);
        ImGui::Image(ImTextureID(uintptr_t(refTexture_)), ImVec2(drawW, thumbH));
        auto fileName = std::filesystem::path(std::u8string(refImagePath_.begin(), refImagePath_.end())).filename().u8string();
        ImGui::TextDisabled("%s  (%dx%d)", std::string(fileName.begin(), fileName.end()).c_str(), refImage_.width,
                            refImage_.height);
    }

    auto hint = [](const char* text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    };
    const float labelW = ImGui::CalcTextSize("Resolution").x + ImGui::GetStyle().ItemInnerSpacing.x;

    ImGui::SeparatorText(textMode_ ? "Text to 3D (AI)" : "Full 3D model (AI reconstruction)");
    hint("Builds the whole model - limbs, face, back - from one picture with a reconstruction model, then makes it "
         "sculptable. Characters on a transparent background work best.");
    static const char* services[] = {"Hugging Face - free models", "fal.ai - paid, your key", "Meshy - paid, your key",
                                     "My generator command", "Offline blockout (inflate silhouette)"};
    static const char* serviceIds[] = {"huggingface", "fal", "meshy", "command", "inflate"};
    int service = 0;
    for (int i = 0; i < 5; ++i)
        if (aiSettings_.generatorService == serviceIds[i]) service = i;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##service", &service, services, 5)) aiSettings_.generatorService = serviceIds[service];
    ImGui::PushItemWidth(-labelW);
    if (service == 0) {
        const auto& presets = ai::hfPresets();
        const char* preview = presets[0].label;
        for (const auto& p : presets)
            if (aiSettings_.hfModel == p.id) preview = p.label;
        if (ImGui::BeginCombo("Model", preview)) {
            for (const auto& p : presets)
                if (ImGui::Selectable(p.label, aiSettings_.hfModel == p.id)) aiSettings_.hfModel = p.id;
            ImGui::EndCombo();
        }
        ImGui::InputText("HF token", hfToken_, sizeof hfToken_, ImGuiInputTextFlags_Password);
        hint(hfToken_[0] || std::getenv("HF_TOKEN")
                 ? "Runs on Hugging Face's shared GPUs with your account's free daily allowance."
                 : "Runs free on Hugging Face's shared GPUs with a small daily allowance. A free token "
                   "(huggingface.co/settings/tokens, read access) raises it; TRELLIS.2 needs one.");
    } else if (service == 1) {
        const auto& models = ai::falModels();
        const char* preview = falModel_;
        for (const auto& m : models)
            if (std::string(falModel_) == m.id) preview = m.label;
        if (ImGui::BeginCombo("Model", preview)) {
            for (const auto& m : models)
                if (ImGui::Selectable(m.label, std::string(falModel_) == m.id))
                    std::snprintf(falModel_, sizeof falModel_, "%s", m.id);
            ImGui::EndCombo();
        }
        ImGui::InputText("Model id", falModel_, sizeof falModel_);
        ImGui::InputText("API key", falKey_, sizeof falKey_, ImGuiInputTextFlags_Password);
        if (!falKey_[0]) hint(std::getenv("FAL_KEY") ? "Using FAL_KEY from the environment." : "Get a key at fal.ai/dashboard/keys");
    } else if (service == 2) {
        ImGui::InputText("API key", meshyKey_, sizeof meshyKey_, ImGuiInputTextFlags_Password);
        if (!meshyKey_[0])
            hint(std::getenv("MESHY_API_KEY") ? "Using MESHY_API_KEY from the environment." : "Get a key at meshy.ai (API settings)");
    } else if (service == 3) {
        hint("Any local model or script: {input} = image, {output} = .glb/.obj to write. See docs/generators.md.");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextMultiline("##generator", generatorCommand_, sizeof generatorCommand_,
                                  ImVec2(-1, ImGui::GetTextLineHeight() * 4));
    } else {
        hint("Offline and instant, but only a puffy blockout of the outline - not a real reconstruction.");
    }
    if (service != 4) {
        ImGui::Checkbox("Remesh for sculpting", &remeshGenerated_);
        if (remeshGenerated_) ImGui::SliderInt("Resolution", &remeshResolution_, 96, 400);
    }
    ImGui::PopItemWidth();
    if (pendingGenerator_.valid()) {
        generatorTime_ += ImGui::GetIO().DeltaTime;
        const int pct = generatorProgress_ ? generatorProgress_->percent.load() : -1;
        ImGui::ProgressBar(pct >= 0 ? float(pct) / 100.0f : -1.0f * float(ImGui::GetTime()), ImVec2(-1, 0),
                           generatorProgress_ ? generatorProgress_->status().c_str() : "Working...");
        if (ImGui::Button("Cancel", ImVec2(-1, 0)) && generatorProgress_) generatorProgress_->cancel = true;
    } else if (ImGui::Button("Generate 3D model", ImVec2(-1, 0))) {
        startGenerator();
    }
    if (ImGui::SmallButton("Save settings")) {
        aiSettings_.generatorCommand = generatorCommand_;
        aiSettings_.falKey = falKey_;
        aiSettings_.falModel = falModel_;
        aiSettings_.meshyKey = meshyKey_;
        aiSettings_.hfToken = hfToken_;
        std::string err;
        status_ = !aiSettingsPath_.empty() && aiSettings_.save(aiSettingsPath_, err) ? "Image to 3D settings saved"
                                                                                        : "Save failed " + err;
    }

    if (textMode_) {
        ImGui::End();
        return;
    }
    ImGui::SeparatorText("Quick shapes (offline)");
    if (ImGui::TreeNode("Silhouette blockout")) {
        hint("Inflates the outline into a rounded blob - good for simple shapes, not characters.");
        ImGui::PushItemWidth(-labelW);
        ImGui::SliderFloat("Thickness", &inflateThickness_, 0.2f, 2.0f, "%.2f");
        ImGui::SliderInt("Detail##inflate", &inflateDetail_, 64, 384);
        ImGui::PopItemWidth();
        if (ImGui::Button("Inflate", ImVec2(-1, 0)))
            runCommand("image_inflate", {{"path", refImagePath_},
                                         {"thickness", double(inflateThickness_)},
                                         {"resolution", double(inflateDetail_)},
                                         {"subdivisions", 2.0}});
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Relief")) {
        hint("Light areas rise: logos, coins, emblems, height maps, texture photos.");
        ImGui::PushItemWidth(-labelW);
        ImGui::SliderFloat("Depth##relief", &reliefDepth_, 0.01f, 0.5f, "%.2f");
        ImGui::SliderInt("Detail##relief", &reliefDetail_, 64, 1024);
        ImGui::PopItemWidth();
        ImGui::Checkbox("Dark is high##relief", &reliefInvert_);
        if (ImGui::Button("Make relief", ImVec2(-1, 0)))
            runCommand("image_relief", {{"path", refImagePath_},
                                        {"depth", double(reliefDepth_)},
                                        {"resolution", double(reliefDetail_)},
                                        {"invert", reliefInvert_}});
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Stamp onto model")) {
        hint("Projects the image onto the model from the current view as surface detail (uses the active layer).");
        ImGui::PushItemWidth(-labelW);
        ImGui::SliderFloat("Depth##project", &projectDepth_, 0.001f, 0.1f, "%.3f", ImGuiSliderFlags_Logarithmic);
        ImGui::PopItemWidth();
        ImGui::Checkbox("Dark is high##project", &projectInvert_);
        if (ImGui::Button("Stamp from this view", ImVec2(-1, 0))) projectImageFromView();
        ImGui::TreePop();
    }
    ImGui::End();
}

void App::drawToolPanel() {
    const float scale = uiScale_;
    ImGui::SetNextWindowPos(ImVec2(10 * scale, 34 * scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(250 * scale, 0), ImVec2(250 * scale, FLT_MAX));
    if (!ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Brush");
    const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    for (int i = 0; i < int(BrushType::Count); ++i) {
        const auto type = BrushType(i);
        const bool selected = brush_ == type && doc_.activeBrush < 0;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        std::string label = std::to_string(i + 1) + "  " + brushName(type);
        if (ImGui::Button(label.c_str(), ImVec2(bw, 0))) {
            brush_ = type;
            doc_.activeBrush = -1;
        }
        if (selected) ImGui::PopStyleColor();
        if (i % 2 == 0) ImGui::SameLine();
    }

    ImGui::SeparatorText("Custom brushes");
    if (ImGui::BeginListBox("##custom", ImVec2(-1, ImGui::GetTextLineHeightWithSpacing() * 5.5f))) {
        for (int i = 0; i < int(doc_.brushes.size()); ++i) {
            if (ImGui::Selectable(doc_.brushes[i].name.c_str(), doc_.activeBrush == i)) doc_.activeBrush = i;
            if (ImGui::IsItemHovered() && !doc_.brushes[i].description.empty())
                ImGui::SetTooltip("%s", doc_.brushes[i].description.c_str());
        }
        ImGui::EndListBox();
    }
    if (ImGui::Button(showBrushLab_ ? "Hide Brush Lab" : "Brush Lab (AI)...", ImVec2(-1, 0)))
        showBrushLab_ = !showBrushLab_;
    if (ImGui::Button(showImageTo3D_ ? "Hide Generate 3D" : "Generate 3D...", ImVec2(-1, 0)))
        showImageTo3D_ = !showImageTo3D_;

    ImGui::PushItemWidth(-ImGui::CalcTextSize("Strength").x - ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::SliderFloat("Size", &radiusPx_, 2.0f, 800.0f, "%.0f px", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Strength", &strength_, 0.01f, 1.0f, "%.2f");
    ImGui::PopItemWidth();
    ImGui::Checkbox("Invert (Alt)", &invertToggle_);
    ImGui::Checkbox("Front faces only", &frontFacesOnly_);

    ImGui::SeparatorText("Symmetry");
    ImGui::Checkbox("X", &symmetry_.x);
    ImGui::SameLine();
    ImGui::Checkbox("Y", &symmetry_.y);
    ImGui::SameLine();
    ImGui::Checkbox("Z", &symmetry_.z);

    ImGui::SeparatorText("Tablet");
    ImGui::Checkbox("Pressure > strength", &pressureStrength_);
    ImGui::Checkbox("Pressure > size", &pressureSize_);
    if (penSeen_) ImGui::Text("Pen pressure: %.2f", penPressure_);
    else ImGui::TextDisabled("No pen detected");

    drawMaskingSection();

    ImGui::SeparatorText("Mesh");
    if (ImGui::Button("Subdivide (Ctrl+D)", ImVec2(-1, 0))) runCommand("subdivide");
    if (ImGui::Button("Voxel remesh", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0)))
        runCommand("remesh", {{"resolution", double(voxelResolution_)}});
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Rebuild as an even, watertight surface (like DynaMesh): merges parts, fixes scans and generated models.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##voxres", &voxelResolution_, 64, 400, "res %d");
    ImGui::Checkbox("Wireframe", &renderSettings_.wireframe);
    ImGui::ColorEdit3("Material", &renderSettings_.baseColor.x, ImGuiColorEditFlags_NoInputs);
    drawPolygroupsSection();

    ImGui::Separator();
    ImGui::Text("%zu verts  %zu tris", doc_.mesh.vertexCount(), doc_.mesh.triangleCount());
    ImGui::Text("%.0f fps", ImGui::GetIO().Framerate);
    if (!status_.empty()) ImGui::TextWrapped("%s", status_.c_str());
    ImGui::End();
}

void App::drawSplatSection() {
    // Opens by itself whenever splats arrive (file, command or AI agent).
    if (doc_.splatsVersion != seenSplatsVersion_) {
        seenSplatsVersion_ = doc_.splatsVersion;
        if (!doc_.splats.empty()) showSplatsPanel_ = true;
    }
    if (!showSplatsPanel_) return;
    const float scale = uiScale_;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 590 * scale, vp->Pos.y + 34 * scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(280 * scale, 0), ImVec2(280 * scale, FLT_MAX));
    if (!ImGui::Begin("Gaussian Splats", &showSplatsPanel_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    struct EndWindow {
        ~EndWindow() { ImGui::End(); }
    } endWindow;
    if (doc_.splats.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Open or drop a .ply capture (Polycam, Luma, Postshot, Scaniverse...) to sculpt against it.");
        ImGui::PopStyleColor();
        if (ImGui::Button("Sculpt to splats", ImVec2(-1, 0))) runCommand("mesh_to_splats");
        return;
    }
    ImGui::Text("%zu splats (SH %d)", doc_.splats.size(), doc_.splats.shDegree);
    ImGui::Checkbox("Show##splats", &renderSettings_.showSplats);
    ImGui::SameLine();
    ImGui::Checkbox("Show mesh", &renderSettings_.showMesh);
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##splatopacity", &renderSettings_.splatOpacity, 0.05f, 1.0f, "opacity %.2f");
    const float third = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
    if (ImGui::Button("Flip", ImVec2(third, 0))) runCommand("splat_transform", {{"rotate_x", 180.0}});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turn upside down (most captures are)");
    ImGui::SameLine();
    if (ImGui::Button("Turn 90", ImVec2(third, 0))) runCommand("splat_transform", {{"rotate_y", 90.0}});
    ImGui::SameLine();
    if (ImGui::Button("Fit", ImVec2(third, 0))) runCommand("splat_transform", {{"fit", true}});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Centre and scale to the sculpt");
    if (ImGui::Button("Clean floaters", ImVec2(-1, 0))) runCommand("splat_clean", {{"min_opacity", 0.1}, {"keep", 0.97}});
    if (ImGui::Button("Convert to mesh", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0)))
        runCommand("splat_to_mesh", {{"resolution", double(splatMeshResolution_)}});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Build a watertight sculpting mesh from the capture (undoable)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##splatres", &splatMeshResolution_, 64, 400, "res %d");
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Save splats...", ImVec2(half, 0))) saveFileDialog(FileAction::SaveSplats);
    ImGui::SameLine();
    if (ImGui::Button("Clear##splats", ImVec2(half, 0))) runCommand("splat_clear");
}

void App::drawBrushCursor() {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float r = radiusPx_ * (pressureSize_ && lastInputPen_ ? std::max(penPressure_, 0.05f) : 1.0f);
    const ImVec2 c(mouseX_, mouseY_);
    const bool inverted = invertToggle_ != ((SDL_GetModState() & SDL_KMOD_ALT) != 0);
    const SDL_Keymod mod = SDL_GetModState();
    const bool masking = drag_ == Drag::Stroke ? strokeMask_ : ctrlDown(mod) && !(mod & SDL_KMOD_SHIFT);
    const ImU32 col = masking                                ? IM_COL32(230, 230, 230, 200)
                      : activeBrush() == BrushType::Smooth ? IM_COL32(120, 200, 255, 200)
                      : inverted                         ? IM_COL32(255, 110, 110, 200)
                                                         : IM_COL32(255, 190, 120, 200);
    dl->AddCircle(c, r, col, 64, 1.5f);
    dl->AddCircle(c, r * (0.25f + 0.75f * (1.0f - strength_)), IM_COL32(255, 255, 255, 60), 48, 1.0f);
}

} // namespace sf
