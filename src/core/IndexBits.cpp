// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/IndexBits.h"

#include "core/ThreadPool.h"

#include <bit>

namespace sf {

void IndexBits::resize(size_t n) {
    const size_t chunks = (n + kChunk - 1) / kChunk;
    words_.assign(chunks * kChunkWords, 0);
    summary_.assign((chunks + 63) / 64, 0);
}

bool IndexBits::empty() const {
    for (uint64_t s : summary_)
        if (s) return false;
    return true;
}

void IndexBits::chunks(std::vector<uint32_t>& out) const {
    out.clear();
    for (size_t i = 0; i < summary_.size(); ++i)
        for (uint64_t bits = summary_[i]; bits; bits &= bits - 1) out.push_back(uint32_t(i * 64 + size_t(std::countr_zero(bits))));
}

void IndexBits::take(std::vector<uint32_t>& out, const std::function<void(size_t, uint64_t)>& perWord) {
    chunks(chunks_);
    const size_t nc = chunks_.size();
    if (nc == 0) return;
    // Count each chunk's members, then write them at their offsets (blocks of chunks, so small sets run inline).
    constexpr size_t kBlock = 8;
    counts_.resize(nc + 1);
    poolFor(nc, [&](size_t b, size_t e) {
        for (size_t k = b; k < e; ++k) {
            const uint64_t* w = words_.data() + size_t(chunks_[k]) * kChunkWords;
            uint32_t n = 0;
            for (uint32_t j = 0; j < kChunkWords; ++j) n += uint32_t(std::popcount(w[j]));
            counts_[k + 1] = n;
        }
    }, kBlock);
    counts_[0] = 0;
    for (size_t k = 0; k < nc; ++k) counts_[k + 1] += counts_[k];
    const size_t base = out.size();
    out.resize(base + counts_[nc]);
    poolFor(nc, [&](size_t b, size_t e) {
        for (size_t k = b; k < e; ++k) {
            const size_t first = size_t(chunks_[k]) * kChunkWords;
            uint32_t* dst = out.data() + base + counts_[k];
            for (uint32_t j = 0; j < kChunkWords; ++j) {
                uint64_t bits = words_[first + j];
                if (!bits) continue;
                if (perWord) perWord(first + j, bits);
                words_[first + j] = 0;
                const uint32_t i0 = uint32_t((first + j) * 64);
                for (; bits; bits &= bits - 1) *dst++ = i0 + uint32_t(std::countr_zero(bits));
            }
        }
    }, kBlock);
    for (uint32_t c : chunks_) summary_[c >> 6] &= ~(uint64_t(1) << (c & 63));
}

} // namespace sf
