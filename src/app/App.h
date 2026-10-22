// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ai/BrushAssistant.h"
#include "ai/ImageTo3DService.h"
#include "core/Commands.h"
#include "core/Image.h"
#include "core/Document.h"
#include "core/Stroke.h"
#include "render/Camera.h"
#include "render/Renderer.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <array>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sf {

struct AppOptions {
    std::string openPath;   // model or image to open at start
    std::string script;     // commands to run after start-up (same language as sculptforge-cli)
    std::string renderPath; // headless: render a PNG and exit
    int width = 1024, height = 1024;
    float yawDegrees = -30.0f, pitchDegrees = -15.0f;
    int turntable = 0; // headless: render N frames around the model (<path>_000.png ...)
    bool wireframe = false;
    bool polygroups = false; // headless: colour each polygroup
    bool hideMesh = false; // headless: show only the splats
    std::string demo;       // scripted demo to play (see Demo.h)
    std::string recordPath; // record the session to this .mp4 (needs ffmpeg) and exit
    std::string demoAsset;  // file the demo opens (image or model)
    std::string demoOutput; // where the demo saves its result (optional)
    int fps = 30;
};

class Demo;

class App {
    friend class Demo;

public:
    App();
    ~App();
    bool init(const AppOptions& options);
    int run(); // returns the process exit code
    void shutdown();

    enum class FileAction { OpenObj, SaveObj, ExportGlb, ExportLods, SaveBrush, LoadBrush, OpenImage, ExportUsd, SaveSplats, ExportUnreal, SaveProject, Count };
    struct DialogRequest {
        App* app;
        FileAction action;
    };
    // Thread-safe: called from native file dialog callbacks.
    void queueFile(FileAction action, std::string path);

private:
    enum class Drag { None, Stroke, Orbit, Pan, Zoom, MaskCanvas };

    void handleEvent(const SDL_Event& e);
    void onMouseDown(float x, float y, Uint8 button);
    void onMouseMove(float x, float y, float dx, float dy, bool fromPen);
    void onMouseUp();
    void onKey(SDL_Keycode key, SDL_Keymod mod);

    bool pick(float x, float y, RayHit& hit) const;
    BrushType activeBrush() const;
    void beginStroke(float x, float y, const RayHit& hit);
    void strokeTo(float x, float y);
    void dabAt(float x, float y);
    void endStroke();

    void runCommand(const std::string& name, const Args& args = {});
    void openFileDialog();
    void saveFileDialog(FileAction action);
    AABB sceneBounds() const; // mesh plus the main body of any splats
    void drawSplatSection();
    void processPendingFiles();

    void setLayerWeight(int index, float weight);
    void animateLayers(float dt);
    void generateLods();
    void clearLods();
    Mesh& displayedMesh();

    void drawUi();
    void drawToolPanel();
    void drawMenuBar();
    void drawLayersPanel();
    void drawGameExportPanel();
    void drawBrushLab();
    void drawImageTo3D();
    bool loadReferenceImage(const std::string& path);
    void projectImageFromView();
    void startGenerator();
    void pollGenerator();
    int renderHeadless();
    void openPath(const std::string& path);
    void drawRecipeEditor(BrushRecipe& recipe);
    void drawAiSettings();
    void sendBrushRequest(const std::string& text);
    void pollBrushRequest();
    const BrushRecipe* activeRecipe() const;
    void drawBrushCursor();
    void updateTitle();

    int runRecording();
    std::unique_ptr<Demo> demo_;
    float uiScale_ = 1.0f; // UI layout scale (fonts are rasterised at this scale)
    ImFont* fontCaption_ = nullptr;
    ImFont* fontCaptionSub_ = nullptr;

    SDL_Window* window_ = nullptr;
    SDL_GLContext glContext_ = nullptr;
    bool running_ = true;
    std::string iniPath_;

    Document doc_;
    CommandRegistry commands_;
    Renderer renderer_;
    RenderSettings renderSettings_;
    Camera camera_;

    // Brush settings
    BrushType brush_ = BrushType::Standard;
    float radiusPx_ = 60.0f;
    float strength_ = 0.5f;
    Symmetry symmetry_;
    bool invertToggle_ = false;
    bool frontFacesOnly_ = false;
    bool pressureStrength_ = true;
    bool pressureSize_ = false;

    // Input / stroke state
    Drag drag_ = Drag::None;
    float mouseX_ = 0.0f, mouseY_ = 0.0f;
    float lastX_ = 0.0f, lastY_ = 0.0f, residual_ = 0.0f;
    float penPressure_ = 1.0f;
    bool lastInputPen_ = false;
    bool penSeen_ = false;
    BrushType strokeBrush_ = BrushType::Standard;
    bool strokeInvert_ = false;
    glm::vec3 grabPlanePoint_{0.0f}, grabLast_{0.0f};

