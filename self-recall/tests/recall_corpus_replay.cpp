#include "RecallPoseData.hpp"
#include "RecallAppearance.hpp"
#include "RecallGear.hpp"
#include "RecallPlayback.hpp"
#include <chrono>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
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
void trimVisibility(std::uint32_t (&words)[kPoseBoneLimit / 32], unsigned count) {
    const auto complete = count / 32;
    const auto partial = count % 32;
    if (partial) words[complete] &= (1u << partial) - 1u;
    for (unsigned i = complete + unsigned(partial != 0); i < std::size(words); ++i) words[i] = 0;
}
void keepBodyOnly(RecordedPoseFrame& frame) {
    auto& header = frame.header;
    require(header.bodyModelCount && header.bodyModelCount <= header.modelCount, "invalid body roster");
    header.modelCount = header.bodyModelCount;
    const auto& last = frame.models[header.modelCount - 1];
    header.boneCount = static_cast<std::uint16_t>(last.identity.firstBone + last.identity.boneCount);
    header.materialCount = static_cast<std::uint16_t>(last.identity.firstMaterial + last.identity.materialCount);
    trimVisibility(frame.visible.bones, header.boneCount);
    trimVisibility(frame.visible.materials, header.materialCount);
}
// Storage-only stress: retain body bones and non-body skeletons through the measured 31-bone
// parasail; runtime selection uses unavailable binding-component labels rather than size.
void keepEquipmentBoneStress(RecordedPoseFrame& frame) {
    const auto original = frame;
    frame.visible = {};
    std::uint16_t models=0,bones=0,materials=0;
    for(unsigned i=0;i<original.header.modelCount;++i) {
        const auto& source=original.models[i];
        if(i>=original.header.bodyModelCount && source.identity.boneCount>31) continue;
        auto& target=frame.models[models++]; target=source;
        target.identity.firstBone=bones; target.identity.firstMaterial=materials;
        for(unsigned b=0;b<source.identity.boneCount;++b) {
            frame.bones[bones]=original.bones[source.identity.firstBone+b];
            if(visibilityBit(original.visible.bones,static_cast<std::uint16_t>(source.identity.firstBone+b))) frame.visible.bones[bones/32]|=1u<<(bones%32);
            ++bones;
        }
        for(unsigned m=0;m<source.identity.materialCount;++m) {
            if(visibilityBit(original.visible.materials,static_cast<std::uint16_t>(source.identity.firstMaterial+m))) frame.visible.materials[materials/32]|=1u<<(materials%32);
            ++materials;
        }
    }
    frame.header.modelCount=models; frame.header.boneCount=bones; frame.header.materialCount=materials;
}

void compare(const RecordedPoseFrame& expected, const RecordedPoseFrame& actual) {
    require(actual.header.key == expected.header.key, "key mismatch");
    require(std::memcmp(&actual.header, &expected.header, sizeof(actual.header)) == 0, "header mismatch");
    require(std::memcmp(actual.models, expected.models, expected.header.modelCount * sizeof(RecordedModelPose)) == 0, "model mismatch");
    require(std::memcmp(actual.bones, expected.bones, expected.header.boneCount * sizeof(RecordedBoneMatrix)) == 0, "bone mismatch");
    require(std::memcmp(&actual.visible, &expected.visible, sizeof(actual.visible)) == 0, "visibility mismatch");
}

