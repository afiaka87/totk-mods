// Host tests for Phantom Foothold's native profiles and material-sheet policy.

#include "GameProfiles.hpp"
#include "launder_policy.hpp"

#include <cstdio>
#include <cstring>
#include <iterator>
#include <vector>

namespace {

using namespace phantom::launder;

int gFailures = 0;
int gChecks = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        ++gChecks;                                                                                 \
        if (!(condition)) {                                                                        \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);                                \
            ++gFailures;                                                                           \
        }                                                                                          \
    } while (false)

void putWord(std::vector<unsigned char>& buffer, uint32_t at, uint32_t value) {
    std::memcpy(buffer.data() + at, &value, sizeof(value));
}

void putFlags(std::vector<unsigned char>& buffer, uint32_t at, uint64_t value) {
    std::memcpy(buffer.data() + at, &value, sizeof(value));
}

void putRow(std::vector<unsigned char>& buffer, uint32_t at, uint32_t material, uint32_t sub,
            uint64_t flags) {
    putWord(buffer, at, material);
    putWord(buffer, at + 4, sub);
    putFlags(buffer, at + FLAGS_IN_ROW, flags);
}

uint64_t rowFlags(const std::vector<unsigned char>& buffer, uint32_t at) {
    uint64_t value = 0;
    std::memcpy(&value, buffer.data() + at + FLAGS_IN_ROW, sizeof(value));
    return value;
}

void putMagic(std::vector<unsigned char>& buffer) {
    putWord(buffer, 0, PHIVE_MAGIC_WORD);
    buffer[4] = PHIVE_MAGIC_TAIL;
}

constexpr uint32_t TILE_SHEET_AT = 0x200;
std::vector<unsigned char> tileContainer(uint32_t rows, unsigned char version = MIN_TILE_VERSION) {
    std::vector<unsigned char> buffer(TILE_SHEET_AT + ROW_BYTES * rows + 16, 0);
    putMagic(buffer);
    buffer[6] = version;
    putWord(buffer, TILE_OFFSETS_TABLE + 4 * TILE_FIXED1_INDEX, TILE_SHEET_AT);
    putWord(buffer, TILE_SIZES_TABLE + 4 * TILE_FIXED1_INDEX, ROW_BYTES * rows);
    return buffer;
}

constexpr uint32_t SHAPE_SHEET_AT = 0x40;
std::vector<unsigned char> shapeContainer(uint32_t rows) {
    std::vector<unsigned char> buffer(SHAPE_SHEET_AT + ROW_BYTES * rows + 16, 0);
    putMagic(buffer);
    putWord(buffer, SHAPE_SECTION_OFFSET, SHAPE_SHEET_AT);
    putWord(buffer, SHAPE_SECTION_SIZE, ROW_BYTES * rows);
    return buffer;
}

void testGameProfiles() {
    using phantom::profiles::kGameProfiles;
    CHECK(std::size(kGameProfiles) == 9);
    for (size_t i = 0; i < std::size(kGameProfiles); ++i) {
        const auto& profile = kGameProfiles[i];
        // Both loaders open with the same prologue on every build, so a moved site fails its guard.
        CHECK(profile.scWords[0] == 0x6DB63BEF);
        CHECK(profile.shWords[0] == 0xD101C3FF);
        CHECK(profile.scDoCreate != profile.shDoCreate);
        CHECK(std::strlen(profile.buildId) == 16);
        for (size_t j = 0; j < i; ++j) {
            CHECK(std::strcmp(profile.version, kGameProfiles[j].version) != 0);
            CHECK(std::strcmp(profile.buildId, kGameProfiles[j].buildId) != 0);
        }
    }
}

void testTileSheetLocation() {
    const std::vector<unsigned char> tile = tileContainer(4);
    const SheetLocation found = locateTileSheet(tile.data(), tile.size());
    CHECK(found.verdict == SheetVerdict::Sheet);
    CHECK(found.offset == TILE_SHEET_AT);
    CHECK(found.rows == 4);

    // Foreign, truncated and null buffers are silent, not refusals.
    std::vector<unsigned char> foreign = tileContainer(4);
    putWord(foreign, 0, 0x11223344);
    CHECK(locateTileSheet(foreign.data(), foreign.size()).verdict == SheetVerdict::NotAContainer);

    std::vector<unsigned char> halfMagic = tileContainer(4);
    halfMagic[4] = 'x';
    CHECK(locateTileSheet(halfMagic.data(), halfMagic.size()).verdict
          == SheetVerdict::NotAContainer);

    CHECK(locateTileSheet(tile.data(), MIN_TILE_BUFFER).verdict == SheetVerdict::NotAContainer);
    CHECK(locateTileSheet(nullptr, 0x4000).verdict == SheetVerdict::NotAContainer);

    const std::vector<unsigned char> old = tileContainer(4, MIN_TILE_VERSION - 1);
    CHECK(locateTileSheet(old.data(), old.size()).verdict == SheetVerdict::NotAContainer);

    std::vector<unsigned char> ragged = tileContainer(4);
    putWord(ragged, TILE_SIZES_TABLE + 4 * TILE_FIXED1_INDEX, ROW_BYTES * 4 + 1);
    CHECK(locateTileSheet(ragged.data(), ragged.size()).verdict == SheetVerdict::Malformed);

    std::vector<unsigned char> empty = tileContainer(4);
    putWord(empty, TILE_SIZES_TABLE + 4 * TILE_FIXED1_INDEX, 0);
    const SheetLocation none = locateTileSheet(empty.data(), empty.size());
    CHECK(none.verdict == SheetVerdict::Sheet);
    CHECK(none.rows == 0);
    CHECK(maskSheet(empty.data(), (uint32_t)empty.size(), none.offset, none.rows) == 0);
}

