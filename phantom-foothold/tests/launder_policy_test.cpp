// Host tests for the load-time material-patch policy (TotK 1.2.1): admission, recognition, row
// transformation, refusals, and accounting.

#include "launder_policy.hpp"

#include <cstdio>
#include <cstring>
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

// Buffer fixtures

void putWord(std::vector<unsigned char>& buffer, uint32_t at, uint32_t value) {
    std::memcpy(buffer.data() + at, &value, sizeof(value));
}

void putFlags(std::vector<unsigned char>& buffer, uint32_t at, uint64_t value) {
    std::memcpy(buffer.data() + at, &value, sizeof(value));
}

// One 16-byte material row: {material, sub-material, flags}.
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

// A tile container whose fixed1 palette holds `rows` entries, placed past the header.
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

// Version-word admission and the all-or-nothing rule

void testVersionGuard() {
    CHECK(evaluateGuard(EXPECT_TILE_LOADER, EXPECT_SHAPE_LOADER) == GuardOutcome::Install);

    // A wrong build installs NOTHING, even when the other site happens to still match.
    CHECK(evaluateGuard(0xDEADBEEF, EXPECT_SHAPE_LOADER) == GuardOutcome::RefusedTileLoader);
    CHECK(evaluateGuard(EXPECT_TILE_LOADER, 0xDEADBEEF) == GuardOutcome::RefusedShapeLoader);
    CHECK(evaluateGuard(0xDEADBEEF, 0xDEADBEEF) == GuardOutcome::RefusedTileLoader);

    // A one-bit difference is still a different build: the guard is an equality test, not a
    // similarity test. A conflicting mod's trampoline overwrites the whole word.
    CHECK(evaluateGuard(EXPECT_TILE_LOADER ^ 1u, EXPECT_SHAPE_LOADER)
          == GuardOutcome::RefusedTileLoader);
    CHECK(evaluateGuard(EXPECT_TILE_LOADER, EXPECT_SHAPE_LOADER ^ 0x80000000u)
          == GuardOutcome::RefusedShapeLoader);

    // Zero is what an unmapped or cleared site reads as; it must never be mistaken for a match.
    CHECK(evaluateGuard(0, 0) == GuardOutcome::RefusedTileLoader);

    // The three recorded words are distinct, so no site can accidentally satisfy another's test.
    CHECK(EXPECT_TILE_LOADER != EXPECT_SHAPE_LOADER);
    CHECK(EXPECT_TILE_LOADER != EXPECT_CLASSIFIER);
    CHECK(EXPECT_SHAPE_LOADER != EXPECT_CLASSIFIER);
}

// Container recognition and sheet location

void testTileSheetLocation() {
    const std::vector<unsigned char> tile = tileContainer(4);
    const SheetLocation found = locateTileSheet(tile.data(), tile.size());
    CHECK(found.verdict == SheetVerdict::Sheet);
    CHECK(found.offset == TILE_SHEET_AT);
    CHECK(found.rows == 4);

    // Not ours: no magic, truncated, or null. Each must be silent, not a refusal -- every
    // asset load in the game reaches this hook.
    std::vector<unsigned char> foreign = tileContainer(4);
    putWord(foreign, 0, 0x11223344);
    CHECK(locateTileSheet(foreign.data(), foreign.size()).verdict == SheetVerdict::NotAContainer);

    std::vector<unsigned char> halfMagic = tileContainer(4);
    halfMagic[4] = 'x';
    CHECK(locateTileSheet(halfMagic.data(), halfMagic.size()).verdict
          == SheetVerdict::NotAContainer);

    CHECK(locateTileSheet(tile.data(), MIN_TILE_BUFFER).verdict == SheetVerdict::NotAContainer);
    CHECK(locateTileSheet(nullptr, 0x4000).verdict == SheetVerdict::NotAContainer);

    // Older tile containers do not carry the fixed1 layout this code reads.
    const std::vector<unsigned char> old = tileContainer(4, MIN_TILE_VERSION - 1);
    CHECK(locateTileSheet(old.data(), old.size()).verdict == SheetVerdict::NotAContainer);

    // A sheet size that is not a whole number of rows means the header is not what we think.
    std::vector<unsigned char> ragged = tileContainer(4);
    putWord(ragged, TILE_SIZES_TABLE + 4 * TILE_FIXED1_INDEX, ROW_BYTES * 4 + 1);
    CHECK(locateTileSheet(ragged.data(), ragged.size()).verdict == SheetVerdict::Malformed);

    // An empty palette is well-formed and simply has nothing to do.
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

    // Shape containers carry no version byte, so byte 6 must not gate them: object shapes
    // would stop laundering the moment one happened to hold a small value.
    std::vector<unsigned char> lowByte = shapeContainer(3);
    lowByte[6] = 0;
    CHECK(locateShapeSheet(lowByte.data(), (uint32_t)lowByte.size()).verdict
          == SheetVerdict::Sheet);
}

