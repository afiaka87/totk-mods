#pragma once

#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

namespace self_recall::pure {

struct CompactEffectHandle {
    std::uint8_t type = 0xFF;
    std::uint8_t padding = 0;
    std::int16_t poolIndex = -1;
    std::uint32_t eventId = 0;
};
static_assert(sizeof(CompactEffectHandle) == 8);

struct WristEffectBinding {
    CompactEffectHandle handle{};
    std::uintptr_t event = 0;
};

struct WristEffectOwner {
    std::uint64_t serial = 0;
    std::uint32_t historyGeneration = 0;
    explicit operator bool() const { return serial && historyGeneration; }
};

class WristEffectOwners {
public:
    std::uint64_t publish(std::uint32_t historyGeneration,
                          std::span<const WristEffectBinding> bindings) {
        clear();
        if (!historyGeneration || bindings.size() > 2 || next_ == UINT64_MAX) return 0;
        for (unsigned i = 0; i < 2; ++i) {
            const auto binding = i < bindings.size() ? bindings[i] : WristEffectBinding{};
            events_[i].store(binding.event);
            handles_[i].store(std::bit_cast<std::uint64_t>(binding.handle));
        }
        history_.store(historyGeneration);
        serial_.store(++next_);
        return next_;
    }
    void clear() { serial_.store(0); }
    bool current(std::uint64_t serial) const { return serial && serial_.load() == serial; }
    WristEffectOwner match(std::uintptr_t event, std::uint32_t eventId) const {
        const auto serial = serial_.load();
        if (!serial || !event) return {};
        const auto history = history_.load();
        bool matched = false;
        for (unsigned i = 0; i < 2; ++i) {
            const auto handle = std::bit_cast<CompactEffectHandle>(handles_[i].load());
            matched |= events_[i].load() == event && handle.type < 0x80 &&
                       handle.poolIndex >= 0 && handle.eventId == eventId;
        }
        return matched && current(serial) ? WristEffectOwner{serial, history} : WristEffectOwner{};
    }
private:
    std::atomic<std::uint64_t> serial_{0};
    std::atomic<std::uint32_t> history_{0};
    std::atomic<std::uintptr_t> events_[2]{};
    std::atomic<std::uint64_t> handles_[2]{};
    std::uint64_t next_ = 0;
};

struct WristMatrixDescriptor { void* object; std::uint64_t serial; };
static_assert(sizeof(WristMatrixDescriptor) == 16);

class WristMatrixProvider {
    struct Vtable {
        bool (*valid)(const WristMatrixProvider*, const void*);
        void (*copy)(WristMatrixProvider*, void*, const void*);
    };
    static bool valid(const WristMatrixProvider* self, const void* data) {
        std::uint64_t serial = 0;
        if (data) std::memcpy(&serial, data, sizeof(serial));
        return self && self->finite_ && serial == self->serial_ && self->owners_->current(serial);
    }
    static void copy(WristMatrixProvider* self, void* out, const void* data) {
        if (!out || !valid(self, data)) return;
        std::memcpy(out, self->matrix_, sizeof(self->matrix_));
        self->copied_ = true;
    }
    inline static const Vtable kVtable{valid, copy};
    const Vtable* vtable_ = &kVtable;
    const WristEffectOwners* owners_;
    std::uint64_t serial_;
    float matrix_[12]{};
    bool finite_ = true;
    bool copied_ = false;
public:
    WristMatrixProvider(const WristEffectOwners& owners, std::uint64_t serial, const float (&matrix)[12])
        : owners_(&owners), serial_(serial) {
        std::memcpy(matrix_, matrix, sizeof(matrix_));
        for (float value : matrix_) finite_ &= std::isfinite(value);
    }
    WristMatrixProvider(const WristMatrixProvider&) = delete;
    WristMatrixProvider& operator=(const WristMatrixProvider&) = delete;
    WristMatrixDescriptor descriptor() { return {this, serial_}; }
    bool copied() const { return copied_; }
};
static_assert(std::is_standard_layout_v<WristMatrixProvider>);

} // namespace self_recall::pure