void testShapeSheetLocation() {
    const std::vector<unsigned char> shape = shapeContainer(3);
    const SheetLocation found = locateShapeSheet(shape.data(), (uint32_t)shape.size());
    CHECK(found.verdict == SheetVerdict::Sheet);
    CHECK(found.offset == SHAPE_SHEET_AT);
    CHECK(found.rows == 3);

    CHECK(locateShapeSheet(shape.data(), MIN_SHAPE_BUFFER).verdict == SheetVerdict::NotAContainer);
    CHECK(locateShapeSheet(nullptr, 0x100).verdict == SheetVerdict::NotAContainer);

    std::vector<unsigned char> foreign = shapeContainer(3);
    putWord(foreign, 0, 0);
    CHECK(locateShapeSheet(foreign.data(), (uint32_t)foreign.size()).verdict
          == SheetVerdict::NotAContainer);

    std::vector<unsigned char> ragged = shapeContainer(3);
    putWord(ragged, SHAPE_SECTION_SIZE, ROW_BYTES * 3 - 4);
    CHECK(locateShapeSheet(ragged.data(), (uint32_t)ragged.size()).verdict
          == SheetVerdict::Malformed);

    // Shape containers have no version byte, so byte 6 must not gate them.
    std::vector<unsigned char> lowByte = shapeContainer(3);
    lowByte[6] = 0;
    CHECK(locateShapeSheet(lowByte.data(), (uint32_t)lowByte.size()).verdict
          == SheetVerdict::Sheet);
}

void testMaskClearsBothClimbBits() {
    std::vector<unsigned char> tile = tileContainer(4);
    putRow(tile, TILE_SHEET_AT + 0 * ROW_BYTES, 0x10, 1, 0x1);            // NoClimb only
    putRow(tile, TILE_SHEET_AT + 1 * ROW_BYTES, 0x11, 1, 0x40);           // second bit only
    putRow(tile, TILE_SHEET_AT + 2 * ROW_BYTES, 0x12, 1, 0x41);           // both
    putRow(tile, TILE_SHEET_AT + 3 * ROW_BYTES, 0x13, 1, 0x0);            // already climbable

    const SheetLocation sheet = locateTileSheet(tile.data(), tile.size());
    CHECK(maskSheet(tile.data(), (uint32_t)tile.size(), sheet.offset, sheet.rows) == 3);
    for (uint32_t row = 0; row < 4; row++) {
        CHECK((rowFlags(tile, TILE_SHEET_AT + row * ROW_BYTES) & CLIMB_BLOCK_BITS) == 0);
    }
}

void testMaskPreservesEveryOtherBit() {
    std::vector<unsigned char> tile = tileContainer(1);
    const uint64_t unrelated = 0xFFFF'FFFF'FFFF'FFBEull;  // everything except the two climb bits
    putRow(tile, TILE_SHEET_AT, 0x30, 7, unrelated | CLIMB_BLOCK_BITS);

    const SheetLocation sheet = locateTileSheet(tile.data(), tile.size());
    CHECK(maskSheet(tile.data(), (uint32_t)tile.size(), sheet.offset, sheet.rows) == 1);
    CHECK(rowFlags(tile, TILE_SHEET_AT) == unrelated);

    uint32_t material = 0;
    uint32_t sub = 0;
    std::memcpy(&material, tile.data() + TILE_SHEET_AT, sizeof(material));
    std::memcpy(&sub, tile.data() + TILE_SHEET_AT + 4, sizeof(sub));
    CHECK(material == 0x30);
    CHECK(sub == 7);
}

