// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The user guide's Markdown subset, table of contents and search (src/core/Guide.cpp).
#include "TestHarness.h"

#include "core/Guide.h"

#include <map>

using namespace sf;
using namespace sf::guide;

TEST(guide_inline_formatting) {
    const Spans s = parseInline("Press <kbd>Ctrl</kbd>+<kbd>D</kbd> to **subdivide** the *whole* `mesh`.");
    CHECK(plainText(s) == "Press Ctrl+D to subdivide the whole mesh.");
    CHECK(s.size() == 11);
    CHECK(s[1].kbd && s[1].text == "Ctrl");
    CHECK(s[3].kbd && s[3].text == "D");
    CHECK(s[5].bold && s[5].text == "subdivide");
    CHECK(s[7].italic && s[7].text == "whole");
    CHECK(s.size() == 11 && s[9].code && s[9].text == "mesh" && s[10].text == ".");
    const Spans code = parseInline("run `remesh resolution=200` now");
    CHECK(code.size() == 3 && code[1].code && code[1].text == "remesh resolution=200");
    // A lone asterisk with spaces around it is not emphasis; escapes and entities are literal.
    CHECK(plainText(parseInline("5 * 3 \\*not\\* &lt;b&gt; &amp;")) == "5 * 3 *not* <b> &");
    CHECK(!parseInline("5 * 3")[0].italic);
    // Underscores never format (environment variables, commands).
    CHECK(plainText(parseInline("SCULPTFORGE_BRUSH_PATH and mask_by_cavity")) == "SCULPTFORGE_BRUSH_PATH and mask_by_cavity");
}

TEST(guide_links) {
    const Spans s = parseInline("See [the **Masking** chapter](masking.md#mask-by-cavity) or <https://example.org>.");
    std::vector<std::string> links;
    for (const auto& sp : s)
        if (!sp.link.empty()) links.push_back(sp.link);
    CHECK(links.size() == 4); // "the ", "Masking" (bold), " chapter" share one target; then the autolink
    CHECK(links.size() == 4 && links[3] == "https://example.org");
    CHECK(links[0] == "masking.md#mask-by-cavity" && links[2] == "masking.md#mask-by-cavity");
    CHECK(plainText(s) == "See the Masking chapter or https://example.org.");
    bool boldLink = false;
    for (const auto& sp : s) boldLink |= sp.bold && sp.link == "masking.md#mask-by-cavity";
    CHECK(boldLink);
    // Brackets that aren't links stay as text; code inside link text works.
    CHECK(plainText(parseInline("an [x] box")) == "an [x] box");
    const Spans c = parseInline("[`brush_save`](scripting.md)");
    CHECK(c.size() == 1 && c[0].code && c[0].link == "scripting.md");

    const Target t = splitTarget("masking.md#mask-by-cavity");
    CHECK(t.file == "masking.md" && t.anchor == "mask-by-cavity" && !t.external);
    CHECK(splitTarget("#top").file.empty() && splitTarget("#top").anchor == "top");
    CHECK(splitTarget("https://github.com/x#y").external);
}

TEST(guide_slugs) {
    CHECK(slugify("Mask by cavity") == "mask-by-cavity");
    CHECK(slugify("Subdivide, remesh & decimate") == "subdivide-remesh--decimate"); // GitHub keeps both dashes
    CHECK(slugify("Send to Unreal (Ctrl+U)") == "send-to-unreal-ctrlu");
    CHECK(slugify("GoZ / ZBrush") == "goz--zbrush");
    CHECK(slugify("Image & text to 3D") == "image--text-to-3d");
}

