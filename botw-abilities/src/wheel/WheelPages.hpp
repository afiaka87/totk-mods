// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstddef>
#include <cstdint>

namespace botw_abilities::wheel {
inline constexpr std::size_t kWedges = 8;
inline constexpr unsigned kPages = 2;
inline constexpr std::size_t kPathCapacity = 64;
inline constexpr std::uint64_t kButtonR = 1ull << 7;

// An ability offered on the abilities page. Each is offered only when its mod installed.
enum class Choice : std::uint8_t { None, Stasis, Cryonis };
constexpr unsigned offer(Choice choice) { return 1u << unsigned(choice); }

// One wedge on the abilities page. A null path leaves the wedge empty and unselectable.
struct Entry {
    const char *path;
    const char16_t *caption;
    Choice choice;
};

// Clockwise from the top wedge: Stasis in Fuse's native slot, Cryonis in amiibo's.
inline constexpr Entry kAbilityPage[kWedges]{
    {nullptr, u"", Choice::None},
    {nullptr, u"", Choice::None},
    {nullptr, u"", Choice::None},
    {nullptr, u"", Choice::None},
    {"UI/Tex/Icon/BotwStasis.bntx", u"Stasis+", Choice::Stasis},
    {"UI/Tex/Icon/BotwCryonis.bntx", u"Cryonis", Choice::Cryonis},
    {nullptr, u"", Choice::None},
    {nullptr, u"", Choice::None},
};

constexpr unsigned turnPage(unsigned page, unsigned turns) { return (page + turns) % kPages; }

constexpr std::uint32_t textLength(const char16_t *text) {
    std::uint32_t length = 0;
    while (text && text[length])
        ++length;
    return length;
}

constexpr bool offered(const Entry &entry, unsigned offers) {
    return entry.path && entry.choice != Choice::None && (offers & offer(entry.choice));
}

struct Wedge {
    const char *path;
    bool valid, check;
};
constexpr Wedge plan(unsigned page, std::size_t wedge, const Wedge &native, unsigned offers) {
    if (page == 0)
        return {native.path, native.valid, native.check};
    const auto &entry = kAbilityPage[wedge];
    if (!offered(entry, offers))
        return {nullptr, false, false};
    return {entry.path, true, false};
}

// Exiting page 1 always cancels, keeping the native ability; an offered wedge is also picked.
struct Exit {
    bool cancel;
    Choice choice;
};
constexpr Exit exitPage(unsigned page, std::int32_t cursor, bool cancelled, unsigned offers) {
    if (page == 0)
        return {false, Choice::None};
    if (cancelled || cursor < 0 || cursor >= std::int32_t(kWedges))
        return {true, Choice::None};
    const auto &entry = kAbilityPage[cursor];
    return {true, offered(entry, offers) ? entry.choice : Choice::None};
}

// A fresh R press in the open wheel turns the page and stays hidden from the game until released.
class PageInput {
public:
    struct Step {
        bool turn;
        std::uint64_t mask;
    };
    Step step(std::uint64_t buttons, bool freshSample, bool paging) {
        if (!freshSample)
            return {false, owned_ ? kButtonR : 0};
        const auto pressed = buttons & ~previous_;
        previous_ = buttons;
        const bool turn = paging && (pressed & kButtonR) != 0;
        if (turn)
            owned_ = true;
        else if (!(buttons & kButtonR))
            owned_ = false;
        return {turn, owned_ ? kButtonR : 0};
    }

private:
    std::uint64_t previous_{};
    bool owned_{};
};
} // namespace botw_abilities::wheel
