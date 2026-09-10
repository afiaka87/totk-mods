#include "RecallGpuLifetime.hpp"
#include "RecallGpuLifetimeBridge.hpp"
#include "doctest.h"

using self_recall::pure::RecallGpuLifetime;

namespace {
constexpr std::uintptr_t poolId = 10, listObjects = 0x1000, listCb = 20, rootCb = 30;
constexpr std::uintptr_t queue = 40, sync = 50;
constexpr std::uint32_t objectStride = 120; // Native RenderDLBuffer begin/end, 818884/818694.
struct Lifetime {
    RecallGpuLifetime ledger;
    Lifetime() { REQUIRE(ledger.configurePool(poolId, listObjects, 4, objectStride)); }
    void record(unsigned slot) {
        REQUIRE(ledger.beginList(listCb, poolId));
        REQUIRE(ledger.bindSlot(listCb, slot));
        REQUIRE(ledger.endList(listCb));
    }
    void frame(std::uint64_t handle) {
        REQUIRE(ledger.beginFrame(rootCb));
        REQUIRE(ledger.callList(rootCb, listObjects));
        REQUIRE(ledger.sealFrame(rootCb, handle));
    }
};
}

TEST_CASE("recording completion and native pool rotation cannot retire pending GPU uses") {
    Lifetime f;
    CHECK(f.ledger.canWrite(0));
    f.record(0);
    CHECK_FALSE(f.ledger.canWrite(0));
    f.frame(101);
    REQUIRE(f.ledger.clearPool(poolId));
    CHECK_FALSE(f.ledger.canWrite(0)); // Sealed, not yet submitted.
    const auto submitted = f.ledger.submittedAndFenced(101, queue, sync);
    REQUIRE(submitted != 0);
    CHECK_FALSE(f.ledger.canWrite(0));
    const auto waited = f.ledger.captureWait(queue, sync);
    CHECK_FALSE(f.ledger.completeWait(waited, 2)); // Timeout.
    CHECK_FALSE(f.ledger.completeWait(waited, 3)); // Failed.
    CHECK_FALSE(f.ledger.canWrite(0));
    CHECK(f.ledger.completeWait(waited, 1));
    CHECK(f.ledger.canWrite(0));
    CHECK(f.ledger.canWrite(1));
}

TEST_CASE("a completed old fence cannot retire a later prepared or submitted frame") {
    Lifetime f;
    f.record(0);
    f.frame(101);
    const auto first = f.ledger.submittedAndFenced(101, queue, sync);
    REQUIRE(first != 0);
    const auto oldWait = f.ledger.captureWait(queue, sync);
    REQUIRE(f.ledger.clearPool(poolId));
    f.record(1);
    f.frame(102);
    CHECK(f.ledger.completeWait(oldWait, 0));
    CHECK(f.ledger.canWrite(0));
    CHECK_FALSE(f.ledger.canWrite(1));
    const auto second = f.ledger.submittedAndFenced(102, queue, sync);
    REQUIRE(second > first);
    REQUIRE(f.ledger.clearPool(poolId));
    CHECK(f.ledger.completeWait(oldWait, 1));
    CHECK_FALSE(f.ledger.canWrite(1));
    CHECK(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 0));
    CHECK(f.ledger.canWrite(1));
}

TEST_CASE("display list storage pins slots until discard even when never submitted") {
    Lifetime f;
    f.record(2);
    CHECK_FALSE(f.ledger.canWrite(2));
    REQUIRE(f.ledger.clearPool(poolId));
    CHECK(f.ledger.canWrite(2));
    f.record(1);
    f.frame(101);
    REQUIRE(f.ledger.submittedAndFenced(101, queue, sync) != 0);
    REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
    CHECK_FALSE(f.ledger.canWrite(1)); // Pool can still supply another list call.
    REQUIRE(f.ledger.clearPool(poolId));
    CHECK(f.ledger.canWrite(1));
}

