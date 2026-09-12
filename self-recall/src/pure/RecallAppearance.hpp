#pragma once

#include <array>
#include <algorithm>
#include <span>
#include <cstring>
#include "RecallPoseData.hpp"

namespace self_recall::pure {
template<unsigned Blocks, unsigned States, unsigned BlockBytes = 256>
class AppearanceBlobs {
    struct State { unsigned first = 0, bytes = 0, refs = 0; };
    std::array<State, States + 1> states_{};
    std::array<unsigned, Blocks + 1> next_{};
    std::array<std::array<std::byte, BlockBytes>, Blocks> bytes_{};
    unsigned freeBlock_ = 0, freeState_ = 0, available_ = 0;
public:
    class Reader {
        const AppearanceBlobs* owner_;
        unsigned block_, offset_ = 0, remaining_;
    public:
        Reader(const AppearanceBlobs* owner, unsigned block, unsigned bytes)
            : owner_(owner), block_(block), remaining_(bytes) {}
        unsigned remaining() const { return remaining_; }
        bool get(unsigned& out) {
            if (!remaining_ || !block_ || block_ > Blocks) return false;
            out = std::to_integer<unsigned>(owner_->bytes_[block_ - 1][offset_++]);
            --remaining_;
            if (offset_ == BlockBytes) { block_ = owner_->next_[block_]; offset_ = 0; }
            return true;
        }
        bool copy(std::span<std::byte> output) {
            if (output.size() > remaining_) return false;
            for (auto& byte : output) { unsigned value; if (!get(value)) return false; byte = std::byte(value); }
            return true;
        }
    };
    Reader reader(unsigned token) const { return {this, size(token) ? states_[token].first : 0, size(token)}; }
    void initialize() {
        for (unsigned i = 1; i <= Blocks; ++i) next_[i] = i == Blocks ? 0 : i + 1;
        for (unsigned i = 1; i <= States; ++i) states_[i] = {i == States ? 0 : i + 1, 0, 0};
        freeBlock_ = freeState_ = 1;
        available_ = Blocks;
    }
    unsigned availableBytes() const { return available_ * BlockBytes; }
    unsigned size(unsigned token) const { return token && token <= States && states_[token].refs ? states_[token].bytes : 0; }
    bool retain(unsigned token) {
        if (!size(token) || states_[token].refs == UINT32_MAX) return false;
        ++states_[token].refs;
        return true;
    }
    void release(unsigned token) {
        if (!size(token)) return;
        if (--states_[token].refs) return;
        unsigned block = states_[token].first;
        while (block) {
            const auto next = next_[block];
            next_[block] = freeBlock_; freeBlock_ = block; ++available_;
            block = next;
        }
        states_[token] = {freeState_, 0, 0}; freeState_ = token;
    }
    bool equal(unsigned token, std::span<const std::byte> input) const {
        if (input.empty() || input.size() != size(token)) return false;
        auto block = states_[token].first;
        for (unsigned offset = 0; offset < input.size();) {
            const auto count = static_cast<unsigned>(std::min<std::size_t>(BlockBytes, input.size() - offset));
            if (!block || std::memcmp(bytes_[block - 1].data(), input.data() + offset, count)) return false;
            block = next_[block]; offset += count;
        }
        return true;
    }
    unsigned create(std::span<const std::byte> input) {
        const auto blocks = (input.size() + BlockBytes - 1) / BlockBytes;
        if (input.empty() || input.size() > UINT32_MAX || !freeState_ || blocks > available_) {
            return 0;
        }
        const auto token = freeState_; freeState_ = states_[token].first;
        auto& state = states_[token]; state = {0, static_cast<unsigned>(input.size()), 1};
        unsigned* link = &state.first;
        for (unsigned offset = 0; offset < input.size();) {
            const auto block = freeBlock_; freeBlock_ = next_[block]; --available_;
            *link = block; link = &next_[block]; *link = 0;
            const auto count = static_cast<unsigned>(std::min<std::size_t>(BlockBytes, input.size() - offset));
            std::memcpy(bytes_[block - 1].data(), input.data() + offset, count); offset += count;
        }
        return token;
    }
};

template<unsigned Capacity = kHistoryCapacity, unsigned Models = kPoseModelLimit>
class AppearanceFrames {
    struct Entry { PoseFrameKey key{}; std::array<unsigned, Models> tokens{}; };
    std::array<Entry, Capacity> frames_{};
public:
    template<class Blobs, class Retired> void collect(Blobs& blobs, Retired retired) {
        for (auto& frame : frames_) {
            if (!frame.key || !retired(frame.key)) continue;
            for (auto token : frame.tokens) blobs.release(token);
            frame = {};
        }
    }
    template<class Blobs> bool bind(Blobs& blobs, PoseFrameKey key, std::span<const unsigned> tokens) {
        if (!key || key.slot >= Capacity || tokens.size() > Models) return false;
        unsigned retained = 0;
        for (auto token : tokens) {
            if (token && !blobs.retain(token)) {
                for (unsigned i = 0; i < retained; ++i) blobs.release(tokens[i]);
                return false;
            }
            ++retained;
        }
        auto& frame = frames_[key.slot];
        for (auto token : frame.tokens) blobs.release(token);
        frame = {}; frame.key = key;
        std::copy(tokens.begin(), tokens.end(), frame.tokens.begin());
        return true;
    }
    unsigned token(PoseFrameKey key, unsigned model) const {
        return key && key.slot < Capacity && model < Models && frames_[key.slot].key == key
             ? frames_[key.slot].tokens[model] : 0;
    }
};
}

#include <lz4.h>

