// Copyright (c) HanSoloBlaster, afiaka87
#include "SharedFiles.hpp"

#include <atomic>
#include <cstdio>

#include <lib.hpp>
#include <nn/fs.h>

#include "pure/merge/GameData.hpp"
#include "pure/merge/Manifest.hpp"
#include "pure/merge/Messages.hpp"
#include "pure/merge/Rows.hpp"
#include "pure/merge/SizeTable.hpp"
#include "pure/merge/SoundBank.hpp"
#include "pure/merge/Tag.hpp"
#include "pure/merge/Zstd.hpp"

namespace fieldworks::shared {
namespace {

namespace merge = fieldworks::merge;

constexpr char kManifestFile[] = "content:/Fieldworks/Merge/manifest.byml";
constexpr unsigned kManifestTries = 8, kSampleCalls = 12;
// Added to a file's budget on top of the room the merge asks for and the budget's old margin.
constexpr std::uint32_t kBudgetMargin = 65536;

// The game's file-load request (sead FileDevice::LoadArg), as far as this code reads it. The request
// starts with the pointer to the file name; the game's string type has no other field before it.
constexpr std::ptrdiff_t kPath = 0x00, kCallerBuffer = 0x20, kCallerBytes = 0x28, kGranule = 0x2C, kDevice = 0x38;

template <typename T>
T& field(std::uint8_t* request, std::ptrdiff_t offset) { return *reinterpret_cast<T*>(request + offset); }

enum : int { kUnread, kReading, kReady, kUnusable };

alignas(8) std::uint8_t g_manifestBytes[8192];
merge::Manifest g_manifest;
std::atomic<int> g_manifestState{kUnread};
unsigned g_manifestTries = 0;
const char* g_version = "121";
std::atomic<bool> g_tableReady{false};
std::atomic<unsigned> g_calls{0}, g_seen{0};

// A game file opened for reading; closed when it goes out of scope.
struct File {
    nn::fs::FileHandle handle{};
    s64 size = 0;
    unsigned result = 0;
    bool opened = false, ok = false;

