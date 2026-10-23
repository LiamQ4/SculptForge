// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// sculptforge --bench: the benchmark of core/Benchmark.h with the GPU side added (uploading what each dab
// changed, and the frame time while orbiting), in a window the size of the app's default one.
#include "app/Bench.h"

#include "render/Camera.h"
#include "render/GL.h"
#include "render/Renderer.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdio>

namespace sf {

namespace {
using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }
} // namespace

int runAppBenchmark(BenchCommandLine cl) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    SDL_Window* window = SDL_CreateWindow("SculptForge benchmark", 1600, 960, SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        std::fprintf(stderr, "OpenGL 3.3 context creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(0); // measure the frame, not the display's refresh
    int code = 1;
    Renderer renderer;
    std::string error;
    if (const char* missing = gl::load([](const char* n) { return reinterpret_cast<gl::GenericProc>(SDL_GL_GetProcAddress(n)); })) {
        std::fprintf(stderr, "Missing OpenGL function: %s\n", missing);
    } else if (!renderer.init(error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
    } else {
        BenchGpu gpu;
        gpu.description = std::string(reinterpret_cast<const char*>(gl::GetString(GL_RENDERER))) + ", OpenGL " +
                          reinterpret_cast<const char*>(gl::GetString(GL_VERSION));
        auto scene = [](Mesh& m) { return std::vector<SceneMesh>{SceneMesh{1, &m, true, true, nullptr}}; };
        gpu.upload = [&](Mesh& m) {
            SDL_PumpEvents();
            const auto t = Clock::now();
            renderer.syncScene(scene(m));
            gl::Finish();
            return msSince(t);
        };
        gpu.orbitFrameMs = [&](Mesh& m) {
            int w = 0, h = 0;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            Camera camera;
            camera.aspect = float(w) / float(std::max(h, 1));
            camera.frame(m.bounds());
            RenderSettings settings;
            renderer.syncScene(scene(m));
            const int warmup = 5, frames = 120;
            Clock::time_point t;
            for (int i = 0; i < warmup + frames; ++i) {
                if (i == warmup) {
                    gl::Finish();
                    t = Clock::now();
                }
                SDL_PumpEvents();
                camera.yaw = float(i) * 6.2831853f / float(frames);
                renderer.draw(camera, w, h, settings);
                SDL_GL_SwapWindow(window);
            }
            gl::Finish();
            return msSince(t) / frames;
        };
        code = runBenchCommandLine(cl, "## With the GPU (sculptforge --bench)\n\n", &gpu);
        renderer.shutdown();
    }
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return code;
}

} // namespace sf
