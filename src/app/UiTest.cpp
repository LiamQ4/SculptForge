// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// UI test scripts (.sfui): drive the real interface the way a person does and
// check what it shows. One step per line, '#' starts a comment, quote words
// containing spaces. Items are found by their ImGui label ("Find models",
// "Brush Lab/Save settings" for the one in that window).
//
//   click LABEL / double_click LABEL / right_click LABEL / hover LABEL
//   type LABEL TEXT          click a text field and type into it
//   key COMBO                e.g. enter, escape, ctrl+z, ctrl+shift+z, 3, comma, backtick, tab, f3
//   hold MODS                ctrl / shift / alt / ctrl+shift ... or none (for strokes and drags)
//   stroke U0 V0 U1 V1 [S]   brush stroke across the model (fractions of its on-screen bounds)
//   drag X0 Y0 X1 Y1 [S]     left-drag in window fractions (rectangle hide, sliders...)
//   orbit DX DY              right-drag on the background, in pixels
//   run COMMANDS             sculptforge-cli commands, e.g. run new_sphere subdivisions=4
//   drop FILE                as if FILE were dropped onto the window (relative to the script's folder)
//   env NAME VALUE           set an environment variable; $SCRIPT_DIR is the script's folder
//   wait SECONDS / wait_idle [TIMEOUT] (AI requests and background jobs) / wait_for LABEL [TIMEOUT]
//   expect LABEL / expect_not LABEL
//   expect_text TEXT / expect_no_text TEXT   any text drawn this frame contains TEXT
//   expect_status TEXT       the status bar message contains TEXT
//   expect_help TEXT         the Ctrl-hover help box shown this frame contains TEXT (hold ctrl, then hover)
//   clipboard TEXT           put TEXT on the clipboard (to test pasting, e.g. a brush code)
//   expect_clipboard TEXT    the clipboard contains TEXT (after a copy)
//   click_subtool NAME [U V] left-click SubTool NAME in the viewport (with the held modifiers, e.g.
//                            hold alt to select it); U V: fractions of its on-screen bounds
//   expect_value NAME OP N   triangles, vertices, layers, brushes, masked, hidden, groups, colored,
//                            can_undo, dirty, splats, show_polygroups, show_mask, wireframe,
//                            subtools (count), active_subtool (index, 0 = top), visible_subtools,
//                            edited_subtools (SubTools with undo history), user_brushes (brushes in the
//                            user library), library_files (.sfbrush files in it; UI tests get an empty
//                            one in --ui-out/brush-library), ui_config (1 once Store Config wrote ui.json),
//                            console_open, command_palette_open, customize, shelves_visible,
//                            right_tray_open, left_tray_open, lightbox_open, shelves (1 shown), left_tray / right_tray (palettes in
//                            the tray, -1 when closed); OP is == != < <= > >=
//   (mesh values are the active SubTool's)
//   shot NAME                save a screenshot NAME.png (to --ui-out)
#include "app/App.h"
#include "app/Demo.h"
#include "app/Ui.h"

#include "core/BrushLibrary.h"
#include "core/Script.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <set>
#include <sstream>

namespace sf {

namespace {

// Splits a line into words; "double quoted" words may contain spaces.
std::vector<std::string> splitWords(const std::string& line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i >= line.size() || line[i] == '#') break;
        std::string w;
        if (line[i] == '"') {
            ++i;
            while (i < line.size() && line[i] != '"') w += line[i++];
            ++i;
        } else {
            while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) w += line[i++];
        }
        out.push_back(w);
    }
    return out;
}

// Everything after the first `skip` words, verbatim (for `run` and `type`).
std::string restOf(const std::string& line, int skip) {
    size_t i = 0;
    for (int k = 0; k < skip; ++k) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i < line.size() && line[i] == '"') {
            i = line.find('"', i + 1);
            i = i == std::string::npos ? line.size() : i + 1;
        } else {
            while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        }
    }
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    std::string rest = line.substr(i);
    if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"') rest = rest.substr(1, rest.size() - 2);
    return rest;
}