// The row transformation

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

    // Material and sub-material identifiers are read, never rewritten.
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

    // The exemption keys on the material identifier alone; the sub-material does not matter.
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

// Refusals: nothing is written unless the whole sheet checks out

void testSheetRunningPastTheBufferIsRefused() {
    std::vector<unsigned char> tile = tileContainer(2);
    putRow(tile, TILE_SHEET_AT + 0 * ROW_BYTES, 0x10, 0, 0x41);
    putRow(tile, TILE_SHEET_AT + 1 * ROW_BYTES, 0x11, 0, 0x41);
    const std::vector<unsigned char> before = tile;

    // Claim more rows than the buffer can hold.
    CHECK(maskSheet(tile.data(), (uint32_t)tile.size(), TILE_SHEET_AT, 64) == SHEET_REFUSED);
    CHECK(tile == before);

    // Exactly filling the buffer is allowed; one byte more is not.
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

    // The bad row is last, so a one-pass implementation would already have rewritten the
    // first two. The plausibility pass runs to completion before the first store.
    const SheetLocation sheet = locateTileSheet(tile.data(), tile.size());
    CHECK(maskSheet(tile.data(), (uint32_t)tile.size(), sheet.offset, sheet.rows)
          == SHEET_REFUSED);
    CHECK(tile == before);

    // The sub-material identifier is checked with the same ceiling.
    std::vector<unsigned char> badSub = tileContainer(2);
    putRow(badSub, TILE_SHEET_AT + 0 * ROW_BYTES, 0x10, 0, 0x41);
    putRow(badSub, TILE_SHEET_AT + 1 * ROW_BYTES, 0x11, MAX_PLAUSIBLE_MATERIAL_ID, 0x41);
    const std::vector<unsigned char> badSubBefore = badSub;
    CHECK(maskSheet(badSub.data(), (uint32_t)badSub.size(), TILE_SHEET_AT, 2) == SHEET_REFUSED);
    CHECK(badSub == badSubBefore);

    // One below the ceiling is still plausible.
    std::vector<unsigned char> edge = tileContainer(1);
    putRow(edge, TILE_SHEET_AT, MAX_PLAUSIBLE_MATERIAL_ID - 1, MAX_PLAUSIBLE_MATERIAL_ID - 1,
           0x41);
    CHECK(maskSheet(edge.data(), (uint32_t)edge.size(), TILE_SHEET_AT, 1) == 1);
}

void testAbsentSheetIsNotARefusal() {
    std::vector<unsigned char> tile = tileContainer(2);
    // A container that declares no palette has nothing to launder, which is not a failure.
    CHECK(maskSheet(tile.data(), (uint32_t)tile.size(), 0, 4) == 0);
    CHECK(maskSheet(tile.data(), (uint32_t)tile.size(), TILE_SHEET_AT, 0) == 0);
}

// Accounting

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
    // Refusals: the first few always speak, then they go quiet.
    for (uint32_t i = 1; i <= BAIL_LOG_LIMIT; i++) {
        CHECK(note(stats, SHEET_REFUSED).shouldLog == true);
    }
    CHECK(note(stats, SHEET_REFUSED).shouldLog == false);
    CHECK(stats.bails == BAIL_LOG_LIMIT + 1);

    // Successes: the first ten, then every 256th, so a full world load stays readable.
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

// End to end, the way the hooks call it

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

    // Re-laundering the same container is a no-op: the rows are already open.
    CHECK(maskSheet(tile.data(), (uint32_t)tile.size(), sheet.offset, sheet.rows) == 0);
}

// Differential check: verbatim copies of the shipped v1.0.0 hook bodies prove the extraction changed nothing.

