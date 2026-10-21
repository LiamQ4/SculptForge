// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Script.h"

#include <cctype>
#include <cstdlib>

namespace sf {

namespace {

ArgValue parseValue(const std::string& raw, bool quoted) {
    if (!quoted) {
        if (raw == "true") return true;
        if (raw == "false") return false;
        char* end = nullptr;
        double d = std::strtod(raw.c_str(), &end);
        if (!raw.empty() && end == raw.c_str() + raw.size()) return d;
    }
    return raw;
}

} // namespace

bool parseScript(const std::string& text, std::vector<ScriptStatement>& out, std::string& error) {
    out.clear();
    size_t i = 0;
    int line = 1;
    std::vector<std::pair<std::string, bool>> tokens; // (text, quoted)
    int stmtLine = 1;

    auto flush = [&]() -> bool {
        if (tokens.empty()) return true;
        ScriptStatement st;
        st.line = stmtLine;
        st.command = tokens[0].first;
        for (size_t t = 1; t < tokens.size(); ++t) {
            // Tokens look like key=value; a quoted value arrives as key= followed by the quoted token.
            std::string tok = tokens[t].first;
            size_t eq = tok.find('=');
            if (eq == std::string::npos || eq == 0) {
                error = "Line " + std::to_string(stmtLine) + ": expected key=value, got '" + tok + "'";
                return false;
            }
            std::string key = tok.substr(0, eq);
            std::string value = tok.substr(eq + 1);
            bool quoted = false;
            if (value.empty() && t + 1 < tokens.size() && tokens[t + 1].second) {
                value = tokens[++t].first;
                quoted = true;
            }
            st.args[key] = parseValue(value, quoted);
        }
        out.push_back(std::move(st));
        tokens.clear();
        return true;
    };

    while (i < text.size()) {
        char c = text[i];
        if (c == '\n' || c == ';') {
            if (!flush()) return false;
            if (c == '\n') ++line;
            ++i;
            continue;
        }
        if (c == '#') {
            while (i < text.size() && text[i] != '\n') ++i;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (tokens.empty()) stmtLine = line;
        if (c == '"') {
            std::string s;
            ++i;
            while (i < text.size() && text[i] != '"') {
                // Only \" and \\ are escapes, so Windows paths like "C:\Users\me\a.obj" stay intact.
                if (text[i] == '\\' && i + 1 < text.size() && (text[i + 1] == '"' || text[i + 1] == '\\')) ++i;
                if (text[i] == '\n') ++line;
                s += text[i++];
            }
            if (i >= text.size()) {
                error = "Line " + std::to_string(line) + ": unterminated string";
                return false;
            }
            ++i;
            tokens.emplace_back(std::move(s), true);
            continue;
        }
        std::string s;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])) && text[i] != ';' &&
               text[i] != '#') {
            if (text[i] == '"' && !s.empty() && s.back() == '=') break; // key="quoted value"
            s += text[i++];
        }
        tokens.emplace_back(std::move(s), false);
    }
    return flush();
}

} // namespace sf
