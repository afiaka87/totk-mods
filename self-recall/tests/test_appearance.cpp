#include "RecallAppearance.hpp"
#include "RecallBase.hpp"
#include "PoseTestSupport.hpp"
#include "doctest.h"
#include <thread>
#include "RecallEffectsEngine.hpp"
#include <memory>
#include <vector>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include "RecallVisual.hpp"

namespace self_recall_tests::test_recall_appearance {
using namespace self_recall::pure;

namespace {
template<class Blobs>
bool readRawBlob(const Blobs& blobs, unsigned token, std::span<std::byte> output) {
    auto reader = blobs.reader(token);
    return reader.remaining() == output.size() && reader.copy(output) && !reader.remaining();
}
constexpr auto retireAll = [](PoseFrameKey) { return true; };
}

TEST_CASE("historical body materials follow frame tokens while live clothing parameters survive uploads") {
    AppearanceBlobs<16, 8, 32> blobs;
    blobs.initialize();
    AppearanceFrames<3, 1> frames;
    std::array<std::byte, 70> bare{}, shirt{}, live{}, upload{}, scratch{};
    bare.fill(std::byte{0x26}); shirt.fill(std::byte{0x91});
    bare[35] = std::byte{0}; shirt[35] = std::byte{1};
    auto first = blobs.create(bare), second = blobs.create(shirt);
    const std::array<unsigned, 1> bareTokens{first}, shirtTokens{second};
    const PoseFrameKey older{1, 1, 0}, newer{2, 1, 1};
    REQUIRE(frames.bind(blobs, older, bareTokens));
    REQUIRE(frames.bind(blobs, newer, shirtTokens));
    blobs.release(first); blobs.release(second);
    for (const auto current : {bare, shirt}) {
        live = current;
        for (const auto key : {newer, older, newer}) {
            const auto token = frames.token(key, 0);
            REQUIRE(readRawBlob(blobs, token, scratch));
            const auto expected = scratch;
            REQUIRE(exchangeParameterBytes(live, scratch));
            upload = live;
            CHECK(upload == expected);
            REQUIRE(exchangeParameterBytes(live, scratch));
            CHECK(live == current);
            CHECK(blobs.equal(token, scratch));
        }
    }
    const auto before = live;
    CHECK_FALSE(exchangeParameterBytes(live, std::span{scratch}.first(69)));
    CHECK(live == before);
    frames.collect(blobs, retireAll);
    CHECK(frames.token(older, 0) == 0);
    CHECK(blobs.availableBytes() == 512);
}

TEST_CASE("controller commit writes a complete interleaved matrix and only four velocity vectors") {
    std::array<float, 0x380 / sizeof(float)> actor;
    actor.fill(71.0f);
    std::array<float, 14> matrix;
    matrix.fill(89.0f);
    ControllerPoseOutput output{matrix.data() + 1, {actor.data() + 0x320 / 4,
        actor.data() + 0x32C / 4, actor.data() + 0x338 / 4, actor.data() + 0x344 / 4}};
    REQUIRE(output.playerCommit(reinterpret_cast<std::uintptr_t>(actor.data())));
    CHECK_FALSE(output.playerCommit(reinterpret_cast<std::uintptr_t>(actor.data()) + 4));
    CHECK_FALSE(output.playerCommit(0));
    Pose pose{};
    pose.rotation.values[1] = -1; pose.rotation.values[3] = 1; pose.rotation.values[8] = 1;
    pose.position = {1930.25f, 1362.5f, -1200.75f};
    const auto before = pose;
    REQUIRE(output.apply(pose));
    const std::array<float, 12> expected{0, -1, 0, 1930.25f, 1, 0, 0, 1362.5f, 0, 0, 1, -1200.75f};
    for (unsigned i = 0; i < expected.size(); ++i) CHECK(matrix[i + 1] == expected[i]);
    CHECK(matrix.front() == 89.0f);
    CHECK(matrix.back() == 89.0f);
    CHECK(std::memcmp(&pose, &before, sizeof(pose)) == 0);
    for (unsigned i = 0; i < actor.size(); ++i)
        CHECK(actor[i] == (i >= 0x320 / 4 && i < 0x350 / 4 ? 0.0f : 71.0f));
    for (unsigned i = 0; i < output.velocities.size(); ++i) {
        auto wrong = output;
        ++wrong.velocities[i];
        CHECK_FALSE(wrong.playerCommit(reinterpret_cast<std::uintptr_t>(actor.data())));
    }
}

TEST_CASE("invalid controller output or recorded pose leaves native results intact") {
    std::array<float, 12> matrix; matrix.fill(9.0f);
    std::array<float, 12> velocity; velocity.fill(7.0f);
    const auto originalMatrix = matrix, originalVelocity = velocity;
    ControllerPoseOutput output{matrix.data(), {velocity.data(), velocity.data() + 3,
        velocity.data() + 6, velocity.data() + 9}};
    Pose invalid{};
    invalid.position.y = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(output.apply(invalid));
    invalid.position.y = 0;
    invalid.rotation.values[8] = std::numeric_limits<float>::infinity();
    CHECK_FALSE(output.apply(invalid));
    for (unsigned i = 0; i < output.velocities.size(); ++i) {
        auto absent = output;
        absent.velocities[i] = nullptr;
        CHECK_FALSE(absent.apply(Pose{}));
    }
    auto absent = output; absent.matrix = nullptr;
    CHECK_FALSE(absent.apply(Pose{}));
    CHECK(matrix == originalMatrix);
    CHECK(velocity == originalVelocity);
}

TEST_CASE("climb pose publication cannot be blocked by an actor reader or mix applied samples") {
    AppliedClimbMailbox mailbox;
    AppliedClimb copied;
    CHECK_FALSE(mailbox.snapshot(copied));
    std::atomic<bool> done{false};
    std::atomic<unsigned> mixed{0};
    std::thread reader([&] {
        while (!done.load()) {
            AppliedClimb value;
            if (mailbox.snapshot(value) &&
                (value.player != value.world || value.actorId != value.world ||
                 value.sample.pose.position.x != static_cast<float>(value.world))) ++mixed;
        }
    });
    for (unsigned i = 1; i <= 10000; ++i) {
        AppliedClimb value;
        value.player = value.actorId = value.world = i;
        value.sample.pose.position.x = static_cast<float>(i);
        mailbox.publish(value);
    }
    done.store(true);
    reader.join();
    CHECK(mixed.load() == 0);
    REQUIRE(mailbox.snapshot(copied));
    CHECK(copied.world == 10000);
    CHECK(copied.sample.pose.position.x == 10000);
}

TEST_CASE("equipment appearance survives source changes, frame replacement and owner retirement") {
    AppearanceBlobs<8, 8, 8> blobs;
    AppearanceFrames<2, 2> frames;
    blobs.initialize();
    std::array<std::byte, 13> material{};
    material[0] = std::byte{1};
    material[12] = std::byte{255};
    const auto glowing = blobs.create(material);
    REQUIRE(glowing);
    REQUIRE(frames.bind(blobs, {1, 1, 0}, std::array<unsigned, 2>{0, glowing}));
    CHECK(blobs.equal(glowing, material));
    material[0] = material[12] = std::byte{0};
    const auto removed = blobs.create(material);
    REQUIRE(removed);
    REQUIRE(frames.bind(blobs, {2, 1, 1}, std::array<unsigned, 2>{0, removed}));
    blobs.release(glowing);
    blobs.release(removed);
    std::array<std::byte, 13> recalled{};
    REQUIRE(readRawBlob(blobs, frames.token({1, 1, 0}, 1), recalled));
    CHECK(recalled[0] == std::byte{1});
    CHECK(recalled[12] == std::byte{255});
    REQUIRE(readRawBlob(blobs, frames.token({2, 1, 1}, 1), recalled));
    CHECK(recalled == material);
    REQUIRE(frames.bind(blobs, {3, 1, 0}, std::array<unsigned, 2>{0, glowing}));
    CHECK(frames.token({1, 1, 0}, 1) == 0);
    CHECK(frames.token({3, 2, 0}, 1) == 0);
    CHECK(frames.token({3, 1, 0}, 1) == glowing);
    frames.collect(blobs, retireAll);
    CHECK(blobs.availableBytes() == 64);
    CHECK(blobs.size(glowing) == 0);
    CHECK(blobs.size(removed) == 0);
}

TEST_CASE("appearance exhaustion and invalid frame publication leave retained history intact") {
    AppearanceBlobs<3, 3, 8> blobs;
    AppearanceFrames<2, 2> frames;
    blobs.initialize();
    const std::array<std::byte, 17> material{std::byte{23}};
    const auto token = blobs.create(material);
    REQUIRE(token);
    REQUIRE(frames.bind(blobs, {1, 1, 0}, std::array<unsigned, 2>{token, token}));
    blobs.release(token);
    CHECK(blobs.create(material) == 0);
    CHECK_FALSE(frames.bind(blobs, {2, 1, 0}, std::array<unsigned, 2>{token, 3}));
    CHECK(frames.token({1, 1, 0}, 0) == token);
    CHECK(blobs.equal(token, material));
    frames.collect(blobs, retireAll);
    CHECK(blobs.availableBytes() == 24);
    CHECK(blobs.create(material) != 0);
}

TEST_CASE("applied pose ownership accepts an admissible route and rejects foreign or invalid state") {
    AppliedClimb applied;
    applied.player = 0x1234; applied.actorId = 42; applied.world = 7;
    applied.sample.flags = SampleAdmissible;
    REQUIRE(matchesAppliedPose(applied, 0x1234, 42, 7));
    CHECK_FALSE(matchesAppliedPose(applied, 0x1235, 42, 7));
    CHECK_FALSE(matchesAppliedPose(applied, 0x1234, 43, 7));
    CHECK_FALSE(matchesAppliedPose(applied, 0x1234, 42, 8));
    applied.sample.flags = 0;
    CHECK_FALSE(matchesAppliedPose(applied, 0x1234, 42, 7));
    applied.sample.flags = SampleAdmissible;
    applied.sample.pose.position.x = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(matchesAppliedPose(applied, 0x1234, 42, 7));
}
}

