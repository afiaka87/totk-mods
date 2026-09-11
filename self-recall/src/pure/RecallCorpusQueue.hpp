#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <span>

namespace self_recall::pure {
enum class CorpusKind : std::uint32_t { Pose = 1, Appearance = 2, Binding = 3, Schema = 4 };
struct CorpusRecordHeader {
    std::uint32_t magic = 0x31524353, kind = 0, bytes = 0, checksum = 0;
    std::uint64_t sequence = 0, time = 0;
};
static_assert(sizeof(CorpusRecordHeader) == 32);

inline std::uint32_t corpusChecksum(std::span<const std::byte> bytes) {
    std::uint32_t hash = 2166136261u;
    for (auto byte : bytes) hash = (hash ^ std::to_integer<unsigned>(byte)) * 16777619u;
    return hash;
}

template<unsigned Slots, unsigned Bytes> class CorpusQueue {
public:
    struct Record { CorpusRecordHeader header; std::array<std::byte, Bytes> data; };
    static_assert(offsetof(Record, data) == sizeof(CorpusRecordHeader));
    bool push(CorpusKind kind, std::uint64_t time,
              std::span<const std::span<const std::byte>> parts) {
        if (producer_.test_and_set(std::memory_order_acquire)) { ++dropped_; return false; }
        struct Unlock { std::atomic_flag& flag; ~Unlock() { flag.clear(std::memory_order_release); } } unlock{producer_};
        unsigned size = 0;
        for (const auto part : parts) {
            if (part.size() > Bytes - size) { ++dropped_; return false; }
            size += static_cast<unsigned>(part.size());
        }
        const auto write = write_.load(std::memory_order_relaxed);
        if (write - read_.load(std::memory_order_acquire) == Slots) { ++dropped_; return false; }
        auto& record = records_[write % Slots];
        unsigned offset = 0;
        for (const auto part : parts) {
            if (!part.empty()) std::memcpy(record.data.data() + offset, part.data(), part.size());
            offset += static_cast<unsigned>(part.size());
        }
        record.header = {0x31524353, static_cast<std::uint32_t>(kind), size,
                         corpusChecksum({record.data.data(), size}), write + 1, time};
        write_.store(write + 1, std::memory_order_release);
        return true;
    }
    const Record* front() const {
        const auto read = read_.load(std::memory_order_relaxed);
        return read == write_.load(std::memory_order_acquire) ? nullptr : &records_[read % Slots];
    }
    void pop() { read_.fetch_add(1, std::memory_order_release); }
    std::uint64_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
private:
    std::array<Record, Slots> records_{};
    std::atomic<std::uint64_t> write_{0}, read_{0}, dropped_{0};
    std::atomic_flag producer_ = ATOMIC_FLAG_INIT;
};
} // namespace self_recall::pure