void testAirwallRowsKeepTheirBlockBits() {
    std::vector<unsigned char> tile = tileContainer(3);
    putRow(tile, TILE_SHEET_AT + 0 * ROW_BYTES, AIRWALL_MATERIAL, 0, 0x41);
    putRow(tile, TILE_SHEET_AT + 1 * ROW_BYTES, 0x20, 0, 0x41);
    putRow(tile, TILE_SHEET_AT + 2 * ROW_BYTES, AIRWALL_MATERIAL, 9, 0x1);

    const SheetLocation sheet = locateTileSheet(tile.data(), tile.size());
    CHECK(maskSheet(tile.data(), (uint32_t)tile.size(), sheet.offset, sheet.rows) == 1);
    CHECK(rowFlags(tile, TILE_SHEET_AT + 0 * ROW_BYTES) == 0x41);
    CHECK(rowFlags(tile, TILE_SHEET_AT + 1 * ROW_BYTES) == 0x0);
    CHECK(rowFlags(tile, TILE_SHEET_AT + 2 * ROW_BYTES) == 0x1);

    CHECK(AIRWALL_MATERIAL == 0x23);
}

void testShapeSheetMasksTheSameWay() {
    std::vector<unsigned char> shape = shapeContainer(2);
    putRow(shape, SHAPE_SHEET_AT + 0 * ROW_BYTES, 0x05, 0, 0x41);
    putRow(shape, SHAPE_SHEET_AT + 1 * ROW_BYTES, AIRWALL_MATERIAL, 0, 0x41);

    const SheetLocation sheet = locateShapeSheet(shape.data(), (uint32_t)shape.size());
    CHECK(maskSheet(shape.data(), (uint32_t)shape.size(), sheet.offset, sheet.rows) == 1);
    CHECK(rowFlags(shape, SHAPE_SHEET_AT + 0 * ROW_BYTES) == 0);
    CHECK(rowFlags(shape, SHAPE_SHEET_AT + 1 * ROW_BYTES) == 0x41);
}

void testSheetRunningPastTheBufferIsRefused() {
    std::vector<unsigned char> tile = tileContainer(2);
    putRow(tile, TILE_SHEET_AT + 0 * ROW_BYTES, 0x10, 0, 0x41);
    putRow(tile, TILE_SHEET_AT + 1 * ROW_BYTES, 0x11, 0, 0x41);
    const std::vector<unsigned char> before = tile;

    CHECK(maskSheet(tile.data(), (uint32_t)tile.size(), TILE_SHEET_AT, 64) == SHEET_REFUSED);
    CHECK(tile == before);

    std::vector<unsigned char> exact(TILE_SHEET_AT + ROW_BYTES * 2, 0);
    putRow(exact, TILE_SHEET_AT + 0 * ROW_BYTES, 0x10, 0, 0x41);
    putRow(exact, TILE_SHEET_AT + 1 * ROW_BYTES, 0x11, 0, 0x41);
    CHECK(maskSheet(exact.data(), (uint32_t)exact.size(), TILE_SHEET_AT, 2) == 2);
    CHECK(maskSheet(exact.data(), (uint32_t)exact.size() - 1, TILE_SHEET_AT, 2) == SHEET_REFUSED);
}

void testImplausibleRowRefusesTheWholeSheet() {
    std::vector<unsigned char> tile = tileContainer(3);
    putRow(tile, TILE_SHEET_AT + 0 * ROW_BYTES, 0x10, 0, 0x41);
    putRow(tile, TILE_SHEET_AT + 1 * ROW_BYTES, 0x11, 0, 0x41);
    putRow(tile, TILE_SHEET_AT + 2 * ROW_BYTES, MAX_PLAUSIBLE_MATERIAL_ID, 0, 0x41);
    const std::vector<unsigned char> before = tile;

    // The bad row is last, so a single-pass implementation would already have written the first two.
    const SheetLocation sheet = locateTileSheet(tile.data(), tile.size());
    CHECK(maskSheet(tile.data(), (uint32_t)tile.size(), sheet.offset, sheet.rows)
          == SHEET_REFUSED);
    CHECK(tile == before);

    std::vector<unsigned char> badSub = tileContainer(2);
    putRow(badSub, TILE_SHEET_AT + 0 * ROW_BYTES, 0x10, 0, 0x41);
    putRow(badSub, TILE_SHEET_AT + 1 * ROW_BYTES, 0x11, MAX_PLAUSIBLE_MATERIAL_ID, 0x41);
    const std::vector<unsigned char> badSubBefore = badSub;
    CHECK(maskSheet(badSub.data(), (uint32_t)badSub.size(), TILE_SHEET_AT, 2) == SHEET_REFUSED);
    CHECK(badSub == badSubBefore);

    std::vector<unsigned char> edge = tileContainer(1);
    putRow(edge, TILE_SHEET_AT, MAX_PLAUSIBLE_MATERIAL_ID - 1, MAX_PLAUSIBLE_MATERIAL_ID - 1,
           0x41);
    CHECK(maskSheet(edge.data(), (uint32_t)edge.size(), TILE_SHEET_AT, 1) == 1);
}

