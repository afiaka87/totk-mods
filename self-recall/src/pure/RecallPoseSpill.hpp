#pragma once

// Included by RecallPoseData.hpp after the payload store and chain decoder.

namespace self_recall::pure {

inline constexpr unsigned kSpillCacheBytes = 2u * kMiB;
inline constexpr unsigned kSpillCacheBlockCount = kSpillCacheBytes / sizeof(PosePayloadBlock);
inline constexpr unsigned kSpillWriteRingBytes = 384u * 1024u;
inline constexpr std::uint64_t kSpillFileBytes = 16ull * kMiB;
// Played groups stay cached this many frames so the render thread's slightly older key still decodes.
inline constexpr unsigned kSpillPlayedMarginFrames = 30;
inline constexpr std::uint32_t kSpillJobMagic = 0x4A475253;

enum class SpillState : std::uint8_t { Free, Open, RamOnly, Pending, Written, Lost };
enum class SpillTier : std::uint8_t { Ram, Sd };
enum class SpillAvailability : std::uint8_t { Available, Loading, Lost };

inline std::uint64_t spillStateValue(std::uint64_t token, SpillState state) {
    return (token << 8) | static_cast<std::uint8_t>(state);
}
inline SpillState spillState(std::uint64_t value) { return static_cast<SpillState>(value & 0xFF); }
inline std::uint64_t spillToken(std::uint64_t value) { return value >> 8; }

struct SpillJobHeader {
    std::uint32_t magic = kSpillJobMagic;
    std::uint32_t group = 0;
    std::uint64_t state = 0;
    std::uint32_t generation = 0;
    std::uint32_t nodes = 0;
    std::uint64_t firstSerial = 0;
    std::uint32_t nodeBytes[kPoseChainFrames]{};
    std::uint32_t dataBytes = 0;
    std::uint32_t reserved = 0;
};
static_assert(sizeof(SpillJobHeader) == 72);

inline constexpr unsigned kSpillMaxGroupBytes =
    sizeof(SpillJobHeader) + kPoseChainFrames * (kPosePayloadMaxBytes + 8u);

// Group fields other than the atomics are written by the recorder before the state
// leaves Free/Open, or by the worker while it holds the claim writer bit.
struct SpillGroup {
    std::atomic<std::uint64_t> state{0};
    std::atomic<std::uint32_t> claims{0};
    std::atomic<bool> cached{false};
    std::atomic<bool> hasSd{false};
    bool released = false;
    std::uint8_t nodes = 0;
    std::uint32_t generation = 0;
    std::uint32_t firstSlot = 0;
    std::uint64_t firstSerial = 0;
    std::uint64_t lastSerial = 0;
    std::uint64_t newestNanoseconds = 0;
    std::uint64_t fileOffset = 0;
    std::uint32_t fileBytes = 0;
    std::uint32_t cacheFirst[kPoseChainFrames]{};
    std::uint32_t cacheBytes[kPoseChainFrames]{};
};

class SpillFile {
public:
    virtual bool write(std::uint64_t offset, std::span<const std::byte> bytes) = 0;
    virtual bool read(std::uint64_t offset, std::span<std::byte> bytes) = 0;
    virtual std::uint64_t nanoseconds() = 0;
protected:
    ~SpillFile() = default;
};

enum class SpillIoKind : std::uint8_t { None, Write, Read, Evict, WriteFailed, ReadFailed, Invalidated };

struct SpillIoReport {
    SpillIoKind kind = SpillIoKind::None;
    std::uint32_t group = 0;
    std::uint32_t bytes = 0;
    std::uint64_t serial = 0;
    std::uint64_t offset = 0;
    std::uint64_t nanoseconds = 0;
    explicit operator bool() const { return kind != SpillIoKind::None; }
};

struct SpillStats {
    std::atomic<std::uint64_t> writes{0}, writeBytes{0}, writeNanoseconds{0}, maxWriteNanoseconds{0};
    std::atomic<std::uint64_t> reads{0}, readBytes{0}, readNanoseconds{0}, maxReadNanoseconds{0};
    std::atomic<std::uint64_t> released{0}, ramOnly{0}, lost{0}, trimmedForSpace{0}, rejectedWhileWriting{0};
    std::atomic<std::uint64_t> failures{0}, overwritten{0};
    std::atomic<std::uint64_t> cacheBlocksUsed{0}, poolBlocksAvailable{0};
};

class PoseSpill {
public:
    explicit PoseSpill(std::span<PosePayloadBlock> cacheBlocks, std::span<SpillGroup> groups,
                       std::span<std::byte> writeRing, std::span<std::byte> ioBuffer)
        : cache_(cacheBlocks), groups_(groups), ring_(writeRing), io_(ioBuffer) {}