    explicit File(const char* path) {
        auto r = nn::fs::OpenFile(&handle, path, nn::fs::OpenMode_Read);
        opened = !r.IsFailure();
        if (opened) r = nn::fs::GetFileSize(&size, handle);
        result = r.IsFailure() ? r.GetInnerValueForDebug() : 0;
        ok = opened && !r.IsFailure() && size >= 0;
    }
    ~File() {
        if (opened) nn::fs::CloseFile(handle);
    }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    bool read(std::uint64_t offset, void* out, std::size_t bytes) {
        if (!ok || offset > static_cast<std::uint64_t>(size) || bytes > static_cast<std::uint64_t>(size) - offset)
            return false;
        const auto r = nn::fs::ReadFile(handle, static_cast<s64>(offset), out, bytes);
        if (r.IsFailure()) result = r.GetInnerValueForDebug();
        return !r.IsFailure();
    }
};

// What the game is about to load for an entry: the packed file's size and the size it unpacks to.
struct Peek {
    bool ok = false;
    std::uint64_t packed = 0, unpacked = 0;
    unsigned result = 0;
};

Peek peek(const merge::Entry& e) {
    char path[320];
    std::snprintf(path, sizeof(path), "content:/%s.zs", e.name);
    File file(path);
    Peek out;
    out.result = file.result;
    if (!file.ok) return out;
    std::uint8_t head[merge::kFrameHeadBytes];
    const std::size_t bytes = static_cast<std::uint64_t>(file.size) < sizeof(head) ? static_cast<std::size_t>(file.size)
                                                                                 : sizeof(head);
    out.packed = static_cast<std::uint64_t>(file.size);
    out.ok = file.read(0, head, bytes) && merge::frameContentSize(head, bytes, out.unpacked);
    out.result = file.result;
    return out;
}

bool isOwn(const merge::Entry& e, const Peek& seen) {
    return seen.ok && seen.packed == e.packed && seen.unpacked == e.size;
}

// Reads the list once. A failed read is tried again on later loads, a few times.
bool manifestReady() {
    for (;;) {
        int state = g_manifestState.load();
        if (state == kReady) return true;
        if (state == kUnusable) return false;
        if (state == kUnread && g_manifestState.compare_exchange_strong(state, kReading)) break;
        svcSleepThread(100'000);
    }
    File file(kManifestFile);
    const bool fits = file.ok && file.size > 0 && static_cast<std::size_t>(file.size) <= sizeof(g_manifestBytes);
    const bool ok = fits && file.read(0, g_manifestBytes, static_cast<std::size_t>(file.size)) &&
                    merge::readManifest(g_manifestBytes, static_cast<std::size_t>(file.size), g_version, g_manifest);
    ++g_manifestTries;
    Logging.Log("[fieldworks] SHARED_LIST file=%s game=%s ok=%u entries=%u bytes=%ld fs=%x try=%u", kManifestFile,
                g_version, ok, g_manifest.count, static_cast<long>(file.size), file.result, g_manifestTries);
    g_manifestState.store(ok ? kReady : g_manifestTries >= kManifestTries ? kUnusable : kUnread);
    return ok;
}

// Reads the entry's merge data and applies it to the loaded file in `work`.
bool apply(const merge::Entry& e, merge::Work& work, merge::Report& report, const merge::Budget* budgets,
           std::uint32_t budgetCount) {
    char path[320];
    std::snprintf(path, sizeof(path), "content:/%s", e.delta);
    File delta(path);
    if (!delta.ok || delta.size <= 0) return work.refuse("the merge data could not be opened");
    const std::size_t bytes = static_cast<std::size_t>(delta.size);
    if (e.kind == merge::Kind::Sounds) {
        const merge::Source source{[](void* context, merge::u64 offset, void* out, std::size_t n) {
            return static_cast<File*>(context)->read(offset, out, n);
        }, &delta};
        return merge::mergeSounds(work, source, bytes, report);
    }
    std::uint8_t* data = work.scratch(bytes);
    if (!data) return false;
    if (!delta.read(0, data, bytes)) return work.refuse("the merge data could not be read");
    switch (e.kind) {
        case merge::Kind::Table: return merge::mergeSizeTable(work, data, bytes, budgets, budgetCount, report);
        case merge::Kind::Rows: return merge::mergeRows(work, data, bytes, report);
        case merge::Kind::GameData: return merge::mergeGameData(work, data, bytes, report);
        case merge::Kind::Tag: return merge::mergeTag(work, data, bytes, report);
        case merge::Kind::Messages: return merge::mergeMessages(work, data, bytes, report);
        default: return work.refuse("the list names a kind this build does not know");
    }
}

// A shared file that is not Fieldworks' own copy, found while the size table loads.
struct Foreign {
    const merge::Entry* entry = nullptr;
    std::uint64_t unpacked = 0;
};

std::uint32_t foreignFiles(Foreign* out) {
    std::uint32_t n = 0;
    for (std::uint32_t i = 0; i < g_manifest.count; ++i) {
        const merge::Entry& e = g_manifest.entries[i];
        if (e.kind == merge::Kind::Table) continue;
        const Peek seen = peek(e);
        if (!seen.ok)
            Logging.Log("[fieldworks] SHARED_BUDGET file=%s result=left reason=file_unreadable fs=%x", e.name,
                        seen.result);
        else if (!isOwn(e, seen)) out[n++] = {&e, seen.unpacked};
    }
    return n;
}

// Loads one listed file through the game (`call`), asking for spare room, then adds Fieldworks' content.
template <typename Call>
std::uint8_t* loadShared(const merge::Entry& e, const char* path, std::uint8_t* request, std::uint32_t& size,
                         std::uint32_t& allocated, bool sizeReported, Call&& call) {
    const u64 started = svcGetSystemTick();
    const bool table = e.kind == merge::Kind::Table;
    const Peek seen = peek(e);
    Foreign foreign[merge::kMaxEntries];
    const std::uint32_t foreignCount = table ? foreignFiles(foreign) : 0;
    if (isOwn(e, seen) && !foreignCount) {
        if (table) g_tableReady = true;
        Logging.Log("[fieldworks] SHARED file=%s result=own path=%s", e.name, path);
        return call();
    }
    if (!seen.ok) {
        Logging.Log("[fieldworks] SHARED file=%s result=left reason=file_unreadable_before_load fs=%x path=%s",
                    e.name, seen.result, path);
        return call();
    }

    std::uint8_t* const callerBuffer = field<std::uint8_t*>(request, kCallerBuffer);
    const std::uint32_t callerBytes = field<std::uint32_t>(request, kCallerBytes);
    std::uint32_t& granule = field<std::uint32_t>(request, kGranule);
    const std::uint32_t granuleBefore = granule;
    std::uint64_t asked = merge::plannedBytes(e, seen.unpacked);
    if (granuleBefore && granuleBefore % 32 == 0) asked = merge::pad(static_cast<std::size_t>(asked), granuleBefore);
    const bool asking = !callerBuffer && asked <= 0x7fffffe0u;
    // The game rounds its allocation up to a multiple of this field, so one multiple is the whole request.
    if (asking) granule = static_cast<std::uint32_t>(asked);
    std::uint8_t* buffer = call();
    granule = granuleBefore;
    if (!buffer) {
        Logging.Log("[fieldworks] SHARED file=%s result=left reason=%s asked=%lu path=%s", e.name,
                    asking ? "larger_load_failed_loading_again" : "game_load_failed", asked, path);
        return asking ? call() : nullptr;
    }
    const std::size_t before = size, capacity = callerBuffer ? callerBytes : allocated;
    if (!before || capacity < before) {
        Logging.Log("[fieldworks] SHARED file=%s result=left reason=sizes_not_reported size=%zu allocated=%zu "
                    "caller_buffer=%u path=%s", e.name, before, capacity, callerBuffer != nullptr, path);
        return buffer;
    }

    merge::Budget budgets[merge::kMaxEntries];
    for (std::uint32_t i = 0; i < foreignCount; ++i) {
        // The other mod's budget kept some margin over its file; keep that margin over the larger load.
        const merge::Entry& other = *foreign[i].entry;
        const std::uint32_t had = merge::budgetOf(buffer, before, other.name);
        const std::uint64_t margin = had > foreign[i].unpacked ? had - foreign[i].unpacked : 0;
        const std::uint64_t want = merge::plannedBytes(other, foreign[i].unpacked) + margin + kBudgetMargin;
        budgets[i] = {other.name, want > 0xffffffffu ? 0xffffffffu : static_cast<std::uint32_t>(want)};
        Logging.Log("[fieldworks] SHARED_BUDGET file=%s result=planned had=%u unpacked=%lu budget=%u", other.name,
                    had, foreign[i].unpacked, budgets[i].size);
    }
    merge::Work work(buffer, before, capacity);
    merge::Report report;
    const bool ok = apply(e, work, report, budgets, foreignCount);
    if (ok) {
        size = static_cast<std::uint32_t>(work.size);
        if (table) g_tableReady = true;
    }
    Logging.Log("[fieldworks] SHARED file=%s kind=%s result=%s added=%u present=%u strings=%u skipped=%u "
                "size=%zu->%zu allocated=%zu asked=%lu caller_buffer=%u granule=%u size_reported=%u table_ready=%u "
                "ms=%lu reason=%s path=%s",
                e.name, merge::kindName(e.kind), ok ? "merged" : "left", report.added, report.present,
                report.strings, report.skipped, before, ok ? work.size : before, capacity, asked,
                callerBuffer != nullptr, granuleBefore, sizeReported, g_tableReady.load(),
                (svcGetSystemTick() - started) / 19200, work.refusal ? work.refusal : "none", path);
    return buffer;
}

// sead::ParallelZstdDecompressor::tryDecompFromDevice: reads a packed file and returns it unpacked.
HOOK_DEFINE_TRAMPOLINE(LoadHook) {
    static std::uint8_t* Callback(void* self, std::uint8_t* request, void* resource, std::uint32_t* outSize,
                                  std::uint32_t* outAllocated, bool* outNeedDelete) {
        const char* path = request ? field<const char*>(request, kPath) : nullptr;
        const unsigned call = g_calls.fetch_add(1);
        if (call < kSampleCalls)
            Logging.Log("[fieldworks] SHARED_SAMPLE call=%u path=%s caller_buffer=%u granule=%u device=%u", call,
                        path ? path : "(none)", request && field<void*>(request, kCallerBuffer) != nullptr,
                        request ? field<std::uint32_t>(request, kGranule) : 0,
                        request && field<void*>(request, kDevice) != nullptr);
        if (call == 300 || call == 3000 || call == 30000)
            Logging.Log("[fieldworks] SHARED_SEEN loads=%u listed_files_seen=%x table_ready=%u", call, g_seen.load(),
                        g_tableReady.load());
        if (!path || !manifestReady()) return Orig(self, request, resource, outSize, outAllocated, outNeedDelete);
        for (std::uint32_t i = 0; i < g_manifest.count; ++i) {
            const merge::Entry& e = g_manifest.entries[i];
            if (!merge::pathNames(path, e.name)) continue;
            g_seen.fetch_or(1u << i);
            // The sizes come back through these even when the game's caller did not ask for them.
            std::uint32_t size = 0, allocated = 0;
            std::uint32_t* const sizeOut = outSize ? outSize : &size;
            std::uint32_t* const allocatedOut = outAllocated ? outAllocated : &allocated;
            return loadShared(e, path, request, *sizeOut, *allocatedOut, outSize != nullptr, [&] {
                return Orig(self, request, resource, sizeOut, allocatedOut, outNeedDelete);
            });
        }
        return Orig(self, request, resource, outSize, outAllocated, outNeedDelete);
    }
};

}  // namespace

void install(std::uintptr_t mainBase, Game game) {
    constexpr std::uint32_t k121[] = {0xd10403ff, 0xa90a7bfd, 0x910283fd, 0xa90b6ffc};
    constexpr std::uint32_t k143[] = {0xd10483ff, 0xa90c7bfd, 0xa90d6ffc, 0xa90e67fa};
    const bool v121 = game == Game::V121;
    const std::ptrdiff_t offset = v121 ? 0xA6863C : 0x5F5088;
    const std::uint32_t* want = v121 ? k121 : k143;
    g_version = v121 ? "121" : "143";
    for (unsigned i = 0; i < 4; ++i) {
        const std::uint32_t have = *reinterpret_cast<const volatile std::uint32_t*>(mainBase + offset + 4 * i);
        if (have == want[i]) continue;
        Logging.Log("[fieldworks] SHARED_OFF reason=game_code_differs game=%s word=%u have=%08x want=%08x", g_version,
                    i, have, want[i]);
        return;
    }
    LoadHook::InstallAtOffset(offset);
    Logging.Log("[fieldworks] SHARED_ON game=%s", g_version);
}

}  // namespace fieldworks::shared
