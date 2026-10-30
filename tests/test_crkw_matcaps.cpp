// SPDX-License-Identifier: GPL-3.0-or-later
#include "TestHarness.h"
#include "core/Image.h"
#include "core/Matcap.h"
#include <cstdlib>
#include <algorithm>
#include <map>

using namespace sf;

TEST(crkw_matcaps_are_listed_and_toon_is_banded) {
    for (const char* name : {"Gray", "Blue Gray", "Basic", "Terracotta", "Plasticine", "Sculpey", "Toon 2-Tone",
                             "Toon 3-Tone", "Toon Ink", "Gloss Plastic", "Jade", "Gold", "Gunmetal"})
        CHECK(findMatcap(name) > 6); // after the upstream seven, so saved indices keep their material
    // v0.9.4 adds Soft Clay: keep all existing CRKW project material indices stable.
    int savedIndex = 7;
    for (const char* name : {"Gray", "Blue Gray", "Basic", "Terracotta", "Plasticine", "Sculpey", "Toon 2-Tone",
                             "Toon 3-Tone", "Toon Ink", "Gloss Plastic", "Jade", "Gold", "Gunmetal"})
        CHECK(findMatcap(name) == savedIndex++);
    CHECK(defaultMatcap() == savedIndex);
    const int size = 128;
    // Share of the sphere's face covered by its `n` most common colours.
    auto flatness = [&](const char* name, size_t n) {
        const std::vector<uint8_t> img = generateMatcap(findMatcap(name), size);
        std::map<uint32_t, int> count;
        int total = 0;
        for (int j = 0; j < size; ++j)
            for (int i = 0; i < size; ++i) {
                const float x = (i + .5f) / size * 2 - 1, y = (j + .5f) / size * 2 - 1;
                if (x * x + y * y > 0.6f) continue; // the face of the sphere, away from the rim
                const uint8_t* p = img.data() + (size_t(j) * size + i) * 4;
                ++count[uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2]];
                ++total;
            }
        std::vector<int> sizes;
        for (const auto& [colour, c] : count) sizes.push_back(c);
        std::sort(sizes.rbegin(), sizes.rend());
        int top = 0;
        for (size_t k = 0; k < std::min(n, sizes.size()); ++k) top += sizes[k];
        return float(top) / float(total);
    };
    // Toon reads as a few flat tones (plus thin soft steps and the highlight); clay is a smooth ramp.
    CHECK(flatness("Toon 2-Tone", 3) > 0.8f);
    CHECK(flatness("Toon 3-Tone", 4) > 0.8f);
    CHECK(flatness("Plasticine", 3) < 0.2f);
    // Ink darkens the silhouette.
    const std::vector<uint8_t> ink = generateMatcap(findMatcap("Toon Ink"), size);
    const uint8_t* rim = ink.data() + (size_t(size / 2) * size + 1) * 4;
    CHECK(rim[0] < 40 && rim[1] < 40 && rim[2] < 40);
}

// SF_MATCAP_SHEET=<file.png> writes every material side by side, for looking at them.
TEST(crkw_matcap_sheet) {
    const char* path = std::getenv("SF_MATCAP_SHEET");
    if (!path) return;
    const int cell = 160, cols = 5, rows = (matcapCount() + cols - 1) / cols;
    Image sheet;
    sheet.width = cols * cell;
    sheet.height = rows * cell;
    sheet.rgba.assign(size_t(sheet.width) * sheet.height * 4, 110);
    for (size_t k = 3; k < sheet.rgba.size(); k += 4) sheet.rgba[k] = 255;
    for (int m = 0; m < matcapCount(); ++m) {
        const std::vector<uint8_t> img = generateMatcap(m, cell);
        const int ox = (m % cols) * cell, oy = (m / cols) * cell;
        for (int j = 0; j < cell; ++j)
            for (int i = 0; i < cell; ++i) {
                const float x = (i + .5f) / cell * 2 - 1, y = (j + .5f) / cell * 2 - 1;
                if (x * x + y * y > 0.96f) continue;
                const uint8_t* s = img.data() + (size_t(j) * cell + i) * 4; // rows bottom-up
                uint8_t* d = sheet.rgba.data() + (size_t(oy + cell - 1 - j) * sheet.width + ox + i) * 4;
                for (int c = 0; c < 4; ++c) d[c] = s[c];
            }
        std::printf("  %2d %s\n", m, matcapName(m));
    }
    std::string error;
    CHECK(sheet.savePng(path, error));
}
