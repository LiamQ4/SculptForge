// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bring-your-own-AI brush designer: turns an artist's description ("sharp
// wrinkles that fade out", "make it grittier") into a BrushRecipe. Works with
// Anthropic's API or any OpenAI-compatible endpoint (OpenAI, Ollama, LM
// Studio, OpenRouter, ...). The model only ever returns recipe data.
#pragma once

#include "core/BrushRecipe.h"

#include <string>
#include <vector>

namespace sf::ai {

struct AiSettings {
    enum class Provider { Anthropic, OpenAICompatible };
    Provider provider = Provider::Anthropic;
    std::string endpoint;
    std::string model;
    std::string apiKey; // empty = use ANTHROPIC_API_KEY / OPENAI_API_KEY from the environment
    std::string effort = "low";
    // Image to 3D. service: "huggingface" (free Spaces), "fal", "meshy",
    // "command" (generatorCommand with {input}/{output}) or "inflate" (offline).
    std::string generatorService = "huggingface";
    std::string hfToken;  // empty = HF_TOKEN from the environment (optional; raises the free quota)
    std::string hfModel = "hunyuan3d-2.1";
    std::string generatorCommand;
    std::string falKey;   // empty = FAL_KEY from the environment
    std::string falModel = "fal-ai/trellis-2";
    std::string falEndpoint = "https://queue.fal.run";
    std::string meshyKey; // empty = MESHY_API_KEY from the environment
    std::string meshyModel = "latest";
    std::string meshyEndpoint = "https://api.meshy.ai";
    std::string resolvedFalKey() const;
    std::string resolvedHfToken() const;
    std::string resolvedMeshyKey() const;

    static AiSettings defaults(Provider provider);
    std::string resolvedKey() const;

    Json toJson() const;
    static AiSettings fromJson(const Json& j);
    // Stored in the user's config directory with owner-only permissions.
    bool save(const std::string& path, std::string& error) const;
    static AiSettings load(const std::string& path);
};

struct ChatMessage {
    enum class Role { User, Assistant } role;
    std::string text;    // exactly what is sent to the model (history is append-only)
    std::string display; // what the chat panel shows
};

struct BrushSuggestion {
    bool ok = false;
    std::string reply; // short artist-facing explanation
    BrushRecipe recipe;
    std::string error;
};

// Where the app keeps ai.json (same location as SDL_GetPrefPath("SculptForge", "SculptForge")).
std::string defaultAiSettingsPath();

std::string brushSystemPrompt();
// The user turn to send: the artist's words plus the brush currently being edited.
ChatMessage composeUserMessage(const std::string& request, const BrushRecipe* current);

// Request pieces (exposed for tests).
std::string requestUrl(const AiSettings& s);
std::vector<std::string> requestHeaders(const AiSettings& s);
std::string buildRequestBody(const AiSettings& s, const std::vector<ChatMessage>& history);
// `rawAssistantJson` receives the model's JSON text, to append to the history.
BrushSuggestion parseResponse(const AiSettings& s, long httpStatus, const std::string& body,
                              std::string* rawAssistantJson = nullptr);

// Blocking (run on a worker thread): sends the conversation, whose last
// message must be the user's, and returns a recipe.
BrushSuggestion requestBrush(const AiSettings& s, const std::vector<ChatMessage>& history,
                             std::string* rawAssistantJson);

} // namespace sf::ai