    std::string status_;
    bool showHelp_ = false;
    bool showAbout_ = false;

    // Results from asynchronous native file dialogs (called off the main thread).
    struct PendingFile {
        FileAction action;
        std::string path;
    };
    DialogRequest dialogRequests_[int(FileAction::Count)];

    // Brush Lab (AI brush designer)
    bool showBrushLab_ = true;
    bool showLayersPanel_ = true;
    bool showGamePanel_ = true;
    bool showSplatsPanel_ = false;
    bool hideUi_ = false;
    uint64_t seenSplatsVersion_ = 0;
    ai::AiSettings aiSettings_;
    std::string aiSettingsPath_;
    std::vector<ai::ChatMessage> chat_; // what the model sees (append-only, successful turns only)
    struct ChatLine {
        enum class Kind { User, Assistant, Error } kind;
        std::string text;
    };
    std::vector<ChatLine> chatLog_; // what the panel shows
    char chatInput_[1024] = "";
    bool refineCurrent_ = true;
    struct BrushReply {
        ai::BrushSuggestion suggestion;
        std::string raw;
    };
    std::future<BrushReply> pendingBrush_;
    struct ModelList {
        std::vector<std::string> models;
        std::string error;
    };
    std::future<ModelList> pendingModels_; // "Find models" on a local server
    ModelList localModels_;
    bool modelsChecked_ = false;
    float busyTime_ = 0.0f;
    bool scrollChat_ = false;
    // Image to 3D
    AppOptions options_;
    bool showImageTo3D_ = false;
    bool textMode_ = false;
    char textPrompt_[512] = "";
    Image refImage_;
    std::string refImagePath_;
    GLuint refTexture_ = 0;
    float inflateThickness_ = 1.0f;
    int inflateDetail_ = 160;
    float reliefDepth_ = 0.12f;
    int reliefDetail_ = 384;
    bool reliefInvert_ = false;
    float projectDepth_ = 0.02f;
    bool projectInvert_ = false;
    char generatorCommand_[1024] = "";
    char falKey_[256] = "", falModel_[256] = "", meshyKey_[256] = "", hfToken_[256] = "";
    bool remeshGenerated_ = true;
    int voxelResolution_ = 200;
    int splatMeshResolution_ = 192;
    int remeshResolution_ = 256;
    std::future<ai::GeneratorResult> pendingGenerator_;
    std::shared_ptr<ai::GenerationProgress> generatorProgress_;
    float generatorTime_ = 0.0f;

    glm::vec3 lastDabHit_{0.0f};
    bool haveLastDab_ = false;
    uint32_t jitterSeed_ = 1;

    // Sculpt layers / expression preview
    char newLayerName_[64] = "Smile";
    int playingLayer_ = -1;
    float playTime_ = 0.0f;

    // Game export / LOD preview
    int lodLevels_ = 4;
    float lodRatio_ = 0.5f;
    std::vector<Mesh> lods_;
    // Unreal Engine hand-off.
    char unrealDestination_[256] = "/Game/SculptForge";
    float unrealHeightCm_ = 0.0f; // 0 = 1 unit = 1 m
    bool unrealNetwork_ = false;  // editor on another computer
    std::future<std::string> pendingUnreal_; // resolves to the status message
    void sendToUnreal();
    void pollUnreal();
    int previewLod_ = -1;
    const Mesh* lastDisplayed_ = nullptr;
    std::mutex pendingMutex_;
    std::vector<PendingFile> pendingFiles_;