#if SELF_RECALL_SD_HISTORY
// In-memory stand-in for the SD card file; the worker thread is replaced by explicit pumping.
class MemoryFile final : public SpillFile {
public:
    std::vector<std::byte> bytes = std::vector<std::byte>(kSpillFileBytes);
    std::uint64_t clock = 0;
    bool write(std::uint64_t offset, std::span<const std::byte> data) override {
        if (offset + data.size() > bytes.size()) return false;
        std::memcpy(bytes.data() + offset, data.data(), data.size());
        return true;
    }
    bool read(std::uint64_t offset, std::span<std::byte> data) override {
        if (offset + data.size() > bytes.size()) return false;
        std::memcpy(data.data(), bytes.data() + offset, data.size());
        return true;
    }
    std::uint64_t nanoseconds() override { return clock += 1000; }
};
#endif
}
int main(int argc, char** argv) {
    try {
        require(argc >= 2 && argc <= 5,
                "usage: recall_corpus_replay corpus.bin [--body-only|--equipment-bones] [--capacity-only] [--sd-stalls] [--pose-mib=N|--pose-kib=N]");
        bool bodyOnly = false, capacityOnly = false, equipmentBones = false, sdStalls = false;
        unsigned poseKiB = kPosePayloadArenaBytes / 1024u;
        for (int i = 2; i < argc; ++i) {
            const std::string_view argument{argv[i]};
            if (argument == "--body-only") bodyOnly = true;
            else if (argument == "--equipment-bones") equipmentBones = true;
            else if (argument == "--capacity-only") capacityOnly = true;
            else if (argument == "--sd-stalls") sdStalls = true;
            else if (argument.starts_with("--pose-mib=")) {
                char* end = nullptr;
                const auto value = std::strtoul(argv[i] + 11, &end, 10);
                require(end && !*end && value && value <= kPosePayloadArenaBytes / kMiB,
                        "invalid pose MiB");
                poseKiB = static_cast<unsigned>(value) * 1024u;
            } else if (argument.starts_with("--pose-kib=")) {
                char* end = nullptr;
                const auto value = std::strtoul(argv[i] + 11, &end, 10);
                require(end && !*end && value && value <= kPosePayloadArenaBytes / 1024u,
                        "invalid pose KiB");
                poseKiB = static_cast<unsigned>(value);
            } else require(false, "unknown option");
        }
        std::ifstream stream(argv[1], std::ios::binary); require(bool(stream), "open corpus");
        auto slots = std::make_unique<PoseHistorySlot[]>(kHistoryCapacity);
        const auto blockCount = poseKiB * 1024u / sizeof(PosePayloadBlock);
        auto blocks = std::make_unique<PosePayloadBlock[]>(blockCount);
#if SELF_RECALL_SD_HISTORY
        auto cacheBlocks = std::make_unique<PosePayloadBlock[]>(kSpillCacheBlockCount);
        auto groups = std::make_unique<SpillGroup[]>(kHistoryCapacity);
        std::vector<std::byte> ring(kSpillWriteRingBytes), io(kSpillMaxGroupBytes);
        auto spill = std::make_unique<PoseSpill>(std::span{cacheBlocks.get(), kSpillCacheBlockCount},
            std::span{groups.get(), kHistoryCapacity}, ring, io);
        spill->setEnabled(true);
        auto file = std::make_unique<MemoryFile>();
        auto history = std::make_unique<PoseHistory>(slots.get(), kHistoryCapacity,
            std::span{blocks.get(), blockCount}, spill.get(), std::uint64_t{kSdHistoryRamSeconds} * 1000000000ull);
        std::uint64_t recalls = 0, recallFrames = 0, rejected = 0, maxLoadPumps = 0, minRecallFrames = UINT64_MAX;
#else
        require(!sdStalls, "--sd-stalls needs an SD history build");
        auto history = std::make_unique<PoseHistory>(slots.get(), kHistoryCapacity,
            std::span{blocks.get(), blockCount});
#endif
        auto appearance = std::make_unique<CompressedAppearanceBlobs<kAppearanceBlockCount,kAppearanceStateCapacity>>();
        appearance->initialize();
        std::deque<RecordedPoseFrame> expected;
        std::uint64_t sequence = 0, poses = 0, checks = 0, appearances = 0, encodeNs = 0, maxEncodeNs = 0, decodeNs = 0, maxDecodeNs = 0;
        unsigned sourceGeneration = 0;
        const auto readPose = [&](const RecordedPoseFrame& frame) {
            if (capacityOnly) return;
            const auto start = std::chrono::steady_clock::now();
            auto lease = history->acquire(frame.header.key); require(bool(lease), "acquire failed");
            const auto ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());
            decodeNs += ns; maxDecodeNs = std::max(maxDecodeNs,ns);
            compare(frame, *lease.get()); ++checks;
        };
#if SELF_RECALL_SD_HISTORY
        // Recall walks newest to oldest, loading SD groups on demand, and compares every frame.
        const auto recallWalk = [&] {
            if (expected.empty()) return;
            const auto anchor = expected.back().header.key;
            require(history->count() == expected.size(), "history count differs from expected window");
            spill->setPlayback(true, anchor.generation, anchor.serial);
            for (std::uint32_t index = 0; index < expected.size(); ++index) {
                std::uint64_t pumps = 0;
                for (;;) {
                    const auto probe = history->spillAvailableThrough(anchor, index, index);
                    require(probe.blocked != SpillAvailability::Lost, "SD frame lost during recall");
                    if (probe.through == index && probe.blocked == SpillAvailability::Available) break;
                    require(++pumps < 4096 && bool(spill->pump(*file)), "SD load made no progress");
                }
                maxLoadPumps = std::max(maxLoadPumps, pumps);
                const auto& frame = expected[expected.size() - 1 - index];
                auto lease = history->before(anchor, index); require(bool(lease), "recall acquire failed");
                compare(frame, *lease.get()); ++checks;
                spill->setPlayback(true, anchor.generation, frame.header.key.serial);
            }
            spill->setPlayback(false, 0, 0);
            while (spill->pump(*file)) {}
            ++recalls; recallFrames += expected.size(); minRecallFrames = std::min<std::uint64_t>(minRecallFrames, expected.size());
        };
#endif
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
            if (equipmentBones) keepEquipmentBoneStress(frame);
            else if (bodyOnly) keepBodyOnly(frame);
            if (sourceGeneration != frame.header.key.generation) {
#if SELF_RECALL_SD_HISTORY
                recallWalk();
#else
                for (auto i=expected.rbegin();i!=expected.rend();++i) readPose(*i);
#endif
                history->clear();expected.clear();sourceGeneration=frame.header.key.generation;
            }
            PoseFrameInput input{frame.header,frame.models,frame.bones,frame.visible};
            const auto start = std::chrono::steady_clock::now();
            const auto result = history->record(input);
            const auto ns=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());
            encodeNs+=ns;maxEncodeNs=std::max(maxEncodeNs,ns);