SDL_Keymod parseMods(const std::string& text, std::string* key = nullptr) {
    SDL_Keymod mods = SDL_KMOD_NONE;
    std::stringstream ss(text);
    std::string part, last;
    while (std::getline(ss, part, '+')) {
        std::string p = part;
        std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (p == "ctrl" || p == "control") mods |= SDL_KMOD_LCTRL;
        else if (p == "shift") mods |= SDL_KMOD_LSHIFT;
        else if (p == "alt") mods |= SDL_KMOD_LALT;
        else if (p == "cmd" || p == "gui" || p == "super") mods |= SDL_KMOD_LGUI;
        else last = part;
    }
    if (key) *key = last;
    return mods;
}

template <class T> bool busy(const std::future<T>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

bool compare(double a, const std::string& op, double b) {
    if (op == "==") return std::abs(a - b) < 1e-6;
    if (op == "!=") return std::abs(a - b) >= 1e-6;
    if (op == "<") return a < b;
    if (op == "<=") return a <= b;
    if (op == ">") return a > b;
    if (op == ">=") return a >= b;
    return false;
}

std::string notFound(const std::string& label) {
    std::string msg = "no UI item labelled \"" + label + "\"";
    const auto similar = Demo::similarItems(label);
    for (size_t i = 0; i < similar.size(); ++i) msg += (i ? ", \"" : " (similar: \"") + similar[i] + "\"";
    if (!similar.empty()) msg += ")";
    return msg;
}

std::string number(double v) {
    std::ostringstream ss;
    ss << v;
    return ss.str();
}

} // namespace

void Demo::fail(int line, const std::string& message) {
    failures_.push_back(scriptName_ + ":" + std::to_string(line) + ": " + message);
    SDL_Log("FAIL %s", failures_.back().c_str());
    if (!shotDir_.empty()) shotRequest_ = shotDir_ + "/FAIL-line-" + std::to_string(line) + ".png";
    step_ = steps_.size(); // stop: later steps depend on this one
    // The frame loop still renders one more frame, so the failure screenshot shows the state.
}

