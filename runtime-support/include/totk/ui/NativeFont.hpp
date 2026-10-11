// SPDX-License-Identifier: MIT
// Copyright (C) Clay Mullis
#pragma once

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace totk::ui::native_font {

enum class Face : std::uint8_t { RodinRegular, RodinBold, Raglan };
constexpr unsigned kFaces = 3, kCells = 96, kCellPixels = 48;
constexpr unsigned kCellsPerBank = 16, kBanksPerFace = 6, kBanks = 18;
constexpr unsigned kBankBytes = 36864, kAtlasBytes = kBanks * kBankBytes;
constexpr unsigned kLines = 16, kTextBytes = 192, kBatch = 128, kQuads = 1152;

[[nodiscard]] inline bool faceFromName(std::string_view name, Face& face) noexcept {
    if (name == "rodin_regular") face = Face::RodinRegular;
    else if (name == "rodin_bold") face = Face::RodinBold;
    else if (name == "raglan") face = Face::Raglan;
    else return false;
    return true;
}

struct Atlas {
    // Private baked game assets. The consumer keeps both arrays alive for the process.
    unsigned char* pixels = nullptr;
    std::size_t bytes = 0;
    const std::uint16_t (*advances)[kCells] = nullptr;
};

struct Line {
    char text[kTextBytes]{};
    float x = 32, y = 32, scale = 1;
    std::uint32_t rgba = 0xffffffff; // low byte red, high byte alpha
    Face face = Face::RodinRegular;
};

struct Frame { Line lines[kLines]{}; unsigned count = 0; };
struct alignas(16) Quad { float rect[4]{}; std::uint32_t style[4]{}; };
static_assert(sizeof(Quad) == 32);

struct PixelGrid {
    float width = 0, height = 0;
    float x(float value) const noexcept { return std::round(value * width / 1280) * 1280 / width; }
    float y(float value) const noexcept { return std::round(value * height / 720) * 720 / height; }
    bool valid() const noexcept {
        return std::isfinite(width) && std::isfinite(height) && width > 0 && height > 0;
    }
};

struct DrawRun { unsigned block = 0, firstVertex = 0, count = 0, bank = 0; };
[[nodiscard]] inline DrawRun nextRun(const Quad* quads, unsigned count, unsigned first) noexcept {
    if (!quads || first >= count || quads[first].style[3] >= kBanks) return {};
    DrawRun run{(first / kBatch) * kBatch, 0, 0, quads[first].style[3]};
    run.firstVertex = (first - run.block) * 6;
    const unsigned end = count < run.block + kBatch ? count : run.block + kBatch;
    for (unsigned i = first; i < end && quads[i].style[3] == run.bank; ++i) ++run.count;
    return run;
}

// One game-thread producer and one UI render consumer; value snapshots contain no game pointers.
template <class T> class Mailbox {
    T slots_[3]{};
    unsigned writer_ = 2, reader_ = 0;
    std::atomic<unsigned> middle_{1};
public:
    void publish(const T& value) noexcept {
        slots_[writer_] = value;
        writer_ = middle_.exchange(writer_ | 4, std::memory_order_acq_rel) & 3;
    }
    const T& consume() noexcept {
        if (middle_.load(std::memory_order_acquire) & 4)
            reader_ = middle_.exchange(reader_, std::memory_order_acq_rel) & 3;
        return slots_[reader_];
    }
};

[[nodiscard]] bool validFrame(const Frame&) noexcept;
[[nodiscard]] bool validAtlas(const Atlas&) noexcept;
unsigned buildQuads(const Frame&, const Atlas&, PixelGrid, Quad*, unsigned capacity) noexcept;

enum class DocumentError { None, TooLarge, Syntax, UnknownField, InvalidValue, Capacity };
struct DocumentResult { DocumentError error{}; unsigned line = 0; explicit operator bool() const { return error == DocumentError::None; } };
// INI/TOML subset: [text.name], quoted text/font, numeric x/y/scale and optional 0xAABBGGRR rgba.
// Parsing is atomic: any error leaves the previous Frame unchanged.
DocumentResult parseDocument(std::string_view source, Frame& output) noexcept;

} // namespace totk::ui::native_font