namespace self_recall::pure {
template<unsigned Blocks, unsigned States> class CompressedAppearanceBlobs {
    AppearanceBlobs<Blocks, States> blobs_;
    alignas(8) mutable std::array<std::byte, LZ4_STREAM_MINSIZE> encoder_{};
    mutable std::array<std::byte, 65536 + 8> scratch_{};
    unsigned encode(std::span<const std::byte> input) const {
        if (input.empty() || input.size() > 65536) return 0;
        const auto packed = LZ4_compress_fast_extState(encoder_.data(),
            reinterpret_cast<const char*>(input.data()), reinterpret_cast<char*>(scratch_.data() + 8),
            static_cast<int>(input.size()), static_cast<int>(input.size()), 1);
        const unsigned header[2]{static_cast<unsigned>(input.size()), packed > 0 ? 1u : 0u};
        std::memcpy(scratch_.data(), header, sizeof(header));
        if (!packed) std::memcpy(scratch_.data() + 8, input.data(), input.size());
        return 8 + (packed ? static_cast<unsigned>(packed) : static_cast<unsigned>(input.size()));
    }
public:
    void initialize() { blobs_.initialize(); }
    unsigned availableBytes() const { return blobs_.availableBytes(); }
    bool retain(unsigned token) { return blobs_.retain(token); }
    void release(unsigned token) { blobs_.release(token); }
    unsigned size(unsigned token) const {
        auto reader = blobs_.reader(token);
        unsigned size = 0;
        return reader.copy(std::as_writable_bytes(std::span{&size, 1})) ? size : 0;
    }
    bool equal(unsigned token, std::span<const std::byte> input) const {
        if (input.size() != size(token)) return false;
        const auto bytes = encode(input);
        return bytes && blobs_.equal(token, {scratch_.data(), bytes});
    }
    unsigned create(std::span<const std::byte> input) {
        const auto bytes = encode(input);
        return bytes ? blobs_.create({scratch_.data(), bytes}) : 0;
    }
    bool copy(unsigned token, std::span<std::byte> output) const {
        auto reader = blobs_.reader(token);
        unsigned header[2]{};
        if (!reader.copy(std::as_writable_bytes(std::span{header})) || header[0] != output.size() ||
            header[1] > 1) return false;
        if (!header[1]) return reader.copy(output) && !reader.remaining();
        unsigned written = 0;
        const auto length = [&](unsigned initial, unsigned& result) {
            result = initial;
            if (initial != 15) return true;
            unsigned byte = 0;
            do {
                if (!reader.get(byte) || result > output.size() || byte > output.size() - result) return false;
                result += byte;
            } while (byte == 255);
            return true;
        };
        while (reader.remaining()) {
            unsigned command = 0, literals = 0;
            if (!reader.get(command) || !length(command >> 4, literals) ||
                literals > output.size() - written || !reader.copy(output.subspan(written, literals))) return false;
            written += literals;
            if (!reader.remaining()) return written == output.size();
            unsigned low = 0, high = 0, match = 0;
            if (!reader.get(low) || !reader.get(high) || !length(command & 15, match)) return false;
            const auto distance = low | (high << 8);
            if (!distance || distance > written || output.size() - written < 4 ||
                match > output.size() - written - 4) return false;
            match += 4;
            for (unsigned i = 0; i < match; ++i) {
                output[written] = output[written - distance];
                ++written;
            }
        }
        return false;
    }
};
}

#include <optional>

namespace self_recall::pure {
inline constexpr unsigned kEquipmentEffectLimit = 64;

enum class EquipmentLoopAction { Keep, Kill, Emit, WakeAndEmit };
inline EquipmentLoopAction planEquipmentLoop(bool selected, bool valid, bool sleeping) {
    if (!selected) return valid ? EquipmentLoopAction::Kill : EquipmentLoopAction::Keep;
    if (sleeping) return EquipmentLoopAction::WakeAndEmit;
    return valid ? EquipmentLoopAction::Keep : EquipmentLoopAction::Emit;
}
inline std::uint64_t equipmentEffectMask(const PoseFrameHeader& header) {
    std::uint64_t mask;
    static_assert(sizeof(header.reserved) == sizeof(mask));
    std::memcpy(&mask, header.reserved, sizeof(mask));
    return mask;
}
inline void setEquipmentEffectMask(PoseFrameHeader& header, std::uint64_t mask) {
    std::memcpy(header.reserved, &mask, sizeof(mask));
}
struct EffectMaskIdentity {
    std::uintptr_t executor = 0, emitter = 0;
    std::uint32_t id = 0;
    bool operator==(const EffectMaskIdentity&) const = default;
};
template<unsigned Capacity> class EffectMaskRestoration {
    struct Entry { EffectMaskIdentity identity{}; std::uint32_t mask = 0; };
    std::array<Entry, Capacity> entries_{};
public:
    bool remember(EffectMaskIdentity identity, std::uint32_t mask) {
        if (!identity.executor || !identity.emitter) return false;
        for (const auto& e : entries_) if (e.identity.executor == identity.executor) return false;
        for (auto& e : entries_) if (!e.identity.executor) {
            e = {identity, mask};
            return true;
        }
        return false;
    }
    std::optional<std::uint32_t> restore(EffectMaskIdentity identity) {
        for (auto& e : entries_) if (e.identity.executor == identity.executor && identity.executor) {
            const auto saved = e;
            e = {};
            if (saved.identity == identity) return saved.mask;
            return {};
        }
        return {};
    }
};
}

#include <cstddef>
#include <utility>

namespace self_recall::pure {
inline bool exchangeParameterBytes(std::span<std::byte> live, std::span<std::byte> snapshot) {
    if (live.size() != snapshot.size()) return false;
    for (std::size_t i = 0; i < live.size(); ++i) std::swap(live[i], snapshot[i]);
    return true;
}
}