double Demo::query(const std::string& name, bool& known) const {
    const Mesh& m = app_.doc_.mesh;
    known = true;
    if (name == "triangles") return double(m.triangleCount());
    if (name == "vertices") return double(m.vertexCount());
    if (name == "layers") return double(m.layers.size());
    if (name == "brushes") return double(app_.doc_.brushes.size());
    if (name == "colored") return m.hasColors() ? 1.0 : 0.0;
    if (name == "can_undo") return app_.doc_.undo.canUndo() ? 1.0 : 0.0;
    if (name == "dirty") return app_.doc_.dirty() ? 1.0 : 0.0;
    if (name == "splats") return double(app_.doc_.splats.size());
    if (name == "show_polygroups") return app_.renderSettings_.showPolygroups ? 1.0 : 0.0;
    if (name == "show_mask") return app_.renderSettings_.showMask ? 1.0 : 0.0;
    if (name == "wireframe") return app_.renderSettings_.wireframe ? 1.0 : 0.0;
    if (name == "shelves") return app_.layout_.shelvesVisible ? 1.0 : 0.0;
    if (name == "left_tray") return app_.layout_.leftTrayOpen ? double(app_.layout_.leftTray.size()) : -1.0;
    if (name == "right_tray") return app_.layout_.rightTrayOpen ? double(app_.layout_.rightTray.size()) : -1.0;
    if (name == "ui_config") {
        std::error_code ec;
        const std::string p = app_.uiConfigPath();
        return !p.empty() && std::filesystem::is_regular_file(std::filesystem::path(std::u8string(p.begin(), p.end())), ec) ? 1.0 : 0.0;
    }
    if (name == "console_open") return app_.layout_.consoleOpen ? 1.0 : 0.0;
    if (name == "command_palette_open") return app_.palette_.isOpen() ? 1.0 : 0.0;
    if (name == "customize") return app_.layout_.customize ? 1.0 : 0.0;
    if (name == "shelves_visible") return app_.layout_.shelvesVisible ? 1.0 : 0.0;
    if (name == "right_tray_open") return app_.layout_.rightTrayOpen ? 1.0 : 0.0;
    if (name == "left_tray_open") return app_.layout_.leftTrayOpen ? 1.0 : 0.0;
    if (name == "lightbox_open") return app_.lightbox_.open() ? 1.0 : 0.0;
    const Document& doc = app_.doc_;
    if (name == "subtools") return double(doc.subtoolCount());
    if (name == "active_subtool") return double(doc.activeSubtool);
    if (name == "visible_subtools")
        return double(std::count_if(doc.subtools.begin(), doc.subtools.end(), [](const SubTool& s) { return s.visible; }));
    if (name == "edited_subtools") { // SubTools with undo history (the active one's lives in doc.undo)
        int n = 0;
        for (int i = 0; i < doc.subtoolCount(); ++i)
            n += (i == doc.activeSubtool ? doc.undo.canUndo() : doc.subtools[size_t(i)].undo.canUndo()) ? 1 : 0;
        return double(n);
    }
    if (name == "user_brushes")
        return double(std::count_if(doc.brushes.begin(), doc.brushes.end(), [](const BrushRecipe& b) { return isUserLibraryBrush(b); }));
    if (name == "library_files") {
        const std::string& dir = brushLibraryPaths().userDir;
        std::error_code ec;
        size_t n = 0;
        if (!dir.empty())
            for (std::filesystem::directory_iterator it(std::filesystem::path(std::u8string(dir.begin(), dir.end())), ec), end;
                 !ec && it != end; it.increment(ec))
                n += it->path().extension() == ".sfbrush";
        return double(n);
    }
    if (name == "masked") {
        size_t n = 0;
        for (float v : m.mask) n += v > 0.5f;
        return double(n);
    }
    if (name == "hidden") {
        size_t n = 0;
        for (uint8_t h : m.hidden) n += h != 0;
        return double(n);
    }
    if (name == "groups") {
        std::set<uint32_t> ids(m.polygroups.begin(), m.polygroups.end());
        return double(std::max<size_t>(ids.size(), m.triangleCount() ? 1 : 0));
    }
    known = false;
    return 0.0;
}

void Demo::testClick(int line, const std::string& label, int button, bool doubleClick) {
    // Find the item (give the UI a moment to appear), glide there, press and release.
    steps_.push_back([this, line, label](float t) {
        ImVec2 to;
        bool visible = false;
        if (!findItem(label, to, &visible)) {
            if (t > 3.0f) fail(line, notFound(label));
            return t > 3.0f;
        }
        if (!visible) {
            // Scrolled out of its panel: wheel the panel towards it, as a person would.
            ImVec4 clip;
            findItem(label, to, &visible, &clip);
            if (clip.z > clip.x && clip.w > clip.y && t <= 2.0f) {
                mouseMotion((clip.x + clip.z) * 0.5f, (clip.y + clip.w) * 0.5f);
                SDL_Event e{};
                e.type = SDL_EVENT_MOUSE_WHEEL;
                e.wheel.windowID = SDL_GetWindowID(app_.window_);
                e.wheel.y = to.y > clip.w ? -1.0f : 1.0f;
                e.wheel.mouse_x = mouse_.x;
                e.wheel.mouse_y = mouse_.y;
                app_.handleEvent(e);
                return false;
            }
            // Off the screen, or the panel won't scroll: a person couldn't click it either.
            if (t > 1.0f) fail(line, "\"" + label + "\" is not visible (off screen or scrolled out of view)");
            return t > 1.0f;
        }
        mouseMotion(to.x, to.y);
        return true;
    });
    steps_.push_back([this, button, doubleClick](float t) {
        // A frame of hover first, so ImGui registers the item as hovered before the press.
        if (t == 0.0f) return false;
        mouseButton(true, button);
        mouseButton(false, button);
        if (doubleClick) {
            SDL_Event e{};
            e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            e.button.windowID = SDL_GetWindowID(app_.window_);
            e.button.button = Uint8(button);
            e.button.down = true;
            e.button.clicks = 2;
            e.button.x = mouse_.x;
            e.button.y = mouse_.y;
            app_.handleEvent(e);
            e.type = SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.down = false;
            app_.handleEvent(e);
        }
        return true;
    });
    wait(0.1f);
}

