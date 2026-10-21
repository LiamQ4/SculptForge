// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/Zip.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace sf {

namespace {

void put16(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v));
    out.push_back(uint8_t(v >> 8));
}
void put32(std::vector<uint8_t>& out, uint32_t v) {
    put16(out, v & 0xFFFF);
    put16(out, v >> 16);
}
uint32_t get16(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8; }
uint32_t get32(const uint8_t* p) { return get16(p) | get16(p + 2) << 16; }

} // namespace

uint32_t crc32(const uint8_t* data, size_t size) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool writeStoredZip(const std::string& path, const std::vector<ZipEntry>& entries, size_t alignment, std::string& error) {
    std::vector<uint8_t> out, central;
    for (const auto& e : entries) {
        if (e.data.size() > 0xFFFFFFFEu || out.size() > 0xFFFFFFFEu) {
            error = "Archive too large (ZIP64 is not supported)";
            return false;
        }
        const uint32_t crc = crc32(e.data.data(), e.data.size());
        const uint32_t offset = uint32_t(out.size());
        // Pad with an extra field so the file data starts on an aligned offset.
        size_t dataStart = offset + 30 + e.name.size();
        size_t pad = alignment > 1 ? (alignment - dataStart % alignment) % alignment : 0;
        if (pad > 0 && pad < 4) pad += alignment; // an extra field needs at least its 4-byte header
        put32(out, 0x04034B50);
        put16(out, 20); // version needed
        put16(out, 0);  // flags
        put16(out, 0);  // stored
        put16(out, 0);  // time
        put16(out, 0x21); // date (1980-01-01)
        put32(out, crc);
        put32(out, uint32_t(e.data.size()));
        put32(out, uint32_t(e.data.size()));
        put16(out, uint32_t(e.name.size()));
        put16(out, uint32_t(pad));
        out.insert(out.end(), e.name.begin(), e.name.end());
        if (pad) {
            put16(out, 0x1986); // padding extra field (same id as Pixar's usdzip)
            put16(out, uint32_t(pad - 4));
            out.insert(out.end(), pad - 4, 0);
        }
        out.insert(out.end(), e.data.begin(), e.data.end());

        put32(central, 0x02014B50);
        put16(central, 20); // version made by
        put16(central, 20);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0x21);
        put32(central, crc);
        put32(central, uint32_t(e.data.size()));
        put32(central, uint32_t(e.data.size()));
        put16(central, uint32_t(e.name.size()));
        put16(central, 0); // extra
        put16(central, 0); // comment
        put16(central, 0); // disk
        put16(central, 0); // internal attributes
        put32(central, 0); // external attributes
        put32(central, offset);
        central.insert(central.end(), e.name.begin(), e.name.end());
    }
    const uint32_t centralOffset = uint32_t(out.size());
    out.insert(out.end(), central.begin(), central.end());
    put32(out, 0x06054B50);
    put16(out, 0);
    put16(out, 0);
    put16(out, uint32_t(entries.size()));
    put16(out, uint32_t(entries.size()));
    put32(out, uint32_t(central.size()));
    put32(out, centralOffset);
    put16(out, 0);

    std::ofstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!f || !f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()))) {
        error = "Cannot write " + path;
        return false;
    }
    return true;
}

bool readStoredZip(const std::string& path, std::vector<ZipEntry>& entries, std::string& error) {
    std::ifstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!f) {
        error = "Cannot open " + path;
        return false;
    }
    std::vector<uint8_t> z((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    // Find the end-of-central-directory record.
    size_t eocd = std::string::npos;
    for (size_t i = z.size() >= 22 ? z.size() - 22 : 0; i + 22 <= z.size(); --i) {
        if (get32(&z[i]) == 0x06054B50) {
            eocd = i;
            break;
        }
        if (i == 0 || z.size() - i > 65557) break;
    }
    if (eocd == std::string::npos) {
        error = path + " is not a ZIP/USDZ archive";
        return false;
    }
    const size_t count = get16(&z[eocd + 10]);
    size_t p = get32(&z[eocd + 16]);
    entries.clear();
    for (size_t k = 0; k < count; ++k) {
        if (p + 46 > z.size() || get32(&z[p]) != 0x02014B50) {
            error = "Corrupt ZIP central directory";
            return false;
        }
        const uint32_t method = get16(&z[p + 10]);
        const uint32_t size = get32(&z[p + 20]);
        const size_t nameLen = get16(&z[p + 28]), extraLen = get16(&z[p + 30]), commentLen = get16(&z[p + 32]);
        const size_t local = get32(&z[p + 42]);
        if (p + 46 + nameLen > z.size()) {
            error = "Corrupt ZIP central directory";
            return false;
        }
        ZipEntry e;
        e.name.assign(reinterpret_cast<const char*>(&z[p + 46]), nameLen);
        p += 46 + nameLen + extraLen + commentLen;
        if (method != 0) {
            error = "'" + e.name + "' is compressed; USDZ files must be uncompressed";
            return false;
        }
        if (local + 30 > z.size() || get32(&z[local]) != 0x04034B50) {
            error = "Corrupt ZIP entry " + e.name;
            return false;
        }
        const size_t data = local + 30 + get16(&z[local + 26]) + get16(&z[local + 28]);
        if (data + size > z.size()) {
            error = "Truncated ZIP entry " + e.name;
            return false;
        }
        e.data.assign(z.begin() + std::ptrdiff_t(data), z.begin() + std::ptrdiff_t(data + size));
        entries.push_back(std::move(e));
    }
    return true;
}

} // namespace sf
