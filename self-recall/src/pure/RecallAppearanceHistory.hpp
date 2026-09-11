#pragma once
#include <array>
#include <algorithm>
#include <span>
#include <cstring>
#include "RecallPoseHistory.hpp"
#include "RecallMemoryProfile.hpp"

namespace self_recall::pure {
template<unsigned Blocks, unsigned States, unsigned BlockBytes = 256>
class AppearanceBlobs {
    struct State { unsigned first = 0, bytes = 0, refs = 0; };
    std::array<State, States + 1> states_{};
    std::array<unsigned, Blocks + 1> next_{};
    std::array<std::array<std::byte, BlockBytes>, Blocks> bytes_{};
    unsigned freeBlock_ = 0, freeState_ = 0, available_ = 0;
#if SELF_RECALL_MEMORY_PROFILE
    PoolMemoryUsage usage_{};
#endif
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
#if SELF_RECALL_MEMORY_PROFILE
    const PoolMemoryUsage& memoryUsage() const { return usage_; }
#endif
    void initialize() {
#if SELF_RECALL_MEMORY_PROFILE
        usage_ = {};
#endif
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
#if SELF_RECALL_MEMORY_PROFILE
        usage_.retain();
#endif
        return true;
    }
    void release(unsigned token) {
        if (!size(token)) return;
#if SELF_RECALL_MEMORY_PROFILE
        usage_.dropReference();
#endif
        if (--states_[token].refs) return;
#if SELF_RECALL_MEMORY_PROFILE
        usage_.release(states_[token].bytes, (states_[token].bytes + BlockBytes - 1) / BlockBytes * BlockBytes);
#endif
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
#if SELF_RECALL_MEMORY_PROFILE
            ++usage_.failures;
#endif
            return 0;
        }
        const auto token = freeState_; freeState_ = states_[token].first;
        auto& state = states_[token]; state = {0, static_cast<unsigned>(input.size()), 1};
#if SELF_RECALL_MEMORY_PROFILE
        usage_.create(state.bytes, static_cast<unsigned>(blocks * BlockBytes));
#endif
        unsigned* link = &state.first;
        for (unsigned offset = 0; offset < input.size();) {
            const auto block = freeBlock_; freeBlock_ = next_[block]; --available_;
            *link = block; link = &next_[block]; *link = 0;
            const auto count = static_cast<unsigned>(std::min<std::size_t>(BlockBytes, input.size() - offset));
            std::memcpy(bytes_[block - 1].data(), input.data() + offset, count); offset += count;
        }
        return token;
    }
    bool copy(unsigned token, std::span<std::byte> output) const {
        if (!size(token) || output.size() != size(token)) return false;
        auto block = states_[token].first;
        for (unsigned offset = 0; offset < output.size();) {
            const auto count = static_cast<unsigned>(std::min<std::size_t>(BlockBytes, output.size() - offset));
            if (!block) return false;
            std::memcpy(output.data() + offset, bytes_[block - 1].data(), count);
            block = next_[block]; offset += count;
        }
        return true;
    }
};

template<unsigned Capacity = kHistoryCapacity, unsigned Models = kPoseModelLimit>
class AppearanceFrames {
    struct Entry { PoseFrameKey key{}; std::array<unsigned, Models> tokens{}; };
    std::array<Entry, Capacity> frames_{};
public:
    template<class Blobs, class Retired> unsigned collect(Blobs& blobs, Retired retired) {
        unsigned released = 0;
        for (auto& frame : frames_) {
            if (!frame.key || !retired(frame.key)) continue;
            for (auto token : frame.tokens) blobs.release(token);
            frame = {};
            ++released;
        }
        return released;
    }
    template<class Blobs> void clear(Blobs& blobs) {
        for (auto& frame : frames_) {
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
} // namespace self_recall::pure