void Demo::testKey(int line, const std::string& combo) {
    std::string keyName;
    const SDL_Keymod mods = parseMods(combo, &keyName);
    std::string lower = keyName;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (lower == "enter") keyName = "Return"; // SDL's name for it
    else if (lower == "esc") keyName = "Escape";
    else if (lower == "comma") keyName = ",";
    else if (lower == "backtick" || lower == "grave") keyName = "`";
    else if (lower == "tab") keyName = "Tab";
    else if (lower == "f3") keyName = "F3";
    const SDL_Keycode key = SDL_GetKeyFromName(keyName.c_str());
    if (key == SDLK_UNKNOWN) {
        run([this, line, keyName] { fail(line, "unknown key \"" + keyName + "\""); });
        return;
    }
    steps_.push_back([this, mods, key](float) {
        const SDL_Keymod before = SDL_GetModState();
        SDL_SetModState(mods | held_);
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.windowID = SDL_GetWindowID(app_.window_);
        e.key.key = key;
        e.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
        e.key.mod = mods | held_;
        e.key.down = true;
        app_.handleEvent(e);
        e.type = SDL_EVENT_KEY_UP;
        e.key.down = false;
        e.key.mod = held_; // the combo's modifiers are released with the key
        app_.handleEvent(e);
        SDL_SetModState(before);
        return true;
    });
    wait(0.1f);
}