namespace self_recall_tests::test_recall_lossless {
using namespace self_recall::pure;

TEST_CASE("expired appearance survives a pinned pose and is released before slot reuse") {
    PoseTestStorage storage{8, 8};
    auto* history = &storage.history;
    PoseTestInput source;
    source.models[0].identity = {1, 2, 3, 0, 1, 0, 0};
    auto& input = source.value;
    input.header.frameEpoch = 1; input.header.elapsedNanoseconds = 1;
    const auto first = history->record(input);
    REQUIRE(first.status == PoseRecordStatus::Recorded);
    auto pinned = history->acquire(first.key); REQUIRE(pinned);
    AppearanceBlobs<4, 4, 16> blobs; blobs.initialize();
    AppearanceFrames<8, 1> frames;
    const std::array<std::byte, 16> material{std::byte{0x81}};
    const auto token = blobs.create(material);
    REQUIRE(frames.bind(blobs, first.key, std::array<unsigned, 1>{token}));
    blobs.release(token);
    input.header.frameEpoch = 2; input.header.elapsedNanoseconds = kRecallWindowNanoseconds + 2;
    REQUIRE(history->record(input).status == PoseRecordStatus::Recorded);
    const auto retired = [&](auto key) { return history->canReleaseAppearance(key); };
    CHECK_FALSE(history->contains(first.key));
    frames.collect(blobs, retired);
    CHECK(blobs.equal(frames.token(first.key, 0), material));
    pinned.release();
    frames.collect(blobs, retired);
    CHECK(frames.token(first.key, 0) == 0);
    CHECK(blobs.availableBytes() == 64);
    frames.collect(blobs, retired);
    CHECK(blobs.availableBytes() == 64);
}

TEST_CASE("packed schemas own only used names definitions and enums without changing their values") {
    using namespace self_recall::equipment_effects::detail;
    alignas(8) std::byte instance[0x60]{}, user[0x50]{}, enumeration[112]{}, scalar[112]{}, integer[112]{};
    char userName[] = "GlowBlade", name[] = "State", first[] = "Drawn", second[] = "Sheathed";
    char scalarName[] = "Glow", integerName[] = "Count";
    EnumValue choices[]{{first, 1, 0}, {second, 7, 0}};
    const void* definitions[]{enumeration, scalar, integer};
    write<const void*>(instance, 0x58, user);
    write<const void*>(user, 0x10, userName);
    write<std::uint16_t>(user, 0x44, 3);
    write<const void*>(user, 0x48, definitions);
    write<const void*>(enumeration, 8, name);
    write<int>(enumeration, 96, 2); write<int>(enumeration, 100, 500);
    write<const void*>(enumeration, 104, choices);
    write<const char*>(scalar, 8, scalarName); write<unsigned>(scalar, 0x58, 2);
    write<unsigned>(scalar, 96, 0x80000000); write<unsigned>(scalar, 100, 0x7fc01234);
    write<const char*>(integer, 8, integerName); write<unsigned>(integer, 0x58, 1);
    write<int>(integer, 96, -9); write<int>(integer, 100, 87);
    const auto size = measureSchema(instance); REQUIRE(size);
    CHECK(size.properties == 3); CHECK(size.enums == 2);
    CHECK(size.names == sizeof(userName) + sizeof(first) + sizeof(second));
    CHECK(size.bytes() < 512);
    std::vector<std::uint64_t> storage((size.bytes() + 7) / 8 + 1, UINT64_MAX);
    PackedEffectSchema compact;
    REQUIRE(compact.bind(std::as_writable_bytes(std::span{storage}), size));
    REQUIRE(copySchema(compact, instance));
    CHECK(storage.back() == UINT64_MAX);
    CHECK(compact.userName != userName);
    CHECK(compact.definitionPointers[0] != enumeration);
    CHECK(read<const void*>(compact.definitions[0], 8) != name);
    CHECK(read<const void*>(compact.definitions[0], 104) != choices);
    std::memset(userName, 'X', sizeof(userName) - 1);
    std::memset(name, 'W', sizeof(name) - 1);
    std::memset(first, 'Y', sizeof(first) - 1);
    std::memset(second, 'Z', sizeof(second) - 1);
    std::memset(scalarName, 'S', sizeof(scalarName) - 1);
    std::memset(integerName, 'I', sizeof(integerName) - 1);
    choices[1].value = 999;
    CHECK(std::strcmp(compact.userName, "GlowBlade") == 0);
    CHECK(std::strcmp(read<const char*>(compact.definitions[0], 8), "State") == 0);
    CHECK(std::strcmp(compact.enums[0].name, "Drawn") == 0);
    CHECK(std::strcmp(compact.enums[1].name, "Sheathed") == 0);
    CHECK(compact.enums[1].value == 7);
    CHECK(read<unsigned>(compact.definitions[1], 96) == 0x80000000);
    CHECK(read<unsigned>(compact.definitions[1], 100) == 0x7fc01234);
    CHECK(read<int>(compact.definitions[2], 96) == -9);
    CHECK(read<int>(compact.definitions[2], 100) == 87);
    CHECK(read<unsigned>(compact.definitions[0], 100) == 2);
    CHECK(read<const void*>(compact.definitions[0], 104) == compact.enums);
    CHECK(read<const void*>(compact.table, 32) == compact.definitionPointers);
    CHECK_FALSE(compact.bind(std::as_writable_bytes(std::span{storage}).first(size.bytes() - 1), size));
    write<std::uint16_t>(user, 0x44, 4);
    CHECK_FALSE(copySchema(compact, instance));
    CHECK_FALSE(measureSchema(nullptr));
}
}