    PoseSpill(const PoseSpill&) = delete;
    PoseSpill& operator=(const PoseSpill&) = delete;

    const PosePayloadStore& cache() const { return cache_; }
    std::span<SpillGroup> groups() { return groups_; }
    std::span<const SpillGroup> groups() const { return groups_; }
    SpillStats& stats() { return stats_; }
    const SpillStats& stats() const { return stats_; }

    // Worker publishes whether the SD file is usable; recorder treats disabled as RAM only.
    void setEnabled(bool enabled) { enabled_.store(enabled, std::memory_order_release); }
    bool enabled() const { return enabled_.load(std::memory_order_acquire); }

    // Recorder side -------------------------------------------------------------------
    void resetGeneration(std::uint32_t generation) {
        generation_.store(generation, std::memory_order_release);
        open_ = kNone;
        tail_ = head_;
    }
    std::uint32_t generation() const { return generation_.load(std::memory_order_acquire); }

    // Returns the group index and token for a new anchor, or kNone when no entry is free.
    std::uint32_t openGroup(std::uint32_t slot, std::uint64_t serial, std::uint64_t nanoseconds,
                            std::uint64_t& token) {
        open_ = kNone;
        if (groups_.empty()) return kNone;
        const auto index = head_;
        auto& group = groups_[index];
        std::uint32_t expected = 0;
        if (!group.claims.compare_exchange_strong(expected, kWriter, std::memory_order_acquire)) return kNone;
        if (group.cached.load(std::memory_order_acquire)) {
            group.claims.store(0, std::memory_order_release);
            return kNone;
        }
        token = ++nextToken_;
        group.generation = generation();
        group.firstSlot = slot;
        group.firstSerial = group.lastSerial = serial;
        group.newestNanoseconds = nanoseconds;
        group.nodes = 1;
        group.released = false;
        group.hasSd.store(false, std::memory_order_release);
        group.state.store(spillStateValue(token, SpillState::Open), std::memory_order_release);
        group.claims.store(0, std::memory_order_release);
        head_ = (head_ + 1) % groups_.size();
        if (head_ == tail_) tail_ = (tail_ + 1) % groups_.size();
        open_ = index;
        return index;
    }

    bool extendGroup(std::uint32_t index, std::uint64_t serial, std::uint64_t nanoseconds) {
        if (index != open_ || index >= groups_.size()) return false;
        auto& group = groups_[index];
        if (group.nodes >= kPoseChainFrames || serial != group.lastSerial + 1) return false;
        ++group.nodes;
        group.lastSerial = serial;
        group.newestNanoseconds = nanoseconds;
        return true;
    }

    std::uint32_t openIndex() const { return open_; }

