// SPDX-License-Identifier: MIT
// Copyright (C) Clay Mullis

// Geometry transfers the accepted BindFS baseline/bearing recipe; no font artwork is embedded.
#include "totk/ui/NativeFont.hpp"

namespace totk::ui::native_font {
namespace {
unsigned glyph(unsigned codepoint) {
    if (codepoint >= 32 && codepoint <= 126) return codepoint - 32;
    return codepoint == 0x2019 ? 95 : '?' - 32;
}
unsigned nextCodepoint(const char* text, unsigned& offset) {
    if (offset >= kTextBytes || !text[offset]) return 0;
    const auto first = static_cast<unsigned char>(text[offset++]);
    if (first < 128) return first;
    unsigned remaining = 0, value = 0, minimum = 0;
    if ((first & 0xe0) == 0xc0) { remaining = 1; value = first & 31; minimum = 128; }
    else if ((first & 0xf0) == 0xe0) { remaining = 2; value = first & 15; minimum = 2048; }
    else if ((first & 0xf8) == 0xf0) { remaining = 3; value = first & 7; minimum = 65536; }
    else return '?';
    for (unsigned i = 0; i < remaining; ++i) {
        if (offset >= kTextBytes) return '?';
        const auto next = static_cast<unsigned char>(text[offset]);
        if ((next & 0xc0) != 0x80) return '?';
        ++offset;
        value = (value << 6) | (next & 63);
    }
    return value >= minimum && value <= 0x10ffff && !(value >= 0xd800 && value <= 0xdfff) ? value : '?';
}
std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) value.remove_suffix(1);
    return value;
}
bool decimal(std::string_view source, float& output) {
    if (source.empty()) return false;
    bool negative = source.front() == '-';
    if (negative) source.remove_prefix(1);
    if (source.empty()) return false;
    float value = 0, fraction = 0.1f;
    bool point = false, digit = false;
    for (char c : source) {
        if (c == '.' && !point) { point = true; continue; }
        if (c < '0' || c > '9') return false;
        digit = true;
        if (point) { value += (c - '0') * fraction; fraction *= 0.1f; }
        else value = value * 10 + (c - '0');
        if (!std::isfinite(value) || value > 16384) return false;
    }
    if (!digit) return false;
    output = negative ? -value : value;
    return true;
}
bool quoted(std::string_view value, std::string_view& out) {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') return false;
    out = {value.data() + 1, value.size() - 2};
    return out.find('"') == std::string_view::npos && out.find('\\') == std::string_view::npos;
}
bool color(std::string_view value, std::uint32_t& out) {
    if (value.size() != 10 || std::string_view{value.data(), 2} != "0x") return false;
    std::uint32_t result = 0;
    for (char c : std::string_view{value.data() + 2, value.size() - 2}) {
        unsigned digit = c >= '0' && c <= '9' ? c - '0' :
                         c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                         c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
        if (digit == 16) return false;
        result = (result << 4) | digit;
    }
    out = result;
    return true;
}
} // namespace