    // Masking (PanelMasking.cpp): Ctrl strokes paint mask, Ctrl+Alt erases;
    // Ctrl+click on the background inverts it, Ctrl+drag there clears it.
    void drawMaskingSection();
    void beginMaskStroke(float x, float y);
    void maskCanvasMove(float x, float y);
    void maskCanvasRelease();
    bool strokeMask_ = false; // the current stroke edits the mask
    float maskStrength_ = 1.0f;
    int maskIterations_ = 1;
    float cavityThreshold_ = 0.02f;
    int maskGroup_ = 0;
    float maskCanvasX_ = 0.0f, maskCanvasY_ = 0.0f;
    bool maskCanvasMoved_ = false;
    // Polygroups & visibility (PanelPolygroups.cpp): Ctrl+Shift click/drag.
    bool polygroupMouseDown(float x, float y, SDL_Keymod mod); // true if it took the click
    void polygroupMouseUp();
    void drawPolygroupsSection();
    void drawPolygroupOverlay();
    bool hideRectActive_ = false;
    float hideRectX_ = 0.0f, hideRectY_ = 0.0f;
    float polygroupAngle_ = 30.0f;
    // visiblePolygroupCount is O(triangles): recomputed only when the key below changes.
    size_t visibleGroups_ = 0;
    std::array<uint64_t, 6> visibleGroupsKey_{};
    // Stroke types, alphas and the extra brushes (PanelBrushes.cpp).
    void drawBrushOptions();
    void snakeHookTo(float x, float y);
    void dragRectTo(float x, float y);
    glm::vec3 screenUpVector(float angle) const; // camera up rotated by `angle` around the view axis
    static void SDLCALL onAlphaChosen(void* userdata, const char* const* files, int filter);
    StrokeType strokeType_ = StrokeType::Dots;
    LazyMouse lazyMouse_;
    Spray spray_;
    float sprayScatter_ = 1.0f;          // jitter radius as a fraction of brush size
    glm::vec2 dragAnchor_{0.0f};         // DragRect: where the click landed (window pixels)
    glm::vec3 dragAnchorHit_{0.0f};
    float dabRadiusScale_ = 1.0f, dabStrengthScale_ = 1.0f, dabAngle_ = 0.0f; // per-dab overrides
    bool alphaInvert_ = false;
    std::mutex alphaMutex_;
    std::string pendingAlphaPath_; // set by the file dialog thread
    // Polypaint and MatCap materials (PanelPolypaint.cpp)
    bool paintMode_ = false;  // Paint brush: colour only, no sculpting (ZBrush Rgb on, Zadd off)
    bool paintRgb_ = false;   // also paint the colour while sculpting (ZBrush Rgb + Zadd)
    bool strokePaint_ = false, strokeSculpt_ = true;
    bool showPolypaintPanel_ = false; // off by default (keeps the viewport and demo videos clear)
    void registerPaintUiCommands();
    void drawPolypaintPanel();
    void paintAt(const DabParams& p);
    void pickColorUnderCursor();
    // First paint stroke on a mesh without polypaint: undo restores this snapshot so the attribute goes away again.
    std::unique_ptr<Mesh> paintSnapshot_;
    void beginPaintStroke();
    void finishPaintStroke(UndoRecord& record);
    const BrushRecipe* strokeRecipe() const; // recipe the current stroke sculpts with (none for Smooth / paint only)
    // ZBrush GoZ bridge and GoZ/STL files (PanelGoz.cpp).
    void drawGozMenu();
    void pollGoz();
    void runGozCommand(const std::string& name, const Args& args = {});
    static void SDLCALL onGozFileChosen(void* userdata, const char* const* files, int filter);
    enum class GozFile { Import, Export, ExportStl, Count };
    struct GozDialog {
        App* app;
        GozFile kind;
    };
    GozDialog gozDialogs_[int(GozFile::Count)];
    std::vector<std::pair<GozFile, std::string>> gozPendingFiles_; // guarded by pendingMutex_
    char gozFolder_[512] = "";   // custom GoZ root (empty = the platform default)
    bool gozAutoReceive_ = false; // import whenever ZBrush rewrites GoZ_ObjectList.txt
    long long gozListTime_ = 0;   // last seen modification time of the object list
    Uint64 gozLastPollNs_ = 0;
    bool gozInitialised_ = false;
    // Projects, unsaved-changes safety, recent files and autosave (PanelProject.cpp).
    bool interactive() const; // false for --render, --record and demos: never prompt
    void initProject();       // end of init(): recent files, autosave restore offer
    void finishProject();     // clean exit: marks the session closed normally
    void tickProject(float dt);
    void saveProjectAs(bool forceDialog); // Ctrl+S (dialog only when needed) / Ctrl+Shift+S
    void saveProjectTo(const std::string& path);
    void storeProjectSettings(); // camera, render, brush and symmetry -> doc_.projectSettings
    void applyProjectSettings(); // and back, after opening a project
    void afterCommand(const std::string& name, const Args& args, const CommandResult& r);
    // Runs `action` now, or after the Save / Don't Save / Cancel prompt if there are unsaved changes.
    void confirmDiscard(std::function<void()> action);
    void requestQuit();
    void openPathConfirmed(const std::string& path); // prompts first if the file replaces the scene
    void drawProjectPopups();
    void drawFileMenuProjectItems();
    void rememberRecent(const std::string& path);
    void saveProjectPrefs();
    std::string prefDir_;
    std::vector<std::string> recentFiles_;
    std::function<void()> discardAction_;
    bool openUnsavedPopup_ = false;
    bool continueAfterSave_ = false; // run discardAction_ once the Save As dialog saved
    bool offerRestore_ = false;
    int autosaveMinutes_ = 5; // 0 = off
    float autosaveTimer_ = 0.0f;
    uint64_t autosaveRevision_ = ~uint64_t(0);
    bool autosaveWritten_ = false;
    std::string windowTitle_;
};

} // namespace sf
