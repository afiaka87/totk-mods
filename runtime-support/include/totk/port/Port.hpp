// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
// Addresses named by their TotK 1.2.1 offset, for 1.2.1 and 1.4.3; unselected stays 1.2.1 (host tests rely on it).
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>

#if defined(_MSC_VER) && !defined(__clang__)
#define TOTK_PORT_NOINLINE __declspec(noinline)
#else
#define TOTK_PORT_NOINLINE [[gnu::noinline]]
#endif

namespace totk::port {

enum class Game : std::uint8_t { Unselected, V121, V143, Unsupported };

// target 0: 1.4.3 has no such place. words: the first two instructions there on each version (zero for data).
struct Entry {
    std::uint32_t source;
    std::uint32_t target;
    std::uint32_t words121[2];
    std::uint32_t words143[2];
};

struct Identity {
    std::uint32_t offset;
    std::uint32_t word;
};

// Entries sorted by source. Identity words sit deep inside routines, where another mod's entry hook cannot reach.
struct Table {
    const Entry* entries;
    std::size_t count;
    const Identity* identity121;
    const Identity* identity143;
    std::size_t identityCount;
};

struct State {
    Game game = Game::Unselected;
    const Table* table = nullptr;
    std::uintptr_t base = 0;
    std::uint32_t early = 0;    // addresses resolved before a game was chosen
    std::uint32_t missing = 0;  // addresses resolved on 1.4.3 that have no entry
    std::uint32_t lastMissing = 0;
};
inline State state;

// Where an address without a 1.4.3 entry resolves: never mapped, so a use faults at once.
inline constexpr std::uintptr_t kFaultAddress = std::uintptr_t{0xBAD} << 12;

TOTK_PORT_NOINLINE inline const Entry* find(std::uint32_t source) noexcept {
    const Table* table = state.table;
    if (!table) return nullptr;
    std::size_t low = 0, high = table->count;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        if (table->entries[middle].source < source) low = middle + 1;
        else high = middle;
    }
    return low < table->count && table->entries[low].source == source ? &table->entries[low] : nullptr;
}

TOTK_PORT_NOINLINE inline std::ptrdiff_t resolve(std::uint32_t source) noexcept {
    if (state.game == Game::V143) {
        const Entry* entry = find(source);
        if (entry && entry->target) return static_cast<std::ptrdiff_t>(entry->target);
        ++state.missing;
        state.lastMissing = source;
        return static_cast<std::ptrdiff_t>(kFaultAddress) - static_cast<std::ptrdiff_t>(state.base);
    }
    if (state.game == Game::Unselected) ++state.early;
    return static_cast<std::ptrdiff_t>(source);
}

[[nodiscard]] inline Game game() noexcept { return state.game; }
[[nodiscard]] inline bool is143() noexcept { return state.game == Game::V143; }
// True on a game this layer can serve. Unselected counts: a mod that never chooses a game is 1.2.1-only.
[[nodiscard]] inline bool supported() noexcept { return state.game != Game::Unsupported; }

[[nodiscard]] inline bool has(std::uint32_t source) noexcept {
    if (state.game != Game::V143) return state.game != Game::Unsupported;
    const Entry* entry = find(source);
    return entry && entry->target;
}

// The instructions the table recorded at this place for the running game; null when unknown.
[[nodiscard]] inline const std::uint32_t* recordedWords(std::uint32_t source) noexcept {
    const Entry* entry = find(source);
    if (!entry) return nullptr;
    return state.game == Game::V143 ? entry->words143 : entry->words121;
}

// Chooses the game directly; a suite that already identified the build calls this.
inline bool use(Game game, const Table& table, std::uintptr_t base) noexcept {
    if (game == Game::V143 && state.early != 0) {
        state.game = Game::Unsupported;
        return false;
    }
    state.table = &table;
    state.base = base;
    state.game = game == Game::V121 || game == Game::V143 ? game : Game::Unsupported;
    return state.game != Game::Unsupported;
}

// An unknown build, or 1.4.3 after an address already resolved, leaves the layer unsupported: nothing installs.
template <typename ReadWord>
inline bool select(std::uintptr_t base, std::size_t textSize, const Table& table, ReadWord readWord) noexcept {
    if (state.game == Game::V121 || state.game == Game::V143) return true;
    const auto matches = [&](const Identity* identity) {
        for (std::size_t i = 0; i < table.identityCount; ++i) {
            if (identity[i].offset + 4 > textSize || readWord(identity[i].offset) != identity[i].word) return false;
        }
        return true;
    };
    const bool is121 = matches(table.identity121);
    const bool is143 = matches(table.identity143);
    return use(is121 == is143 ? Game::Unsupported : is121 ? Game::V121 : Game::V143, table, base);
}

// A place named by its 1.2.1 offset; it converts to the running game's offset at use (`base + kRoutine`).
class Code {
public:
    constexpr Code(std::uint32_t source) noexcept : source_(source) {}  // NOLINT(google-explicit-constructor)
    operator std::ptrdiff_t() const noexcept { return resolve(source_); }  // NOLINT(google-explicit-constructor)
    [[nodiscard]] constexpr std::uint32_t source() const noexcept { return source_; }
    [[nodiscard]] bool available() const noexcept { return has(source_); }

private:
    std::uint32_t source_;
};

template <std::integral T>
[[nodiscard]] inline std::uintptr_t operator+(T base, Code code) noexcept {
    return static_cast<std::uintptr_t>(base) + static_cast<std::uintptr_t>(resolve(code.source()));
}
// An offset from a routine's start is a different place on each game: give it its own entry.
template <std::integral T>
std::ptrdiff_t operator+(Code, T) = delete;
template <std::integral T>
std::ptrdiff_t operator-(Code, T) = delete;

// The translating twin of core::ImageOffset, for constants read as `kRoutine.value`.
struct ImageOffset {
    Code value;
};

// A member offset or other small constant whose value differs between the two games.
class Member {
public:
    constexpr Member(std::int32_t v121, std::int32_t v143) noexcept : v121_(v121), v143_(v143) {}
    operator std::ptrdiff_t() const noexcept {  // NOLINT(google-explicit-constructor)
        if (state.game == Game::Unselected) ++state.early;
        return state.game == Game::V143 ? v143_ : v121_;
    }
    [[nodiscard]] constexpr std::int32_t v121() const noexcept { return v121_; }
    [[nodiscard]] constexpr std::int32_t v143() const noexcept { return v143_; }

private:
    std::int32_t v121_;
    std::int32_t v143_;
};

template <typename T>
[[nodiscard]] inline T pick(T v121, T v143) noexcept {
    return state.game == Game::V143 ? v143 : v121;
}

template <typename... Places>
[[nodiscard]] inline bool all(const Places&... places) noexcept {
    return (... && Code(places).available());
}

// `0x64EEEC_v121` is a 1.2.1 place translated at use; `0x3E67F0_v143` is a plain 1.4.3 offset, never translated.
inline namespace literals {
constexpr Code operator""_v121(unsigned long long source) noexcept { return Code(static_cast<std::uint32_t>(source)); }
constexpr std::ptrdiff_t operator""_v143(unsigned long long offset) noexcept {
    return static_cast<std::ptrdiff_t>(offset);
}
}  // namespace literals

}  // namespace totk::port
