// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstdint>

namespace bivouac::engine {

enum class RefundWriteError : std::uint8_t {
    None = 0,
    GlobalsUnavailable,
    ManagerUnavailable,
    SlotUnavailable,
    NameUnavailable,
    DifferentItem,
};

struct RefundWriteResult {
    RefundWriteError error = RefundWriteError::None;
    bool restoredName = false;
    const char* currentName = nullptr;

    constexpr explicit operator bool() const {
        return error == RefundWriteError::None;
    }
};

struct MapNavigation {
    std::uintptr_t manager = 0;
    alignas(8) unsigned char root[16] = {};
    alignas(8) unsigned char iconData[16] = {};
};

struct QueueSnapshot {
    std::int32_t enumCapacity = 0;
    std::uint32_t enumWriteIndex = 0;
    std::int32_t vector2Capacity = 0;
    std::uint32_t vector2WriteIndex = 0;
};

class GameDataService {
public:
    void setMainBase(std::uintptr_t mainBase) { mMainBase = mainBase; }

    RefundWriteResult refundMaterial(std::uint32_t gameDataIndex,
                                     const char* expectedItemName) const;

    bool openMapNavigation(std::uint32_t rootHash,
                           std::uint32_t iconDataHash,
                           MapNavigation* navigation) const;
    bool fetchMapSlot(const MapNavigation& navigation,
                      std::uint32_t arrayHash,
                      std::uint32_t slot,
                      void* outputHandle16) const;
    bool readEnum(const MapNavigation& navigation,
                  const void* handle16,
                  std::uint32_t fieldHash,
                  std::uint32_t* value) const;
    bool mapSettersReady(const MapNavigation& navigation,
                         int enumPushes,
                         int vector2Pushes) const;
    QueueSnapshot mapQueueSnapshot(const MapNavigation& navigation) const;
    void writeEnum(const MapNavigation& navigation,
                   std::uint32_t value,
                   const void* handle16,
                   std::uint32_t fieldHash) const;
    void writeVector2(const MapNavigation& navigation,
                      const float value[2],
                      const void* handle16,
                      std::uint32_t fieldHash) const;
    void writeBool(const MapNavigation& navigation,
                   bool value,
                   const void* handle16,
                   std::uint32_t fieldHash) const;

private:
    std::uintptr_t mMainBase = 0;
};

const char* refundWriteErrorName(RefundWriteError error);

} // namespace bivouac::engine