namespace self_recall_tests::test_recall_equipment_effects {
using namespace self_recall::pure;

TEST_CASE("selected loops wake after native sleep and resume without restarting valid events") {
    CHECK(planEquipmentLoop(true, false, true) == EquipmentLoopAction::WakeAndEmit);
    CHECK(planEquipmentLoop(true, true, true) == EquipmentLoopAction::WakeAndEmit);
    CHECK(planEquipmentLoop(true, false, false) == EquipmentLoopAction::Emit);
    CHECK(planEquipmentLoop(true, true, false) == EquipmentLoopAction::Keep);
    CHECK(planEquipmentLoop(false, true, false) == EquipmentLoopAction::Kill);
    CHECK(planEquipmentLoop(false, true, true) == EquipmentLoopAction::Kill);
    CHECK(planEquipmentLoop(false, false, false) == EquipmentLoopAction::Keep);
    CHECK(planEquipmentLoop(false, false, true) == EquipmentLoopAction::Keep);
}

TEST_CASE("effect schemas reject excess counts and unknown native property types") {
    using namespace self_recall::equipment_effects::detail;
    alignas(8) std::byte instance[0x60]{}, user[0x50]{}, definition[112]{};
    const void* definitions[]{definition};
    write<const void*>(instance, 0x58, user);
    write<const char*>(user, 0x10, "TestWeapon");
    write<std::uint16_t>(user, 0x44, kProperties + 1);
    CHECK_FALSE(measureSchema(instance));
    write<std::uint16_t>(user, 0x44, 1);
    write<const void*>(user, 0x48, definitions);
    write<const char*>(definition, 8, "State");
    write<unsigned>(definition, 0x58, 6);
    CHECK_FALSE(measureSchema(instance));
    write<unsigned>(definition, 0x58, 0);
    write<int>(definition, 96, kEnums + 1);
    write<int>(definition, 100, kEnums + 1);
    CHECK_FALSE(measureSchema(instance));
    write<int>(definition, 96, 1);
    write<int>(definition, 100, 1);
    CHECK_FALSE(measureSchema(instance));
    write<int>(definition, 96, 0);
    auto size = measureSchema(instance);
    REQUIRE(size);
    std::vector<std::uint64_t> storage((size.bytes() + 7) / 8);
    PackedEffectSchema packed;
    REQUIRE(packed.bind(std::as_writable_bytes(std::span{storage}), size));
    CHECK(copySchema(packed, instance));
    write<int>(definition, 96, 1);
    write<int>(definition, 100, 0);
    CHECK_FALSE(measureSchema(instance));
}

TEST_CASE("equipment effects retain all slot bits across recorded frame copies") {
    PoseFrameHeader header{};
    header.bodyModelCount = 3;
    header.key = {99, 7, 4};
    setEquipmentEffectMask(header, (std::uint64_t{1} << 63) | 1);
    const auto recorded = header;
    setEquipmentEffectMask(header, 0);
    const auto mask = equipmentEffectMask(recorded);
    CHECK((mask & 1) != 0);
    CHECK((mask & (std::uint64_t{1} << 63)) != 0);
    CHECK((mask & (std::uint64_t{1} << 1)) == 0);
    CHECK(recorded.bodyModelCount == 3);
    CHECK(recorded.key.generation == 7);
    CHECK(equipmentEffectMask(header) == 0);
}

TEST_CASE("temporary effect masks restore once and reject reused native identities") {
    EffectMaskRestoration<2> masks;
    EffectMaskIdentity first{11, 100, 7}, second{22, 200, 8};
    REQUIRE(masks.remember(first, 0x81234567));
    REQUIRE(masks.remember(second, 0));
    CHECK_FALSE(masks.remember({33, 300, 9}, 42));
    CHECK_FALSE(masks.remember(first, 0));
    CHECK(masks.restore(first) == 0x81234567);
    CHECK_FALSE(masks.restore(first));
    CHECK(masks.restore(second) == 0);
    REQUIRE(masks.remember(first, 0x1234));
    CHECK_FALSE(masks.restore({11, 100, 8}));
    REQUIRE(masks.remember(first, 0x4321));
    CHECK_FALSE(masks.restore({11, 101, 7}));
    CHECK_FALSE(masks.restore(first));
    REQUIRE(masks.remember(first, 0x99));
    CHECK_FALSE(masks.restore(second));
    CHECK(masks.restore(first) == 0x99);
}
}

