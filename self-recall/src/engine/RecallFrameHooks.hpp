#pragma once

#include <cstdint>

namespace self_recall::frame {

struct CompletedModelPhase {
    void* scene = nullptr;
    void* queue = nullptr;
    std::uint64_t epoch = 0;
    std::uint32_t queuedGroups = 0;
    std::uint32_t queuedSingles = 0;
};

struct Observers {
    void (*beginFrame)(std::uint64_t epoch) = nullptr;
    void (*modelsComplete)(const CompletedModelPhase&) = nullptr;
    void (*prepareScene)(void* scene, std::uint64_t epoch) = nullptr;
};

struct Diagnostics {
    std::uint64_t frames = 0;
    std::uint64_t completedScenes = 0;
    std::uint64_t rejectedOwners = 0;
    std::uint64_t singleCompletions = 0;
    std::uint64_t multiCompletions = 0;
    std::uint64_t joinFailures = 0;
};

void install(Observers observers = {});
Diagnostics diagnostics();

}  // namespace self_recall::frame
