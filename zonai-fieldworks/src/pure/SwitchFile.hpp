// Copyright (c) HanSoloBlaster, afiaka87
// The switch file and the wheel captions, kept free of game code so the host tests can cover them.
// The file holds one "member-id=1" or "member-id=0" line per member.
#pragma once

#include <cstddef>
#include <string_view>

namespace fieldworks::pure {

// Missing members, unknown ids and malformed lines leave a member off.
inline void parseSwitches(std::string_view text, const char* const* ids, std::size_t count, bool* on) {
    for (std::size_t i = 0; i < count; ++i) on[i] = false;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.remove_suffix(1);
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        std::string_view key = line.substr(0, equals);
        std::string_view value = line.substr(equals + 1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.remove_suffix(1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
        if (value != "1" && value != "0") continue;
        for (std::size_t i = 0; i < count; ++i)
            if (key == ids[i]) on[i] = value == "1";
    }
}

// Returns the number of bytes written, or zero when the text does not fit.
inline std::size_t formatSwitches(const char* const* ids, const bool* on, std::size_t count, char* out,
                                  std::size_t capacity) {
    std::size_t at = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const std::string_view id{ids[i]};
        if (at + id.size() + 3 > capacity) return 0;
        for (const char c : id) out[at++] = c;
        out[at++] = '=';
        out[at++] = on[i] ? '1' : '0';
        out[at++] = '\n';
    }
    return at;
}

// When a changed switch takes effect (design 06, section 3a).
enum class Pending : unsigned char {
    None,         // the saved switch is the running one
    Now,          // changes on the next controller update
    Busy,         // turns off once the member finishes what it is doing (riding a linked vehicle)
    Unavailable,  // the member could not be installed, so it cannot turn on
};

// "Bivouac: On"; while a change waits: "Fleet Sync: Off when the current action ends".
inline std::size_t switchCaption(const char16_t* name, bool saved, Pending pending, char16_t* out,
                                 std::size_t capacity) {
    std::size_t at = 0;
    const auto append = [&](const char16_t* text) {
        for (; *text && at + 1 < capacity; ++text) out[at++] = *text;
    };
    if (!capacity) return 0;
    append(name);
    append(saved ? u": On" : u": Off");
    if (pending == Pending::Busy) append(u" when the current action ends");
    if (pending == Pending::Unavailable) append(u" (could not start)");
    out[at] = u'\0';
    return at;
}

}  // namespace fieldworks::pure
