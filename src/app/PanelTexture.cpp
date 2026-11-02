// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Texture Map sub-palette (Tool palette): texture painting on UVs (core/Texture.h). Auto UV unwraps the
// SubTool on a worker thread (core/AutoUv.h, xatlas); New gives it a 1K-8K texture, after which the Paint
// brush, Rgb sculpting, fills and the Paint Layers palette work on the texture (the Vertex / Texture toggle
// switches back). Polypaint <-> Texture convert, Import / Export load and save PNGs, Show UVs draws the
// texture with the UV layout over it. Everything but Auto UV and the file dialogs goes through commands,
// so it is undoable and scriptable.
#include "app/App.h"
#include "app/Demo.h"
#include "app/Ui.h"

#include "core/Polypaint.h"
#include "core/TextureTransfer.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <thread>
#include <utility>
#include <unordered_map>

namespace sf {

namespace {

constexpr const char* kPath = "Tool > Texture Map";
constexpr int kSizes[] = {1024, 2048, 4096, 8192};
constexpr const char* kSizeLabels[] = {"1K", "2K", "4K", "8K"};

// File dialogs answer on another thread: they leave the chosen path here for pollAutoUv.
enum TextureFile { kExportTexture, kImportTexture };
struct TextureDialog {
    App* app = nullptr;
    TextureFile kind = kExportTexture;
    uint32_t subtool = 0;
    bool owned = false;
};
TextureDialog g_dialogs[2];
std::mutex g_fileMutex;
struct TextureFileResult { TextureFile kind; std::string path; uint32_t subtool; };
std::vector<TextureFileResult> g_files;

const SDL_DialogFileFilter kPngFilter[] = {{"PNG image", "png"}};
const SDL_DialogFileFilter kImageFilters[] = {{"Images", "png;jpg;jpeg;tga;bmp;psd"}, {"All files", "*"}};

void SDLCALL onTextureFile(void* user, const char* const* files, int) {
    wakeMainLoop(); // a file dialog answered (maybe on another thread): redraw now
    auto* d = static_cast<TextureDialog*>(user);
    if (files && files[0]) {
        std::string path = files[0];
        if (d->kind == kExportTexture && std::filesystem::path(std::u8string(path.begin(), path.end())).extension().empty())
            path += ".png";
        std::lock_guard lock(g_fileMutex);
        g_files.push_back({d->kind, std::move(path), d->subtool});
    }
    if (d->owned) delete d;
}

// Line segments of the UV layout in 0..1: every triangle edge on small meshes, only the UV island borders
// (edges whose two sides have different UVs, or no other side) on big ones.
std::vector<glm::vec2> uvLayoutLines(const Mesh& m) {
    std::vector<glm::vec2> out;
    const size_t nt = m.triangleCount();
    if (m.uvs.size() != nt * 3) return out;
    if (nt <= 20000) {
        out.reserve(nt * 6);
        for (size_t t = 0; t < nt; ++t)
            for (int e = 0; e < 3; ++e) out.insert(out.end(), {m.uvs[t * 3 + size_t(e)], m.uvs[t * 3 + size_t(e + 1) % 3]});
        return out;
    }
    std::unordered_map<uint64_t, uint32_t> edges; // (a, b) with a < b -> corner index of the edge's start
    edges.reserve(nt * 2);
    auto key = [](uint32_t a, uint32_t b) { return a < b ? (uint64_t(a) << 32) | b : (uint64_t(b) << 32) | a; };
    std::vector<uint8_t> matched(nt * 3, 0);
    for (uint32_t t = 0; t < nt; ++t)
        for (int e = 0; e < 3; ++e) {
            const uint32_t a = m.triangles[t][e], b = m.triangles[t][(e + 1) % 3];
            const uint32_t corner = t * 3 + uint32_t(e);
            auto [it, inserted] = edges.try_emplace(key(a, b), corner);
            if (inserted) continue;
            // The twin: same UVs at both ends means the edge is inside an island.
            const uint32_t o = it->second, ot = o / 3, oe = o % 3;
            const glm::vec2 oa = m.uvs[o], ob = m.uvs[ot * 3 + (oe + 1) % 3];
            const glm::vec2 ua = m.uvs[corner], ub = m.uvs[t * 3 + uint32_t(e + 1) % 3];
            const bool same = m.triangles[ot][oe] == a ? (oa == ua && ob == ub) : (oa == ub && ob == ua);
            if (same) matched[o] = matched[corner] = 1;
        }
    for (uint32_t c = 0; c < nt * 3; ++c)
        if (!matched[c]) out.insert(out.end(), {m.uvs[c], m.uvs[(c / 3) * 3 + (c % 3 + 1) % 3]});
    return out;
}

} // namespace

void App::registerTextureUiActions() {
    auto add = [this](const char* id, const char* label, const char* palette, std::function<void()> run,
                      std::function<bool()> checked, std::function<bool()> enabled, const char* description,
                      const char* command = "") {
        Action a;
        a.id = id;
        a.label = label;
        a.palette = palette;
        a.run = std::move(run);
        a.checked = std::move(checked);
        a.enabled = std::move(enabled);
        a.description = description;
        a.command = command;
        actions_.add(std::move(a));
    };
    auto textured = [this] { return hasTexture(doc_.mesh); };
    add("draw.paint_vertex", "Vertex", "Draw", [this] { runCommand("paint_target", {{"target", std::string("vertex")}}); },
        [this] { return !paintsTexture(doc_.mesh); }, textured,
        "Paint into the vertices (vertex paint): the colour resolution is the mesh's. Shown when the Part has a "
        "texture (Tool > Texture Map).");
    add("draw.paint_texture", "Texture", "Draw", [this] { runCommand("paint_target", {{"target", std::string("texture")}}); },
        [this] { return paintsTexture(doc_.mesh); }, textured,
        "Paint into the Part's texture: the colour resolution comes from the texture, not the mesh. The Paint "
        "brush, Rgb sculpting, fills and Paint Layers all work on it.");
    // Shown on the top shelf and in the Color palette while painting a SubTool that has no texture yet.
    add("draw.new_texture", "New Texture", "Draw", [this] { startNewTexture(); }, nullptr,
        [this] { return !autoUv_ && doc_.mesh.triangleCount() > 0; },
        "Start texture painting: give the Part a texture (the size chosen in Tool > Texture Map, 2K by default) "
        "so paint goes into texels instead of vertices, and the detail comes from the texture, not the mesh. "
        "Without UVs, Auto UV unwraps the model first (in the background, with its progress in the status bar).",
        "texture_create");
    add("view.texture", "Texture", "Render", [this] { renderSettings_.showTexture = !renderSettings_.showTexture; },
        [this] { return renderSettings_.showTexture; }, nullptr,
        "Show the painted texture on the model (multiplied over the material, like vertex paint).");
    add("texture.auto_uv", "Auto UV", kPath, [this] { startAutoUv(); }, nullptr,
        [this] { return !autoUv_ && doc_.mesh.triangleCount() > 0; },
        "Unwrap the Part automatically: cut it into UV islands and pack them, so any sculpt can carry a texture. "
        "Replaces existing UVs (undoable). Runs in the background; big meshes can take a minute or more.",
        "uv_auto");
    add("texture.new", "New Texture", kPath,
        [this] { runCommand("texture_create", {{"size", std::to_string(kSizes[std::clamp(textureSizeChoice_, 0, 3)])}}); },
        nullptr, [this] { return doc_.mesh.hasUvs(); },
        "Give the Part a texture of the chosen size to paint into (white). Needs UVs: Auto UV makes them. "
        "Replaces an existing texture (undoable).",
        "texture_create");
    add("texture.show_uvs", "Show UVs", kPath, [this] { showUvView_ = !showUvView_; }, [this] { return showUvView_; },
        [this] { return doc_.mesh.hasUvs(); }, "Show the texture with the UV layout drawn over it, below these buttons.");
    add("texture.from_polypaint", "Vertex Paint > Texture", kPath, [this] { runCommand("texture_from_polypaint"); }, nullptr,
        [this] { return doc_.mesh.hasUvs() && doc_.mesh.hasColors(); },
        "Bake the vertex paint (all paint layers) into the active texture layer. Makes a 2K texture if there is none.",
        "texture_from_polypaint");
    add("texture.to_polypaint", "Texture > Vertex Paint", kPath, [this] { runCommand("polypaint_from_texture"); }, nullptr,
        textured,
        "Copy the texture into the vertex paint of every vertex (detail finer than the mesh is lost).",
        "polypaint_from_texture");
    add("texture.export", "Export Texture...", kPath,
        [this] { SDL_ShowSaveFileDialog(onTextureFile, &g_dialogs[kExportTexture], window_, kPngFilter, 1, nullptr); },
        nullptr, textured, "Save the texture (every visible layer composited) as a PNG image.", "texture_export");
    add("texture.import", "Import Texture...", kPath,
        [this] { SDL_ShowOpenFileDialog(onTextureFile, &g_dialogs[kImportTexture], window_, kImageFilters, 2, nullptr, false); },
        nullptr, [this] { return doc_.mesh.hasUvs(); },
        "Load an image as the Part's texture (replacing its layers; undoable).", "texture_import");
    add("texture.delete", "Delete Texture", kPath, [this] { runCommand("texture_delete"); }, nullptr,
        [this] { return doc_.mesh.texture != nullptr; },
        "Remove the Part's texture (undoable). Painting goes back to the vertices.", "texture_delete");
    add("geometry.keep_texture", "Keep Texture", "Tool > Remesh", [this] { keepTexture_ = !keepTexture_; },
        [this] { return keepTexture_; }, nullptr,
        "Voxel Remesh and Quad Remesh keep a painted texture: the new mesh gets Auto UVs and the texture (every layer) "
        "is projected onto it from the old surface, in the same undo step. Off: the texture is dropped. Merging "
        "Parts keeps textures too. Shown under Remesh and Quad Remesh when the Part has a texture.",
        "");
    for (int i = 0; i < 2; ++i) g_dialogs[i] = {this, TextureFile(i)};
}

void App::drawKeepTextureCheckbox(const char* id, const char* path) {
    ImGui::Checkbox(id, &keepTexture_);
    ui::HelpText("Keep Texture", nullptr,
                 "Keep the painted texture: the new mesh gets Auto UVs and the texture (every layer) is projected onto "
                 "it from the old surface, in the same undo step (it adds the Auto UV time). Off: the texture is "
                 "dropped. Scripts: keep_texture=false.",
                 path);
}

void App::startNewTexture() {
    if (autoUv_ || doc_.mesh.triangleCount() == 0) return;
    if (!doc_.mesh.hasUvs()) {
        startAutoUv();
        textureAfterAutoUv_ = autoUv_ != nullptr; // pollAutoUv makes the texture when the UVs are in
        return;
    }
    runCommand("texture_create", {{"size", std::to_string(kSizes[std::clamp(textureSizeChoice_, 0, 3)])}});
}

void App::startAutoUv() {
    if (autoUv_ || doc_.mesh.triangleCount() == 0) return;
    auto job = std::make_unique<AutoUvJob>();
    job->progress = std::make_shared<std::atomic<float>>(0.0f);
    job->cancel = std::make_shared<std::atomic<bool>>(false);
    job->result = std::make_shared<AutoUvResult>();
    job->error = std::make_shared<std::string>();
    job->subtool = doc_.activeSubtoolInfo().id;
    job->triangles = doc_.mesh.triangleCount();
    const int res = doc_.mesh.texture ? std::max(doc_.mesh.texture->width, doc_.mesh.texture->height)
                                      : kSizes[std::clamp(textureSizeChoice_, 0, 3)];
    std::promise<bool> promise;
    job->done = promise.get_future();
    std::thread([promise = std::move(promise), positions = doc_.mesh.positions, triangles = doc_.mesh.triangles, res,
                 progress = job->progress, cancel = job->cancel, result = job->result, error = job->error]() mutable {
        const bool ok = computeAutoUv(positions, triangles, res, *result, *error, [&](float f) {
            progress->store(f);
            return !cancel->load();
        });
        promise.set_value(ok);
    }).detach();
    autoUv_ = std::move(job);
    status_ = "Auto UV: unwrapping " + std::to_string(doc_.mesh.triangleCount()) + " triangles...";
}

void App::pollAutoUv() {
    std::vector<TextureFileResult> files;
    {
        std::lock_guard lock(g_fileMutex);
        files.swap(g_files);
    }
    for (const auto& [kind, path, subtool] : files) {
        if (subtool) {
            int index = -1;
            for (int i = 0; i < doc_.subtoolCount(); ++i)
                if (doc_.subtools[size_t(i)].id == subtool) index = i;
            if (index < 0) { status_ = "Texture import skipped: the target SubTool was deleted"; continue; }
            if (index != doc_.activeSubtool) runCommand("subtool_select", {{"index", double(index)}});
        }
        runCommand(kind == kExportTexture ? "texture_export" : "texture_import", {{"path", path}});
        if (kind == kImportTexture && lastCommandOk_) {
            doc_.mesh.previewTexture.reset();
            doc_.mesh.previewUvs.clear();
            if (doc_.activeSubtoolInfo().crkwMetaHuman.isObject())
                doc_.activeSubtoolInfo().crkwMetaHuman.set("preview", "OFF");
            renderSettings_.showTexture = true;
            renderSettings_.showPolypaint = true;
        }
    }

    if (!autoUv_) return;
    if (autoUv_->done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        status_ = "Auto UV: " + std::to_string(int(autoUv_->progress->load() * 100.0f)) + "%";
        return;
    }
    const std::unique_ptr<AutoUvJob> job = std::move(autoUv_);
    const bool makeTexture = std::exchange(textureAfterAutoUv_, false);
    if (!job->done.get()) {
        status_ = *job->error;
        return;
    }
    // The SubTool it unwrapped may be inactive now, or changed topology meanwhile.
    int index = -1;
    for (int i = 0; i < doc_.subtoolCount(); ++i)
        if (doc_.subtools[size_t(i)].id == job->subtool) index = i;
    if (index < 0 || doc_.subtoolMesh(index).triangleCount() != job->triangles) {
        status_ = "Auto UV skipped: the mesh changed while it was being unwrapped";
        return;
    }
    if (index != doc_.activeSubtool) doc_.selectSubtool(index);
    if (doc_.sculptor.strokeActive()) endStroke();
    Mesh m = doc_.mesh;
    // A painted texture and its layers move onto the new UVs, in the same undo step (core/TextureTransfer.h).
    const std::string note = setUvsKeepingTexture(m, std::move(job->result->uvs));
    doc_.replaceMeshUndoable(std::move(m), "Auto UV");
    uvLinesKey_ = 0;
    updateTitle();
    const std::string islands = "Auto UV: " + std::to_string(job->result->charts) + " UV islands";
    if (makeTexture && !hasTexture(doc_.mesh)) {
        runCommand("texture_create", {{"size", std::to_string(kSizes[std::clamp(textureSizeChoice_, 0, 3)])}});
        if (hasTexture(doc_.mesh)) status_ = islands + ", new " + std::to_string(doc_.mesh.texture->width / 1024) +
                                             "K texture: paint goes into it";
        return;
    }
    status_ = islands + (!note.empty() ? note : hasTexture(doc_.mesh) ? "" : ". Now make a texture: New Texture");
}

void App::pasteTextureImage() {
    if (!doc_.mesh.hasUvs()) {
        status_ = "The SubTool needs UVs before pasting a texture";
        return;
    }
    Image image;
    if (!readClipboardImage(image)) return;
    const auto file = std::filesystem::temp_directory_path() /
        ("sculptforge_texture_" + std::to_string(SDL_GetTicksNS()) + ".png");
    const auto u8 = file.u8string();
    std::string error;
    if (!image.savePng(std::string(u8.begin(), u8.end()), error)) {
        status_ = "Couldn't save the pasted texture: " + error;
        return;
    }
    runCommand("texture_import", {{"path", std::string(u8.begin(), u8.end())}});
    std::error_code ec;
    std::filesystem::remove(file, ec); // texture_import has already read the pixels
    if (!lastCommandOk_) return;
    // A MetaHuman guide would otherwise cover the texture that was just pasted.
    doc_.mesh.previewTexture.reset();
    doc_.mesh.previewUvs.clear();
    if (doc_.activeSubtoolInfo().crkwMetaHuman.isObject())
        doc_.activeSubtoolInfo().crkwMetaHuman.set("preview", "OFF");
    renderSettings_.showTexture = true;
    renderSettings_.showPolypaint = true;
    status_ = "Pasted texture " + std::to_string(image.width) + "x" + std::to_string(image.height) + " (Ctrl+Z to undo)";
}

void App::clearTextureManagerPreviews() {
    for (auto& preview : textureManagerPreviews_)
        if (preview.texture) gl::DeleteTextures(1, &preview.texture);
    textureManagerPreviews_.clear();
}

void App::drawTextureManager() {
    if (!showTextureManager_) {
        clearTextureManagerPreviews();
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(480 * uiScale_, 620 * uiScale_), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(340 * uiScale_, 300 * uiScale_), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::Begin("CRKW Texture Manager", &showTextureManager_)) { ImGui::End(); return; }
    ImGui::TextWrapped("Hover a square and press Ctrl+V, or use Paste / Swap image. Each card controls one SubTool.");
    ImGui::TextDisabled("Replaces texture layers. Ctrl+Z undoes the selected SubTool.");
    ImGui::Separator();

    // Separate small GPU previews work for inactive/hidden SubTools and don't depend on scene uploads.
    std::erase_if(textureManagerPreviews_, [&](TextureManagerPreview& preview) {
        const bool removed = std::none_of(doc_.subtools.begin(), doc_.subtools.end(),
            [&](const SubTool& subtool) { return subtool.id == preview.subtool; });
        if (removed && preview.texture) gl::DeleteTextures(1, &preview.texture);
        return removed;
    });
    int paste = -1, browse = -1, select = -1;
    const int columns = ImGui::GetContentRegionAvail().x >= 400 * uiScale_ ? 2 : 1;
    if (ImGui::BeginTable("##texturecards", columns, ImGuiTableFlags_SizingStretchSame)) {
        for (int i = 0; i < doc_.subtoolCount(); ++i) {
            ImGui::TableNextColumn();
            const auto& subtool = doc_.subtools[size_t(i)];
            const Mesh& mesh = doc_.subtoolMesh(i);
            const auto& source = mesh.previewTexture ? mesh.previewTexture : mesh.texture;
            const std::string suffix = std::to_string(i);
            if (ImGui::Selectable((subtool.name + "##texture.select." + suffix).c_str(), i == doc_.activeSubtool)) select = i;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s%s", subtool.name.c_str(), subtool.visible ? "" : " (hidden)");
            if (source)
                ImGui::TextDisabled("%s %dx%d", mesh.previewTexture ? "Guide" : "Texture", source->width, source->height);
            else ImGui::TextDisabled("%s", mesh.hasUvs() ? "No texture" : "No UVs");

            const float side = ImGui::GetContentRegionAvail().x;
            const ImVec2 p = ImGui::GetCursorScreenPos(), end(p.x + side, p.y + side);
            const bool visible = ImGui::IsRectVisible(ImVec2(side, side));
            const bool clicked = ImGui::InvisibleButton(("##texture.card." + suffix).c_str(), ImVec2(side, side));
            const bool hovered = ImGui::IsItemHovered();
            if (hovered) {
                imagePasteHoverFrame_ = ImGui::GetFrameCount();
                ImGui::SetTooltip("%s: Ctrl+V to paste, click to swap image", subtool.name.c_str());
                if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) paste = i;
            }
            if (clicked) browse = i;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, end, ImGui::GetColorU32(ImGuiCol_FrameBg), 4 * uiScale_);
            if (source && visible && source->width > 0 && source->height > 0 && !source->pixels.empty()) {
                auto found = std::find_if(textureManagerPreviews_.begin(), textureManagerPreviews_.end(),
                    [&](const TextureManagerPreview& preview) { return preview.subtool == subtool.id; });
                if (found == textureManagerPreviews_.end()) {
                    textureManagerPreviews_.push_back({subtool.id});
                    found = std::prev(textureManagerPreviews_.end());
                }
                auto& preview = *found;
                if (!preview.texture || preview.serial != source->serial || preview.revision != source->revision) {
                    const float factor = std::min(1.0f, 256.0f / float(std::max(source->width, source->height)));
                    const int w = std::max(1, int(source->width * factor)), h = std::max(1, int(source->height * factor));
                    std::vector<uint8_t> pixels(size_t(w) * h * 4);
                    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
                        const size_t from = (size_t(y * source->height / h) * source->width + x * source->width / w) * 4;
                        std::copy_n(source->pixels.data() + from, 4, pixels.data() + (size_t(y) * w + x) * 4);
                    }
                    if (!preview.texture) gl::GenTextures(1, &preview.texture);
                    gl::BindTexture(GL_TEXTURE_2D, preview.texture);
                    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
                    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_RGBA8), w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_LINEAR));
                    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(GL_LINEAR));
                    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(GL_CLAMP_TO_EDGE));
                    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLint(GL_CLAMP_TO_EDGE));
                    gl::BindTexture(GL_TEXTURE_2D, 0);
                    preview.serial = source->serial; preview.revision = source->revision;
                }
                const float aspect = float(source->width) / float(source->height);
                const float h = std::min(side, side / aspect), w = h * aspect;
                const ImVec2 start(p.x + (side - w) * .5f, p.y + (side - h) * .5f);
                dl->AddImage(ImTextureID(uintptr_t(preview.texture)), start, ImVec2(start.x + w, start.y + h), ImVec2(0, 1), ImVec2(1, 0));
            } else if (!source) {
                const char* text = "Paste texture";
                const ImVec2 size = ImGui::CalcTextSize(text);
                dl->AddText(ImVec2(p.x + (side - size.x) * .5f, p.y + (side - size.y) * .5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
            }
            dl->AddRect(p, end, ImGui::GetColorU32(hovered || i == doc_.activeSubtool ? ImGuiCol_ButtonHovered : ImGuiCol_Border), 4 * uiScale_, 0, 2 * uiScale_);
            ImGui::BeginDisabled(!mesh.hasUvs());
            const float half = (side - ImGui::GetStyle().ItemSpacing.x) * .5f;
            if (ImGui::Button(("Paste##texture.paste." + suffix).c_str(), ImVec2(half, 0))) paste = i;
            ImGui::SameLine();
            if (ImGui::Button(("Swap image...##texture.swap." + suffix).c_str(), ImVec2(-1, 0))) browse = i;
            ImGui::EndDisabled();
            ImGui::Spacing();
        }
        ImGui::EndTable();
    }
    // Defer selection until every card is drawn: switching SubTools moves their meshes.
    const int target = paste >= 0 ? paste : browse >= 0 ? browse : select;
    if (target >= 0) {
        if (target != doc_.activeSubtool) runCommand("subtool_select", {{"index", double(target)}});
        if (paste >= 0) pasteTextureImage();
        if (browse >= 0) {
            if (!doc_.mesh.hasUvs()) status_ = "The SubTool needs UVs before importing a texture";
            else SDL_ShowOpenFileDialog(onTextureFile,
                new TextureDialog{this, kImportTexture, doc_.activeSubtoolInfo().id, true}, window_, kImageFilters, 2, nullptr, false);
        }
    }
    ImGui::Separator();
    ImGui::TextWrapped("%s", status_.c_str());
    ImGui::End();
}