namespace legacy {

constexpr uint64_t LEGACY_CLIMB_BLOCK_BITS = 0x41;
constexpr uint32_t LEGACY_AIRWALL_MAT = 0x23;

struct LaunderStats {
    uint32_t files = 0;
    uint32_t rows = 0;
    uint32_t bails = 0;
};

int maskSheet(unsigned char* buf, uint32_t bufSize, uint32_t off, uint32_t rows) {
    if (off == 0 || rows == 0) { return 0; }
    if ((uint64_t)off + 16ull * rows > bufSize) { return -1; }
    for (uint32_t r = 0; r < rows; r++) {
        const uint32_t mat = *(uint32_t*)(buf + off + 16 * r);
        const uint32_t sub = *(uint32_t*)(buf + off + 16 * r + 4);
        if (mat >= 0x10000 || sub >= 0x10000) { return -1; }
    }
    int cleared = 0;
    for (uint32_t r = 0; r < rows; r++) {
        unsigned char* row = buf + off + 16 * r;
        const uint32_t mat = *(uint32_t*)row;
        if (mat == LEGACY_AIRWALL_MAT) { continue; }
        uint64_t* flags = (uint64_t*)(row + 8);
        if (*flags & LEGACY_CLIMB_BLOCK_BITS) {
            *flags &= ~LEGACY_CLIMB_BLOCK_BITS;
            cleared++;
        }
    }
    return cleared;
}

// note(), reporting whether it would have logged instead of logging.
bool note(LaunderStats& s, int cleared) {
    if (cleared < 0) {
        s.bails++;
        return s.bails <= 8;
    }
    if (cleared == 0) { return false; }
    s.files++;
    s.rows += (uint32_t)cleared;
    return s.files <= 10 || (s.files & 0xFF) == 0;
}

// The two hook bodies, reporting what they passed to note() (-2 = never called).
constexpr int NOT_NOTED = -2;

int tileHook(unsigned char* buf, uint64_t size) {
    if (buf != nullptr && size > 0x1C0 && *(uint32_t*)buf == 0x76696850 && buf[4] == 'e'
        && buf[6] >= 2) {
        const uint32_t f1off = *(uint32_t*)(buf + 12 + 4 * 5);
        const uint32_t f1size = *(uint32_t*)(buf + 12 + 44 + 340 + 4 * 5);
        if ((f1size & 15) == 0) { return maskSheet(buf, (uint32_t)size, f1off, f1size / 16); }
        return -1;
    }
    return NOT_NOTED;
}

int shapeHook(unsigned char* buf, uint32_t size) {
    if (buf != nullptr && size > 0x30 && *(uint32_t*)buf == 0x76696850 && buf[4] == 'e') {
        const uint32_t moff = *(uint32_t*)(buf + 0x10);
        const uint32_t msize = *(uint32_t*)(buf + 0x20);
        if ((msize & 15) == 0) { return maskSheet(buf, size, moff, msize / 16); }
        return -1;
    }
    return NOT_NOTED;
}

}  // namespace legacy

// What the refactored hook body passes to note(), so the two can be compared directly.
int tileResult(unsigned char* buffer, uint64_t size) {
    const SheetLocation sheet = locateTileSheet(buffer, size);
    switch (sheet.verdict) {
    case SheetVerdict::NotAContainer: return legacy::NOT_NOTED;
    case SheetVerdict::Malformed: return SHEET_REFUSED;
    case SheetVerdict::Sheet: return maskSheet(buffer, (uint32_t)size, sheet.offset, sheet.rows);
    }
    return legacy::NOT_NOTED;
}

int shapeResult(unsigned char* buffer, uint32_t size) {
    const SheetLocation sheet = locateShapeSheet(buffer, size);
    switch (sheet.verdict) {
    case SheetVerdict::NotAContainer: return legacy::NOT_NOTED;
    case SheetVerdict::Malformed: return SHEET_REFUSED;
    case SheetVerdict::Sheet: return maskSheet(buffer, size, sheet.offset, sheet.rows);
    }
    return legacy::NOT_NOTED;
}

