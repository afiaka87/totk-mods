#pragma once
#include <array>
#include <algorithm>
#include <span>
#include <cstring>
#include "RecallPoseHistory.hpp"

namespace self_recall::pure {
template<unsigned Blocks, unsigned States, unsigned BlockBytes = 256>
class AppearanceBlobs {
    struct State { unsigned first = 0, bytes = 0, refs = 0; };
    std::array<State, States + 1> states_{};
    std::array<unsigned, Blocks + 1> next_{};
    std::array<std::array<std::byte, BlockBytes>, Blocks> bytes_{};
    unsigned freeBlock_ = 0, freeState_ = 0, available_ = 0;
public:
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
        if (!size(token) || --states_[token].refs) return;
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
        if (input.empty() || input.size() > UINT32_MAX || !freeState_ || blocks > available_) return 0;
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