    // Copies a closed group's node bytes into the write ring. `load` fills one node.
    template<class LoadNode>
    bool enqueue(std::uint32_t index, LoadNode&& load) {
        const auto scratch = std::span{scratch_};
        if (index >= groups_.size()) return false;
        auto& group = groups_[index];
        const auto value = group.state.load(std::memory_order_acquire);
        if (spillState(value) != SpillState::Open) return false;
        if (index == open_) open_ = kNone;
        SpillJobHeader header;
        header.group = index;
        header.state = spillStateValue(spillToken(value), SpillState::Pending);
        header.generation = group.generation;
        header.nodes = group.nodes;
        header.firstSerial = group.firstSerial;
        auto markRamOnly = [&] {
            group.state.store(spillStateValue(spillToken(value), SpillState::RamOnly), std::memory_order_release);
            stats_.ramOnly.fetch_add(1, std::memory_order_relaxed);
            return false;
        };
        if (!enabled()) return markRamOnly();
        // Measure first so a partially written job never becomes visible.
        std::uint32_t total = 0;
        for (unsigned n = 0; n < group.nodes; ++n) {
            const auto bytes = load(n, std::span<std::byte>{});
            if (!bytes) return markRamOnly();
            header.nodeBytes[n] = bytes;
            total += bytes;
        }
        header.dataBytes = total;
        const auto needed = std::uint64_t{sizeof(header)} + total;
        const auto used = writePos_.load(std::memory_order_acquire) - readPos_.load(std::memory_order_acquire);
        if (needed > kSpillMaxGroupBytes || needed > ring_.size() - used) return markRamOnly();
        auto position = writePos_.load(std::memory_order_acquire);
        group.state.store(header.state, std::memory_order_release);
        ringCopyIn(position, std::as_bytes(std::span{&header, 1}));
        position += sizeof(header);
        for (unsigned n = 0; n < group.nodes; ++n) {
            const auto bytes = load(n, scratch);
            if (bytes != header.nodeBytes[n]) {
                // The recorder owns these slots; a mismatch means the payload changed underneath.
                group.state.store(spillStateValue(spillToken(value), SpillState::RamOnly), std::memory_order_release);
                stats_.failures.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            ringCopyIn(position, scratch.first(bytes));
            position += bytes;
        }
        writePos_.store(position, std::memory_order_release);
        return true;
    }

    std::uint64_t pendingBytes() const {
        return writePos_.load(std::memory_order_acquire) - readPos_.load(std::memory_order_acquire);
    }

    // Worker side ---------------------------------------------------------------------
    void setPlayback(bool active, std::uint32_t generation, std::uint64_t serial) {
        playbackSerial_.store(serial, std::memory_order_release);
        playbackGeneration_.store(generation, std::memory_order_release);
        playbackActive_.store(active, std::memory_order_release);
    }
    bool playbackActive() const { return playbackActive_.load(std::memory_order_acquire); }

    SpillIoReport pump(SpillFile& file) {
        if (auto report = writeOne(file)) return report;
        if (auto report = evictOne()) return report;
        return loadOne(file);
    }

    // Reader side ---------------------------------------------------------------------
    SpillAvailability availability(std::uint32_t index, std::uint32_t token) const {
        if (index >= groups_.size()) return SpillAvailability::Lost;
        const auto& group = groups_[index];
        const auto value = group.state.load(std::memory_order_acquire);
        if ((spillToken(value) & 0xFFFFFFFFu) != token) return SpillAvailability::Lost;
        if (group.cached.load(std::memory_order_acquire)) return SpillAvailability::Available;
        const auto state = spillState(value);
        return state == SpillState::Written ? SpillAvailability::Loading : SpillAvailability::Lost;
    }

    bool decode(std::uint32_t index, std::uint32_t token, unsigned node,
                PoseDecodedFrame& decoded, const PoseFrameHeader& header) const {
        if (index >= groups_.size() || node >= kPoseChainFrames) return false;
        auto& group = groups_[index];
        auto readers = group.claims.load(std::memory_order_relaxed);
        do {
            if (readers >= kWriter - 1) return false;
        } while (!group.claims.compare_exchange_weak(readers, readers + 1, std::memory_order_acquire,
                                                     std::memory_order_relaxed));
        const bool valid = group.cached.load(std::memory_order_acquire) &&
            (spillToken(group.state.load(std::memory_order_acquire)) & 0xFFFFFFFFu) == token &&
            node < group.nodes && group.cacheBytes[node];
        const bool ok = valid && decodePoseChain(cache_, group.cacheFirst[node], group.cacheBytes[node], decoded, header);
        group.claims.fetch_sub(1, std::memory_order_release);
        return ok;
    }

    // Recorder release bookkeeping, used by PoseHistory.
    template<class Visit>
    void forEachLiveGroup(Visit&& visit) {
        for (auto index = tail_; index != head_; index = (index + 1) % groups_.size())
            if (!visit(index, groups_[index])) break;
    }
    void advanceTail(std::uint64_t oldestSerial) {
        while (tail_ != head_) {
            const auto& group = groups_[tail_];
            if (group.generation == generation() && group.lastSerial >= oldestSerial) break;
            tail_ = (tail_ + 1) % groups_.size();
        }
    }

    static constexpr std::uint32_t kNone = UINT32_MAX;

private:
    static constexpr std::uint32_t kWriter = 1u << 31;

    void ringCopyIn(std::uint64_t position, std::span<const std::byte> bytes) {
        const auto start = static_cast<std::size_t>(position % ring_.size());
        const auto first = std::min(bytes.size(), ring_.size() - start);
        std::memcpy(ring_.data() + start, bytes.data(), first);
        if (first < bytes.size()) std::memcpy(ring_.data(), bytes.data() + first, bytes.size() - first);
    }
    void ringCopyOut(std::uint64_t position, std::span<std::byte> bytes) const {
        const auto start = static_cast<std::size_t>(position % ring_.size());
        const auto first = std::min(bytes.size(), ring_.size() - start);
        std::memcpy(bytes.data(), ring_.data() + start, first);
        if (first < bytes.size()) std::memcpy(bytes.data() + first, ring_.data(), bytes.size() - first);
    }

    static void recordMax(std::atomic<std::uint64_t>& target, std::uint64_t value) {
        auto current = target.load(std::memory_order_relaxed);
        while (value > current && !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {}
    }

    SpillIoReport writeOne(SpillFile& file) {
        const auto read = readPos_.load(std::memory_order_acquire);
        if (writePos_.load(std::memory_order_acquire) == read) return {};
        SpillJobHeader header;
        ringCopyOut(read, std::as_writable_bytes(std::span{&header, 1}));
        const auto total = sizeof(header) + header.dataBytes;
        SpillIoReport report{SpillIoKind::Write, header.group, static_cast<std::uint32_t>(total), header.firstSerial};
        if (header.magic != kSpillJobMagic || total > io_.size() || header.group >= groups_.size()) {
            // A corrupt job cannot be skipped safely; discard everything queued.
            readPos_.store(writePos_.load(std::memory_order_acquire), std::memory_order_release);
            stats_.failures.fetch_add(1, std::memory_order_relaxed);
            report.kind = SpillIoKind::WriteFailed;
            return report;
        }
        ringCopyOut(read, io_.first(total));
        readPos_.store(read + total, std::memory_order_release);
        auto& group = groups_[header.group];
        if (header.generation != generation() || group.state.load(std::memory_order_acquire) != header.state) {
            report.kind = SpillIoKind::Invalidated;  // superseded by a history reset
            return report;
        }
        if (filePos_ + total > kSpillFileBytes) filePos_ = 0;
        invalidateOverlaps(filePos_, total);
        const auto start = file.nanoseconds();
        const bool ok = file.write(filePos_, io_.first(total));
        report.nanoseconds = file.nanoseconds() - start;
        report.offset = filePos_;
        auto expected = header.state;
        if (!ok) {
            group.state.compare_exchange_strong(expected, spillStateValue(spillToken(header.state), SpillState::RamOnly),
                                                std::memory_order_acq_rel);
            stats_.failures.fetch_add(1, std::memory_order_relaxed);
            report.kind = SpillIoKind::WriteFailed;
            return report;
        }
        // Pending groups are never cached, so only a brief reader probe can hold the claim.
        for (;;) {
            std::uint32_t claims = 0;
            if (group.claims.compare_exchange_weak(claims, kWriter, std::memory_order_acquire)) break;
        }
        group.fileOffset = filePos_;
        group.fileBytes = static_cast<std::uint32_t>(total);
        group.claims.store(0, std::memory_order_release);
        group.state.compare_exchange_strong(expected, spillStateValue(spillToken(header.state), SpillState::Written),
                                            std::memory_order_acq_rel);
        filePos_ += total;
        stats_.writes.fetch_add(1, std::memory_order_relaxed);
        stats_.writeBytes.fetch_add(total, std::memory_order_relaxed);
        stats_.writeNanoseconds.fetch_add(report.nanoseconds, std::memory_order_relaxed);
        recordMax(stats_.maxWriteNanoseconds, report.nanoseconds);
        return report;
    }

    void invalidateOverlaps(std::uint64_t offset, std::uint64_t bytes) {
        for (std::uint32_t i = 0; i < groups_.size(); ++i) {
            auto& group = groups_[i];
            auto value = group.state.load(std::memory_order_acquire);
            if (spillState(value) != SpillState::Written) continue;
            if (group.fileOffset >= offset + bytes || offset >= group.fileOffset + group.fileBytes) continue;
            if (group.state.compare_exchange_strong(value, spillStateValue(spillToken(value), SpillState::Lost),
                                                    std::memory_order_acq_rel))
                stats_.overwritten.fetch_add(1, std::memory_order_relaxed);
        }
    }

    SpillIoReport evictOne() {
        const bool active = playbackActive();
        const auto generation = playbackGeneration_.load(std::memory_order_acquire);
        const auto serial = playbackSerial_.load(std::memory_order_acquire);
        for (std::uint32_t i = 0; i < groups_.size(); ++i) {
            auto& group = groups_[i];
            if (!group.cached.load(std::memory_order_acquire)) continue;
            const bool played = group.firstSerial > serial + kSpillPlayedMarginFrames;
            if (active && group.generation == generation && !played) continue;
            std::uint32_t claims = 0;
            if (!group.claims.compare_exchange_strong(claims, kWriter, std::memory_order_acquire)) continue;
            group.cached.store(false, std::memory_order_release);
            releaseCached(group);
            group.claims.store(0, std::memory_order_release);
            return {SpillIoKind::Evict, i, 0, group.firstSerial};
        }
        return {};
    }

    void releaseCached(SpillGroup& group) {
        for (unsigned n = group.nodes; n--;) {
            if (group.cacheBytes[n]) cache_.release(group.cacheFirst[n], group.cacheBytes[n]);
            group.cacheBytes[n] = 0;
        }
        stats_.cacheBlocksUsed.store(cacheUsed(), std::memory_order_relaxed);
    }

    std::uint64_t cacheUsed() const {
        std::uint64_t used = 0;
        for (const auto& group : groups_)
            if (group.cached.load(std::memory_order_acquire))
                for (unsigned n = 0; n < group.nodes; ++n) used += PosePayloadStore::blocksFor(group.cacheBytes[n]);
        return used;
    }

    SpillIoReport loadOne(SpillFile& file) {
        if (!playbackActive()) return {};
        const auto generation = playbackGeneration_.load(std::memory_order_acquire);
        const auto serial = playbackSerial_.load(std::memory_order_acquire);
        std::uint32_t best = kNone;
        for (std::uint32_t i = 0; i < groups_.size(); ++i) {
            const auto& group = groups_[i];
            if (group.generation != generation || !group.hasSd.load(std::memory_order_acquire) ||
                group.cached.load(std::memory_order_acquire) ||
                spillState(group.state.load(std::memory_order_acquire)) != SpillState::Written ||
                group.firstSerial > serial + kSpillPlayedMarginFrames) continue;
            if (best == kNone || group.lastSerial > groups_[best].lastSerial) best = i;
        }
        if (best == kNone) return {};
        auto& group = groups_[best];
        std::uint32_t claims = 0;
        if (!group.claims.compare_exchange_strong(claims, kWriter, std::memory_order_acquire)) return {};
        struct Unclaim { SpillGroup& g; ~Unclaim() { g.claims.store(0, std::memory_order_release); } } unclaim{group};
        const auto state = group.state.load(std::memory_order_acquire);
        SpillIoReport report{SpillIoKind::Read, best, group.fileBytes, group.firstSerial, group.fileOffset};
        if (spillState(state) != SpillState::Written || group.fileBytes > io_.size() ||
            group.fileBytes < sizeof(SpillJobHeader)) return {};
        std::uint32_t blocks = 0;
        const auto start = file.nanoseconds();
        const bool ok = file.read(group.fileOffset, io_.first(group.fileBytes));
        report.nanoseconds = file.nanoseconds() - start;
        SpillJobHeader header;
        if (ok) std::memcpy(&header, io_.data(), sizeof(header));
        const bool valid = ok && header.magic == kSpillJobMagic && header.state == spillStateValue(spillToken(state), SpillState::Pending) &&
            header.group == best && header.nodes == group.nodes && header.nodes <= kPoseChainFrames &&
            sizeof(header) + header.dataBytes == group.fileBytes;
        if (valid)
            for (unsigned n = 0; n < header.nodes; ++n) blocks += PosePayloadStore::blocksFor(header.nodeBytes[n]);
        if (!valid) {
            auto expected = state;
            if (group.state.compare_exchange_strong(expected, spillStateValue(spillToken(state), SpillState::Lost),
                                                    std::memory_order_acq_rel))
                stats_.lost.fetch_add(1, std::memory_order_relaxed);
            stats_.failures.fetch_add(1, std::memory_order_relaxed);
            report.kind = SpillIoKind::ReadFailed;
            return report;
        }
        if (blocks > cacheAvailable()) return {};
        std::size_t offset = sizeof(header);
        unsigned parent = UINT32_MAX, parentBytes = 0;
        for (unsigned n = 0; n < header.nodes; ++n) {
            const auto bytes = header.nodeBytes[n];
            const auto first = cache_.store(io_.subspan(offset, bytes), parent, parentBytes);
            group.cacheFirst[n] = first;
            group.cacheBytes[n] = bytes;
            parent = first;
            parentBytes = bytes;
            offset += bytes;
        }
        group.cached.store(true, std::memory_order_release);
        stats_.cacheBlocksUsed.store(cacheUsed(), std::memory_order_relaxed);
        stats_.reads.fetch_add(1, std::memory_order_relaxed);
        stats_.readBytes.fetch_add(group.fileBytes, std::memory_order_relaxed);
        stats_.readNanoseconds.fetch_add(report.nanoseconds, std::memory_order_relaxed);
        recordMax(stats_.maxReadNanoseconds, report.nanoseconds);
        return report;
    }

    std::uint64_t cacheAvailable() const { return cache_.availableBlocks(); }

    PosePayloadStore cache_;
    std::span<SpillGroup> groups_;
    std::span<std::byte> ring_;
    std::span<std::byte> io_;
    SpillStats stats_;
    std::atomic<bool> enabled_{false};
    std::atomic<std::uint32_t> generation_{0};
    std::atomic<std::uint64_t> writePos_{0}, readPos_{0};
    std::atomic<std::uint64_t> playbackSerial_{0};
    std::atomic<std::uint32_t> playbackGeneration_{0};
    std::atomic<bool> playbackActive_{false};
    std::uint64_t nextToken_ = 0;
    std::array<std::byte, kPosePayloadMaxBytes + 8> scratch_{};
    std::uint64_t filePos_ = 0;
    std::uint32_t head_ = 0, tail_ = 0, open_ = kNone;
};

}
