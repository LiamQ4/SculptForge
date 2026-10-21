// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal blocking HTTPS client. libcurl on Linux/macOS, WinHTTP on Windows,
// so no extra dependency needs to be shipped. Call from a worker thread.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace sf::ai {

struct HttpResponse {
    long status = 0;   // 0 if the request never reached the server
    std::string body;
    std::string error; // transport error description
};

// `method` is "GET" or "POST"; `headers` are "Name: value" strings. Redirects
// are followed (result files are usually served from a CDN).
HttpResponse httpRequest(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                         const std::string& body = {}, int timeoutSeconds = 120);

// Like httpRequest, but hands the body to `onData` as it arrives (server-sent
// events, progress). Return false from `onData` to stop reading early.
HttpResponse httpRequestStream(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                               const std::string& body, int timeoutSeconds,
                               const std::function<bool(const char* data, size_t size)>& onData);

inline HttpResponse httpPostJson(const std::string& url, const std::vector<std::string>& headers, const std::string& body,
                                 int timeoutSeconds = 120) {
    return httpRequest("POST", url, headers, body, timeoutSeconds);
}

} // namespace sf::ai
