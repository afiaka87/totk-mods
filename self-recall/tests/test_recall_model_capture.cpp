#include <array>
#include <memory>

#include "RecallModelView.hpp"
#include "doctest.h"

namespace pure = self_recall::pure;
namespace model = self_recall::model;

TEST_CASE("completed model capture requires the whole roster and owns the copied bytes") {
    constexpr std::array<std::uint16_t, 5> counts{75, 47, 16, 31, 1};
    auto source = std::make_unique<pure::RecordedBoneMatrix[]>(170);
    auto workspace = std::make_unique<model::CaptureWorkspace>();
    auto storage = std::make_unique<pure::PoseHistorySlot[]>(2);
    std::unique_ptr<pure::PosePayloadBlock[]> payload = std::make_unique<pure::PosePayloadBlock[]>(2 * 36);
    pure::PoseHistory history{storage.get(), 2, {payload.get(), 2 * 36}};
    std::array<model::View, 5> views{};
    std::array<std::array<std::uint32_t, 3>, 5> boneVisible{};
    std::array<std::array<std::uint32_t, 2>, 5> materialVisible{};
    constexpr std::array<std::uint16_t, 5> materials{35, 9, 0, 2, 1};
    std::uint16_t offset = 0;
    for (std::size_t i = 0; i < views.size(); ++i) {
        views[i].identity = {100 + i, 200 + i, 300 + i, counts[i], materials[i]};
        views[i].boneBytes = source.get() + offset;
        views[i].boneVisibility = boneVisible[i].data();
        views[i].materialVisibility = materialVisible[i].data();
        boneVisible[i][0] = 1;
        materialVisible[i][0] = 1;
        views[i].pose.visibility = static_cast<std::uint32_t>(i);
        offset = static_cast<std::uint16_t>(offset + counts[i]);
    }
    views[0].wristIndex = 3;
    source[3].words[12] = 0x3f800000u;
    pure::PoseFrameHeader header{};
    header.worldGeneration = 1;
    header.modelGeneration = 1;
    header.frameEpoch = 42;
    header.elapsedNanoseconds = 700000000;
    header.modelCount = 5;
    header.boneCount = 170;
    header.materialCount = 47;
    boneVisible[0][2] = 1u << 10; // body bone 74
    materialVisible[0][1] = 1u << 2; // body material 34

    CHECK(model::recordCompleted(header, std::span(views).first(4), *workspace, history).status ==
          model::CaptureStatus::ModelCountMismatch);
    CHECK(history.count() == 0);
    views[4].boneBytes = nullptr;
    CHECK(model::recordCompleted(header, views, *workspace, history).status ==
          model::CaptureStatus::BoneRangeMismatch);
    views[4].boneBytes = source.get() + 169;
    views[4].boneVisibility = nullptr;
    CHECK(model::recordCompleted(header, views, *workspace, history).status ==
          model::CaptureStatus::MissingVisibility);
    views[4].boneVisibility = boneVisible[4].data();
    views[0].wristIndex = -1;
    CHECK(model::recordCompleted(header, views, *workspace, history).status ==
          model::CaptureStatus::MissingWrist);
    views[0].wristIndex = 3;
    const auto report = model::recordCompleted(header, views, *workspace, history);
    REQUIRE(report.status == model::CaptureStatus::Recorded);
    auto frame = history.acquire(report.history.key);
    REQUIRE(frame);
    CHECK(frame.get()->header.wristMatrix[3] == 1);
    CHECK(frame.get()->models[3].identity.firstBone == 138);
    CHECK(frame.get()->models[3].identity.boneCount == 31);
    CHECK(frame.get()->models[3].identity.unit == 103);
    CHECK(frame.get()->models[4].identity.firstBone == 169);
    CHECK(frame.get()->models[1].identity.firstMaterial == 35);
    CHECK(frame.get()->models[3].identity.firstMaterial == 44);
    CHECK(pure::visibilityBit(frame.get()->visible.bones, 74));
    CHECK(pure::visibilityBit(frame.get()->visible.bones, 75));
    CHECK_FALSE(pure::visibilityBit(frame.get()->visible.bones, 76));
    CHECK(pure::visibilityBit(frame.get()->visible.bones, 169));
    CHECK(pure::visibilityBit(frame.get()->visible.materials, 34));
    CHECK(pure::visibilityBit(frame.get()->visible.materials, 35));
    CHECK(pure::visibilityBit(frame.get()->visible.materials, 44));
    CHECK_FALSE(pure::visibilityBit(frame.get()->visible.materials, 45));
    boneVisible[0][2] = 0;
    materialVisible[0][1] = 0;
    CHECK(pure::visibilityBit(frame.get()->visible.bones, 74));
    CHECK(pure::visibilityBit(frame.get()->visible.materials, 34));
    source[3].words[12] = 0;
    views[3].identity.unit = 999;
    CHECK(frame.get()->bones[3].words[12] == 0x3f800000u);
    CHECK(frame.get()->models[3].identity.unit == 103);
    views[3].identity.unit = 103;
    CHECK(model::recordCompleted(header, views, *workspace, history).history.status ==
          pure::PoseRecordStatus::DuplicateFrame);
    frame.release();
    ++header.frameEpoch;
    header.elapsedNanoseconds += 16666667;
    ++views[4].identity.materialCount;
    ++header.materialCount;
    const auto replaced = model::recordCompleted(header, views, *workspace, history);
    REQUIRE(replaced.status == model::CaptureStatus::Recorded);
    CHECK(replaced.history.key.generation != report.history.key.generation);
    CHECK_FALSE(history.acquire(report.history.key));
    CHECK(history.count() == 1);
    ++header.materialCount;
    CHECK(model::recordCompleted(header, views, *workspace, history).status ==
          model::CaptureStatus::MaterialRangeMismatch);
}
