// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Polypaint & Materials panel: the Paint brush (ZBrush Rgb / Zadd), colour
// picker and swatches, fills, and the viewport MatCap material picker.
#include "app/App.h"
#include "app/Ui.h"

#include "core/Matcap.h"
#include "core/Polypaint.h"

#include <glm/gtc/type_ptr.hpp>

#include <cfloat>
#include <iterator>

namespace sf {

void App::registerPaintUiCommands() {
    // A view setting, so it lives with the app (scripts: --script "set_material name=Chrome").
    std::string names = "studio";
    for (int i = 0; i < matcapCount(); ++i) names += std::string(", ") + matcapName(i);
    commands_.add({"set_material",
                   "Viewport material: studio (default clay lighting) or a built-in MatCap. Polypaint multiplies "
                   "the matcap; cavity shading stays on.",
                   {{"name", ParamSpec::Type::String, "One of: " + names + ".", true}},
                   [this, names](Document&, const Args& a) -> CommandResult {
                       const std::string name = argString(a, "name");
                       if (name == "studio" || name == "Studio") {
                           renderSettings_.matcap = -1;
                           return {true, "Material: Studio"};
                       }
                       const int index = findMatcap(name);
                       if (index < 0) return {false, "Unknown material '" + name + "' (" + names + ")"};
                       renderSettings_.matcap = index;
                       return {true, std::string("Material: ") + matcapName(index)};
                   }});
}

const BrushRecipe* App::strokeRecipe() const {
    return strokeBrush_ == BrushType::Smooth || !strokeSculpt_ || strokeMask_ ? nullptr : activeRecipe();
}

void App::paintAt(const DabParams& p) {
    paintDab(doc_.sculptor, doc_.mesh, doc_.bvh, p, doc_.paintColor, symmetry_);
}

void App::beginPaintStroke() {
    paintSnapshot_.reset();
    const Mesh& m = doc_.mesh;
    if (strokePaint_ && !m.paintLayers.empty() && m.activePaintLayer >= 0 &&
        m.activePaintLayer < int(m.paintLayers.size()) && !paintLayerEditable(m, m.activePaintLayer)) {
        const PaintLayer& l = m.paintLayers[size_t(m.activePaintLayer)];
        status_ = "Paint layer '" + l.name + "' is " + (l.locked ? "locked" : "hidden");
    }
    if (strokePaint_ && !doc_.mesh.hasColors()) paintSnapshot_ = std::make_unique<Mesh>(doc_.mesh);
}

void App::finishPaintStroke(UndoRecord& record) {
    if (!paintSnapshot_) return;
    UndoRecord snapshot;
    snapshot.label = std::move(record.label);
    snapshot.mesh = std::move(paintSnapshot_);
    record = std::move(snapshot);
}

void App::pickColorUnderCursor() {
    doc_.syncBvh();
    RayHit hit;
    if (!pick(mouseX_, mouseY_, hit)) return;
    if (!doc_.mesh.hasColors()) {
        status_ = "The mesh has no polypaint to pick from";
        return;
    }
    doc_.paintColor = sampleColor(doc_.mesh, hit.triangle, hit.position);
    status_ = "Paint colour " + colorToHex(doc_.paintColor);
}

// Color palette: the Paint brush (ZBrush Rgb / Zadd), the colour, swatches and fills.
void App::drawPalette_Color() {
    ImGui::SeparatorText("Paint");
    const bool painting = paintMode_;
    if (painting) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button("Paint brush (P)", ImVec2(-1, 0))) paintMode_ = !paintMode_;
    if (painting) ImGui::PopStyleColor();
    ui::HelpAction(*this, "draw.paint");
    ImGui::Checkbox("RGB while sculpting", &paintRgb_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sculpt brushes also paint the colour (ZBrush Rgb with Zadd)");

    // The picker edits display (sRGB) values; the mesh stores linear colour.
    glm::vec3 srgb = linearToSrgb(doc_.paintColor);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::ColorPicker3("##paintcolor", glm::value_ptr(srgb),
                            ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoSidePreview |
                                ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_DisplayHex))
        doc_.paintColor = srgbToLinear(srgb);
    static const glm::vec3 kSwatches[] = {{1.0f, 1.0f, 1.0f},   {0.1f, 0.1f, 0.1f},   {0.85f, 0.12f, 0.1f},
                                          {0.95f, 0.5f, 0.12f}, {0.95f, 0.85f, 0.2f}, {0.2f, 0.65f, 0.25f},
                                          {0.15f, 0.35f, 0.85f}, {0.5f, 0.25f, 0.7f}, {0.87f, 0.66f, 0.55f},
                                          {0.45f, 0.28f, 0.16f}};
    const float swatch = (ImGui::GetContentRegionAvail().x - 4 * ImGui::GetStyle().ItemSpacing.x) / 5.0f;
    for (int i = 0; i < int(std::size(kSwatches)); ++i) {
        const glm::vec3& c = kSwatches[i];
        ImGui::PushID(i);
        if (ImGui::ColorButton("##swatch", ImVec4(c.x, c.y, c.z, 1.0f), ImGuiColorEditFlags_NoTooltip,
                               ImVec2(swatch, swatch * 0.6f)))
            doc_.paintColor = srgbToLinear(c);
        ImGui::PopID();
        if (i % 5 != 4) ImGui::SameLine();
    }
    ImGui::TextDisabled("C over the model picks its colour");
    {
        const Mesh& m = doc_.mesh;
        const bool layered = !m.paintLayers.empty() && m.activePaintLayer >= 0 && m.activePaintLayer < int(m.paintLayers.size());
        ImGui::TextDisabled("Painting on: %s", layered ? m.paintLayers[size_t(m.activePaintLayer)].name.c_str() : "Base");
        ui::HelpText("Paint layer", nullptr,
                     "The paint layer the Paint brush and fills write into. Add and arrange layers in Tool > Paint Layers.",
                     "Color");
    }

    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    const std::string hex = colorToHex(doc_.paintColor);
    if (ImGui::Button("Fill", ImVec2(half, 0))) runCommand("paint_fill", {{"color", hex}});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fill the unmasked area (the whole mesh if nothing is masked)");
    ImGui::SameLine();
    ImGui::BeginDisabled(!doc_.mesh.hasMask());
    if (ImGui::Button("Fill masked", ImVec2(half, 0)))
        runCommand("paint_fill", {{"color", hex}, {"area", std::string("masked")}});
    ImGui::EndDisabled();
    if (ImGui::Button("Cavity", ImVec2(half, 0))) runCommand("paint_from_cavity");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Darken creases and cavities (respects the mask)");
    ImGui::SameLine();
    ImGui::BeginDisabled(!doc_.mesh.hasColors());
    if (ImGui::Button("Clear", ImVec2(half, 0))) runCommand("paint_clear");
    ImGui::EndDisabled();
    ui::HelpText("Clear", nullptr, "Remove all polypaint from the SubTool, paint layers included (undoable).", "Color");
    ImGui::Checkbox("Show polypaint", &renderSettings_.showPolypaint);
}

// Material palette: the material grid and the studio material's colour.
void App::drawPalette_Material() {
    sf::drawMaterialGrid(*this, GridMode::Palette); // Pickers.cpp, as in the material picker (M)
    ImGui::ColorEdit3("Base colour", &renderSettings_.baseColor.x, ImGuiColorEditFlags_NoInputs);
    ui::HelpText("Base colour", nullptr, "Colour of the Studio material (the clay colour).", "Material");
}

} // namespace sf