TEST_CASE("direct and nested list commands transfer private slot use to their root frame") {
    Lifetime f;
    constexpr std::uintptr_t otherPool = 11, otherObjects = 0x2000, otherCb = 21;
    REQUIRE(f.ledger.configurePool(otherPool, otherObjects, 2, objectStride));
    f.record(0);
    REQUIRE(f.ledger.beginList(otherCb, otherPool));
    REQUIRE(f.ledger.callList(otherCb, listObjects));
    REQUIRE(f.ledger.endList(otherCb));
    REQUIRE(f.ledger.clearPool(poolId));
    CHECK_FALSE(f.ledger.canWrite(0));
    REQUIRE(f.ledger.beginFrame(rootCb));
    REQUIRE(f.ledger.bindSlot(rootCb, 1));
    REQUIRE(f.ledger.callList(rootCb, otherObjects));
    REQUIRE(f.ledger.sealFrame(rootCb, 101));
    REQUIRE(f.ledger.clearPool(otherPool));
    CHECK_FALSE(f.ledger.canWrite(0));
    CHECK_FALSE(f.ledger.canWrite(1));
    CHECK(f.ledger.canWrite(2));
    REQUIRE(f.ledger.submittedAndFenced(101, queue, sync) != 0);
    REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
    CHECK(f.ledger.canWrite(0));
    CHECK(f.ledger.canWrite(1));
}

TEST_CASE("repeated root submission requires the later GPU fence before buffer reuse") {
    Lifetime f;
    f.record(0);
    f.frame(101);
    REQUIRE(f.ledger.clearPool(poolId));
    REQUIRE(f.ledger.submittedAndFenced(101, queue, sync) != 0);
    const auto priorWait = f.ledger.captureWait(queue, sync);
    REQUIRE(f.ledger.completeWait(priorWait, 1));
    CHECK(f.ledger.canWrite(0));
    REQUIRE(f.ledger.beginSubmission(101));
    CHECK_FALSE(f.ledger.canWrite(0)); // Pin before the repeated native enqueue.
    REQUIRE(f.ledger.submittedAndFenced(101, queue, sync) > priorWait);
    REQUIRE(f.ledger.completeWait(priorWait, 1));
    CHECK_FALSE(f.ledger.canWrite(0));
    REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
    CHECK(f.ledger.canWrite(0));
}

TEST_CASE("foreign waits and fabricated future tickets cannot release a private buffer") {
    Lifetime f;
    f.record(0);
    f.frame(101);
    REQUIRE(f.ledger.clearPool(poolId));
    const auto ticket = f.ledger.submittedAndFenced(101, queue, sync);
    REQUIRE(ticket != 0);
    CHECK(f.ledger.captureWait(queue + 1, sync) == 0);
    CHECK(f.ledger.captureWait(queue, sync + 1) == 0);
    CHECK_FALSE(f.ledger.completeWait(0, 1));
    CHECK_FALSE(f.ledger.completeWait(ticket + 1, 1));
    CHECK_FALSE(f.ledger.canWrite(0));
}

TEST_CASE("unknown command ownership or lost recording state freezes writes") {
    Lifetime f;
    SUBCASE("unregistered command buffer") { CHECK_FALSE(f.ledger.bindSlot(900, 0)); }
    SUBCASE("reset racing active list recording") {
        REQUIRE(f.ledger.beginList(listCb, poolId));
        CHECK_FALSE(f.ledger.clearPool(poolId));
    }
    SUBCASE("unknown submitted root") { CHECK(f.ledger.submittedAndFenced(999, queue, sync) == 0); }
    SUBCASE("foreign queue replaces bound queue") {
        f.frame(101);
        REQUIRE(f.ledger.submittedAndFenced(101, queue, sync) != 0);
        CHECK(f.ledger.submittedAndFenced(101, queue + 1, sync) == 0);
    }
    SUBCASE("misaligned list inside registered storage") {
        REQUIRE(f.ledger.beginFrame(rootCb));
        CHECK_FALSE(f.ledger.callList(rootCb, listObjects + 1));
    }
    SUBCASE("duplicate active recorder") {
        REQUIRE(f.ledger.beginList(listCb, poolId));
        CHECK_FALSE(f.ledger.beginList(listCb, poolId));
    }
    REQUIRE(f.ledger.failed());
    for (unsigned slot = 0; slot < RecallGpuLifetime::kSlots; ++slot) CHECK_FALSE(f.ledger.canWrite(slot));
}