void App::drawTextureMapContents() {
    const Mesh& m = doc_.mesh;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Texture Manager##crkw.texture_manager", ImVec2(-1, 0))) showTextureManager_ = true;

    // Status line: UVs, texture size, where paint goes.
    if (autoUv_) {
        ImGui::ProgressBar(autoUv_->progress->load(), ImVec2(-1, 0), "Unwrapping...");
        ui::HelpText("Auto UV", nullptr, "Unwrapping the Part in the background; you can keep working.", kPath);
    } else if (!m.hasUvs()) {
        ImGui::TextDisabled("No UVs: Auto UV makes them");
    } else if (!hasTexture(m)) {
        ImGui::TextDisabled("UVs, no texture yet");
    } else {
        const PaintTexture& t = *m.texture;
        ImGui::TextDisabled("%dx%d texture, %d layer%s", t.width, t.height, int(t.layers.size()) + 1, t.layers.empty() ? "" : "s");
    }

    // CRKW: the same hover + Ctrl+V interaction as Generate 3D's image box.
    const float width = ImGui::GetContentRegionAvail().x, height = 100.0f * uiScale_;
    const ImVec2 p = ImGui::GetCursorScreenPos(), end(p.x + width, p.y + height);
    const bool clicked = ImGui::InvisibleButton("##texturepaste", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) imagePasteHoverFrame_ = ImGui::GetFrameCount();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, end, ImGui::GetColorU32(ImGuiCol_FrameBg), 4 * uiScale_);
    const GLuint texture = hasTexture(m) && !m.previewTexture ? renderer_.paintTexture(doc_.activeSubtoolInfo().id) : 0;
    if (texture) {
        const float aspect = float(m.texture->width) / float(m.texture->height);
        const float h = std::min(height, width / aspect), w = h * aspect;
        const ImVec2 start(p.x + (width - w) * .5f, p.y + (height - h) * .5f);
        dl->AddImage(ImTextureID(uintptr_t(texture)), start, ImVec2(start.x + w, start.y + h), ImVec2(0, 1), ImVec2(1, 0));
    } else {
        const char* text = "Paste a texture here";
        const ImVec2 size = ImGui::CalcTextSize(text);
        dl->AddText(ImVec2(p.x + (width - size.x) * .5f, p.y + (height - size.y) * .5f), ImGui::GetColorU32(ImGuiCol_Text), text);
    }
    dl->AddRect(p, end, ImGui::GetColorU32(hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Border), 4 * uiScale_);
    if (hovered) {
        ImGui::SetTooltip("Hover + Ctrl+V: paste texture. Click: import image. Replaces texture layers; Ctrl+Z undoes it.");
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) pasteTextureImage();
    }
    if (clicked && m.hasUvs())
        SDL_ShowOpenFileDialog(onTextureFile, &g_dialogs[kImportTexture], window_, kImageFilters, 2, nullptr, false);
    ImGui::TextDisabled("Hover + Ctrl+V, or click to import");
    if (m.previewTexture) ImGui::TextDisabled("MetaHuman guide preview is on");

    ui::ActionButton(*this, "texture.auto_uv", ImVec2(half, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "texture.show_uvs", ImVec2(-1, 0));
    ui::ActionButton(*this, "ui.uv_editor", ImVec2(-1, 0), ui::Look::IconLabel);

    ImGui::SetNextItemWidth(half);
    ImGui::Combo("##texsize", &textureSizeChoice_, kSizeLabels, 4);
    ui::HelpText("Texture size", nullptr,
                 "Size of a new texture: 1K, 2K (the default), 4K or 8K texels square. Bigger holds finer paint and uses "
                 "more memory (8K: 256 MB per layer).",
                 kPath);
    ImGui::SameLine();
    ui::ActionButton(*this, "texture.new", ImVec2(-1, 0));

    if (hasTexture(m)) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Paint into");
        ImGui::SameLine();
        ui::Segmented(*this, {"draw.paint_vertex", "draw.paint_texture"});
    }
    ui::ActionButton(*this, "texture.from_polypaint", ImVec2(half, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "texture.to_polypaint", ImVec2(-1, 0));
    ui::ActionButton(*this, "texture.import", ImVec2(half, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "texture.export", ImVec2(-1, 0));
    ui::ActionButton(*this, "texture.delete", ImVec2(half, 0));
    if (hasTexture(m)) {
        ImGui::SameLine();
        ImGui::Checkbox("Show texture", &renderSettings_.showTexture);
        ui::HelpAction(*this, "view.texture");
    }

    // UV view: the texture (or a dark square) with the UV layout over it, v = 0 at the bottom.
    if (showUvView_ && m.hasUvs()) {
        const float side = ImGui::GetContentRegionAvail().x;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const GLuint tex = hasTexture(m) ? renderer_.paintTexture(doc_.activeSubtoolInfo().id) : 0;
        if (tex) dl->AddImage(ImTextureID(uintptr_t(tex)), p, ImVec2(p.x + side, p.y + side), ImVec2(0, 1), ImVec2(1, 0));
        else dl->AddRectFilled(p, ImVec2(p.x + side, p.y + side), IM_COL32(40, 40, 46, 255));
        const uint64_t key = (uint64_t(doc_.activeSubtoolInfo().id) << 40) ^ uint64_t(uintptr_t(m.uvs.data())) ^ m.uvs.size();
        if (key != uvLinesKey_) {
            uvLines_ = uvLayoutLines(m);
            uvLinesKey_ = key;
        }
        const size_t step = std::max<size_t>(1, uvLines_.size() / 2 / 150000) * 2; // cap what one frame draws
        const ImU32 col = tex ? IM_COL32(20, 20, 30, 150) : IM_COL32(150, 200, 255, 150);
        auto at = [&](const glm::vec2& uv) { return ImVec2(p.x + uv.x * side, p.y + (1.0f - uv.y) * side); };
        dl->PushClipRect(p, ImVec2(p.x + side, p.y + side), true);
        for (size_t i = 0; i + 1 < uvLines_.size(); i += step) dl->AddLine(at(uvLines_[i]), at(uvLines_[i + 1]), col, 1.0f);
        dl->PopClipRect();
        dl->AddRect(p, ImVec2(p.x + side, p.y + side), IM_COL32(90, 90, 100, 255));
        ImGui::Dummy(ImVec2(side, side));
        ui::HelpText("UV view", nullptr,
                     "The texture with the Part's UV layout over it (island borders on big meshes). Paint on the model; "
                     "the texture fills in here.",
                     kPath);
    }
    drawTextureChannelsContents(); // Material Channels: toggles, solo view, Export Maps (PanelPbr.cpp)
}

} // namespace sf
