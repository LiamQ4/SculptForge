// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Demo.h"

#include "app/App.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstdarg>
#include <chrono>
#include <future>
#include <map>
#include <memory>

// ---------------------------------------------------------------------------
// Dear ImGui test-engine hooks: record where every labelled item was drawn.
// ---------------------------------------------------------------------------

namespace {

struct Placed {
    ImRect rect;
    ImRect clip; // the part of the screen the item's window shows
};

struct ItemTable {
    std::map<ImGuiID, Placed> rects;
    std::map<std::string, Placed> byLabel;
    // Every item drawn this frame with the ID-stack seed it was created under, so
    // items that never report a label (combo boxes) can be matched by hashing one.
    struct Seeded {
        ImGuiID id, seed;
        std::string window;
        Placed placed;
        std::vector<std::string> scopes;
    };
    std::vector<Seeded> frame;
    std::vector<std::string> scopes; // Demo::pushScope: panels drawn inside the shell's trays and drop-downs
};
ItemTable& items() {
    static ItemTable t;
    return t;
}

} // namespace

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData*) {
    ImRect clip = bb;
    if (ctx && ctx->CurrentWindow) {
        clip = ctx->CurrentWindow->ClipRect;
        clip.ClipWith(ImRect(ImVec2(0, 0), ctx->IO.DisplaySize));
    }
    items().rects[id] = {bb, clip};
    if (id && ctx && ctx->CurrentWindow && !ctx->CurrentWindow->IDStack.empty()) {
        std::string window = ctx->CurrentWindow->RootWindow->Name; // a child (list, table) counts as its panel
        if (const size_t hash = window.find("##"); hash != std::string::npos) window.resize(hash);
        items().frame.push_back({id, ctx->CurrentWindow->IDStack.back(), std::move(window), {bb, clip}, items().scopes});
    }
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags) {
    auto it = items().rects.find(id);
    if (!label || it == items().rects.end()) return;
    std::string text(label);
    std::string window = ctx && ctx->CurrentWindow ? ctx->CurrentWindow->RootWindow->Name : "";
    if (const size_t hash = window.find("##"); hash != std::string::npos) window.resize(hash);
    auto add = [&](const std::string& key) {
        items().byLabel[key] = it->second;
        if (!window.empty()) items().byLabel[window + "/" + key] = it->second;
        for (const auto& scope : items().scopes) items().byLabel[scope + "/" + key] = it->second;
    };
    add(text);
    if (const size_t hash = text.find("##"); hash != std::string::npos && hash > 0) add(text.substr(0, hash));
    // Labels that start with an icon (ICON_LC_*: UTF-8 private-use characters, U+E000-U+F8FF) are also found
    // by their words: "AI Brush" for "<sparkles> AI Brush".
    if (text.size() > 3 && static_cast<unsigned char>(text[0]) == 0xEE) {
        size_t start = 3;
        while (start < text.size() && text[start] == ' ') ++start;
        const std::string words = text.substr(start);
        if (!words.empty() && words.compare(0, 2, "##") != 0) {
            add(words);
            if (const size_t hash = words.find("##"); hash != std::string::npos && hash > 0) add(words.substr(0, hash));
        }
    }
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }

namespace sf {

namespace {

template <class T> bool ready(const std::future<T>& f) {
    return !f.valid() || f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

float smooth(float u) {
    u = std::clamp(u, 0.0f, 1.0f);
    return u * u * (3.0f - 2.0f * u);
}

// Point on a sphere of radius `r` from latitude/longitude in degrees (front = +Z).
glm::vec3 onSphere(float latDeg, float lonDeg, float r = 1.0f) {
    const float lat = glm::radians(latDeg), lon = glm::radians(lonDeg);
    return r * glm::vec3(std::cos(lat) * std::sin(lon), std::sin(lat), std::cos(lat) * std::cos(lon));
}

} // namespace

void Demo::beginFrame() {
    items().byLabel.clear();
    items().frame.clear();
    items().scopes.clear();
}

void Demo::pushScope(const char* name) { items().scopes.emplace_back(name); }

void Demo::popScope() {
    if (!items().scopes.empty()) items().scopes.pop_back();
}

std::vector<std::string> Demo::similarItems(const std::string& label) {
    auto lower = [](std::string s) {
        for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    std::string want = lower(label);
    if (const size_t hash = want.find_first_not_of('#'); hash != std::string::npos) want = want.substr(hash);
    std::vector<std::string> out;
    for (const auto& [name, rect] : items().byLabel)
        if (lower(name).find(want) != std::string::npos && out.size() < 8) out.push_back(name);
    return out;
}

bool Demo::findItem(const std::string& label, ImVec2& center, bool* visible, ImVec4* clipOut) const {
    auto found = [&](const Placed& p) {
        center = p.rect.GetCenter();
        if (visible) *visible = p.clip.Contains(center);
        if (clipOut) *clipOut = ImVec4(p.clip.Min.x, p.clip.Min.y, p.clip.Max.x, p.clip.Max.y);
        return true;
    };
    auto it = items().byLabel.find(label);
    if (it != items().byLabel.end()) return found(it->second);
    // Unlabelled items: hash the label the way ImGui's GetID() would.
    std::string window, name = label;
    if (const size_t slash = label.find('/'); slash != std::string::npos && slash > 0 && label.compare(0, 2, "##") != 0) {
        window = label.substr(0, slash);
        name = label.substr(slash + 1);
    }
    for (auto s = items().frame.rbegin(); s != items().frame.rend(); ++s) {
        if (!window.empty() && s->window != window &&
            std::find(s->scopes.begin(), s->scopes.end(), window) == s->scopes.end())
            continue;
        if (ImHashStr(name.c_str(), 0, s->seed) == s->id) return found(s->placed);
    }
    return false;
}

std::vector<std::string> Demo::names() { return {"showcase", "sculpt", "image-to-3d", "brush-lab", "game-ready", "splats"}; }

bool Demo::load(const std::string& name, std::string& error) {
    if (name == "showcase") buildShowcase();
    else if (name == "sculpt") buildSculpt();
    else if (name == "image-to-3d") buildImageTo3D();
    else if (name == "brush-lab") buildBrushLab();
    else if (name == "game-ready") buildGameReady();
    else if (name == "splats") buildSplats();
    else {
        error = "Unknown demo '" + name + "'";
        return false;
    }
    wait(1.0f); // hold the last frame
    return true;
}

void Demo::update(float dt) {
    clock_ += dt;
    if (ImGuiWindow* w = ImGui::FindWindowByName("Brush Lab"); w && std::getenv("SF_DEMO_TRACE"))
        SDL_Log("t=%.2f step=%zu brushlab=(%.0f,%.0f) mouse=(%.0f,%.0f) down=%d", clock_, step_, w->Pos.x, w->Pos.y, mouse_.x,
                mouse_.y, int(buttonDown_));
    stride_ = 1;
    while (step_ < steps_.size()) {
        if (!steps_[step_](stepTime_)) {
            stepTime_ += dt;
            return;
        }
        ++step_;
        stepTime_ = 0.0f;
    }
}

// ---------------------------------------------------------------- input

void Demo::mouseMotion(float x, float y) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = SDL_GetWindowID(app_.window_);
    e.motion.x = x;
    e.motion.y = y;
    e.motion.xrel = x - mouse_.x;
    e.motion.yrel = y - mouse_.y;
    e.motion.state = buttonDown_ ? SDL_BUTTON_LMASK : 0;
    mouse_ = ImVec2(x, y);
    app_.handleEvent(e);
}

void Demo::mouseButton(bool down, int button) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.windowID = SDL_GetWindowID(app_.window_);
    e.button.button = Uint8(button);
    e.button.down = down;
    e.button.clicks = 1;
    e.button.x = mouse_.x;
    e.button.y = mouse_.y;
    buttonDown_ = down && button == SDL_BUTTON_LEFT;
    app_.handleEvent(e);
}

ImVec2 Demo::project(const glm::vec3& p) const {
    const CanvasRect r = app_.canvasRect();
    const glm::vec4 c = app_.camera_.projection() * app_.camera_.view() * glm::vec4(p, 1.0f);
    const glm::vec2 ndc = glm::vec2(c) / c.w;
    return ImVec2(r.x + (ndc.x * 0.5f + 0.5f) * r.w, r.y + (0.5f - ndc.y * 0.5f) * r.h);
}

ImVec2 Demo::onModel(float u, float v) const {
    const AABB b = app_.doc_.mesh.bounds();
    ImVec2 lo(1e9f, 1e9f), hi(-1e9f, -1e9f);
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 c((i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y, (i & 4) ? b.max.z : b.min.z);
        const ImVec2 p = project(c);
        lo = ImVec2(std::min(lo.x, p.x), std::min(lo.y, p.y));
        hi = ImVec2(std::max(hi.x, p.x), std::max(hi.y, p.y));
    }
    ImVec2 p(lo.x + (hi.x - lo.x) * u, lo.y + (hi.y - lo.y) * v);
    const ImVec2 centre((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f);
    for (int i = 0; i < 20; ++i) { // walk towards the centre until the ray hits the sculpt
        RayHit hit;
        if (app_.pick(p.x, p.y, hit)) break;
        p = ImVec2(p.x + (centre.x - p.x) * 0.15f, p.y + (centre.y - p.y) * 0.15f);
    }
    return p;
}

// ---------------------------------------------------------------- building blocks

void Demo::wait(float seconds) {
    steps_.push_back([seconds](float t) { return t >= seconds; });
}

void Demo::caption(const std::string& title, const std::string& subtitle, float seconds) {
    steps_.push_back([this, title, subtitle, seconds](float) {
        captions_.push_back({title, subtitle, clock_, seconds});
        return true;
    });
}

void Demo::moveTo(float x, float y, float seconds) {
    auto from = std::make_shared<ImVec2>();
    steps_.push_back([this, x, y, seconds, from](float t) {
        if (t == 0.0f) *from = mouse_;
        const float u = smooth(t / seconds);
        mouseMotion(from->x + (x - from->x) * u, from->y + (y - from->y) * u);
        return t >= seconds;
    });
}

void Demo::moveToItem(const std::string& label, float seconds) {
    auto from = std::make_shared<ImVec2>();
    steps_.push_back([this, label, seconds, from](float t) {
        ImVec2 to;
        if (!findItem(label, to)) { // not on screen yet: give the UI a moment
            if (t > 3.0f) SDL_Log("Demo: no UI item \"%s\" to move to", label.c_str());
            return t > 3.0f;
        }
        if (t == 0.0f || (from->x == 0 && from->y == 0)) *from = mouse_;
        const float u = smooth(t / seconds);
        mouseMotion(from->x + (to.x - from->x) * u, from->y + (to.y - from->y) * u);
        return t >= seconds;
    });
}

void Demo::click(const std::string& label) {
    moveToItem(label);
    steps_.push_back([this](float t) {
        if (t == 0.0f) mouseButton(true, SDL_BUTTON_LEFT);
        if (t >= 0.08f) {
            mouseButton(false, SDL_BUTTON_LEFT);
            return true;
        }
        return false;
    });
    wait(0.25f);
}

void Demo::stroke(std::function<ImVec2(float u)> path, float seconds, bool invert) {
    // Glide to the start, press, follow the path, release.
    auto started = std::make_shared<bool>(false);
    auto from = std::make_shared<ImVec2>();
    steps_.push_back([this, path, from](float t) {
        const ImVec2 p = path(0.0f);
        const float u = smooth(t / 0.35f);
        if (t == 0.0f) *from = mouse_;
        mouseMotion(from->x + (p.x - from->x) * u, from->y + (p.y - from->y) * u);
        return t >= 0.35f;
    });
    steps_.push_back([this, path, seconds, invert, started](float t) {
        if (!*started) {
            app_.invertToggle_ = invert;
            mouseButton(true, SDL_BUTTON_LEFT);
            *started = true;
        }
        const ImVec2 p = path(std::min(t / seconds, 1.0f));
        mouseMotion(p.x, p.y);
        if (t >= seconds) {
            mouseButton(false, SDL_BUTTON_LEFT);
            app_.invertToggle_ = false;
            *started = false;
            return true;
        }
        return false;
    });
}

void Demo::orbit(float dx, float dy, float seconds) {
    steps_.push_back([this, dx, dy, seconds](float t) {
        if (t == 0.0f) mouseButton(true, SDL_BUTTON_RIGHT);
        const float u0 = smooth(std::max(0.0f, t - 1.0f / 30.0f) / seconds), u1 = smooth(t / seconds);
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.windowID = SDL_GetWindowID(app_.window_);
        e.motion.x = mouse_.x;
        e.motion.y = mouse_.y;
        e.motion.xrel = dx * (u1 - u0);
        e.motion.yrel = dy * (u1 - u0);
        e.motion.state = SDL_BUTTON_RMASK;
        app_.handleEvent(e);
        if (t >= seconds) {
            mouseButton(false, SDL_BUTTON_RIGHT);
            return true;
        }
        return false;
    });
}

void Demo::type(char* buffer, size_t size, const std::string& text, float seconds) {
    steps_.push_back([buffer, size, text, seconds](float t) {
        const size_t n = std::min(text.size(), size_t(float(text.size()) * std::min(t / seconds, 1.0f)));
        std::snprintf(buffer, size, "%s", text.substr(0, n).c_str());
        return t >= seconds;
    });
}

void Demo::run(std::function<void()> action) {
    steps_.push_back([action](float) {
        action();
        return true;
    });
}

void Demo::waitUntil(std::function<bool()> done, float timeout, int stride) {
    steps_.push_back([this, done, timeout, stride](float t) {
        stride_ = stride;
        // Real work (network, AI) runs in real time: pace the timeline to the clock so the
        // on-screen "N x speed" label is accurate.
        SDL_Delay(Uint32(1000 / std::max(app_.options_.fps, 1)));
        return done() || t >= timeout;
    });
}

// ---------------------------------------------------------------- overlay

void Demo::drawOverlay() {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 size = ImGui::GetIO().DisplaySize;

    // Lower-third captions (SF_DEMO_NO_CAPTIONS=1 records a clean take for posters).
    static const bool noCaptions = std::getenv("SF_DEMO_NO_CAPTIONS") != nullptr;
    for (const auto& c : captions_) {
        if (noCaptions) break;
        const float t = clock_ - c.start;
        if (t < 0.0f || t > c.duration) continue;
        const float a = std::min({1.0f, t / 0.35f, (c.duration - t) / 0.35f});
        ImFont* big = app_.fontCaption_ ? app_.fontCaption_ : ImGui::GetFont();
        ImFont* small = app_.fontCaptionSub_ ? app_.fontCaptionSub_ : ImGui::GetFont();
        const float bigSize = size.y * 0.042f, smallSize = size.y * 0.024f;
        const ImVec2 tsz = big->CalcTextSizeA(bigSize, FLT_MAX, 0.0f, c.title.c_str());
        const ImVec2 ssz = small->CalcTextSizeA(smallSize, FLT_MAX, 0.0f, c.subtitle.c_str());
        const float pad = size.y * 0.022f;
        const ImVec2 p0(size.x * 0.5f - std::max(tsz.x, ssz.x) * 0.5f - pad * 1.5f, size.y * 0.80f);
        const ImVec2 p1(size.x * 0.5f + std::max(tsz.x, ssz.x) * 0.5f + pad * 1.5f,
                        p0.y + tsz.y + (c.subtitle.empty() ? 0.0f : ssz.y + pad * 0.3f) + pad * 2.0f);
        dl->AddRectFilled(p0, p1, IM_COL32(12, 12, 14, int(215 * a)), pad * 0.6f);
        dl->AddRectFilled(ImVec2(p0.x, p1.y - 4.0f), ImVec2(p1.x, p1.y), IM_COL32(242, 140, 60, int(255 * a)), pad * 0.6f,
                          ImDrawFlags_RoundCornersBottom);
        dl->AddText(big, bigSize, ImVec2(size.x * 0.5f - tsz.x * 0.5f, p0.y + pad), IM_COL32(255, 255, 255, int(255 * a)),
                    c.title.c_str());
        if (!c.subtitle.empty())
            dl->AddText(small, smallSize, ImVec2(size.x * 0.5f - ssz.x * 0.5f, p0.y + pad + tsz.y + pad * 0.3f),
                        IM_COL32(205, 205, 210, int(255 * a)), c.subtitle.c_str());
    }

    // Honest time-lapse marker while waiting on a model.
    if (stride_ > 1) {
        char text[48];
        std::snprintf(text, sizeof text, "%dx speed - waiting for the model", stride_);
        ImFont* f = app_.fontCaptionSub_ ? app_.fontCaptionSub_ : ImGui::GetFont();
        const float fs = size.y * 0.022f;
        const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
        const ImVec2 p(size.x - ts.x - 40.0f, 60.0f);
        dl->AddRectFilled(ImVec2(p.x - 14, p.y - 8), ImVec2(p.x + ts.x + 14, p.y + ts.y + 8), IM_COL32(12, 12, 14, 200), 8.0f);
        dl->AddText(f, fs, p, IM_COL32(255, 200, 150, 255), text);
    }

    // Cursor arrow.
    const float s = size.y / 1080.0f * 1.15f;
    const ImVec2 m = mouse_;
    const ImVec2 pts[] = {ImVec2(m.x, m.y), ImVec2(m.x, m.y + 22 * s), ImVec2(m.x + 5.5f * s, m.y + 16.5f * s),
                          ImVec2(m.x + 9.5f * s, m.y + 25 * s), ImVec2(m.x + 13 * s, m.y + 23.5f * s),
                          ImVec2(m.x + 9 * s, m.y + 15 * s), ImVec2(m.x + 16 * s, m.y + 15 * s)};
    dl->AddConcavePolyFilled(pts, 7, IM_COL32(255, 255, 255, 255));
    dl->AddPolyline(pts, 7, IM_COL32(0, 0, 0, 255), ImDrawFlags_Closed, 1.5f * s);
    if (buttonDown_) dl->AddCircle(m, 14.0f * s, IM_COL32(255, 160, 80, 220), 32, 2.5f * s);
}

// ---------------------------------------------------------------- demos

void Demo::buildShowcase() {
    const float k = float(app_.options_.height) / 1080.0f;
    run([this] {
        app_.openPath(app_.options_.demoAsset);
        app_.runCommand("subdivide");
        app_.symmetry_.x = true;
        app_.showBrushLab_ = false;
        app_.hideUi_ = true;
        app_.camera_.frame(app_.doc_.mesh.bounds());
        app_.camera_.yaw = glm::radians(85.0f); // profile
        app_.camera_.pitch = glm::radians(-6.0f);
        app_.camera_.distance *= 0.86f;
        mouse_ = ImVec2(-50.0f, -50.0f); // cursor off screen for the beauty shot
    });
    caption("SculptForge", "The free, open-source alternative to ZBrush", 4.2f);
    // Orbiting turns 0.008 rad per pixel: 240 px glides from profile to a three-quarter front view.
    orbit(240.0f, 0.0f, 4.5f);
    run([this] {
        app_.hideUi_ = false;
    });
    caption("Sculpt the way you already do", "Brushes, custom brush recipes and symmetry", 4.0f);
    auto line = [this](float u0, float v0, float u1, float v1) {
        return [this, u0, v0, u1, v1](float u) {
            const ImVec2 a = onModel(u0, v0), b = onModel(u1, v1);
            return ImVec2(a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u);
        };
    };
    // Weather the mane: Edge Wear only chips the ridges, so the forms stay intact.
    run([this, k] {
        app_.radiusPx_ = 70.0f * k;
        app_.strength_ = 0.9f;
        // Custom brushes sit on the bottom shelf next to the built-in ones.
        auto& shelf = app_.layout_.bottomShelf;
        shelf.insert(std::find(shelf.begin(), shelf.end(), "widget.add_brush"), "recipe:Edge Wear");
    });
    click("Edge Wear");
    stroke(line(0.16f, 0.30f, 0.20f, 0.70f), 1.4f);
    stroke(line(0.30f, 0.06f, 0.18f, 0.30f), 1.1f);
    stroke(line(0.30f, 0.62f, 0.40f, 0.86f), 1.1f);
    // From clay to stone.
    auto from = std::make_shared<glm::vec3>();
    steps_.push_back([this, from](float t) {
        if (t == 0.0f) *from = app_.renderSettings_.baseColor;
        app_.renderSettings_.baseColor = glm::mix(*from, glm::vec3(0.62f, 0.60f, 0.56f), smooth(t / 1.6f));
        return t >= 1.6f;
    });
    run([this] {
        app_.hideUi_ = true;
        mouse_ = ImVec2(-50.0f, -50.0f);
    });
    // One continuous swing across the front (the bust has a flat back), with both captions on it.
    caption("Ready for games and film", "glTF, FBX and USD export with LODs and blend shapes", 4.0f);
    run([this] { captions_.push_back({"Free and open source", "Windows · macOS · Linux", clock_ + 4.2f, 4.6f}); });
    orbit(-160.0f, 0.0f, 8.8f);
}

void Demo::buildSculpt() {
    run([this] {
        app_.runCommand("new_sphere", {{"subdivisions", 6.0}});
        app_.symmetry_.x = true;
        app_.strength_ = 0.8f;
        app_.camera_.yaw = 0.0f;
        app_.camera_.pitch = glm::radians(-4.0f);
        app_.camera_.distance *= 0.78f;
        app_.showBrushLab_ = false;
    });
    caption("Sculpt the way you already do", "ZBrush-style brushes, symmetry, tablet pressure", 4.5f);
    wait(1.0f);
    auto arc = [this](float lat0, float lon0, float lat1, float lon1) {
        return [this, lat0, lon0, lat1, lon1](float u) {
            return project(onSphere(lat0 + (lat1 - lat0) * u, lon0 + (lon1 - lon0) * u));
        };
    };
    // Pull a point on the surface across the screen (Move brush).
    auto pull = [this](float lat, float lon, float dx, float dy) {
        return [this, lat, lon, dx, dy](float u) {
            const ImVec2 p = project(onSphere(lat, lon));
            const float e = smooth(u);
            return ImVec2(p.x + dx * e, p.y + dy * e);
        };
    };
    const float k = float(app_.options_.height) / 1080.0f; // screen offsets were authored at 1080p

    // Horns: drag the upper skull out with the Move brush (mirrored by symmetry).
    run([this, k] { app_.radiusPx_ = 110.0f * k; });
    click("Move##brush.move");
    stroke(pull(52, -38, -150 * k, -170 * k), 1.6f);
    run([this, k] { app_.radiusPx_ = 70.0f * k; });
    stroke(pull(70, -48, -140 * k, -150 * k), 1.3f);
    caption("Symmetry mirrors every stroke", "", 3.0f);

    // Heavy brow and cheekbones with Clay.
    run([this, k] { app_.radiusPx_ = 62.0f * k; });
    click("Clay##brush.clay");
    for (int pass = 0; pass < 3; ++pass) stroke(arc(24, -52, 30, -4), 0.9f);
    for (int pass = 0; pass < 2; ++pass) stroke(arc(-14, -58, -6, -24), 0.9f);

    // Eye sockets carved under the brow.
    run([this, k] {
        app_.radiusPx_ = 40.0f * k;
        app_.strength_ = 0.9f;
    });
    click("Standard##brush.standard");
    stroke(arc(10, -30, 10, -16), 0.7f, true);
    stroke(arc(6, -32, 6, -14), 0.7f, true);

    // Turn to the side and pull a snout forward.
    orbit(-330.0f * k, 0.0f, 1.6f);
    run([this, k] { app_.radiusPx_ = 120.0f * k; });
    click("Move##brush.move");
    stroke(pull(-14, 0, -190 * k, 30 * k), 1.6f);
    orbit(250.0f * k, 0.0f, 1.4f);

    // Nostrils and a mouth line.
    run([this, k] { app_.radiusPx_ = 30.0f * k; });
    click("Crease##brush.crease");
    stroke(arc(-36, -30, -33, 0), 1.0f, true);
    run([this, k] { app_.radiusPx_ = 34.0f * k; });
    click("Inflate##brush.inflate");
    stroke(arc(-6, -10, -4, -6), 0.5f);

    // Soften.
    run([this, k] {
        app_.radiusPx_ = 80.0f * k;
        app_.strength_ = 0.35f;
    });
    click("Smooth##brush.smooth");
    stroke(arc(-45, -60, 45, -60), 1.0f);
    caption("Smooth, fast and scriptable", "Unlimited undo, layers, voxel remesh, one-click LODs", 5.0f);
    orbit(-760.0f * k, 30.0f * k, 6.0f);
    wait(0.5f);
    run([this] {
        if (!app_.options_.demoOutput.empty()) app_.runCommand("export_glb", {{"path", app_.options_.demoOutput}});
    });
}

void Demo::buildImageTo3D() {
    run([this] {
        app_.runCommand("new_sphere", {{"subdivisions", 4.0}});
        app_.showBrushLab_ = false;
        app_.aiSettings_.generatorService = "huggingface";
        app_.aiSettings_.hfModel = "hunyuan3d-2.1";
        app_.remeshGenerated_ = true;
        app_.remeshResolution_ = 256;
    });
    caption("Image to 3D", "Drop in a drawing or concept - get a sculptable model", 4.0f);
    wait(1.0f);
    run([this] {
        app_.openPath(app_.options_.demoAsset);
        SDL_Log("demo: opened %s -> %s", app_.options_.demoAsset.c_str(), app_.status_.c_str());
    });
    wait(1.5f);
    caption("Free models, no account needed", "Hunyuan3D, TRELLIS.2 and Stable Fast 3D via Hugging Face", 4.0f);
    click("Generate 3D model");
    waitUntil([this] { return ready(app_.pendingGenerator_); }, 600.0f, 10);
    run([this] {
        app_.pollGenerator();
        app_.showImageTo3D_ = false;
        app_.camera_.frame(app_.doc_.mesh.bounds());
        app_.camera_.yaw = glm::radians(-25.0f);
    });
    caption("Ready to sculpt", "Remeshed into one clean, watertight surface", 4.0f);
    orbit(520.0f, 0.0f, 4.0f);
    run([this] {
        app_.radiusPx_ = 55.0f;
        app_.symmetry_.x = true;
    });
    click("Clay##brush.clay");
    stroke([this](float u) { return project(glm::vec3(-0.25f + 0.15f * u, 0.15f - 0.3f * u, 0.38f)); }, 1.2f);
    orbit(-260.0f, 30.0f, 2.5f);
}

void Demo::buildBrushLab() {
    run([this] {
        app_.runCommand("new_plane", {{"resolution", 320.0}});
        app_.runCommand("subdivide");
        app_.showBrushLab_ = true;
        app_.camera_.yaw = glm::radians(-20.0f);
        app_.camera_.pitch = glm::radians(-50.0f);
        app_.camera_.distance *= 0.62f;
        app_.camera_.target = glm::vec3(-0.42f, 0.0f, 0.05f); // plane to the right of the Brush Lab panel
        app_.radiusPx_ = 70.0f * float(app_.options_.height) / 1080.0f;
        app_.strength_ = 0.6f;
    });
    caption("Brush Lab", "Describe the brush you want. AI builds it.", 4.0f);
    wait(1.0f);
    moveToItem("##prompt");
    type(app_.chatInput_, sizeof app_.chatInput_, "dry cracked mud", 1.4f);
    click("Create");
    waitUntil([this] { return ready(app_.pendingBrush_); }, 240.0f, 8);
    run([this] { app_.pollBrushRequest(); });
    caption("Your own AI, even fully local", "This brush came from a small open model running on this machine", 4.5f);
    auto line = [this](float z0, float z1) {
        return [this, z0, z1](float u) { return project(glm::vec3(-0.45f + 0.9f * u, 0.0f, z0 + (z1 - z0) * u)); };
    };
    stroke(line(-0.4f, -0.3f), 1.6f);
    stroke(line(-0.05f, 0.0f), 1.6f);
    stroke(line(0.3f, 0.25f), 1.6f);
    moveToItem("##prompt");
    type(app_.chatInput_, sizeof app_.chatInput_, "deeper cracks, sharper edges", 1.6f);
    click("Create");
    waitUntil([this] { return ready(app_.pendingBrush_); }, 240.0f, 8);
    run([this] { app_.pollBrushRequest(); });
    caption("Refine it in plain words", "Every setting is also a slider", 3.5f);
    stroke(line(-0.45f, 0.45f), 1.8f);
    stroke(line(0.45f, -0.45f), 1.8f);
    wait(1.5f);
}

void Demo::buildGameReady() {
    run([this] {
        app_.openPath(app_.options_.demoAsset);
        app_.symmetry_.x = true;
        app_.strength_ = 0.8f;
        app_.showBrushLab_ = false;
        app_.camera_.frame(app_.doc_.mesh.bounds());
        app_.camera_.yaw = glm::radians(-20.0f);
        app_.camera_.pitch = glm::radians(-6.0f);
        app_.camera_.distance *= 0.85f;
        std::snprintf(app_.newLayerName_, sizeof app_.newLayerName_, "Grin");
        app_.layout_.openSubpalettes = {"tool.layers", "tool.export"}; // the Tool palette's Layers and Export
    });
    caption("Built for game & animation pipelines", "Sculpt layers become blend shapes / morph targets", 4.0f);
    click("+ Add");
    const float k = float(app_.options_.height) / 1080.0f;
    run([this, k] { app_.radiusPx_ = 70.0f * k; });
    click("Move##brush.move");
    // Pull the mouth corners up and back into a grin, on the new layer only.
    stroke([this, k](float u) {
        const ImVec2 p = project(onSphere(-32.0f, -28.0f));
        return ImVec2(p.x - 40.0f * k * smooth(u), p.y - 70.0f * k * smooth(u));
    }, 1.2f);
    stroke([this, k](float u) {
        const ImVec2 p = project(onSphere(-20.0f, -45.0f));
        return ImVec2(p.x - 30.0f * k * smooth(u), p.y - 55.0f * k * smooth(u));
    }, 1.0f);
    caption("Animate the expression", "Weights export as glTF morph targets and UsdSkel blend shapes", 4.0f);
    click("Play");
    wait(4.0f);
    click("Stop");
    caption("One-click LOD chain", "Blend shapes survive on every level", 4.0f);
    click("Generate LOD chain");
    click("LOD1");
    wait(1.0f);
    click("LOD2");
    wait(1.0f);
    click("LOD3");
    wait(1.5f);
    click("Game Ready/Sculpt");
    run([this] { app_.renderSettings_.wireframe = false; });
    caption("Send to Unreal Engine", "FBX with LODs and morph targets, straight into the editor", 4.5f);
    moveToItem("Game Ready/Send to Unreal", 0.8f);
    wait(1.5f);
    moveToItem("Game Ready/Export FBX for Unreal...", 0.6f);
    wait(1.0f);
    caption("Plus glTF, USD/USDZ and OBJ", "Validated with the Khronos and OpenUSD tools", 3.5f);
    orbit(500.0f * k, 0.0f, 3.5f);
}

void Demo::buildSplats() {
    run([this] {
        app_.openPath(app_.options_.demoAsset);
        app_.showBrushLab_ = false;
        app_.layout_.rightTray = {"splats", "tool"}; // the Splats palette first in the tray
        app_.camera_.frame(app_.sceneBounds());
        app_.camera_.yaw = glm::radians(-30.0f);
    });
    caption("Gaussian splats", "Turn a sculpt into splats for web viewers and engines", 4.0f);
    wait(1.0f);
    click("Gaussian Splats/Sculpt to splats");
    click("Gaussian Splats/Show mesh");
    orbit(450.0f, 20.0f, 4.0f);
    caption("And captures back into meshes", "Splat -> watertight, sculptable mesh in under a second", 4.0f);
    click("Gaussian Splats/Convert to mesh");
    click("Gaussian Splats/Show mesh");
    click("Gaussian Splats/Show##splats");
    orbit(-450.0f, -20.0f, 4.0f);
    caption("Open standards", "PLY, .splat, glTF KHR_gaussian_splatting, OpenUSD ParticleField", 3.5f);
    wait(2.0f);
}

} // namespace sf
