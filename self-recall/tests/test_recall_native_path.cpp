#include <limits>
#include <memory>
#include "RecallPathFrame.hpp"
#include "RecallRenderFrames.hpp"
#include "doctest.h"

using namespace self_recall::pure;

namespace {
NativePathRoute route() {
    NativePathRoute result;
    result.anchor = {105, 7, 5};
    result.world = 2;
    result.historyCount = 6;
    result.newestNanoseconds = 1000;
    result.oldestNanoseconds = 700;
    result.route.count = 4;
    result.route.points[0] = {{5, 1, 0}, 5};
    result.route.points[1] = {{3, 8, 0}, 3};
    result.route.points[2] = {{1, 2, 0}, 1};
    result.route.points[3] = {{0, 0, 0}, 0};
    result.phases[0] = 0;
    result.phases[1] = 1.0f / 6;
    result.phases[2] = 2.0f / 3;
    result.phases[3] = 1;
    return result;
}
PoseFrameHeader selected() {
    PoseFrameHeader result;
    result.key = {103, 7, 3};
    result.worldGeneration = 2;
    result.elapsedNanoseconds = 950;
    result.route.flags = SampleAdmissible;
    result.route.pose.position = {3, 8, 0};
    return result;
}
}

TEST_CASE("native ribbon trims against the latched animation key and begins at its recorded root") {
    const auto source = route();
    auto header = selected();
    NativePathFrame out;
    REQUIRE(buildNativePathFrame(out, source, header, 50) == NativePathStatus::Ready);
    CHECK(out.key == header.key);
    CHECK(out.epoch == 50);
    REQUIRE(out.count == 3);
    CHECK(out.points[0].position.y == 8);
    CHECK(out.points[0].phase == doctest::Approx(1.0 / 6));
    CHECK(out.points[1].position.x == 1);
    CHECK(out.points[1].phase == doctest::Approx(2.0 / 3));
    CHECK(out.points[2].phase == 1);
    header.key.serial = 102;
    header.elapsedNanoseconds = 900;
    header.route.pose.position = {2, 4, 1};
    REQUIRE(buildNativePathFrame(out, source, header, 51) == NativePathStatus::Ready);
    CHECK(out.points[0].position.z == 1);
    CHECK(out.points[0].phase == doctest::Approx(1.0 / 3));
    header.key.serial = 100;
    header.elapsedNanoseconds = 700;
    header.route.pose.position = {0, 0, 0};
    CHECK(buildNativePathFrame(out, source, header, 52) == NativePathStatus::Empty);
    CHECK(out.count == 1);
}

TEST_CASE("native ribbon rejects stale routes and malformed geometry without publishing a partial path") {
    auto source = route();
    auto header = selected();
    NativePathFrame out;
    SUBCASE("different history generation") { ++header.key.generation; }
    SUBCASE("different world") { ++header.worldGeneration; }
    SUBCASE("future key") { header.key.serial = 106; }
    SUBCASE("expired key") { header.key.serial = 99; }
    SUBCASE("bad capacity") { source.route.count = kMaxRenderPoints + 1; }
    SUBCASE("unordered vertex") { source.route.points[2].ordinal = 4; }
    SUBCASE("NaN vertex") { source.route.points[2].position.z = std::numeric_limits<float>::quiet_NaN(); }
    SUBCASE("infinite root") { header.route.pose.position.x = std::numeric_limits<float>::infinity(); }
    SUBCASE("unsafe frame") { header.route.flags = 0; }
    SUBCASE("expired time") { header.elapsedNanoseconds = 699; }
    SUBCASE("key and timestamp disagree") { header.elapsedNanoseconds = 750; }
    SUBCASE("reversed UV time") { source.phases[2] = 0; }
    SUBCASE("invalid UV time") { source.phases[2] = std::numeric_limits<float>::infinity(); }
    const auto status = buildNativePathFrame(out, source, header, 50);
    CHECK(status != NativePathStatus::Ready);
    CHECK(status != NativePathStatus::Empty);
    CHECK(out.count == 0);
    CHECK_FALSE(out.key);
    CHECK(out.epoch == 0);
}

TEST_CASE("swimming ribbon uses the recorded water surface while the player keeps the submerged root") {
    auto header = selected();
    header.haveWaterHeight = true;
    header.waterHeight = 9;
    const auto original = header.route.pose.position;
    CHECK(recallTrailPosition(header).y == doctest::Approx(9.03f));
    NativePathFrame output;
    REQUIRE(buildNativePathFrame(output, route(), header, 51) == NativePathStatus::Ready);
    CHECK(output.points[0].position.y == doctest::Approx(9.03f));
    CHECK(header.route.pose.position.y == original.y);
    header.haveWaterHeight = false;
    CHECK(recallTrailPosition(header).y == 8);
    header.haveWaterHeight = true;
    header.waterHeight = 7; // Diving above the water keeps the aerial route.
    CHECK(recallTrailPosition(header).y == 8);
    header.waterHeight = 12; // Unrelated/deep surface cannot project the route.
    CHECK(recallTrailPosition(header).y == 8);
    header.waterHeight = std::numeric_limits<float>::quiet_NaN();
    CHECK(recallTrailPosition(header).y == 8);
}

TEST_CASE("stationary historical animation needs no artificial ribbon segment") {
    auto source = route();
    auto header = selected();
    for (unsigned i = 0; i < source.route.count; ++i) source.route.points[i].position = {3, 8, 0};
    NativePathFrame out;
    CHECK(buildNativePathFrame(out, source, header, 50) == NativePathStatus::Empty);
    CHECK(out.count == 1);
}

TEST_CASE("native path reads the requested render epoch without requiring a wrist or borrowing the playback cursor") {
    auto recorded = std::make_unique<RecordedPoseFrame>();
    auto store = std::make_unique<RenderFrameStore>();
    recorded->header = selected();
    recorded->header.modelCount = 1;
    REQUIRE(store->begin(*recorded, 101) == RenderPrepareStatus::Ready);
    --recorded->header.key.serial;
    recorded->header.route.pose.position.y = 4;
    REQUIRE(store->begin(*recorded, 102) == RenderPrepareStatus::Ready);
    PoseFrameHeader header;
    REQUIRE(store->copyHeader(101, 7, header));
    CHECK(header.key.serial == 103);
    CHECK(header.route.pose.position.y == 8);
    REQUIRE(store->copyHeader(102, 7, header));
    CHECK(header.key.serial == 102);
    CHECK(header.route.pose.position.y == 4);
    CHECK_FALSE(store->copyHeader(102, 8, header));
    CHECK_FALSE(store->copyHeader(103, 7, header));
}
