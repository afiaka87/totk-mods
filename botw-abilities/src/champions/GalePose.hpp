// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>

namespace botw_abilities::champions {

struct LocalPose {
    float position[3]{};
    float rotation[4]{0, 0, 0, 1};
};
static_assert(sizeof(LocalPose) == 28);
static_assert(std::endian::native == std::endian::little);

class GalePose {
public:
    static std::optional<GalePose> parse(std::span<const std::byte> bytes) {
        if (bytes.size() < headerSize || std::memcmp(bytes.data(), "CBGP", 4) != 0) return {};
        GalePose asset(bytes);
        if (asset.read<std::uint16_t>(4) != 2 || asset.read<std::uint16_t>(8) != 3 ||
            asset.read<std::uint16_t>(10) != sizeof(LocalPose) || !asset.boneCount_ ||
            asset.boneCount_ > 128 || (asset.step_ != .25f && asset.step_ != .5f)) return {};
        if (bytes.size() < asset.poseOffset() || asset.checksum() != asset.read<std::uint32_t>(16)) return {};
        for (unsigned i = 0; i < asset.boneCount_; ++i) {
            const auto name = asset.boneName(i);
            const auto parent = asset.parent(i);
            if (name.empty() || (i == 0 ? parent != -1 : parent < 0 || parent >= int(i))) return {};
            const auto flags = asset.channels(i);
            if (flags & ~unsigned(animatedPosition | animatedRotation)) return {};
            asset.stride_ += (flags & animatedPosition ? 12 : 0) + (flags & animatedRotation ? 8 : 0);
            for (unsigned j = 0; j < i; ++j) if (asset.boneName(j) == name) return {};
            if (!valid(asset.read<LocalPose>(asset.restOffset() + i * sizeof(LocalPose)))) return {};
        }
        std::size_t total = 0;
        constexpr std::string_view names[]{"Nml_Wait_To_Squat_Wait", "Squat_Wait_FootL", "Squat_Wait_FootR"};
        for (unsigned i = 0; i < 3; ++i) {
            const auto offset = asset.clipOffset(i);
            const auto count = asset.read<std::uint16_t>(offset + 32);
            if (asset.name(offset) != names[i] || count < 2 ||
                asset.read<std::uint16_t>(offset + 34) != unsigned(i != 0) ||
                asset.read<std::uint32_t>(offset + 36) != total) return {};
            total += count;
        }
        if (bytes.size() - asset.poseOffset() != total * asset.stride_) return {};
        for (std::size_t sample = 0; sample < total; ++sample) {
            std::size_t cursor = 0;
            for (unsigned bone = 0; bone < asset.boneCount_; ++bone)
                if (!valid(asset.decode(asset.poseOffset() + sample * asset.stride_, bone, cursor))) return {};
        }
        float distance[2]{};
        std::size_t end = 0, left = 0, right = 0;
        for (unsigned bone = 0; bone < asset.boneCount_; ++bone) {
            const auto last = asset.decode(asset.row(0, asset.samples(0) - 1), bone, end);
            distance[0] += 1 - std::abs(dot(last, asset.decode(asset.row(1, 0), bone, left)));
            distance[1] += 1 - std::abs(dot(last, asset.decode(asset.row(2, 0), bone, right)));
        }
        asset.continuation_ = distance[0] <= distance[1] ? 1 : 2;
        return asset;
    }

    unsigned boneCount() const { return boneCount_; }
    // The foot loop whose first frame continues the intro's last frame.
    unsigned continuation() const { return continuation_; }
    std::uint32_t fingerprint() const { return read<std::uint32_t>(16); }
    std::string_view boneName(unsigned bone) const { return bone < boneCount_ ? name(headerSize + bone * boneSize) : std::string_view{}; }
    int parent(unsigned bone) const { return bone < boneCount_ ? read<std::int16_t>(headerSize + bone * boneSize + 32) : -1; }
    float duration(unsigned clip) const { return clip < 3 ? (samples(clip) - 1) * step_ : 0; }

    bool sample(unsigned clip, float frame, std::span<LocalPose> out) const {
        if (clip >= 3 || out.size() < boneCount_ || !std::isfinite(frame)) return false;
        const auto count = samples(clip);
        const float length = (count - 1) * step_;
        const float position = clip == 0 ? std::clamp(frame, 0.0f, length) : std::fmod(std::max(0.0f, frame), length);
        const float bounded = position / step_;
        const auto a = unsigned(bounded), b = std::min(a + 1, count - 1);
        const float t = bounded - a;
        std::size_t leftCursor = 0, rightCursor = 0;
        for (unsigned i = 0; i < boneCount_; ++i) {
            const auto left = decode(row(clip, a), i, leftCursor), right = decode(row(clip, b), i, rightCursor);
            LocalPose result;
            for (unsigned axis = 0; axis < 3; ++axis)
                result.position[axis] = left.position[axis] + t * (right.position[axis] - left.position[axis]);
            float dot = 0, norm = 0;
            for (unsigned axis = 0; axis < 4; ++axis) dot += left.rotation[axis] * right.rotation[axis];
            for (unsigned axis = 0; axis < 4; ++axis) {
                result.rotation[axis] = left.rotation[axis] + t * ((dot < 0 ? -right.rotation[axis] : right.rotation[axis]) - left.rotation[axis]);
                norm += result.rotation[axis] * result.rotation[axis];
            }
            const float inverse = 1.0f / std::sqrt(norm);
            for (float& value : result.rotation) value *= inverse;
            out[i] = result;
        }
        return true;
    }