bool Demo::parseStep(int line, const std::vector<std::string>& w, std::string& error) {
    const std::string& verb = w[0];
    auto need = [&](size_t n) {
        if (w.size() >= n + 1) return true;
        error = "'" + verb + "' needs " + std::to_string(n) + " argument" + (n == 1 ? "" : "s");
        return false;
    };
    auto num = [&](size_t i, float fallback) { return i < w.size() ? std::strtof(w[i].c_str(), nullptr) : fallback; };

    if (verb == "click" || verb == "double_click" || verb == "right_click") {
        if (!need(1)) return false;
        testClick(line, w[1], verb == "right_click" ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT, verb == "double_click");
    } else if (verb == "hover") {
        if (!need(1)) return false;
        const std::string label = w[1];
        steps_.push_back([this, line, label](float t) {
            ImVec2 to;
            if (findItem(label, to)) {
                mouseMotion(to.x, to.y);
                return true;
            }
            if (t > 3.0f) fail(line, notFound(label));
            return t > 3.0f;
        });
        wait(0.6f); // tooltips appear after a short delay
    } else if (verb == "type") {
        if (!need(2)) return false;
        testClick(line, w[1], SDL_BUTTON_LEFT, false);
        auto text = std::make_shared<std::string>(restOf(lineText_, 2));
        steps_.push_back([this, text](float) {
            // Select what's there, then type over it like a person would.
            testKeyNow(SDLK_A, SDL_KMOD_LCTRL);
            SDL_Event e{};
            e.type = SDL_EVENT_TEXT_INPUT;
            e.text.windowID = SDL_GetWindowID(app_.window_);
            e.text.text = text->c_str();
            app_.handleEvent(e);
            return true;
        });
        wait(0.1f);
    } else if (verb == "key") {
        if (!need(1)) return false;
        testKey(line, w[1]);
    } else if (verb == "hold") {
        if (!need(1)) return false;
        const SDL_Keymod mods = w[1] == "none" ? SDL_KMOD_NONE : parseMods(w[1]);
        run([this, mods] {
            held_ = mods;
            SDL_SetModState(mods);
        });
    } else if (verb == "stroke") {
        if (!need(4)) return false;
        const float u0 = num(1, 0), v0 = num(2, 0), u1 = num(3, 0), v1 = num(4, 0), seconds = num(5, 0.6f);
        auto ends = std::make_shared<std::pair<ImVec2, ImVec2>>();
        run([this, ends, u0, v0, u1, v1] { *ends = {onModel(u0, v0), onModel(u1, v1)}; });
        stroke([ends](float u) {
            return ImVec2(ends->first.x + (ends->second.x - ends->first.x) * u,
                          ends->first.y + (ends->second.y - ends->first.y) * u);
        }, seconds);
        run([this] { SDL_SetModState(held_); });
        wait(0.1f);
    } else if (verb == "click_subtool") {
        if (!need(1)) return false;
        const std::string name = w[1];
        const float u = num(2, 0.5f), v = num(3, 0.5f);
        steps_.push_back([this, line, name, u, v](float t) {
            const int index = app_.doc_.findSubtool(name);
            if (index < 0) {
                fail(line, "no SubTool named \"" + name + "\"");
                return true;
            }
            // The point of its on-screen bounds nearest (U, V) where it's the front SubTool and no panel covers it.
            const AABB b = app_.doc_.subtoolMesh(index).bounds();
            ImVec2 lo(1e9f, 1e9f), hi(-1e9f, -1e9f);
            for (int i = 0; i < 8; ++i) {
                const ImVec2 p = project(glm::vec3((i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y,
                                                   (i & 4) ? b.max.z : b.min.z));
                lo = ImVec2(std::min(lo.x, p.x), std::min(lo.y, p.y));
                hi = ImVec2(std::max(hi.x, p.x), std::max(hi.y, p.y));
            }
            const ImVec2 want(lo.x + (hi.x - lo.x) * u, lo.y + (hi.y - lo.y) * v);
            auto covered = [](ImVec2 q) {
                for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
                    if (w->WasActive && !w->Hidden && w->Rect().Contains(q)) return true;
                return false;
            };
            ImVec2 p;
            bool found = false;
            float best = 1e30f;
            constexpr int kGrid = 16;
            for (int gy = 0; gy <= kGrid; ++gy) {
                for (int gx = 0; gx <= kGrid; ++gx) {
                    const ImVec2 q(lo.x + (hi.x - lo.x) * float(gx) / kGrid, lo.y + (hi.y - lo.y) * float(gy) / kGrid);
                    const float d = (q.x - want.x) * (q.x - want.x) + (q.y - want.y) * (q.y - want.y);
                    if (d >= best || covered(q) || app_.pickSubtool(q.x, q.y) != index) continue;
                    p = q;
                    best = d;
                    found = true;
                }
            }
            if (!found) {
                fail(line, "SubTool \"" + name + "\" can't be clicked (hidden, covered or off screen)");
                return true;
            }
            if (t == 0.0f) { // hover for a frame first
                mouseMotion(p.x, p.y);
                return false;
            }
            mouseButton(true, SDL_BUTTON_LEFT);
            mouseButton(false, SDL_BUTTON_LEFT);
            return true;
        });
        wait(0.1f);
    } else if (verb == "drag") {
        if (!need(4)) return false;
        const float x0 = num(1, 0), y0 = num(2, 0), x1 = num(3, 0), y1 = num(4, 0), seconds = num(5, 0.4f);
        steps_.push_back([this, x0, y0, x1, y1, seconds](float t) {
            int ww = 0, wh = 0;
            SDL_GetWindowSize(app_.window_, &ww, &wh);
            const ImVec2 a(x0 * float(ww), y0 * float(wh)), b(x1 * float(ww), y1 * float(wh));
            if (t == 0.0f) {
                mouseMotion(a.x, a.y);
                return false;
            }
            if (!buttonDown_ && t < seconds) mouseButton(true, SDL_BUTTON_LEFT);
            const float u = std::min(t / seconds, 1.0f);
            mouseMotion(a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u);
            if (t >= seconds) {
                mouseButton(false, SDL_BUTTON_LEFT);
                return true;
            }
            return false;
        });
        wait(0.1f);
    } else if (verb == "orbit") {
        if (!need(2)) return false;
        orbit(num(1, 0), num(2, 0), 0.4f);
    } else if (verb == "drop") {
        if (!need(1)) return false;
        auto file = std::filesystem::path(std::u8string(w[1].begin(), w[1].end()));
        if (file.is_relative()) file = std::filesystem::path(std::u8string(scriptDir_.begin(), scriptDir_.end())) / file;
        const auto u8 = file.lexically_normal().u8string();
        run([this, path = std::string(u8.begin(), u8.end())] { app_.openPathConfirmed(path); });
    } else if (verb == "env") {
        if (!need(2)) return false;
        std::string value = restOf(lineText_, 2);
        for (size_t at = value.find("$SCRIPT_DIR"); at != std::string::npos; at = value.find("$SCRIPT_DIR", at))
            value.replace(at, 11, scriptDir_);
        run([name = w[1], value] {
#if defined(_WIN32)
            _putenv_s(name.c_str(), value.c_str());
#else
            setenv(name.c_str(), value.c_str(), 1);
#endif
        });
    } else if (verb == "run") {
        if (!need(1)) return false;
        std::vector<ScriptStatement> statements;
        if (!parseScript(restOf(lineText_, 1), statements, error)) return false;
        run([this, line, statements] {
            for (const auto& st : statements) {
                app_.runCommand(st.command, st.args);
                if (!app_.lastCommandOk_) {
                    fail(line, st.command + " failed: " + app_.status_);
                    return;
                }
            }
        });
    } else if (verb == "wait") {
        if (!need(1)) return false;
        wait(num(1, 0));
    } else if (verb == "wait_idle") {
        const float timeout = num(1, 120.0f);
        steps_.push_back([this, line, timeout](float t) {
            const bool idle = !busy(app_.pendingBrush_) && !busy(app_.pendingGenerator_) && !busy(app_.pendingModels_) &&
                              !busy(app_.pendingUnreal_) && !busy(app_.pendingComfyCheck_) && !app_.galleryBusy();
            if (idle) return true;
            SDL_Delay(10); // real work runs in real time
            if (t > timeout) fail(line, "still busy after " + number(timeout) + " s");
            return t > timeout;
        });
        wait(0.2f); // let the app pick up the results
    } else if (verb == "wait_for") {
        if (!need(1)) return false;
        const std::string label = w[1];
        const float timeout = num(2, 10.0f);
        steps_.push_back([this, line, label, timeout](float t) {
            ImVec2 at;
            if (findItem(label, at)) return true;
            SDL_Delay(10);
            if (t > timeout) fail(line, "\"" + label + "\" did not appear within " + number(timeout) + " s");
            return t > timeout;
        });
    } else if (verb == "expect" || verb == "expect_not") {
        if (!need(1)) return false;
        const std::string label = w[1];
        const bool want = verb == "expect";
        // Expectations look at the frame drawn after the previous step settled.
        steps_.push_back([this, line, label, want](float t) {
            if (t == 0.0f) return false;
            ImVec2 at;
            if (findItem(label, at) != want)
                fail(line, want ? "expected UI item \"" + label + "\"" : "UI item \"" + label + "\" should not be shown");
            return true;
        });
    } else if (verb == "expect_text" || verb == "expect_no_text") {
        if (!need(1)) return false;
        const std::string text = restOf(lineText_, 1);
        const bool want = verb == "expect_text";
        steps_.push_back([this, line, text, want](float t) {
            if (t == 0.0f) return false;
            if ((frameText_.find(text) != std::string::npos) != want)
                fail(line, want ? "no text \"" + text + "\" on screen" : "text \"" + text + "\" is on screen");
            return true;
        });
    } else if (verb == "expect_status") {
        if (!need(1)) return false;
        const std::string text = restOf(lineText_, 1);
        run([this, line, text] {
            if (app_.status_.find(text) == std::string::npos)
                fail(line, "status is \"" + app_.status_ + "\", expected it to contain \"" + text + "\"");
        });
    } else if (verb == "expect_help") {
        if (!need(1)) return false;
        const std::string text = restOf(lineText_, 1);
        steps_.push_back([this, line, text](float t) {
            if (t == 0.0f) return false; // the frame drawn after the previous step
            const std::string& shown = ui::HelpShown();
            if (shown.empty()) fail(line, "no Ctrl-help box is shown (hold ctrl, then hover an item)");
            else if (shown.find(text) == std::string::npos)
                fail(line, "the Ctrl-help box says \"" + shown.substr(0, 120) + "\", expected \"" + text + "\"");
            return true;
        });
    } else if (verb == "clipboard") {
        if (!need(1)) return false;
        const std::string text = restOf(lineText_, 1);
        run([this, line, text] {
            if (!SDL_SetClipboardText(text.c_str())) fail(line, std::string("can't set the clipboard: ") + SDL_GetError());
        });
    } else if (verb == "expect_clipboard") {
        if (!need(1)) return false;
        const std::string text = restOf(lineText_, 1);
        run([this, line, text] {
            char* clip = SDL_GetClipboardText();
            const std::string have = clip ? clip : "";
            SDL_free(clip);
            if (have.find(text) == std::string::npos)
                fail(line, "clipboard is \"" + have.substr(0, 80) + "\", expected it to contain \"" + text + "\"");
        });
    } else if (verb == "expect_value") {
        if (!need(3)) return false;
        bool known = false;
        query(w[1], known);
        if (!known) {
            error = "unknown value '" + w[1] + "'";
            return false;
        }
        const std::string name = w[1], op = w[2];
        const double want = num(3, 0);
        run([this, line, name, op, want] {
            bool k = false;
            const double v = query(name, k);
            if (!compare(v, op, want)) fail(line, name + " is " + number(v) + ", expected " + op + " " + number(want));
        });
    } else if (verb == "shot") {
        if (!need(1)) return false;
        const std::string name = w[1];
        steps_.push_back([this, name](float t) {
            if (t == 0.0f) return false; // a settled frame
            if (!shotDir_.empty()) shotRequest_ = shotDir_ + "/" + name + ".png";
            return true;
        });
        wait(0.05f);
    } else {
        error = "unknown step '" + verb + "'";
        return false;
    }
    return true;
}

void Demo::testKeyNow(SDL_Keycode key, SDL_Keymod mods) {
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.windowID = SDL_GetWindowID(app_.window_);
    e.key.key = key;
    e.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
    e.key.mod = mods;
    e.key.down = true;
    app_.handleEvent(e);
    e.type = SDL_EVENT_KEY_UP;
    e.key.down = false;
    e.key.mod = held_;
    app_.handleEvent(e);
}

bool Demo::loadScript(const std::string& path, std::string& error) {
    std::ifstream f(std::filesystem::path(std::u8string(path.begin(), path.end())));
    if (!f) {
        error = "Cannot read " + path;
        return false;
    }
    testing_ = true;
    scriptName_ = std::filesystem::path(std::u8string(path.begin(), path.end())).filename().string();
    {
        std::error_code ec;
        const auto dir = std::filesystem::absolute(std::filesystem::path(std::u8string(path.begin(), path.end())), ec).parent_path().u8string();
        scriptDir_.assign(dir.begin(), dir.end());
    }
    shotDir_ = app_.options_.uiOutDir;
    if (!shotDir_.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(std::u8string(shotDir_.begin(), shotDir_.end())), ec);
    }
    mouse_ = ImVec2(float(app_.options_.width) * 0.5f, float(app_.options_.height) * 0.5f);
    wait(0.2f); // first frames: windows find their places
    std::string line;
    int n = 0;
    while (std::getline(f, line)) {
        ++n;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto words = splitWords(line);
        if (words.empty()) continue;
        lineText_ = line;
        std::string err;
        if (!parseStep(n, words, err)) {
            error = scriptName_ + ":" + std::to_string(n) + ": " + err;
            return false;
        }
    }
    wait(0.1f);
    return true;
}

} // namespace sf