namespace self_recall_tests::test_recall_monochrome {
using namespace self_recall::pure;

namespace {
template<class T> T read(std::span<const std::byte> bytes, std::size_t offset) {
    T value;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}
template<class T> void write(std::span<std::byte> bytes, std::size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

std::uint32_t shaderAttribute(float value) {
    volatile float multiplied = value * 255.0f;
    return static_cast<std::uint32_t>(multiplied);
}
}

TEST_CASE("private monochrome filter reaches native full-strength targets without changing borrowed state") {
    std::array<std::byte, kMonochromeFilterBytes> native{}, privateFilter{};
    for (std::size_t i = 0; i < native.size(); ++i)
        native[i] = static_cast<std::byte>((i * 37u + 11u) & 0xFFu);
    write(native, 0x64, std::uint32_t{0xA57C0010});
    write(native, 0x1C, 0.15f);
    write(native, 0x58, 0.2f);
    write(native, 0x60, 4.0f);
    write(native, 0x7C, 1.0f);
    write(native, 0x80, 1.0f);
    const auto original = native;
    REQUIRE(buildMonochromeFilter(native, privateFilter) == MonochromeFilterStatus::Ready);
    CHECK(native == original);
    const auto flags = read<std::uint32_t>(privateFilter, 0x64);
    CHECK((flags & 1u) != 0);
    CHECK((flags & 0x10u) == 0);
    CHECK((flags & ~0x11u) == (read<std::uint32_t>(original, 0x64) & ~0x11u));
    CHECK(read<float>(privateFilter, 0x1C) > 0.99f);
    const auto weight = read<float>(privateFilter, 0x58) / read<float>(privateFilter, 0x60);
    CHECK(1.0f + weight * (read<float>(privateFilter, 0x7C) - 1.0f) == 0.375f);
    CHECK(1.0f + weight * (read<float>(privateFilter, 0x80) - 1.0f) == 0.25f);
    for (std::size_t i = 0; i < native.size(); ++i) {
        bool replaced = false;
        for (const auto offset : {0x1Cu, 0x58u, 0x60u, 0x64u, 0x7Cu, 0x80u})
            replaced |= i >= offset && i < offset + 4u;
        if (!replaced) CHECK(privateFilter[i] == original[i]);
    }
}

TEST_CASE("private monochrome filter refuses bad spans and overlap before any writes") {
    std::array<std::byte, kMonochromeFilterBytes + 1> source{}, destination{};
    destination.fill(std::byte{0xA6});
    const auto before = destination;
    const std::span<const std::byte> native{source.data(), kMonochromeFilterBytes};
    const std::span<std::byte> output{destination.data(), kMonochromeFilterBytes};
    CHECK(buildMonochromeFilter({}, output) == MonochromeFilterStatus::InvalidSize);
    CHECK(buildMonochromeFilter(native, {}) == MonochromeFilterStatus::InvalidSize);
    CHECK(buildMonochromeFilter(native.first(kMonochromeFilterBytes - 1), output) ==
          MonochromeFilterStatus::InvalidSize);
    CHECK(buildMonochromeFilter(native, output.first(kMonochromeFilterBytes - 1)) ==
          MonochromeFilterStatus::InvalidSize);
    CHECK(buildMonochromeFilter(source, output) == MonochromeFilterStatus::InvalidSize);
    CHECK(buildMonochromeFilter(native, destination) == MonochromeFilterStatus::InvalidSize);
    CHECK(destination == before);

    const auto original = source;
    CHECK(buildMonochromeFilter(native, {source.data(), kMonochromeFilterBytes}) ==
          MonochromeFilterStatus::OverlappingBuffers);
    CHECK(buildMonochromeFilter(native, {source.data() + 1, kMonochromeFilterBytes}) ==
          MonochromeFilterStatus::OverlappingBuffers);
    CHECK(buildMonochromeFilter({source.data() + 1, kMonochromeFilterBytes},
                               {source.data(), kMonochromeFilterBytes}) ==
          MonochromeFilterStatus::OverlappingBuffers);
    CHECK(source == original);

    std::array<std::byte, kMonochromeFilterBytes * 2> adjacent{};
    CHECK(buildMonochromeFilter({adjacent.data(), kMonochromeFilterBytes},
                               {adjacent.data() + kMonochromeFilterBytes, kMonochromeFilterBytes}) ==
          MonochromeFilterStatus::Ready);
}

TEST_CASE("monochrome exclusion preserves every decoded 16-bit shader attribute") {
    for (std::uint32_t flags = 0; flags <= 0xFFFFu; ++flags) {
        CAPTURE(flags);
        const float source = flags == 0xFFFFu ? 257.0f :
            static_cast<float>((static_cast<double>(flags) + 0.5) / 255.0);
        REQUIRE(shaderAttribute(source) == flags);
        const auto originalBits = std::bit_cast<std::uint32_t>(source);
        float excluded = -1.0f;
        REQUIRE(enableMonochromeExclusion(source, excluded) == ObjectAttributeStatus::Ready);
        CHECK(shaderAttribute(excluded) == (flags | 0x80u));
        CHECK(std::bit_cast<std::uint32_t>(source) == originalBits);
        if (flags & 0x80u)
            CHECK(std::bit_cast<std::uint32_t>(excluded) == originalBits);
    }
}

TEST_CASE("monochrome exclusion accepts shader boundaries and rejects invalid floats without publishing") {
    for (const float source : {0.0f, -0.0f, std::numeric_limits<float>::denorm_min(),
                              127.0f / 255.0f, 128.0f / 255.0f, 1.0f, 256.0f, 257.0f}) {
        CAPTURE(source);
        float excluded = -1.0f;
        REQUIRE(enableMonochromeExclusion(source, excluded) == ObjectAttributeStatus::Ready);
        CHECK(shaderAttribute(excluded) == (shaderAttribute(source) | 0x80u));
    }
    float zeroExcluded = -1.0f;
    REQUIRE(enableMonochromeExclusion(0.0f, zeroExcluded) == ObjectAttributeStatus::Ready);
    CHECK(std::bit_cast<std::uint32_t>(zeroExcluded) == 0x3F008081u);

    for (const float source : {-1.0f, -std::numeric_limits<float>::denorm_min(),
                              std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN(),
                              std::nextafter(257.0f, std::numeric_limits<float>::infinity())}) {
        float excluded = 42.25f;
        CHECK(enableMonochromeExclusion(source, excluded) == ObjectAttributeStatus::InvalidValue);
        CHECK(excluded == 42.25f);
    }
}

TEST_CASE("naturally excluded appearance still refreshes each stale native buffer") {
    float source = std::bit_cast<float>(0x3FD0D0D1u);
    const float oldAppearance = std::bit_cast<float>(0x3FFF7F7Fu);
    std::array<float, 3> gpu{oldAppearance, oldAppearance, oldAppearance};
    unsigned pending = 0;
    for (unsigned buffer = 0; buffer < gpu.size(); ++buffer) {
        CAPTURE(buffer);
        float excluded = 0;
        REQUIRE(enableMonochromeExclusion(source, excluded) == ObjectAttributeStatus::Ready);
        REQUIRE(excluded == source);
        REQUIRE(gpu[buffer] != excluded);
        const auto original = source;
        uploadMonochromeAttribute(source, excluded,
            [&](float value) { source = value; pending |= 7; },
            [&] {
                if (pending & (1u << buffer)) gpu[buffer] = source;
                pending &= ~(1u << buffer);
            });
        CHECK(gpu[buffer] == excluded);
        CHECK(source == original);
    }
}

TEST_CASE("temporary monochrome upload restores CPU and schedules native GPU restoration on exit") {
    float source = 0.0f, gpu = -1.0f, excluded = -1.0f;
    bool dirty = false;
    REQUIRE(enableMonochromeExclusion(source, excluded) == ObjectAttributeStatus::Ready);
    const auto calculate = [&] { if (dirty) gpu = source; dirty = false; };
    uploadMonochromeAttribute(source, excluded,
        [&](float value) { source = value; dirty = true; }, calculate);
    CHECK(gpu == excluded);
    CHECK(source == 0.0f);
    CHECK(dirty);
    calculate();
    CHECK(gpu == 0.0f);
    CHECK_FALSE(dirty);
}
}
