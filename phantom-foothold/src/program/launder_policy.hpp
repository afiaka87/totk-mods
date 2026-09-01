#pragma once
#include <stdint.h>

// Phantom Foothold load-time material-patch policy (TotK 1.2.1): container recognition, sheet location,
// row rewriting, and hook admission. Engine-free and host-tested; fail-closed and loud.

namespace phantom::launder {

// Flag bits that block climbing: bit 0 (NoClimb) and bit 6; clearing both matches the earlier data edits.
inline constexpr uint64_t CLIMB_BLOCK_BITS = 0x41;

// The invisible-barrier material class. Its rows keep their block bits: laundering them
// would make airwalls climbable and let the player leave intended space.
inline constexpr uint32_t AIRWALL_MATERIAL = 0x23;

// A material row is {u32 material, u32 sub-material, u64 flags}.
inline constexpr uint32_t ROW_BYTES = 16;
inline constexpr uint32_t FLAGS_IN_ROW = 8;

// Plausibility ceiling for the two identifier words of a row. Real sheets stay far below
// this; a buffer that is not actually a material sheet almost always breaks it immediately.
inline constexpr uint32_t MAX_PLAUSIBLE_MATERIAL_ID = 0x10000;

// "Phive" container magic: 'Phiv' as a little-endian word, then 'e'.
inline constexpr uint32_t PHIVE_MAGIC_WORD = 0x76696850;
inline constexpr unsigned char PHIVE_MAGIC_TAIL = 'e';

// Tile containers (bphsc) additionally carry a version byte; the fixed1 palette layout
// below is only valid from version 2 on.
inline constexpr unsigned char MIN_TILE_VERSION = 2;

// Tile header: an 11-entry offsets table at +12 and an 8-entry sizes table after it.
// The fixed1 palette is index 5 of both.
inline constexpr uint32_t TILE_OFFSETS_TABLE = 12;
inline constexpr uint32_t TILE_SIZES_TABLE = 12 + 44 + 340;
inline constexpr uint32_t TILE_FIXED1_INDEX = 5;
inline constexpr uint64_t MIN_TILE_BUFFER = 0x1C0;

// Shape header (bphsh): material section offset at +0x10, its byte size at +0x20.
inline constexpr uint32_t SHAPE_SECTION_OFFSET = 0x10;
inline constexpr uint32_t SHAPE_SECTION_SIZE = 0x20;
inline constexpr uint32_t MIN_SHAPE_BUFFER = 0x30;

// Reported when a sheet was left untouched because it failed a bounds or plausibility
// check. Distinct from "cleared nothing", which is a normal container with no blocked rows.
inline constexpr int SHEET_REFUSED = -1;

// Locating a material sheet

// NotAContainer: not ours, say nothing; Malformed: sheet size is not whole rows; Sheet: offset/rows usable.
enum class SheetVerdict : uint8_t { NotAContainer, Malformed, Sheet };

struct SheetLocation {
    SheetVerdict verdict = SheetVerdict::NotAContainer;
    uint32_t offset = 0;
    uint32_t rows = 0;
};

// Word loads stay direct casts, matching the loaders themselves: these run for every
// collision container the game opens and for every row of every sheet.
inline uint32_t readWord(const unsigned char* buffer, uint32_t at) {
    return *(const uint32_t*)(buffer + at);
}

inline bool hasPhiveMagic(const unsigned char* buffer) {
    return readWord(buffer, 0) == PHIVE_MAGIC_WORD && buffer[4] == PHIVE_MAGIC_TAIL;
}

// World/shrine tile container: the fixed1 palette.
inline SheetLocation locateTileSheet(const unsigned char* buffer, uint64_t size) {
    if (buffer == nullptr || size <= MIN_TILE_BUFFER || !hasPhiveMagic(buffer)
        || buffer[6] < MIN_TILE_VERSION) {
        return {};
    }
    const uint32_t offset = readWord(buffer, TILE_OFFSETS_TABLE + 4 * TILE_FIXED1_INDEX);
    const uint32_t bytes = readWord(buffer, TILE_SIZES_TABLE + 4 * TILE_FIXED1_INDEX);
    if ((bytes & (ROW_BYTES - 1)) != 0) { return {SheetVerdict::Malformed, 0, 0}; }
    return {SheetVerdict::Sheet, offset, bytes / ROW_BYTES};
}

// Object shape container: the material section. This one loader also serves the shape
// blobs embedded in tiles, so it needs no separate case.
inline SheetLocation locateShapeSheet(const unsigned char* buffer, uint32_t size) {
    if (buffer == nullptr || size <= MIN_SHAPE_BUFFER || !hasPhiveMagic(buffer)) { return {}; }
    const uint32_t offset = readWord(buffer, SHAPE_SECTION_OFFSET);
    const uint32_t bytes = readWord(buffer, SHAPE_SECTION_SIZE);
    if ((bytes & (ROW_BYTES - 1)) != 0) { return {SheetVerdict::Malformed, 0, 0}; }
    return {SheetVerdict::Sheet, offset, bytes / ROW_BYTES};
}

// Rewriting a material sheet

// Clear the climb-block bits of every non-airwall row; returns rows changed, or SHEET_REFUSED with nothing written.
inline int maskSheet(unsigned char* buffer, uint32_t bufferSize, uint32_t offset, uint32_t rows) {
    if (offset == 0 || rows == 0) { return 0; }
    if ((uint64_t)offset + (uint64_t)ROW_BYTES * rows > bufferSize) { return SHEET_REFUSED; }
    for (uint32_t row = 0; row < rows; row++) {
        const unsigned char* entry = buffer + offset + ROW_BYTES * row;
        if (readWord(entry, 0) >= MAX_PLAUSIBLE_MATERIAL_ID
            || readWord(entry, 4) >= MAX_PLAUSIBLE_MATERIAL_ID) {
            return SHEET_REFUSED;
        }
    }
    int cleared = 0;
    for (uint32_t row = 0; row < rows; row++) {
        unsigned char* entry = buffer + offset + ROW_BYTES * row;
        if (readWord(entry, 0) == AIRWALL_MATERIAL) { continue; }
        uint64_t* flags = (uint64_t*)(entry + FLAGS_IN_ROW);
        if ((*flags & CLIMB_BLOCK_BITS) != 0) {
            *flags &= ~CLIMB_BLOCK_BITS;
            cleared++;
        }
    }
    return cleared;
}

// Accounting

// Running totals per loader; `containers` counts only containers that actually changed.
struct Stats {
    uint32_t containers = 0;
    uint32_t rows = 0;
    uint32_t bails = 0;
};

// Log throttling. Refusals are rare and each one matters, so the first few are always
// reported; successes are common, so after the first ten only every 256th is.
inline constexpr uint32_t BAIL_LOG_LIMIT = 8;
inline constexpr uint32_t CLEAR_LOG_LIMIT = 10;
inline constexpr uint32_t CLEAR_LOG_INTERVAL = 0xFF;

enum class NoteKind : uint8_t { Unchanged, Refused, Cleared };

struct NoteOutcome {
    NoteKind kind = NoteKind::Unchanged;
    bool shouldLog = false;
};

// Fold one sheet result into the running totals and say whether it deserves a log line.
inline NoteOutcome note(Stats& stats, int cleared) {
    if (cleared < 0) {
        stats.bails++;
        return {NoteKind::Refused, stats.bails <= BAIL_LOG_LIMIT};
    }
    if (cleared == 0) { return {NoteKind::Unchanged, false}; }
    stats.containers++;
    stats.rows += (uint32_t)cleared;
    const bool shouldLog =
        stats.containers <= CLEAR_LOG_LIMIT || (stats.containers & CLEAR_LOG_INTERVAL) == 0;
    return {NoteKind::Cleared, shouldLog};
}

// Version guard

// First instruction word of each hook site on build 9B4E43650501A4D4.
inline constexpr uint32_t EXPECT_TILE_LOADER = 0x6DB63BEF;   // stp d15,d14,[sp,#-0xA0]!
inline constexpr uint32_t EXPECT_SHAPE_LOADER = 0xD101C3FF;  // sub sp,sp,#0x70
inline constexpr uint32_t EXPECT_CLASSIFIER = 0xB9429408;    // ldr w8,[x0,#0x294]

// All-or-nothing: both loader sites verify or the module installs nothing; the tile site is checked first.
enum class GuardOutcome : uint8_t { Install, RefusedTileLoader, RefusedShapeLoader };

constexpr GuardOutcome evaluateGuard(uint32_t tileWord, uint32_t shapeWord) {
    if (tileWord != EXPECT_TILE_LOADER) { return GuardOutcome::RefusedTileLoader; }
    if (shapeWord != EXPECT_SHAPE_LOADER) { return GuardOutcome::RefusedShapeLoader; }
    return GuardOutcome::Install;
}

}  // namespace phantom::launder