TEST_CASE("lifetime records are bounded and only completed frame records are recycled") {
    Lifetime f;
    SUBCASE("all frame records still awaiting submission") {
        for (unsigned i = 0; i < RecallGpuLifetime::kFrames; ++i) {
            REQUIRE(f.ledger.beginFrame(rootCb));
            REQUIRE(f.ledger.sealFrame(rootCb, 101 + i));
        }
        CHECK_FALSE(f.ledger.beginFrame(rootCb));
    }
    SUBCASE("completed records support sustained frame submission") {
        for (unsigned i = 0; i < 32; ++i) {
            f.record(i % 3);
            f.frame(101 + i);
            REQUIRE(f.ledger.clearPool(poolId));
            REQUIRE(f.ledger.submittedAndFenced(101 + i, queue, sync) != 0);
            REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
            CHECK(f.ledger.canWrite(i % 3));
        }
        CHECK_FALSE(f.ledger.failed());
    }
}

TEST_CASE("invalid pool ranges and overlapping list ownership are rejected") {
    Lifetime f;
    SUBCASE("range overflow") {
        CHECK_FALSE(f.ledger.configurePool(11, UINTPTR_MAX - 10, 2, objectStride));
    }
    SUBCASE("overlapping pools") {
        CHECK_FALSE(f.ledger.configurePool(11, listObjects + objectStride, 2, objectStride));
    }
    SUBCASE("resize a pool still containing private commands") {
        f.record(0);
        CHECK_FALSE(f.ledger.configurePool(poolId, 0x2000, 4, objectStride));
    }
    SUBCASE("invalid slot") {
        REQUIRE(f.ledger.beginFrame(rootCb));
        CHECK_FALSE(f.ledger.bindSlot(rootCb, RecallGpuLifetime::kSlots));
    }
    CHECK(f.ledger.failed());
}

TEST_CASE("foreign recordings may copy ordinary lists but cannot silently consume private ones") {
    Lifetime f;
    REQUIRE(f.ledger.callList(900, listObjects));
    REQUIRE(f.ledger.callList(900, 0x9000));
    CHECK_FALSE(f.ledger.failed());
    f.record(0);
    CHECK_FALSE(f.ledger.callList(900, listObjects));
    CHECK(f.ledger.failed());
    CHECK_FALSE(f.ledger.canWrite(0));
}

TEST_CASE("GPU recorder admission uses the actual ARM return PC, not decompiler expression addresses") {
    namespace site = self_recall::gpu_lifetime::site;
    struct Call { std::uint32_t pc, instruction, target; std::uintptr_t returnPc; };
    const Call calls[]{
        {0x96F38C, 0x97FAA53E, 0x818884, site::kModelListBeginReturn},
        {0x96F3DC, 0x97FAA4AE, 0x818694, site::kModelListEndReturn},
        {0x97709C, 0x94000132, 0x977564, site::kModelPoolClearReturn},
    };
    for (const auto& call : calls) {
        REQUIRE((call.instruction & 0xFC000000u) == 0x94000000u);
        const auto immediate = std::int64_t(call.instruction & 0x03FFFFFFu) -
            ((call.instruction & 0x02000000u) ? 0x04000000ll : 0ll);
        CHECK(std::int64_t(call.pc) + immediate * 4 == call.target);
        CHECK(call.returnPc == call.pc + 4);
    }
    for (const std::uintptr_t base : {std::uintptr_t{0x8052B000}, std::uintptr_t{0x7100000000}}) {
        CHECK(site::isModelListBegin(base, base + calls[0].pc + 4));
        CHECK_FALSE(site::isModelListBegin(base, base + 0x96F394)); // Rejected every real begin in 04.
        CHECK_FALSE(site::isModelListBegin(base, base + 0x81808C)); // A different native list lane.
        CHECK_FALSE(site::isModelListBegin(base, base - 1));
    }
}

