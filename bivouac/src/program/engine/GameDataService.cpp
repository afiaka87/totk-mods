// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "GameDataService.hpp"

#include "RefundPolicy.hpp"

namespace bivouac::engine {
namespace {

constexpr std::ptrdiff_t kGameDataManagerIndirectOffset = 0x0462E3D8;
constexpr std::ptrdiff_t kPouchGameDataGlobalOffset = 0x046CBC98;
constexpr std::ptrdiff_t kEmptyString64Offset = 0x0462E1B8;
constexpr std::ptrdiff_t kGetStructOffset = 0x00DDBB04;
constexpr std::ptrdiff_t kGetStructStructOffset = 0x0080E6C4;
constexpr std::ptrdiff_t kGetStructByIndexOffset = 0x00942FE8;
constexpr std::ptrdiff_t kGetStructString64Offset = 0x00D44600;
constexpr std::ptrdiff_t kGetStructEnumOffset = 0x00BAD788;
constexpr std::ptrdiff_t kSetStructString64Offset = 0x00C12E4C;
constexpr std::ptrdiff_t kAddStructIntOffset = 0x015D6930;
constexpr std::ptrdiff_t kSetStructEnumOffset = 0x0080F2F0;
constexpr std::ptrdiff_t kSetStructVector2Offset = 0x0199D7A0;
constexpr std::ptrdiff_t kSetStructBoolOffset = 0x007AB63C;

constexpr std::uint32_t kMaterialArrayHash = 0x4290322E;
constexpr std::uint32_t kPouchNameFieldHash = 0x25EFA387;
constexpr std::uint32_t kMaterialCountFieldHash = 0x22344481;

constexpr std::uintptr_t kEnumStoreOffset = 680;
constexpr std::uintptr_t kVector2StoreOffset = 872;
constexpr std::uintptr_t kQueueCapacityOffset = 40;
constexpr std::uintptr_t kQueueBufferOffset = 48;
constexpr std::uintptr_t kQueueControlOffset = 56;
constexpr std::uint32_t kQueueIndexMask = 0xFFFFF;
constexpr int kQueueHeadroom = 2;

bool validPointer(std::uintptr_t value) {
    return value >= 0x1000000 && value < 0x8000000000ull
        && (value & 0x3) == 0;
}

bool queueHasRoom(std::uintptr_t store, int pushes) {
    if (!validPointer(store)) {
        return false;
    }
    const auto capacity =
        *reinterpret_cast<std::int32_t*>(store + kQueueCapacityOffset);
    const auto buffer =
        *reinterpret_cast<std::uintptr_t*>(store + kQueueBufferOffset);
    const auto writeIndex = static_cast<std::int32_t>(
        *reinterpret_cast<std::uint32_t*>(store + kQueueControlOffset)
        & kQueueIndexMask);
    return capacity > 0 && validPointer(buffer)
        && writeIndex + pushes + kQueueHeadroom <= capacity;
}

} // namespace

RefundWriteResult GameDataService::refundMaterial(
    std::uint32_t gameDataIndex, const char* expectedItemName) const {
    const auto managerIndirect =
        *reinterpret_cast<std::uintptr_t*>(mMainBase + kGameDataManagerIndirectOffset);
    const auto pouchGlobal =
        *reinterpret_cast<std::uintptr_t*>(mMainBase + kPouchGameDataGlobalOffset);
    if (!validPointer(managerIndirect) || !validPointer(pouchGlobal)) {
        return {RefundWriteError::GlobalsUnavailable};
    }
    const auto manager = *reinterpret_cast<std::uintptr_t*>(managerIndirect);
    if (!validPointer(manager)) {
        return {RefundWriteError::ManagerUnavailable};
    }

    auto mode = *reinterpret_cast<std::uint32_t*>(pouchGlobal + 0x188);
    if (mode >= 2) {
        mode = 0;
    }
    const auto handleArgument = pouchGlobal + 0xC0ull * mode + 0x58ull;

    alignas(8) unsigned char record[16] = {};
    const auto getStructByIndex =
        reinterpret_cast<std::uint32_t (*)(std::uintptr_t, void*, std::uintptr_t,
                                           std::uint32_t, std::uint32_t)>(
            mMainBase + kGetStructByIndexOffset);
    if ((getStructByIndex(manager, record, handleArgument, kMaterialArrayHash,
                          gameDataIndex) & 1u) == 0) {
        return {RefundWriteError::SlotUnavailable};
    }

    const char* current =
        *reinterpret_cast<const char**>(mMainBase + kEmptyString64Offset);
    const auto getString =
        reinterpret_cast<std::uint32_t (*)(std::uintptr_t, const char**, void*,
                                           std::uint32_t)>(
            mMainBase + kGetStructString64Offset);
    if ((getString(manager, &current, record, kPouchNameFieldHash) & 1u) == 0) {
        return {RefundWriteError::NameUnavailable};
    }

    const auto nameDecision =
        refund::decideSlotName(current, expectedItemName);
    bool restoredName = false;
    if (nameDecision == refund::SlotNameDecision::RestoreEmptiedName) {
        const auto setString =
            reinterpret_cast<std::uint32_t (*)(std::uintptr_t, const char**,
                                               void*, std::uint32_t)>(
                mMainBase + kSetStructString64Offset);
        const char* nameToSet = expectedItemName;
        setString(manager, &nameToSet, record, kPouchNameFieldHash);
        restoredName = true;
    } else if (nameDecision == refund::SlotNameDecision::RefuseDifferentItem) {
        return {RefundWriteError::DifferentItem, false, current};
    }

    const auto addInt =
        reinterpret_cast<std::uint32_t (*)(std::uintptr_t, std::int64_t, void*,
                                           std::uint32_t)>(
            mMainBase + kAddStructIntOffset);
    addInt(manager, 1, record, kMaterialCountFieldHash);
    return {RefundWriteError::None, restoredName, current};
}

bool GameDataService::openMapNavigation(
    std::uint32_t rootHash, std::uint32_t iconDataHash,
    MapNavigation* navigation) const {
    if (navigation == nullptr) {
        return false;
    }
    *navigation = {};
    const auto indirect =
        *reinterpret_cast<std::uintptr_t*>(mMainBase + kGameDataManagerIndirectOffset);
    if (!validPointer(indirect)) {
        return false;
    }
    const auto manager = *reinterpret_cast<std::uintptr_t*>(indirect);
    if (!validPointer(manager)) {
        return false;
    }
    navigation->manager = manager;

    const auto getStruct =
        reinterpret_cast<std::uint32_t (*)(std::uintptr_t, void*, std::uint32_t)>(
            mMainBase + kGetStructOffset);
    if ((getStruct(manager, navigation->root, rootHash) & 1u) == 0) {
        return false;
    }
    const auto getStructStruct =
        reinterpret_cast<std::uint32_t (*)(std::uintptr_t, void*, const void*,
                                           std::uint32_t)>(
            mMainBase + kGetStructStructOffset);
    return (getStructStruct(manager, navigation->iconData, navigation->root,
                            iconDataHash) & 1u) != 0;
}

bool GameDataService::fetchMapSlot(
    const MapNavigation& navigation, std::uint32_t arrayHash,
    std::uint32_t slot, void* outputHandle16) const {
    if (outputHandle16 == nullptr) {
        return false;
    }
    auto* output = static_cast<unsigned char*>(outputHandle16);
    for (int index = 0; index < 16; ++index) {
        output[index] = 0;
    }
    const auto getByIndex =
        reinterpret_cast<std::uint32_t (*)(std::uintptr_t, void*, const void*,
                                           std::uint32_t, std::uint32_t)>(
            mMainBase + kGetStructByIndexOffset);
    return (getByIndex(navigation.manager, outputHandle16, navigation.iconData,
                       arrayHash, slot) & 1u) != 0;
}

bool GameDataService::readEnum(
    const MapNavigation& navigation, const void* handle16,
    std::uint32_t fieldHash, std::uint32_t* value) const {
    if (value == nullptr) {
        return false;
    }
    const auto getEnum =
        reinterpret_cast<std::uint32_t (*)(std::uintptr_t, std::uint32_t*,
                                           const void*, std::uint32_t)>(
            mMainBase + kGetStructEnumOffset);
    return (getEnum(navigation.manager, value, handle16, fieldHash) & 1u) != 0;
}

bool GameDataService::mapSettersReady(
    const MapNavigation& navigation, int enumPushes,
    int vector2Pushes) const {
    return queueHasRoom(navigation.manager + kEnumStoreOffset, enumPushes)
        && queueHasRoom(
            navigation.manager + kVector2StoreOffset, vector2Pushes);
}

QueueSnapshot GameDataService::mapQueueSnapshot(
    const MapNavigation& navigation) const {
    const auto enumStore = navigation.manager + kEnumStoreOffset;
    const auto vector2Store = navigation.manager + kVector2StoreOffset;
    return {
        *reinterpret_cast<std::int32_t*>(enumStore + kQueueCapacityOffset),
        *reinterpret_cast<std::uint32_t*>(enumStore + kQueueControlOffset)
            & kQueueIndexMask,
        *reinterpret_cast<std::int32_t*>(vector2Store + kQueueCapacityOffset),
        *reinterpret_cast<std::uint32_t*>(vector2Store + kQueueControlOffset)
            & kQueueIndexMask,
    };
}

void GameDataService::writeEnum(
    const MapNavigation& navigation, std::uint32_t value,
    const void* handle16, std::uint32_t fieldHash) const {
    const auto setEnum =
        reinterpret_cast<std::uint32_t (*)(std::uintptr_t, std::uint32_t,
                                           const void*, std::uint32_t)>(
            mMainBase + kSetStructEnumOffset);
    setEnum(navigation.manager, value, handle16, fieldHash);
}

void GameDataService::writeVector2(
    const MapNavigation& navigation, const float value[2],
    const void* handle16, std::uint32_t fieldHash) const {
    const auto setVector2 =
        reinterpret_cast<std::uint32_t (*)(std::uintptr_t, const void*,
                                           const void*, std::uint32_t)>(
            mMainBase + kSetStructVector2Offset);
    setVector2(navigation.manager, value, handle16, fieldHash);
}

void GameDataService::writeBool(
    const MapNavigation& navigation, bool value,
    const void* handle16, std::uint32_t fieldHash) const {
    const auto setBool =
        reinterpret_cast<std::uint32_t (*)(std::uintptr_t, std::uint32_t,
                                           const void*, std::uint32_t)>(
            mMainBase + kSetStructBoolOffset);
    setBool(navigation.manager, value ? 1u : 0u, handle16, fieldHash);
}

const char* refundWriteErrorName(RefundWriteError error) {
    switch (error) {
    case RefundWriteError::None:
        return "none";
    case RefundWriteError::GlobalsUnavailable:
        return "globals unavailable";
    case RefundWriteError::ManagerUnavailable:
        return "manager unavailable";
    case RefundWriteError::SlotUnavailable:
        return "slot unavailable";
    case RefundWriteError::NameUnavailable:
        return "name unavailable";
    case RefundWriteError::DifferentItem:
        return "different item";
    }
    return "unknown";
}

} // namespace bivouac::engine