#if SELF_RECALL_SD_HISTORY
            // Stall model: the card makes no progress for 3 s out of every 20 s of frames.
            const bool stalled = sdStalls && (poses % 600) >= 510;
            if (!stalled) while (spill->pump(*file)) {}
            if (result.status == PoseRecordStatus::StorageFull && sdStalls) { ++rejected; ++poses; continue; }
#endif
            if (result.status != PoseRecordStatus::Recorded) {
                std::cerr << "record refused: status=" << unsigned(result.status) << " poses=" << poses
                    << " models=" << frame.header.modelCount << " bones=" << frame.header.boneCount << '\n';
                require(false, "record refused");
            }
            frame.header.key=result.key;
            expected.push_back(frame);
            while (!expected.empty() && !history->contains(expected.front().header.key)) expected.pop_front();
            require(!expected.empty(), "empty recorded window");
#if SELF_RECALL_SD_HISTORY
            readPose(expected.back());
            if (!capacityOnly && poses % 900 == 899) recallWalk();
#else
            readPose(expected.back());readPose(expected.front());
            readPose(expected[(poses*137)%expected.size()]);
#endif
            ++poses;
        }
#if SELF_RECALL_SD_HISTORY
        while (spill->pump(*file)) {}
        if (!capacityOnly) recallWalk();
#else
        for (auto i=expected.rbegin();i!=expected.rend();++i) readPose(*i);
#endif
        history->clear();
        std::cout << "{\"poses\":"<<poses<<",\"exact_pose_reads\":"<<checks<<",\"appearance_round_trips\":"<<appearances
            <<",\"history_object_bytes\":"<<sizeof(PoseHistory)
            <<",\"appearance_object_bytes\":"<<sizeof(*appearance)<<",\"body_only\":"<<(bodyOnly ? "true" : "false")
            <<",\"equipment_bone_stress\":"<<(equipmentBones ? "true" : "false")
            <<",\"pose_pool_bytes\":"<<blockCount*sizeof(PosePayloadBlock)
            <<",\"desktop_encode_mean_ns\":"<<encodeNs/poses
            <<",\"desktop_encode_max_ns\":"<<maxEncodeNs;
#if SELF_RECALL_SD_HISTORY
        const auto& stats = spill->stats();
        std::cout << ",\"sd_ram_seconds\":"<<kSdHistoryRamSeconds<<",\"sd_stalls\":"<<(sdStalls ? "true" : "false")
            <<",\"sd_recalls\":"<<recalls<<",\"sd_recall_frames\":"<<recallFrames
            <<",\"sd_min_recall_frames\":"<<(recalls ? minRecallFrames : 0)
            <<",\"sd_writes\":"<<stats.writes.load()<<",\"sd_write_bytes\":"<<stats.writeBytes.load()
            <<",\"sd_reads\":"<<stats.reads.load()<<",\"sd_released\":"<<stats.released.load()
            <<",\"sd_ram_only\":"<<stats.ramOnly.load()<<",\"sd_lost\":"<<stats.lost.load()<<",\"sd_overwritten\":"<<stats.overwritten.load()
            <<",\"sd_trimmed\":"<<stats.trimmedForSpace.load()<<",\"sd_rejected\":"<<rejected
            <<",\"sd_failures\":"<<stats.failures.load()<<",\"sd_max_load_pumps\":"<<maxLoadPumps;
#endif
        if (!capacityOnly)
            std::cout << ",\"desktop_decode_mean_ns\":"<<decodeNs/checks
                      <<",\"desktop_decode_max_ns\":"<<maxDecodeNs;
        std::cout << "}\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
