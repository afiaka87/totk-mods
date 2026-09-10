#pragma once

#include <cmath>
#include <cstdint>

namespace self_recall::pure {

enum class PaletteParameter : unsigned { Character, Other };

struct PaletteWriterIdentity {
    std::uintptr_t model = 0;
    std::uintptr_t material = 0;
    std::uintptr_t resource = 0;
    bool operator==(const PaletteWriterIdentity&) const = default;
    explicit operator bool() const { return model && material && resource; }
};

struct PalettePreparedBuffer {
    PaletteWriterIdentity identity{};
    std::uint64_t epoch = 0;
    std::uintptr_t buffer = 0;
    unsigned index = 0;
    std::uint32_t bytes = 0;
    bool matches(PaletteWriterIdentity current, std::uint64_t currentEpoch,
                 std::uintptr_t currentBuffer, unsigned currentIndex, std::uint32_t currentBytes) const {
        return identity && identity == current && epoch && epoch == currentEpoch &&
            buffer && buffer == currentBuffer && index < 3 && index == currentIndex &&
            bytes && bytes <= UINT16_MAX && bytes == currentBytes;
    }
};

class PaletteWrites {
public:
    void beginFrame(std::uint64_t epoch, bool recall) {
        epoch_ = epoch;
        recall_ = recall && epoch;
        identity_ = {};
        mask_ = 0;
        indices_[0] = indices_[1] = -1;
    }
    bool record(std::uint64_t epoch, PaletteWriterIdentity identity,
                int materialIndex, PaletteParameter parameter, int parameterIndex,
                float appliedValue) {
        if (!epoch || epoch != epoch_) return false;
        if (!identity || materialIndex != 0 || parameterIndex < 0 ||
            !std::isfinite(appliedValue) || appliedValue < 0 || appliedValue > 1 ||
            static_cast<unsigned>(parameter) > 1) { mask_ = 0; return false; }
        if (identity != identity_) {
            identity_ = identity;
            mask_ = 0;
            indices_[0] = indices_[1] = -1;
        }
        const auto i = static_cast<unsigned>(parameter);
        if (i == 0) mask_ = 0;
        indices_[i] = parameterIndex;
        values_[i] = appliedValue;
        mask_ |= 1u << i;
        return true;
    }
    bool complete(std::uint64_t epoch, PaletteWriterIdentity freshIdentity) const {
        return epoch && epoch == epoch_ && mask_ == 3 && freshIdentity == identity_ &&
            indices_[0] != indices_[1];
    }
    bool recall(std::uint64_t epoch) const { return epoch && epoch == epoch_ && recall_; }
    bool hasModel(std::uint64_t epoch, std::uintptr_t model) const {
        return epoch && epoch == epoch_ && model && model == identity_.model;
    }
    int index(PaletteParameter parameter) const { return indices_[static_cast<unsigned>(parameter)]; }
    float value(PaletteParameter parameter) const { return values_[static_cast<unsigned>(parameter)]; }

private:
    PaletteWriterIdentity identity_{};
    std::uint64_t epoch_ = 0;
    int indices_[2]{-1, -1};
    float values_[2]{};
    unsigned mask_ = 0;
    bool recall_ = false;
};

} // namespace self_recall::pure