TEST_CASE("native model-list admission permits a private palette bind and retains it through the GPU fence") {
    namespace site = self_recall::gpu_lifetime::site;
    constexpr std::uintptr_t base = 0x8052B000;
    constexpr std::uintptr_t actualCaller = base + 0x96F390;
    Lifetime f;
    REQUIRE(site::isModelListBegin(base, actualCaller));
    REQUIRE(f.ledger.beginList(listCb, poolId));
    REQUIRE(f.ledger.bindSlot(listCb, 0));
    REQUIRE(f.ledger.endList(listCb));
    f.frame(101);
    REQUIRE(f.ledger.clearPool(poolId));
    CHECK_FALSE(f.ledger.canWrite(0));
    REQUIRE(f.ledger.beginSubmission(101));
    REQUIRE(f.ledger.submittedAndFenced(101, queue, sync));
    CHECK_FALSE(f.ledger.canWrite(0));
    const auto ticket = f.ledger.captureWait(queue, sync);
    CHECK_FALSE(f.ledger.completeWait(ticket, 2));
    CHECK_FALSE(f.ledger.canWrite(0));
    REQUIRE(f.ledger.completeWait(ticket, 1));
    CHECK(f.ledger.canWrite(0));
    CHECK_FALSE(f.ledger.failed());
}

TEST_CASE("all native layer-list return PCs are admitted with the model-list lane") {
    namespace site = self_recall::gpu_lifetime::site;
    constexpr std::uint32_t instructions[]{
        0x940004EF, 0x940004AF, 0x9400037D, 0x9400032D, 0x940001FF, 0x940000C8, 0x97FFFE21
    };
    constexpr std::uintptr_t base = 0x8052B000;
    for (unsigned i = 0; i < 7; ++i) {
        const auto pc = site::kLayerListBeginCalls[i];
        const auto word = instructions[i];
        REQUIRE((word & 0xFC000000u) == 0x94000000u);
        const auto offset = std::int64_t(word & 0x03FFFFFFu) - ((word & 0x02000000u) ? 0x04000000ll : 0ll);
        REQUIRE(std::int64_t(pc) + 4 * offset == 0x818884);
        CHECK(site::isRenderListBegin(base, base + pc + 4));
        CHECK_FALSE(site::isRenderListBegin(base, base + pc));
        CHECK_FALSE(site::isRenderListBegin(base, base + pc + 8));
    }
    CHECK(site::isRenderListBegin(base, base + 0x96F390));
    CHECK_FALSE(site::isRenderListBegin(base, base + 0x96F394));
    CHECK_FALSE(site::isRenderListBegin(base, base - 1));
}

TEST_CASE("native model to layer to retained display to root survives source pool reset") {
    Lifetime f;
    constexpr std::uintptr_t layerCb = 23, display = 12, copies = 0x4000;
    REQUIRE(f.ledger.configurePool(display, copies, 200, objectStride));
    for (unsigned frame = 1; frame <= 180; ++frame) {
        const unsigned slot = frame % 3;
        REQUIRE(f.ledger.canWrite(slot));
        f.record(slot);
        REQUIRE(f.ledger.beginList(layerCb, poolId));
        REQUIRE(f.ledger.callList(layerCb, listObjects)); // 05 froze at this untracked destination.
        REQUIRE(f.ledger.endList(layerCb));
        REQUIRE(f.ledger.clearPool(display));
        REQUIRE(f.ledger.copyListObject(listObjects + objectStride, copies));
        REQUIRE(f.ledger.beginFrame(rootCb));
        REQUIRE(f.ledger.callList(rootCb, copies));
        REQUIRE(f.ledger.sealFrame(rootCb, 100 + frame));
        REQUIRE(f.ledger.clearPool(poolId));
        CHECK_FALSE(f.ledger.canWrite(slot));
        REQUIRE(f.ledger.beginSubmission(100 + frame));
        REQUIRE(f.ledger.submittedAndFenced(100 + frame, queue, sync));
        REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
        CHECK_FALSE(f.ledger.canWrite(slot)); // Display still retains a callable copy.
        REQUIRE(f.ledger.clearPool(display));
        CHECK(f.ledger.canWrite(slot));
    }
    CHECK_FALSE(f.ledger.failed());
}