// A spread of buffers: real containers, near-misses, and junk the loaders also see.
std::vector<std::vector<unsigned char>> differentialCorpus() {
    std::vector<std::vector<unsigned char>> corpus;

    for (uint32_t rows : {0u, 1u, 2u, 5u, 17u}) {
        std::vector<unsigned char> tile = tileContainer(rows == 0 ? 1 : rows);
        if (rows == 0) { putWord(tile, TILE_SIZES_TABLE + 4 * TILE_FIXED1_INDEX, 0); }
        for (uint32_t row = 0; row < rows; row++) {
            const uint32_t material = (row % 3 == 0) ? AIRWALL_MATERIAL : 0x10 + row;
            const uint64_t flags = (row % 2 == 0) ? 0x41 : (row == 1 ? 0x40 : 0x8000);
            putRow(tile, TILE_SHEET_AT + row * ROW_BYTES, material, row, flags);
        }
        corpus.push_back(tile);

        std::vector<unsigned char> shape = shapeContainer(rows == 0 ? 1 : rows);
        for (uint32_t row = 0; row < rows; row++) {
            putRow(shape, SHAPE_SHEET_AT + row * ROW_BYTES, 0x20 + row, row, 0x41);
        }
        corpus.push_back(shape);
    }

    // Version byte below the floor, ragged sizes, an out-of-range offset, an implausible
    // row, no magic at all, and a buffer too small to be either container.
    std::vector<unsigned char> oldTile = tileContainer(2, 1);
    putRow(oldTile, TILE_SHEET_AT, 0x10, 0, 0x41);
    corpus.push_back(oldTile);

    std::vector<unsigned char> ragged = tileContainer(2);
    putWord(ragged, TILE_SIZES_TABLE + 4 * TILE_FIXED1_INDEX, 33);
    corpus.push_back(ragged);

    std::vector<unsigned char> raggedShape = shapeContainer(2);
    putWord(raggedShape, SHAPE_SECTION_SIZE, 20);
    corpus.push_back(raggedShape);

    std::vector<unsigned char> farOffset = tileContainer(2);
    putWord(farOffset, TILE_OFFSETS_TABLE + 4 * TILE_FIXED1_INDEX, 0x100000);
    corpus.push_back(farOffset);

    std::vector<unsigned char> implausible = tileContainer(3);
    putRow(implausible, TILE_SHEET_AT + 0 * ROW_BYTES, 0x10, 0, 0x41);
    putRow(implausible, TILE_SHEET_AT + 1 * ROW_BYTES, 0x11, 0, 0x41);
    putRow(implausible, TILE_SHEET_AT + 2 * ROW_BYTES, 0xABCDEF, 0, 0x41);
    corpus.push_back(implausible);

    std::vector<unsigned char> junk(0x400, 0xAB);
    corpus.push_back(junk);

    corpus.push_back(std::vector<unsigned char>(0x20, 0));
    return corpus;
}

void testHookBodiesMatchTheReleasedOnes() {
    for (const std::vector<unsigned char>& original : differentialCorpus()) {
        std::vector<unsigned char> now = original;
        std::vector<unsigned char> before = original;

        const int nowResult = tileResult(now.data(), now.size());
        const int beforeResult = legacy::tileHook(before.data(), before.size());
        CHECK(nowResult == beforeResult);
        CHECK(now == before);  // identical bytes written, or identically left alone

        std::vector<unsigned char> nowShape = original;
        std::vector<unsigned char> beforeShape = original;
        const int nowShapeResult = shapeResult(nowShape.data(), (uint32_t)nowShape.size());
        const int beforeShapeResult = legacy::shapeHook(beforeShape.data(), (uint32_t)beforeShape.size());
        CHECK(nowShapeResult == beforeShapeResult);
        CHECK(nowShape == beforeShape);
    }

    // A null buffer reaches both loaders and must be ignored by both.
    CHECK(tileResult(nullptr, 0x4000) == legacy::tileHook(nullptr, 0x4000));
    CHECK(shapeResult(nullptr, 0x4000) == legacy::shapeHook(nullptr, 0x4000));
}

void testAccountingMatchesTheReleasedOne() {
    Stats now;
    legacy::LaunderStats before;
    // A long mixed run so both throttles are exercised well past their thresholds.
    for (int step = 0; step < 3000; step++) {
        const int cleared = (step % 7 == 0) ? SHEET_REFUSED : (step % 3);
        CHECK(note(now, cleared).shouldLog == legacy::note(before, cleared));
        CHECK(now.containers == before.files);
        CHECK(now.rows == before.rows);
        CHECK(now.bails == before.bails);
    }
}

}  // namespace

int main() {
    testVersionGuard();
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
    testHookBodiesMatchTheReleasedOnes();
    testAccountingMatchesTheReleasedOne();

    std::printf("%s: %d checks, %d failures\n", gFailures == 0 ? "PASS" : "FAIL", gChecks,
                gFailures);
    return gFailures == 0 ? 0 : 1;
}
