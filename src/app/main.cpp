// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/App.h"

#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

void usage() {
    std::puts(
        "Usage: sculptforge [model / splats / image file] [options]\n"
        "\n"
        "  --run \"<commands>\"     Run commands at start-up (see sculptforge-cli --list)\n"
        "  --render out.png         Render headless and exit (thumbnails, review sheets)\n"
        "  --turntable N            With --render: N frames around the model (out_000.png ...)\n"
        "  --size WxH               Render size (default 1024x1024)\n"
        "  --view YAW,PITCH         Camera angles in degrees (default -30,-15)\n"
        "  --zoom F                 Move the camera F times closer than the framed view (default 1)\n"
        "  --wireframe              Render with wireframe overlay\n"
        "  --polygroups             Render with polygroup colours\n"
        "  --hide-mesh              Render only the Gaussian splats\n"
        "  --demo NAME              Play a scripted demo (sculpt, image-to-3d, brush-lab, game-ready, splats)\n"
        "  --record out.mp4         Record the demo offscreen to a video (needs ffmpeg) and exit\n"
        "  --demo-asset FILE        Image or model the demo opens\n"
        "  --demo-output FILE       Save the demo's result (e.g. creature.glb)\n"
        "  --fps N                  Recording frame rate (default 30)\n"
        "  --ui-test FILE.sfui      Run a UI test script headless and exit (0 = pass); settings are isolated\n"
        "  --ui-out DIR             Where UI tests save screenshots");
}

// UI tests must never read or change the user's real settings, recent files or
// autosaves: point the per-user data directories at a fresh temporary folder.
void isolateSettings() {
    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path(ec) /
                     ("sculptforge-ui-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir, ec);
    const std::string d = dir.string();
#if defined(_WIN32)
    _putenv_s("APPDATA", d.c_str());
#else
    setenv("XDG_DATA_HOME", d.c_str(), 1);
    setenv("XDG_CONFIG_HOME", d.c_str(), 1);
#if defined(__APPLE__)
    setenv("HOME", d.c_str(), 1);
#endif
#endif
    for (const char* key : {"ANTHROPIC_API_KEY", "OPENAI_API_KEY", "HF_TOKEN", "HUGGING_FACE_HUB_TOKEN", "FAL_KEY", "MESHY_API_KEY"}) {
#if defined(_WIN32)
        _putenv_s(key, "");
#else
        unsetenv(key);
#endif
    }
}

} // namespace

int main(int argc, char** argv) {
    sf::AppOptions options;
    bool sizeGiven = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--run") options.script += next() + "\n";
        else if (a == "--render") options.renderPath = next();
        else if (a == "--turntable") options.turntable = std::atoi(next().c_str());
        else if (a == "--size") sizeGiven = std::sscanf(next().c_str(), "%dx%d", &options.width, &options.height) == 2;
        else if (a == "--view") std::sscanf(next().c_str(), "%f,%f", &options.yawDegrees, &options.pitchDegrees);
        else if (a == "--zoom") options.zoom = std::max(0.05f, float(std::atof(next().c_str())));
        else if (a == "--wireframe") options.wireframe = true;
        else if (a == "--polygroups") options.polygroups = true;
        else if (a == "--hide-mesh") options.hideMesh = true;
        else if (a == "--demo") options.demo = next();
        else if (a == "--record") options.recordPath = next();
        else if (a == "--demo-asset") options.demoAsset = next();
        else if (a == "--demo-output") options.demoOutput = next();
        else if (a == "--fps") options.fps = std::atoi(next().c_str());
        else if (a == "--ui-test") options.uiTest = next();
        else if (a == "--ui-out") options.uiOutDir = next();
        else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else if (!a.empty() && a[0] != '-') options.openPath = a;
        else {
            std::fprintf(stderr, "Unknown option %s\n", a.c_str());
            usage();
            return 1;
        }
    }
    if (!options.uiTest.empty()) {
        if (!sizeGiven) options.width = 1600, options.height = 960;
        isolateSettings();
    }
    sf::App app;
    if (!app.init(options)) {
        app.shutdown();
        return 1;
    }
    int code = app.run();
    app.shutdown();
    return code;
}
