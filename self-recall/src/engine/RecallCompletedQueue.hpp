#pragma once

#include <cstring>
#include "RecallModelView.hpp"

namespace self_recall::model {

enum class CompletedQueueStatus { Ready, MissingBody, MissingQueue, Limit };
struct CompletedQueueReport {
    CompletedQueueStatus status = CompletedQueueStatus::MissingQueue;
    std::uint32_t singles = 0, groups = 0, visited = 0;
};

inline CompletedQueueReport inspectCompletedQueue(const void* queue, std::span<View> views,
                                                   unsigned bodyModels) {
    CompletedQueueReport result;
    if (!queue || !bodyModels || bodyModels > views.size()) return result;
    const auto read = []<class T>(const void* p, std::size_t offset) {
        T value;
        std::memcpy(&value, static_cast<const std::byte*>(p) + offset, sizeof(value));
        return value;
    };
    result.singles = read.operator()<std::uint32_t>(queue, 0x20);
    result.groups = read.operator()<std::uint32_t>(queue, 0x58);
    if (result.singles > 16384 || result.groups > 16384) {
        result.status = CompletedQueueStatus::Limit;
        return result;
    }
    const auto* singles = read.operator()<const void* const*>(queue, 0x28);
    const auto* groups = read.operator()<const void* const*>(queue, 0x60);
    if ((!singles && result.singles) || (!groups && result.groups)) return result;
    for (auto& view : views) view.pose.queueAdmission = 0;
    bool bodyPresent = false;
    const auto walk = [&](const void* node) {
        for (; node; node = read.operator()<const void*>(node, 8)) {
            if (++result.visited > 65536) { result.status = CompletedQueueStatus::Limit; return false; }
            const auto unit = read.operator()<std::uintptr_t>(node, 0);
            for (unsigned j = 0; j < views.size(); ++j) {
                if (views[j].identity.unit != unit) continue;
                if (read.operator()<std::uint8_t>(node, 0x1E) & 0x40u)
                    views[j].pose.queueAdmission = 1;
                if (j < bodyModels) bodyPresent = true;
            }
        }
        return true;
    };
    for (std::uint32_t i = 0; i < result.singles; ++i) {
        if (!singles[i]) return result;
        const auto* entries = read.operator()<const void* const*>(singles[i], 0x28);
        if (!entries || !entries[0]) return result;
        if (!walk(entries[0])) return result;
    }
    for (std::uint32_t i = 0; i < result.groups; ++i)
        if (!walk(groups[i])) return result;
    result.status = bodyPresent ? CompletedQueueStatus::Ready : CompletedQueueStatus::MissingBody;
    return result;
}

} // namespace self_recall::model