void testAbsentSheetIsNotARefusal() {
    std::vector<unsigned char> tile = tileContainer(2);
    CHECK(maskSheet(tile.data(), (uint32_t)tile.size(), 0, 4) == 0);
    CHECK(maskSheet(tile.data(), (uint32_t)tile.size(), TILE_SHEET_AT, 0) == 0);
}

void testStatsCountOnlyContainersThatChanged() {
    Stats stats;
    CHECK(note(stats, 0).kind == NoteKind::Unchanged);
    CHECK(note(stats, 0).shouldLog == false);
    CHECK(stats.containers == 0);
    CHECK(stats.rows == 0);

    CHECK(note(stats, 5).kind == NoteKind::Cleared);
    CHECK(note(stats, 7).kind == NoteKind::Cleared);
    CHECK(stats.containers == 2);
    CHECK(stats.rows == 12);

    CHECK(note(stats, SHEET_REFUSED).kind == NoteKind::Refused);
    CHECK(stats.bails == 1);
    CHECK(stats.containers == 2);  // a refusal is not a container we touched
}

void testLogThrottling() {
    Stats stats;
    for (uint32_t i = 1; i <= BAIL_LOG_LIMIT; i++) {
        CHECK(note(stats, SHEET_REFUSED).shouldLog == true);
    }
    CHECK(note(stats, SHEET_REFUSED).shouldLog == false);
    CHECK(stats.bails == BAIL_LOG_LIMIT + 1);

    Stats cleared;
    for (uint32_t i = 1; i <= CLEAR_LOG_LIMIT; i++) {
        CHECK(note(cleared, 1).shouldLog == true);
    }
    bool sawInterval = false;
    for (uint32_t i = CLEAR_LOG_LIMIT + 1; i <= 512; i++) {
        const bool logged = note(cleared, 1).shouldLog;
        CHECK(logged == ((i & CLEAR_LOG_INTERVAL) == 0));
        sawInterval = sawInterval || logged;
    }
    CHECK(sawInterval);
    CHECK(cleared.containers == 512);
    CHECK(cleared.rows == 512);
}

void testWholeContainerPass() {
    std::vector<unsigned char> tile = tileContainer(5);
    putRow(tile, TILE_SHEET_AT + 0 * ROW_BYTES, 0x01, 0, 0x41);              // blocked
    putRow(tile, TILE_SHEET_AT + 1 * ROW_BYTES, AIRWALL_MATERIAL, 0, 0x41);  // exempt
    putRow(tile, TILE_SHEET_AT + 2 * ROW_BYTES, 0x02, 0, 0x00);              // already open
    putRow(tile, TILE_SHEET_AT + 3 * ROW_BYTES, 0x03, 0, 0x40);              // blocked
    putRow(tile, TILE_SHEET_AT + 4 * ROW_BYTES, 0x04, 0, 0x8001);            // blocked + other

    Stats stats;
    const SheetLocation sheet = locateTileSheet(tile.data(), tile.size());
    CHECK(sheet.verdict == SheetVerdict::Sheet);
    const int cleared = maskSheet(tile.data(), (uint32_t)tile.size(), sheet.offset, sheet.rows);
    CHECK(cleared == 3);
    const NoteOutcome outcome = note(stats, cleared);
    CHECK(outcome.kind == NoteKind::Cleared);
    CHECK(outcome.shouldLog == true);
    CHECK(stats.containers == 1);
    CHECK(stats.rows == 3);
    CHECK(stats.bails == 0);
    CHECK(rowFlags(tile, TILE_SHEET_AT + 4 * ROW_BYTES) == 0x8000);

    CHECK(maskSheet(tile.data(), (uint32_t)tile.size(), sheet.offset, sheet.rows) == 0);
}

}  // namespace

int main() {
    testGameProfiles();
    testTileSheetLocation();
    testShapeSheetLocation();
    testMaskClearsBothClimbBits();
    testMaskPreservesEveryOtherBit();
    testAirwallRowsKeepTheirBlockBits();
    testShapeSheetMasksTheSameWay();
    testSheetRunningPastTheBufferIsRefused();
    testImplausibleRowRefusesTheWholeSheet();
    testAbsentSheetIsNotARefusal();
    testStatsCountOnlyContainersThatChanged();
    testLogThrottling();
    testWholeContainerPass();

    std::printf("%s: %d checks, %d failures\n", gFailures == 0 ? "PASS" : "FAIL", gChecks,
                gFailures);
    return gFailures == 0 ? 0 : 1;
}