TEST_CASE("retained display copy can be drawn again after source lists and GPU work retire") {
    Lifetime f;
    REQUIRE(f.ledger.configurePool(12, 0x4000, 2, objectStride));
    f.record(1);
    REQUIRE(f.ledger.copyListObject(listObjects, 0x4000));
    REQUIRE(f.ledger.clearPool(poolId));
    for (unsigned i = 0; i < 3; ++i) {
        REQUIRE(f.ledger.beginFrame(rootCb));
        REQUIRE(f.ledger.callList(rootCb, 0x4000));
        REQUIRE(f.ledger.sealFrame(rootCb, 101 + i));
        REQUIRE(f.ledger.submittedAndFenced(101 + i, queue, sync));
        REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
        CHECK_FALSE(f.ledger.canWrite(1));
    }
    REQUIRE(f.ledger.clearPool(12));
    CHECK(f.ledger.canWrite(1));
}

TEST_CASE("alias corruption and missing intermediate ownership still fail closed") {
    Lifetime f;
    f.record(0);
    SUBCASE("unknown layer reproduces the 05 stop") { CHECK_FALSE(f.ledger.callList(999, listObjects)); }
    SUBCASE("private copy outside registered retained storage") {
        CHECK_FALSE(f.ledger.copyListObject(listObjects, 0x9000));
    }
    SUBCASE("misaligned retained object") {
        REQUIRE(f.ledger.configurePool(12, 0x4000, 2, objectStride));
        CHECK_FALSE(f.ledger.copyListObject(listObjects, 0x4001));
    }
    CHECK(f.ledger.failed());
    CHECK_FALSE(f.ledger.canWrite(0));
}

TEST_CASE("direct list commands need a later same-queue fence and successful wait") {
    Lifetime f;
    f.record(0);
    const auto mask = f.ledger.beginDirect(listObjects);
    REQUIRE(mask == 1);
    REQUIRE(f.ledger.endDirect(mask));
    REQUIRE(f.ledger.clearPool(poolId));
    CHECK_FALSE(f.ledger.canWrite(0));
    f.frame(101); // Empty root still places a fence after the direct list.
    const auto directTicket = f.ledger.captureDirectFence();
    REQUIRE(directTicket != 0);
    REQUIRE(f.ledger.submittedAndFenced(101, queue, sync, directTicket));
    CHECK_FALSE(f.ledger.canWrite(0));
    CHECK_FALSE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 2));
    REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
    CHECK(f.ledger.canWrite(0));
}

TEST_CASE("an in-flight direct submit cannot be covered by a racing fence") {
    Lifetime f;
    f.record(0);
    const auto mask = f.ledger.beginDirect(listObjects);
    REQUIRE(mask);
    const auto tooEarly = f.ledger.captureDirectFence();
    CHECK(tooEarly == 0);
    REQUIRE(f.ledger.endDirect(mask));
    REQUIRE(f.ledger.clearPool(poolId));
    f.frame(101);
    REQUIRE(f.ledger.submittedAndFenced(101, queue, sync, tooEarly));
    REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
    CHECK_FALSE(f.ledger.canWrite(0));
    f.frame(102);
    REQUIRE(f.ledger.submittedAndFenced(102, queue, sync, f.ledger.captureDirectFence()));
    REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
    CHECK(f.ledger.canWrite(0));
}

TEST_CASE("direct submit after a fence snapshot remains pinned after that fence completes") {
    Lifetime f;
    f.record(2);
    auto mask = f.ledger.beginDirect(listObjects);
    REQUIRE(f.ledger.endDirect(mask));
    const auto oldTicket = f.ledger.captureDirectFence();
    mask = f.ledger.beginDirect(listObjects);
    REQUIRE(f.ledger.endDirect(mask));
    REQUIRE(f.ledger.clearPool(poolId));
    f.frame(101);
    REQUIRE(f.ledger.submittedAndFenced(101, queue, sync, oldTicket));
    REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
    CHECK_FALSE(f.ledger.canWrite(2));
    f.frame(102);
    REQUIRE(f.ledger.submittedAndFenced(102, queue, sync, f.ledger.captureDirectFence()));
    REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
    CHECK(f.ledger.canWrite(2));
}