bool validAtlas(const Atlas& atlas) noexcept {
    if (!atlas.pixels || !atlas.advances || !atlas.bytes || atlas.bytes > kAtlasBytes ||
        atlas.bytes % kBankBytes || (!atlas.glyphSlots && atlas.bytes != kAtlasBytes) ||
        (reinterpret_cast<std::uintptr_t>(atlas.pixels) & 4095)) return false;
    for (unsigned face = 0; face < kFaces; ++face)
        for (unsigned cell = 0; cell < kCells; ++cell)
            if (!atlas.advances[face][cell] || atlas.advances[face][cell] > 16 * 64 ||
                (atlas.glyphSlots && atlas.glyphSlots[face][cell] >= atlas.bytes / (kCellPixels*kCellPixels))) return false;
    return true;
}
bool validFrame(const Frame& frame) noexcept {
    if (frame.count > kLines) return false;
    unsigned quads = 0;
    for (unsigned i = 0; i < frame.count; ++i) {
        const auto& line = frame.lines[i];
        if (!std::memchr(line.text, 0, sizeof(line.text)) ||
            static_cast<unsigned>(line.face) >= kFaces || !std::isfinite(line.scale) ||
            !(line.scale > 0) || line.scale > 16 || !std::isfinite(line.x) ||
            !std::isfinite(line.y) || std::fabs(line.x) > 16384 || std::fabs(line.y) > 16384) return false;
        unsigned offset = 0;
        while (offset < kTextBytes && line.text[offset]) {
            if (line.text[offset] == '\n') { ++offset; continue; }
            if (glyph(nextCodepoint(line.text, offset)) && ++quads > kQuads) return false;
        }
    }
    return true;
}
unsigned buildQuads(const Frame& frame, const Atlas& atlas, PixelGrid grid,
                    Quad* output, unsigned capacity) noexcept {
    if (!output || !atlas.advances || !validFrame(frame) || !grid.valid()) return 0;
    unsigned count = 0;
    for (unsigned n = 0; n < frame.count; ++n) {
        const auto& line = frame.lines[n];
        const auto face = static_cast<unsigned>(line.face);
        float x = line.x, y = line.y;
        unsigned offset = 0;
        while (offset < kTextBytes && line.text[offset]) {
            if (line.text[offset] == '\n') { ++offset; x = line.x; y += 22 * line.scale; continue; }
            const unsigned cell = glyph(nextCodepoint(line.text, offset));
            const unsigned slot = atlas.glyphSlots ? atlas.glyphSlots[face][cell] : face*kCells+cell;
            if (cell) {
                if (count == capacity) return count;
                output[count++] = {{grid.x(x + line.scale) - line.scale,
                                    grid.y(y + 12 * line.scale) - 12 * line.scale,
                                    16 * line.scale, 16 * line.scale},
                                   {(slot % kCellsPerBank) * kCellPixels * kCellPixels, 0,
                                    line.rgba, slot / kCellsPerBank}};
            }
            x += atlas.advances[face][cell] * line.scale / 64;
        }
    }
    return count;
}

DocumentResult parseDocument(std::string_view source, Frame& output) noexcept {
    if (source.size() > 16384) return {DocumentError::TooLarge, 0};
    Frame candidate{};
    unsigned row = 0, fields = 0;
    auto finish = [&]() { return candidate.count == 0 || (fields & 3) == 3; };
    while (!source.empty()) {
        ++row;
        const auto end = source.find('\n');
        auto line = trim({source.data(), end == std::string_view::npos ? source.size() : end});
        source = end == std::string_view::npos ? std::string_view{} :
            std::string_view{source.data() + end + 1, source.size() - end - 1};
        if (line.empty() || line.front() == '#') continue;
        if (line.front() == '[') {
            if (!finish()) return {DocumentError::InvalidValue, row};
            if (line.size() < 8 || std::string_view{line.data(), 6} != "[text." || line.back() != ']') return {DocumentError::Syntax, row};
            if (candidate.count == kLines) return {DocumentError::Capacity, row};
            ++candidate.count;
            fields = 0;
            continue;
        }
        const auto equals = line.find('=');
        if (!candidate.count || equals == std::string_view::npos) return {DocumentError::Syntax, row};
        const auto key = trim({line.data(), equals});
        const auto value = trim({line.data() + equals + 1, line.size() - equals - 1});
        auto& entry = candidate.lines[candidate.count - 1];
        unsigned field = 0;
        bool ok = false;
        std::string_view text;
        if (key == "text") {
            field = 1;
            ok = quoted(value, text) && text.size() < kTextBytes && text.find('\0') == std::string_view::npos;
            if (ok) { std::memcpy(entry.text, text.data(), text.size()); entry.text[text.size()] = 0; }
        } else if (key == "font") { field = 2; ok = quoted(value, text) && faceFromName(text, entry.face); }
        else if (key == "x") { field = 4; ok = decimal(value, entry.x); }
        else if (key == "y") { field = 8; ok = decimal(value, entry.y); }
        else if (key == "scale") { field = 16; ok = decimal(value, entry.scale); }
        else if (key == "rgba") { field = 32; ok = color(value, entry.rgba); }
        else return {DocumentError::UnknownField, row};
        if (!ok || (fields & field)) return {DocumentError::InvalidValue, row};
        fields |= field;
    }
    if (!finish() || !candidate.count || !validFrame(candidate)) return {DocumentError::InvalidValue, row};
    output = candidate;
    return {};
}
} // namespace totk::ui::native_font
