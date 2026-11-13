// Copyright (c) HanSoloBlaster, afiaka87
// Appends sounds to a loaded sound bank (BARS) in place, in the layout the offline tool writes.
#pragma once

#include "Work.hpp"

namespace fieldworks::merge {

// Reads `bytes` of the merge data at `offset`; the waveforms go straight to their final place.
struct Source {
    bool (*read)(void* context, u64 offset, void* out, std::size_t bytes) = nullptr;
    void* context = nullptr;
    bool operator()(u64 offset, void* out, std::size_t bytes) const { return read(context, offset, out, bytes); }
};

// The merge data: "FWSB", count, then per sound {name hash, settings offset, settings bytes, waveform
// offset, waveform bytes}, the settings records, and the waveforms. A sound the bank has is left alone.
inline bool mergeSounds(Work& w, const Source& source, std::size_t deltaSize, Report& report) {
    u8* d = w.base;
    const auto u = [&](std::size_t at) { return rd32(d + at, false); };
    const char* const unknown = "the loaded file is not a sound bank this build understands";
    if (w.size < 28 || std::memcmp(d, "BARS", 4) || rd16(d + 8, false) != 0xfeff || u(4) != w.size)
        return w.refuse(unknown);
    const u32 n = u(12);
    const std::size_t indexEnd = 16 + 12 * std::size_t{n};
    if (!n || n > 4096 || indexEnd > w.size) return w.refuse(unknown);
    std::size_t firstSettings = w.size, settingsEnd = 0, audioStart = w.size;
    for (u32 i = 0; i < n; ++i) {
        const std::size_t settings = u(16 + 4 * std::size_t{n} + 8 * i), audio = u(20 + 4 * std::size_t{n} + 8 * i);
        if ((i && u(16 + 4 * i) <= u(12 + 4 * i)) || settings < indexEnd || settings > w.size - 76 ||
            std::memcmp(d + settings, "AMTA", 4) || u(settings + 8) < 76 || u(settings + 8) > w.size - settings ||
            audio > w.size - 16 || std::memcmp(d + audio, "BWAV", 4))
            return w.refuse(unknown);
        if (settings < firstSettings) firstSettings = settings;
        if (settings + u(settings + 8) > settingsEnd) settingsEnd = settings + u(settings + 8);
        if (audio < audioStart) audioStart = audio;
    }
    if (settingsEnd > audioStart) return w.refuse(unknown);

    u8 head[8];
    if (deltaSize < 8 || !source(0, head, 8) || std::memcmp(head, "FWSB", 4)) return w.refuse("the merge data is unreadable");
    const u32 m = rd32(head + 4, false);
    if (!m || m > 1024 || 8 + 20 * std::size_t{m} > deltaSize) return w.refuse("the merge data is unreadable");
    const std::size_t mark = w.top;
    u8* records = w.scratch(20 * std::size_t{m});
    u32* order = w.scratchArray<u32>(m);
    if (!records || !order) return false;
    if (!source(8, records, 20 * std::size_t{m})) return w.refuse("the merge data could not be read");
    const auto field = [&](u32 j, int which) { return rd32(records + 20 * std::size_t{j} + 4 * which, false); };
    const auto present = [&](u32 hash) {
        u32 low = 0, high = n;
        while (low < high) {
            const u32 mid = (low + high) / 2;
            if (u(16 + 4 * std::size_t{mid}) < hash) low = mid + 1; else high = mid;
        }
        return low < n && u(16 + 4 * std::size_t{low}) == hash;
    };
    // `order` lists the sounds to add by ascending name hash; `add` marks them in file order.
    u8* add = w.scratch(m);
    if (!add) return false;
    u32 k = 0;
    std::size_t settingsBytes = 0;
    for (u32 j = 0; j < m; ++j) {
        const u64 settingsLast = u64{field(j, 1)} + field(j, 2), waveLast = u64{field(j, 3)} + field(j, 4);
        if (field(j, 2) < 76 || field(j, 2) % 4 || field(j, 4) < 128 || settingsLast > deltaSize || waveLast > deltaSize)
            return w.refuse("the merge data is unreadable");
        if (present(field(j, 0))) continue;
        u32 at = k++;
        for (; at && field(order[at - 1], 0) > field(j, 0); --at) order[at] = order[at - 1];
        if (at && field(order[at - 1], 0) == field(j, 0)) return w.refuse("the merge data is unreadable");
        order[at] = j;
        add[j] = 1;
        settingsBytes += field(j, 2);
    }
    report.present = m - k;
    if (!k) {
        w.release(mark);
        return true;
    }
    // New layout: index, the old gap and settings moved up, new settings, then the old audio, then new audio.
    const std::size_t moved = 12 * std::size_t{k};
    const std::size_t newSettings = settingsEnd + moved;
    const std::size_t newAudioStart = pad(newSettings + settingsBytes, 64);
    const std::size_t shift = newAudioStart - audioStart, oldAudio = w.size - audioStart;
    std::size_t total = newAudioStart + oldAudio;
    for (u32 j = 0; j < m; ++j)
        if (add[j]) total = pad(total, 64) + field(j, 4);
    u8* blobs = w.scratch(settingsBytes);
    if (!blobs) return false;
    if (total > w.top) return w.refuse("no room left after the file");

    // Everything read here lands beyond the bank's current end, so a failed read leaves the bank as loaded.
    std::size_t cursor = newAudioStart + oldAudio, blob = 0;
    for (u32 j = 0; j < m; ++j) {
        if (!add[j]) continue;
        const std::size_t at = pad(cursor, 64), bytes = field(j, 4);
        std::memset(d + cursor, 0, at - cursor);
        if (!source(field(j, 3), d + at, bytes) || std::memcmp(d + at, "BWAV", 4) ||
            !source(field(j, 1), blobs + blob, field(j, 2)) || std::memcmp(blobs + blob, "AMTA", 4) ||
            rd32(blobs + blob + 8, false) != field(j, 2))
            return w.refuse("the merge data could not be read");
        // From here the record holds the sound's places in the bank, not in the merge data.
        wr32(records + 20 * std::size_t{j} + 12, static_cast<u32>(at), false);
        wr32(records + 20 * std::size_t{j} + 4, static_cast<u32>(newSettings + blob), false);
        blob += field(j, 2);
        cursor = at + bytes;
    }
    std::memmove(d + newAudioStart, d + audioStart, oldAudio);
    std::memmove(d + indexEnd + moved, d + indexEnd, settingsEnd - indexEnd);
    std::memcpy(d + newSettings, blobs, settingsBytes);
    std::memset(d + newSettings + settingsBytes, 0, newAudioStart - newSettings - settingsBytes);
    // Index, written from the top so each old entry is read before its place is reused.
    const std::size_t oldPairs = 16 + 4 * std::size_t{n}, newPairs = 16 + 4 * (std::size_t{n} + k);
    for (u32 out = n + k, i = n, j = k; out-- > 0;) {
        const bool fresh = j && (!i || field(order[j - 1], 0) > u(16 + 4 * std::size_t{i - 1}));
        if (fresh) {
            --j;
            wr32(d + newPairs + 8 * std::size_t{out}, field(order[j], 1), false);
            wr32(d + newPairs + 8 * std::size_t{out} + 4, field(order[j], 3), false);
        } else {
            --i;
            const u32 settings = u(oldPairs + 8 * std::size_t{i}), audio = u(oldPairs + 8 * std::size_t{i} + 4);
            wr32(d + newPairs + 8 * std::size_t{out}, static_cast<u32>(settings + moved), false);
            wr32(d + newPairs + 8 * std::size_t{out} + 4, static_cast<u32>(audio + shift), false);
        }
    }
    for (u32 out = n + k, i = n, j = k; out-- > 0;) {
        const bool fresh = j && (!i || field(order[j - 1], 0) > u(16 + 4 * std::size_t{i - 1}));
        const u32 hash = fresh ? field(order[--j], 0) : u(16 + 4 * std::size_t{--i});
        wr32(d + 16 + 4 * std::size_t{out}, hash, false);
    }
    wr32(d + 4, static_cast<u32>(total), false);
    wr32(d + 12, n + k, false);
    w.size = total;
    report.added = k;
    w.release(mark);
    return true;
}

}  // namespace fieldworks::merge
