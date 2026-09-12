#include "RecallPoseData.hpp"
#include "RecallAppearance.hpp"
#include <chrono>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace self_recall::pure;
namespace {
enum class CorpusKind : std::uint32_t { Pose = 1, Appearance = 2 };
struct CorpusRecordHeader {
    std::uint32_t magic = 0, kind = 0, bytes = 0, checksum = 0;
    std::uint64_t sequence = 0, time = 0;
};
static_assert(sizeof(CorpusRecordHeader) == 32);

struct LegacyHistorySampleV108 {
    Pose pose{};
    float engineVelocity[3]{};
    float pathSpeed = 0;
    std::uint64_t recordedTick = 0;
    std::uint32_t serial = 0;
    std::uint16_t tickDelta = 0;
    std::uint8_t flags = 0;
    std::uint8_t animKind = 0;
    std::uint8_t animSlot = 0;
    float animFrame = 0;
    float animRate = 0;
    std::int32_t stickX = 0;
    std::int32_t stickY = 0;
};
static_assert(sizeof(LegacyHistorySampleV108) == 104);

struct LegacyPoseFrameHeaderV108 {
    PoseFrameKey key{};
    std::uint64_t frameEpoch = 0;
    std::uint64_t elapsedNanoseconds = 0;
    std::uint32_t worldGeneration = 0;
    std::uint32_t modelGeneration = 0;
    LegacyHistorySampleV108 route{};
    float wristMatrix[12]{};
    std::uint16_t modelCount = 0;
    std::uint16_t boneCount = 0;
    std::uint16_t materialCount = 0;
    bool haveWrist = false;
    std::uint8_t bodyModelCount = 0;
    std::uint8_t reserved[8]{};
    float waterHeight = 0;
    bool haveWaterHeight = false;
    std::uint8_t waterPadding[11]{};
};
static_assert(sizeof(LegacyPoseFrameHeaderV108) == 224);

PoseFrameHeader adapt(const LegacyPoseFrameHeaderV108& legacy) {
    PoseFrameHeader current{};
    current.key = legacy.key;
    current.frameEpoch = legacy.frameEpoch;
    current.elapsedNanoseconds = legacy.elapsedNanoseconds;
    current.worldGeneration = legacy.worldGeneration;
    current.modelGeneration = legacy.modelGeneration;
    current.route.pose = legacy.route.pose;
    std::memcpy(current.route.engineVelocity, legacy.route.engineVelocity,
                sizeof(current.route.engineVelocity));
    current.route.pathSpeed = legacy.route.pathSpeed;
    current.route.flags = legacy.route.flags;
    std::memcpy(current.wristMatrix, legacy.wristMatrix, sizeof(current.wristMatrix));
    current.modelCount = legacy.modelCount;
    current.boneCount = legacy.boneCount;
    current.materialCount = legacy.materialCount;
    current.haveWrist = legacy.haveWrist;
    current.bodyModelCount = legacy.bodyModelCount;
    std::memcpy(current.reserved, legacy.reserved, sizeof(current.reserved));
    current.waterHeight = legacy.waterHeight;
    current.haveWaterHeight = legacy.haveWaterHeight;
    std::memcpy(current.waterPadding, legacy.waterPadding, sizeof(current.waterPadding));
    return current;
}

std::uint32_t corpusChecksum(std::span<const std::byte> bytes) {
    std::uint32_t value = 2166136261u;
    for (auto byte : bytes) value = (value ^ std::to_integer<std::uint8_t>(byte)) * 16777619u;
    return value;
}
void require(bool pass, const char* message) { if (!pass) throw std::runtime_error(message); }
void compare(const RecordedPoseFrame& expected, const RecordedPoseFrame& actual) {
    require(actual.header.key == expected.header.key, "key mismatch");
    require(std::memcmp(&actual.header, &expected.header, sizeof(actual.header)) == 0, "header mismatch");
    require(std::memcmp(actual.models, expected.models, expected.header.modelCount * sizeof(RecordedModelPose)) == 0, "model mismatch");
    require(std::memcmp(actual.bones, expected.bones, expected.header.boneCount * sizeof(RecordedBoneMatrix)) == 0, "bone mismatch");
    require(std::memcmp(&actual.visible, &expected.visible, sizeof(actual.visible)) == 0, "visibility mismatch");
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "usage: recall_corpus_replay corpus.bin");
        std::ifstream stream(argv[1], std::ios::binary); require(bool(stream), "open corpus");
        auto slots = std::make_unique<PoseHistorySlot[]>(kHistoryCapacity);
        auto blocks = std::make_unique<PosePayloadBlock[]>(kPosePayloadBlockCount);
        auto history = std::make_unique<PoseHistory>(slots.get(), kHistoryCapacity,
            std::span{blocks.get(), kPosePayloadBlockCount});
        auto appearance = std::make_unique<CompressedAppearanceBlobs<kAppearanceBlockCount,kAppearanceStateCapacity>>();
        appearance->initialize();
        std::deque<RecordedPoseFrame> expected;
        std::uint64_t sequence = 0, poses = 0, checks = 0, appearances = 0, encodeNs = 0, maxEncodeNs = 0, decodeNs = 0, maxDecodeNs = 0;
        unsigned sourceGeneration = 0;
        const auto readPose = [&](const RecordedPoseFrame& frame) {
            const auto start = std::chrono::steady_clock::now();
            auto lease = history->acquire(frame.header.key); require(bool(lease), "acquire failed");
            const auto ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());
            decodeNs += ns; maxDecodeNs = std::max(maxDecodeNs,ns);
            compare(frame, *lease.get()); ++checks;
        };
        while (stream) {
            CorpusRecordHeader record;
            stream.read(reinterpret_cast<char*>(&record), sizeof(record));
            if (!stream || !record.magic) break;
            require(record.magic == 0x31524353 && record.sequence == ++sequence && record.bytes <= 68*1024, "invalid corpus header");
            std::vector<std::byte> payload(record.bytes);
            stream.read(reinterpret_cast<char*>(payload.data()), payload.size());
            require(bool(stream) && corpusChecksum(payload) == record.checksum, "corrupt corpus record");
            if (record.kind == unsigned(CorpusKind::Appearance)) {
                require(payload.size() > 4, "appearance layout");
                const auto source = std::span{payload}.subspan(4);
                auto token = appearance->create(source); require(token != 0, "appearance allocation");
                std::vector<std::byte> restored(source.size());
                require(appearance->copy(token, restored) && std::equal(source.begin(),source.end(),restored.begin()), "appearance bytes differ");
                require(appearance->equal(token, source), "appearance equality");
                appearance->release(token); ++appearances;
            }
            if (record.kind != unsigned(CorpusKind::Pose)) continue;
            unsigned layout[4]; require(payload.size() >= sizeof(layout), "pose layout");
            std::memcpy(layout, payload.data(), sizeof(layout));
            require((layout[0] == sizeof(PoseFrameHeader) ||
                     layout[0] == sizeof(LegacyPoseFrameHeaderV108)) &&
                    layout[1] <= kPoseModelLimit && layout[2] <= kPoseBoneLimit &&
                    layout[3] == sizeof(RecordedVisibility), "pose ABI differs");
            RecordedPoseFrame frame{};
            auto* cursor = payload.data() + sizeof(layout);
            if (layout[0] == sizeof(LegacyPoseFrameHeaderV108)) {
                LegacyPoseFrameHeaderV108 legacy;
                std::memcpy(&legacy, cursor, sizeof(legacy));
                frame.header = adapt(legacy);
            } else {
                std::memcpy(&frame.header, cursor, sizeof(frame.header));
            }
            cursor += layout[0];
            require(payload.size() == sizeof(layout)+layout[0]+layout[1]*sizeof(RecordedModelPose)+layout[2]*sizeof(RecordedBoneMatrix)+sizeof(frame.visible), "pose payload size");
            std::memcpy(frame.models,cursor,layout[1]*sizeof(RecordedModelPose));cursor+=layout[1]*sizeof(RecordedModelPose);
            std::memcpy(frame.bones,cursor,layout[2]*sizeof(RecordedBoneMatrix));cursor+=layout[2]*sizeof(RecordedBoneMatrix);
            std::memcpy(&frame.visible,cursor,sizeof(frame.visible));
            if (sourceGeneration != frame.header.key.generation) {
                for (auto i=expected.rbegin();i!=expected.rend();++i) readPose(*i);
                history->clear();expected.clear();sourceGeneration=frame.header.key.generation;
            }
            PoseFrameInput input{frame.header,frame.models,frame.bones,frame.visible};
            const auto start = std::chrono::steady_clock::now();
            const auto result = history->record(input);
            const auto ns=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());
            encodeNs+=ns;maxEncodeNs=std::max(maxEncodeNs,ns);
            require(result.status == PoseRecordStatus::Recorded, "record refused");
            frame.header.key=result.key;
            expected.push_back(frame);
            while (!expected.empty() && !history->contains(expected.front().header.key)) expected.pop_front();
            require(!expected.empty(), "empty recorded window");
            readPose(expected.back());readPose(expected.front());
            readPose(expected[(poses*137)%expected.size()]);
            ++poses;
        }
        for (auto i=expected.rbegin();i!=expected.rend();++i) readPose(*i);
        history->clear();
        std::cout << "{\"poses\":"<<poses<<",\"exact_pose_reads\":"<<checks<<",\"appearance_round_trips\":"<<appearances
            <<",\"history_object_bytes\":"<<sizeof(PoseHistory)
            <<",\"appearance_object_bytes\":"<<sizeof(*appearance)<<",\"desktop_encode_mean_ns\":"<<encodeNs/poses
            <<",\"desktop_encode_max_ns\":"<<maxEncodeNs<<",\"desktop_decode_mean_ns\":"<<decodeNs/checks
            <<",\"desktop_decode_max_ns\":"<<maxDecodeNs<<"}\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