TEST(guide_blocks) {
    const char* md = "# Masking\n"
                     "\n"
                     "Masks protect parts of the model.\n"
                     "They work with every brush.\n"
                     "\n"
                     "## Paint a mask\n"
                     "\n"
                     "1. Hold <kbd>Ctrl</kbd>.\n"
                     "2. Stroke over the model,\n"
                     "   as with any brush.\n"
                     "   - Nested detail\n"
                     "3. Release.\n"
                     "\n"
                     "> **Tip:** Ctrl+click the background\n"
                     "> inverts the mask.\n"
                     "\n"
                     "<!-- BEGIN GENERATED: x -->\n"
                     "| Command | Does |\n"
                     "|---|:--|\n"
                     "| `mask_clear` | Clears it |\n"
                     "| `mask_all` | Masks `a|b` everything |\n"
                     "| <kbd>`</kbd> | Console |\n"
                     "<!-- END GENERATED: x -->\n"
                     "\n"
                     "![The mask on a lion](images/masking.jpg)\n"
                     "\n"
                     "```sh\n"
                     "sculptforge-cli -c 'mask_all'\n"
                     "  indented\n"
                     "```\n"
                     "\n"
                     "---\n"
                     "## Paint a mask\n"
                     "- one\n"
                     "- two\n";
    const guide::Document d = parse(md);
    CHECK(d.title == "Masking");
    std::map<BlockKind, int> count;
    for (const auto& b : d.blocks) count[b.kind]++;
    CHECK(count[BlockKind::Heading] == 3);
    CHECK(count[BlockKind::Paragraph] == 1);
    CHECK(count[BlockKind::Numbered] == 3);
    CHECK(count[BlockKind::Bullet] == 3);
    CHECK(count[BlockKind::Callout] == 1);
    CHECK(count[BlockKind::Table] == 1);
    CHECK(count[BlockKind::Image] == 1);
    CHECK(count[BlockKind::Code] == 1);
    CHECK(count[BlockKind::Rule] == 1);
    CHECK(d.blocks.size() > 2 && plainText(d.blocks[1].spans) == "Masks protect parts of the model. They work with every brush.");
    // Wrapped list items join; nesting is one level.
    const Block* second = nullptr;
    const Block* nested = nullptr;
    const Block* table = nullptr;
    const Block* code = nullptr;
    std::vector<std::string> anchors;
    for (const auto& b : d.blocks) {
        if (b.kind == BlockKind::Numbered && b.number == 2) second = &b;
        if (b.kind == BlockKind::Bullet && !nested) nested = &b;
        if (b.kind == BlockKind::Table) table = &b;
        if (b.kind == BlockKind::Code) code = &b;
        if (b.kind == BlockKind::Heading) anchors.push_back(b.anchor);
    }
    CHECK(second && plainText(second->spans) == "Stroke over the model, as with any brush.");
    CHECK(nested && nested->level == 1 && plainText(nested->spans) == "Nested detail");
    CHECK(table && table->rows.size() == 4 && table->rows[0].size() == 2);
    CHECK(table && table->rows[3][0].size() == 1 && table->rows[3][0][0].kbd && table->rows[3][0][0].text == "`");
    CHECK(table && table->rows[2][1].size() == 3 && table->rows[2][1][1].code && table->rows[2][1][1].text == "a|b");
    CHECK(code && code->lang == "sh" && code->text == "sculptforge-cli -c 'mask_all'\n  indented");
    CHECK((anchors == std::vector<std::string>{"masking", "paint-a-mask", "paint-a-mask-1"})); // duplicates numbered
    for (const auto& b : d.blocks)
        if (b.kind == BlockKind::Callout) {
            CHECK(b.spans.size() >= 2 && b.spans[0].bold && b.spans[0].text == "Tip:");
            CHECK(plainText(b.spans) == "Tip: Ctrl+click the background inverts the mask.");
        }
    const auto refs = references(d);
    CHECK(refs.size() == 1 && refs[0].image && refs[0].target == "images/masking.jpg");
}

namespace {
const std::map<std::string, std::string>& sampleBook() {
    static const std::map<std::string, std::string> files = {
        {"index.md", "# Guide\n\nIntro with a [link](https://example.org).\n\n## Start\n\n1. [Welcome](welcome.md)\n"
                     "2. [Masking](masking.md)\n\n## Reference\n\n- [Glossary](glossary.md)\n- [Masking again](masking.md#x)\n"},
        {"welcome.md", "# Welcome to SculptForge\n\nSculpt things. Masking is covered later.\n"},
        {"masking.md", "# Masking\n\nProtect areas from brushes.\n\n## Mask by cavity\n\nMasks the crevices of the "
                       "model, so you can paint dirt into them. Cavity masking needs detail.\n"},
        {"glossary.md", "# Glossary\n\n| ZBrush | SculptForge |\n|---|---|\n| DynaMesh | Voxel remesh |\n"},
    };
    return files;
}
bool loadSample(const std::string& file, std::string& text, void*) {
    const auto it = sampleBook().find(file);
    if (it == sampleBook().end()) return false;
    text = it->second;
    return true;
}
bool loadBroken(const std::string& file, std::string& text, void* user) {
    return file != "glossary.md" && loadSample(file, text, user);
}
} // namespace

TEST(guide_book_and_search) {
    Book book;
    std::string err;
    CHECK(book.load(loadSample, nullptr, err));
    CHECK(book.chapters().size() == 3); // masking.md listed twice counts once
    CHECK(book.find("masking.md") == 1 && book.find("index.md") == -1);
    CHECK(book.chapters()[0].title == "Welcome to SculptForge" && book.chapters()[0].group == "Start");
    CHECK(book.chapters()[2].group == "Reference");

    auto hits = book.search("CAVITY");
    CHECK(hits.size() == 1); // the heading and the paragraph are one section: one result
    CHECK(!hits.empty() && book.chapters()[size_t(hits[0].chapter)].file == "masking.md");
    CHECK(!hits.empty() && hits[0].section == "Mask by cavity" && hits[0].anchor == "mask-by-cavity");
    // The snippet comes from the text (not the heading) and marks the match; the heading adds to the score.
    CHECK(!hits.empty() && book.chapters()[1].doc.blocks[size_t(hits[0].block)].kind == BlockKind::Paragraph);
    CHECK(!hits.empty() && hits[0].snippet.substr(hits[0].matchStart, hits[0].matchLength) == "Cavity");
    CHECK(!hits.empty() && hits[0].score > book.search("crevices")[0].score);
    // Every word must appear; tables are searchable.
    CHECK(book.search("dirt crevices").size() == 1);
    CHECK(book.search("dirt glossary").empty());
    CHECK(book.search("dynamesh").size() == 1);
    CHECK(book.search("   ").empty());
    CHECK(book.search("masking").size() >= 3); // titles and text in two chapters

    Book broken;
    CHECK(!broken.load(loadBroken, nullptr, err) && err.find("glossary.md") != std::string::npos);
}