    bool sampleChannel(float frames, unsigned loop, std::span<LocalPose> out, std::span<LocalPose> morph) const {
        if (!std::isfinite(frames) || frames < 0 || loop < 1 || loop > 2) return false;
        const float intro = duration(0);
        if (frames < intro) return sample(0, frames, out);
        if (!sample(loop, frames - intro, out)) return false;
        const float blend = std::min(1.0f, (frames - intro) / 6.0f);
        if (blend == 1) return true;
        if (out.data() == morph.data() || !sample(0, intro, morph)) return false;
        for (unsigned bone = 0; bone < boneCount_; ++bone) {
            auto& to = out[bone];
            const auto& from = morph[bone];
            float dot = 0, norm = 0;
            for (unsigned axis = 0; axis < 3; ++axis)
                to.position[axis] = from.position[axis] + blend * (to.position[axis] - from.position[axis]);
            for (unsigned axis = 0; axis < 4; ++axis) dot += from.rotation[axis] * to.rotation[axis];
            for (unsigned axis = 0; axis < 4; ++axis) {
                to.rotation[axis] = from.rotation[axis] + blend * ((dot < 0 ? -to.rotation[axis] : to.rotation[axis]) - from.rotation[axis]);
                norm += to.rotation[axis] * to.rotation[axis];
            }
            if (!std::isfinite(norm) || norm < .0001f) return false;
            for (auto& value : to.rotation) value /= std::sqrt(norm);
        }
        return true;
    }

private:
    static constexpr std::size_t headerSize = 20, boneSize = 36, clipSize = 40;
    static constexpr unsigned animatedPosition = 1, animatedRotation = 2;
    std::span<const std::byte> bytes_;
    unsigned boneCount_;
    float step_;
    std::size_t stride_ = 0;
    unsigned continuation_ = 1;

    explicit GalePose(std::span<const std::byte> bytes) : bytes_(bytes), boneCount_(read<std::uint16_t>(6)), step_(read<float>(12)) {}
    template<class T> T read(std::size_t offset) const {
        T value;
        std::memcpy(&value, bytes_.data() + offset, sizeof(value));
        return value;
    }
    std::string_view name(std::size_t offset) const {
        const auto* text = reinterpret_cast<const char*>(bytes_.data() + offset);
        const auto* end = static_cast<const char*>(std::memchr(text, 0, 32));
        return end ? std::string_view(text, end) : std::string_view{};
    }
    std::size_t clipOffset(unsigned clip) const { return headerSize + boneCount_ * boneSize + clip * clipSize; }
    std::size_t restOffset() const { return clipOffset(3); }
    std::size_t poseOffset() const { return restOffset() + boneCount_ * sizeof(LocalPose); }
    unsigned samples(unsigned clip) const { return read<std::uint16_t>(clipOffset(clip) + 32); }
    unsigned channels(unsigned bone) const { return read<std::uint16_t>(headerSize + bone * boneSize + 34); }
    std::size_t row(unsigned clip, unsigned sample) const {
        return poseOffset() + (read<std::uint32_t>(clipOffset(clip) + 36) + sample) * stride_;
    }
    // Unchanging channels come from the rest pose; animated rotations are 16-bit components.
    LocalPose decode(std::size_t row, unsigned bone, std::size_t& cursor) const {
        auto pose = read<LocalPose>(restOffset() + bone * sizeof(LocalPose));
        const auto flags = channels(bone);
        if (flags & animatedPosition) {
            std::memcpy(pose.position, bytes_.data() + row + cursor, sizeof(pose.position));
            cursor += sizeof(pose.position);
        }
        if (flags & animatedRotation) {
            for (unsigned axis = 0; axis < 4; ++axis)
                pose.rotation[axis] = read<std::int16_t>(row + cursor + axis * 2) / 32767.0f;
            cursor += 8;
        }
        return pose;
    }
    static float dot(const LocalPose& a, const LocalPose& b) {
        float value = 0;
        for (unsigned axis = 0; axis < 4; ++axis) value += a.rotation[axis] * b.rotation[axis];
        return value;
    }
    static bool valid(const LocalPose& pose) {
        for (float value : pose.position) if (!std::isfinite(value)) return false;
        float norm = 0;
        for (float value : pose.rotation) norm += value * value;
        return std::isfinite(norm) && std::abs(norm - 1) <= .001f;
    }
    std::uint32_t checksum() const {
        std::uint32_t value = ~0u;
        for (auto byte : bytes_.subspan(headerSize)) {
            value ^= std::to_integer<std::uint8_t>(byte);
            for (unsigned bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xEDB88320u & (0u - (value & 1u)));
        }
        return ~value;
    }
};

} // namespace botw_abilities::champions
