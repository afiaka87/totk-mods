// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "Audio.hpp"
#include "AudioPlayback.hpp"
#include "AudioTables.hpp"
#include "totk/engine/CodeCheck.hpp"
#include <lib.hpp>

namespace botw_abilities::champions::audio {
namespace {
using totk::engine::readMemory;
using totk::engine::isPlausibleAddress;

constexpr std::uintptr_t kFindAsset = 0xAFD150,
                         kEmit = 0xAFD244, kFade = 0xAF78BC, kSoundSystem = 0x471A578;
constexpr totk::engine::CodeSite kSites[]{
    {kFindAsset, 0xA9BA7BFD, 0},
    {kEmit, 0xA9BA7BFD, 0}, {kFade, 0xA9BB7BFD, 0xF9000BF9}};
std::uintptr_t g_base{};
bool g_ready{};
Playback<kAssets.size()> g_playback;
SoundEvents g_events;
std::atomic_flag g_writing{};

struct Sink {
    bool play(Cue cue) { return audio::play(cue); }
    bool stop(Cue cue) { return audio::stop(cue); }
    bool sustain(Cue cue, bool active) { return audio::sustain(cue, active); }
    bool stopAll() { return audio::stopAll(); }
} g_sink;

struct Writer {
    bool acquired = !g_writing.test_and_set(std::memory_order_acquire);
    ~Writer() { if (acquired) g_writing.clear(std::memory_order_release); }
};

template <class Fn> Fn call(std::uintptr_t offset) {
    return reinterpret_cast<Fn>(g_base + offset);
}

std::uintptr_t speaker() {
    const auto system = readMemory<std::uintptr_t>(g_base + kSoundSystem);
    if (!isPlausibleAddress(system)) return 0;
    const int count = readMemory<int>(system + 32), link = readMemory<int>(system + 36);
    if (count < 0 || count > 256 || link < 0 || link > 1024) return 0;
    const auto anchor = system + 16;
    auto node = readMemory<std::uintptr_t>(system + 24);
    for (int i = 0; i < count && node != anchor && isPlausibleAddress(node); ++i) {
        if (node < static_cast<unsigned>(link)) return 0;
        const auto user = node - static_cast<unsigned>(link);
        const auto name = readMemory<std::uintptr_t>(user + 16);
        if (totk::engine::isPlausibleStringAddress(name) &&
            std::strcmp(reinterpret_cast<const char*>(name), "UI_GlobalSound") == 0) {
            const int instances = readMemory<int>(user + 48), offset = readMemory<int>(user + 52);
            const auto head = readMemory<std::uintptr_t>(user + 32);
            if (instances < 1 || instances > 1024 || offset < 0 || offset > 1024 ||
                head < static_cast<unsigned>(offset)) return 0;
            const auto instance = head - static_cast<unsigned>(offset);
            if (!isPlausibleAddress(instance)) return 0;
            const auto owner = readMemory<std::uintptr_t>(instance + 0x58);
            return isPlausibleAddress(owner) && isPlausibleAddress(readMemory<std::uintptr_t>(owner + 0x18))
                       ? instance : 0;
        }
        node = readMemory<std::uintptr_t>(node + 8);
    }
    return 0;
}

std::array<RefusalCounter, kAssets.size()> g_refusals{};
RefusalCounter g_startRefusals;

Handle emit(std::size_t cue) {
    const auto instance = speaker();
    if (!instance) {
        if (g_refusals[cue].report())
            Logging.Log("[champions] AUDIO_REFUSED cue=%zu reason=speaker count=%u", cue, g_refusals[cue].count);
        return {};
    }
    const auto owner = readMemory<std::uintptr_t>(instance + 0x58);
    const auto resource = readMemory<std::uintptr_t>(owner + 0x18);
    Locator locator{};
    const bool found = call<bool (*)(std::uintptr_t, Locator*, const char*)>(kFindAsset)(resource, &locator, kAssets[cue]);
    const auto address = reinterpret_cast<std::uintptr_t>(locator.table);
    if (!found || !donorTableAddress(address)) {
        if (g_refusals[cue].report())
            Logging.Log("[champions] AUDIO_REFUSED cue=%zu reason=private-call found=%u table=%p count=%u", cue,
                        unsigned(found), reinterpret_cast<void*>(address), g_refusals[cue].count);
        return {};
    }
    Handle handle;
    call<std::uintptr_t (*)(std::uintptr_t, const Locator*, Handle*, void*)>(kEmit)(instance, &locator, &handle, nullptr);
    return handle;
}

void fade(Handle handle) {
    // The native handle checks its serial before fading an event that may already have ended.
    call<void (*)(Handle*, unsigned)>(kFade)(&handle, 6);
}

bool start(Cue cue, bool looping) {
    const auto index = static_cast<std::size_t>(cue);
    Writer writer;
    if (!g_ready || !writer.acquired || index >= kAssets.size() || (looping && !kLoops[index])) {
        if (g_startRefusals.report())
            Logging.Log("[champions] AUDIO_REFUSED cue=%zu ready=%u writer=%u sustain=%u count=%u", index,
                        unsigned(g_ready), unsigned(writer.acquired), unsigned(looping), g_startRefusals.count);
        return false;
    }
    return g_playback.play(index, looping, emit, fade);
}
} // namespace

bool install(std::uintptr_t base) {
    if (g_ready) return true;
    if (!totk::engine::codeMatches(base, kSites, "champions", "AUDIO_INSTALL_REFUSED")) return false;
    g_base = base;
    g_ready = true;
    Logging.Log("[champions] AUDIO_INSTALLED cues=%zu native-private-calls=1", kAssets.size());
    return true;
}

bool play(Cue cue) { return start(cue, false); }

bool sustain(Cue cue, bool active) {
    return active ? start(cue, true) : stop(cue);
}

bool stop(Cue cue) {
    Writer writer;
    if (!g_ready) return true;
    if (!writer.acquired) {
        Logging.Log("[champions] AUDIO_STOP_REFUSED cue=%u reason=writer", unsigned(cue));
        return false;
    }
    g_playback.stop(static_cast<std::size_t>(cue), fade);
    return true;
}

bool stopAll() {
    Writer writer;
    if (!g_ready) return true;
    if (!writer.acquired) {
        Logging.Log("[champions] AUDIO_STOP_ALL_REFUSED reason=writer");
        return false;
    }
    g_playback.stopAll(fade);
    return true;
}

bool update(const SoundActivity& active) { return g_events.update(active, g_sink); }
} // namespace botw_abilities::champions::audio
