// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include <lib.hpp>

#include "BivouacPolicy.hpp"
#include "CampCues.hpp"
#include "MenuPolicy.hpp"
#include "MotherBase.hpp"
#include "PersistencePolicy.hpp"
#include "PlacementPolicy.hpp"
#include "RefundPolicy.hpp"
#include "engine/ActorRuntime.hpp"
#include "engine/CampAudio.hpp"
#include "engine/GameDataService.hpp"
#include "engine/MapStampService.hpp"
#include "engine/PlayerService.hpp"
#include "engine/RaycastService.hpp"
#include "feature/CampAssembler.hpp"
#include "feature/CampGeometry.hpp"
#include "feature/PlacementCoordinator.hpp"
#include "persistence/CampLedgerCodec.hpp"
#include "persistence/CampLedgerStore.hpp"
#include "presentation/CampStampRenderer.hpp"
#include "runtime/BivouacConfig.hpp"
#include "runtime/BivouacRuntime.hpp"
#include "runtime/BivouacState.hpp"
#include "totk/engine/Npad.hpp"
#include <program/sd_logger.hpp>

#ifndef BV_DIAG
#define BV_DIAG 1
#endif
#if BV_DIAG
  #define BVLOG(...) Logging.Log("[bv] " __VA_ARGS__)
#else
  #define BVLOG(...) ((void)0)
#endif

// Test kit: force-place and golden-apple provisioning combos plus the wildberry trigger.
#ifndef BV_PROBE
#define BV_PROBE 0
#endif

namespace {

using namespace bivouac::runtime::config;

namespace off {
    constexpr ptrdiff_t NpadCalc        = 0x02a267bc;
    constexpr ptrdiff_t RaycastWorker   = 0x00858590;
    constexpr ptrdiff_t PouchCountLeaf  = 0x015C9870;
    constexpr ptrdiff_t PouchNameGetter = 0x00CD2B30;
    constexpr ptrdiff_t ConsumeMaterial = 0x01A6CBB0;
    constexpr ptrdiff_t GmdGetStructStructByIndex = 0x00942FE8;
    constexpr ptrdiff_t GmdGetStructString64      = 0x00D44600;
    constexpr ptrdiff_t PouchGmdGlobal            = 0x046CBC98;
    constexpr ptrdiff_t GmdMgrIndirect            = 0x0462E3D8;
    constexpr ptrdiff_t GmdEmptyString64          = 0x0462E1B8;
    constexpr ptrdiff_t StampIconWidgetTick       = 0x01AA7888;
    constexpr ptrdiff_t MapHoverDispatch          = 0x00EC29A0;
    constexpr ptrdiff_t MinimapIconPass           = 0x00D1EF30;
    constexpr ptrdiff_t ClimbSpeedPost            = 0x01D579E8;
}

constexpr u32 POUCH_NAME_FIELD_HASH = 0x25EFA387;
constexpr u32 MATERIAL_STRUCT_ARRAY_HASH = 0x4290322E;

constexpr int MENU_MIN_STARVE_TICKS = 4;
constexpr int ARRIVAL_WARMUP_TICKS = 3;
constexpr int AUDIO_PRIME_GAP_TICKS = 60;
constexpr float TEARDOWN_REACH_METERS = 10.0f;

// Map screen hover dispatch: the instruction at MapHoverDispatch is "SUB W8, W8, #2".
constexpr u32 MAP_HOVER_DISPATCH_WORD = 0x51000908;
constexpr u32 MINIMAP_ICON_PASS_WORD = 0xD10243FF; // SUB SP, SP, #0x90
constexpr u32 CLIMB_SPEED_POST_WORD = 0xAA1403E0;
volatile u32 g_climbHeartbeat = 0;
constexpr u32 MAP_HOVER_TYPE_STAMP = 12;
constexpr u32 MAP_HOVER_TYPE_NO_ACTION = 2;
constexpr uintptr_t MAP_SCREEN_HOVER_INDEX_OFFSET = 0x30C;
constexpr uintptr_t INLINE_FLOAT_CTX_GPR_OFFSET = 0x200;

using Vec3 = bivouac::runtime::Vec3;
using SlotState = bivouac::runtime::SlotState;
using PlaceStep = bivouac::runtime::PlaceStep;
using PlacementLane = bivouac::placement::Lane;
using Site = bivouac::runtime::Site;
using Cue = bivouac::cues::Cue;

static_assert(MAX_SITES == static_cast<int>(bivouac::persistence::kMaximumSites));
constexpr size_t LEDGER_BUF_MAX = bivouac::persistence::kMaximumLedgerBytes;

static_assert(MAX_PROPS == bivouac::runtime::kMaximumProps);
static_assert(REFUND_QUEUE_CAP == bivouac::feature::kRefundQueueCapacity);
bivouac::runtime::BivouacRuntimeState g;

alignas(4) unsigned char gLedgerBuf[LEDGER_BUF_MAX];
alignas(4) bivouac::persistence::CampRecord gLedgerRecords[MAX_SITES];
bivouac::persistence::CampLedgerStore gLedgerStore;
bivouac::engine::ActorRuntime gActors;
bivouac::engine::GameDataService gGameData;
bivouac::engine::PlayerService gPlayers;
bivouac::engine::RaycastService gRaycasts;
bivouac::engine::MapStampService gMapStamps{
    gGameData, gActors, gPlayers};
bivouac::feature::CampAssembler gCampAssembler{gActors};
bivouac::feature::PlacementCoordinator gPlacementCoordinator{
    gRaycasts, gActors};
totk::engine::NpadReader gNpadReader;

inline float fsqrt(float v) { return v > 0.0f ? __builtin_sqrtf(v) : 0.0f; }
inline int   cm(float meters) { return (int)(meters * 100.0f + (meters >= 0.0f ? 0.5f : -0.5f)); }

inline bool nameEq(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) { return false; }
    for (int i = 0; i < 64; i++) { if (a[i] != b[i]) { return false; } if (a[i] == '\0') { return true; } }
    return false;
}
inline bool okPtr(u64 p) { return p >= 0x1000000 && p < 0x8000000000ull && (p & 0x3) == 0; }

void queueCue(Cue cue) { g.audio.pendingCue = cue; }

void audioTick(bool warmedUp) {
    if (!warmedUp) { return; }
    if (!bivouac::audio::ready()) {
        if (g.scene.tick - g.audio.lastPrimeTick >= AUDIO_PRIME_GAP_TICKS) {
            g.audio.lastPrimeTick = g.scene.tick;
            bivouac::audio::prime();
        }
        return;
    }
    const Cue cue = g.audio.pendingCue;
    if (cue == Cue::None) { return; }
    g.audio.pendingCue = Cue::None;
    const char* name = bivouac::cues::cueName(cue);
    if (!bivouac::audio::playCue(name)) { BVLOG("audio: cue %s refused", name); }
}

void ledgerWrite() {
    if (g.persistence.sdState != 1) { return; }
    for (int i = 0; i < MAX_SITES; i++) {
        const Site& s = g.camps.sites[i];
        gLedgerRecords[i] = {
            s.anchor.x,
            s.anchor.y,
            s.anchor.z,
            s.nx,
            s.nz,
            s.seq,
            s.tier,
            s.gapFloor,
            s.gapRoof,
            s.stampSlot,
            s.waterMode,
            s.active,
        };
    }
    const bivouac::persistence::EncodeResult encoded =
        bivouac::persistence::encodeLedger(
            gLedgerRecords, MAX_SITES, g.camps.nextSeq, gLedgerBuf, LEDGER_BUF_MAX);
    if (!encoded) {
        BVLOG("ledger: encode refused (%u)", static_cast<u32>(encoded.error));
        return;
    }
    const bivouac::persistence::StorageResult stored =
        gLedgerStore.write(LEDGER_DIR, LEDGER_PATH, gLedgerBuf,
                           static_cast<s64>(encoded.bytesWritten));
    if (stored) {
        g.persistence.ledgerDirty = false;
        g.persistence.ledgerWroteTick = g.scene.tick;
        BVLOG("ledger: wrote %u sites (%d B)", encoded.encodedCount,
              static_cast<int>(encoded.bytesWritten));
    } else {
        BVLOG("ledger: write failed (%s, native=%x)",
              bivouac::persistence::storageErrorName(stored.error),
              stored.nativeResult);
    }
}

void ledgerRead() {
    g.persistence.ledgerLoaded = true;
    const bivouac::persistence::ReadResult loaded =
        gLedgerStore.read(LEDGER_PATH, gLedgerBuf, static_cast<s64>(LEDGER_BUF_MAX));
    const s64 size = loaded.bytesRead;
    if (size < 0) { BVLOG("ledger: none yet (first run)"); return; }
    const bivouac::persistence::DecodeResult decoded =
        bivouac::persistence::decodeLedger(
            gLedgerBuf, static_cast<size_t>(size),
            {static_cast<u8>(CampTier::BASE), LEDGER_OVERLAP_METERS});
    if (!decoded) {
        if (decoded.error == bivouac::persistence::DecodeError::TooSmall) {
            BVLOG("ledger REJECTED (too small: %d B)", static_cast<int>(size));
        } else {
            BVLOG("ledger REJECTED (%s)",
                  bivouac::persistence::decodeErrorName(decoded.error));
        }
        return;
    }
    g.camps.nextSeq = decoded.document.nextSequence;
    g.persistence.ledgerDirty = decoded.document.needsRewrite;
    for (u32 i = 0; i < decoded.document.count; ++i) {
        const bivouac::persistence::CampRecord& record = decoded.document.sites[i];
        Site& s = g.camps.sites[i];
        s = Site{};
        s.anchor = {record.x, record.y, record.z};
        s.nx = record.normalX;
        s.nz = record.normalZ;
        s.seq = record.sequence;
        s.waterMode = record.waterMode;
        s.tier = record.tier;
        s.gapFloor = record.floorGap;
        s.gapRoof = record.roofGap;
        s.stampSlot = record.stampSlot;
        s.active = record.active;
    }
    if (decoded.document.count > 0) {
        g.map.mapServiceDirty = true;
    }
    BVLOG("ledger: read %u sites (v%u, nextSeq=%u)", decoded.document.count,
          static_cast<u32>(bivouac::persistence::kLedgerVersion), g.camps.nextSeq);
}

// Arrival assistance deliberately survives this: our own warp's loading screen passes through here.
void resetAllRuntime(const char* why) {
    gCampAssembler.resetRuntime(g.camps, g.spawn);
    g.trigger.trigPending = 0;
    gPlacementCoordinator.reset(g.placement);
    g.menu.menuStarvedTicks = 0;
    g.map.mapServiceDirty = true;
    BVLOG("scene teardown (%s): all runtime reset (sites kept)", why);
}

int nearestActiveSite(float x, float y, float z, float* outDist) {
    int best = -1; float bestD2 = 1e30f;
    for (int i = 0; i < MAX_SITES; i++) {
        if (!g.camps.sites[i].active) { continue; }
        const float dx = g.camps.sites[i].anchor.x - x, dy = g.camps.sites[i].anchor.y - y, dz = g.camps.sites[i].anchor.z - z;
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < bestD2) { bestD2 = d2; best = i; }
    }
    if (best >= 0 && outDist != nullptr) { *outDist = fsqrt(bestD2); }
    return best;
}

// Stamp handle indices of camps inside the cliff separation radius; the minimap shows exactly these.
void updateNearStamps(float x, float y, float z) {
    int count = 0;
    const float limit2 = SITE_SEPARATION_METERS * SITE_SEPARATION_METERS;
    for (int i = 0; i < MAX_SITES; i++) {
        const Site& s = g.camps.sites[i];
        if (!s.active || s.handleIndex == 0xFFFFFFFFu) { continue; }
        const float dx = s.anchor.x - x, dy = s.anchor.y - y, dz = s.anchor.z - z;
        if (dx * dx + dy * dy + dz * dz < limit2) { g.map.nearHandles[count++] = s.handleIndex; }
    }
    g.map.nearStampCount = count;
}

bool stampHandleNearLink(u32 handleIndex) {
    const int count = g.map.nearStampCount;
    for (int i = 0; i < count && i < MAX_SITES; i++) {
        if (g.map.nearHandles[i] == handleIndex) { return true; }
    }
    return false;
}

int findFreeSiteIndex() {
    for (int i = 0; i < MAX_SITES; i++) { if (!g.camps.sites[i].active) { return i; } }
    return -1;
}

bool teardownNearestCamp(float x, float y, float z, float maximumDistance, const char* who) {
    float d = 0.0f;
    const int idx = nearestActiveSite(x, y, z, &d);
    if (idx < 0 || d > maximumDistance) {
        BVLOG("%s: teardown - no camp within %dcm", who, cm(maximumDistance));
        return false;
    }
    Site& site = g.camps.sites[idx];
    gCampAssembler.teardownSite(site);
    gMapStamps.clearCampStamp(site, g.persistence);
    site.active = false;
    g.persistence.ledgerDirty = true;
    queueCue(Cue::Removed);
    BVLOG("%s: TEARDOWN camp #%u (%dcm away)", who, site.seq, cm(d));
    return true;
}

bool climbRecentNow(int* outDtFlag, int* outDtEnd) {
    const int dtFlag = g.scene.gameplayTick - g.trigger.lastClimbTick;
    const int dtEnd  = g.scene.gameplayTick - g.trigger.climbEndTick;
    if (outDtFlag != nullptr) { *outDtFlag = dtFlag; }
    if (outDtEnd  != nullptr) { *outDtEnd  = dtEnd; }
    return bivouac::policy::climbIsRecent(
        g.scene.gameplayTick, g.trigger.lastClimbTick, g.trigger.climbEndTick,
        CLIMB_RECENT_TICKS, CLIMB_MENU_GRACE_TICKS);
}

bool triggerTierFor(const char* name, CampTier* outTier) {
    const bivouac::policy::TierMatch match = bivouac::policy::tierForItem(name);
    if (match.matched) {
        if (outTier != nullptr) { *outTier = match.tier; }
        return true;
    }
#if BV_PROBE
    if (nameEq(name, "Item_Fruit_B")) {
        if (outTier != nullptr) { *outTier = CampTier::CAMP; }
        return true;
    }
#endif
    return false;
}

void scheduleRefundFor(const char* itemName, u32 gmdIdx, const char* why) {
    const bivouac::feature::RefundScheduleResult scheduled =
        g.refunds.schedule(itemName, gmdIdx, g.scene.tick, REFUND_DELAY_TICKS);
    if (scheduled.status == bivouac::feature::RefundScheduleStatus::NotRefundable) {
        BVLOG("refund: not refundable (%s)", why);
        return;
    }
    if (scheduled.status == bivouac::feature::RefundScheduleStatus::QueueFull) {
        BVLOG("refund: FAILED (queue full) +1 %s slot %u - %s", itemName, gmdIdx, why);
        return;
    }
    BVLOG("refund[%d]: scheduled +1 %s (slot %u) - %s", scheduled.queueIndex,
          itemName, gmdIdx, why);
}

void refundTick() {
    const bivouac::feature::DueRefund due = g.refunds.takeDue(g.scene.tick);
    if (!due.available) { return; }

    const int queueIndex = due.queueIndex;
    const u32 refundGmdIdx = due.gameDataIndex;
    const char* refundItemName = due.itemName;

    const auto result =
        gGameData.refundMaterial(refundGmdIdx, refundItemName);
    if (!result) {
        if (result.error == bivouac::engine::RefundWriteError::DifferentItem) {
            BVLOG("refund[%d]: SKIPPED (slot %u now holds %s, not %s)",
                  queueIndex, refundGmdIdx,
                  result.currentName != nullptr ? result.currentName : "<null>",
                  refundItemName);
        } else {
            BVLOG("refund[%d]: FAILED (slot %u, %s)", queueIndex, refundGmdIdx,
                  bivouac::engine::refundWriteErrorName(result.error));
        }
        return;
    }
    if (result.restoredName) {
        BVLOG("refund[%d]: slot %u was emptied (last item) - name restored",
              queueIndex, refundGmdIdx);
    }
    BVLOG("refund[%d]: +1 %s (slot %u)", queueIndex, refundItemName, refundGmdIdx);
}

// Backup trigger for the static-site consumers; menu eats of materials never reach this leaf.
HOOK_DEFINE_INLINE(PouchLeafHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        const s32 delta = (s32)ctx->W[1];
        if (delta >= 0) { return; }
        void* mgr    = (void*)ctx->X[0];
        void* record = (void*)ctx->X[2];
        if (!okPtr((u64)record)) { return; }

        static const char* kEmptyName = "";
        const char* name = kEmptyName;
        auto getName = (u32(*)(void*, const char**, void*, u32))(g.scene.mainBase + off::PouchNameGetter);
        if ((getName(mgr, &name, record, POUCH_NAME_FIELD_HASH) & 1u) == 0) { return; }
        CampTier tier = CampTier::CAMP;
        const bool isTrigger = triggerTierFor(name, &tier);
        if (!isTrigger) { return; }

        int dtFlag = 0, dtEnd = 0;
        const bool climbRecent = climbRecentNow(&dtFlag, &dtEnd);
        BVLOG("leaf: %s delta=%d dtFlag=%d dtEnd=%d recent=%d pend=%u", name, delta, dtFlag, dtEnd, climbRecent ? 1 : 0, g.trigger.trigPending);
        if (!climbRecent) { return; }
        if (g.scene.tick - g.trigger.lastTrigTick < TRIGGER_COOLDOWN_TICKS) { return; }
        if (g.trigger.trigPending) { return; }

        if (g.scene.player != nullptr) {
            const auto player = gPlayers.snapshot(g.scene.player);
            float d = 0.0f;
            const int nearIdx = nearestActiveSite(
                player.position.x, player.position.y, player.position.z, &d);
            if (nearIdx >= 0 && d < SITE_SEPARATION_METERS) {
                BVLOG("REFUSED: %dcm from site#%u (min %dcm) - leaf path, no refund", cm(d), g.camps.sites[nearIdx].seq, cm(SITE_SEPARATION_METERS));
                queueCue(Cue::Refused);
                return;
            }
        }

        g.trigger.trigTier = (u8)tier;
        g.trigger.trigGmdIdx = 0; g.trigger.trigItemName[0] = '\0';
        g.trigger.trigArmedTick = g.scene.tick;
        g.trigger.lastTrigTick  = g.scene.tick;
        g.placement.placeLane     = PlacementLane::Cliff;
        g.placement.placeStep     = PlaceStep::IDLE;
        g.placement.motherBaseFallbackFromCliff = false;
        g.trigger.trigPending   = 1;
    }
};

const char* materialSlotName(u32 slotIndex) {
    const u64 gmdIndirect = *(u64*)(g.scene.mainBase + off::GmdMgrIndirect);
    const u64 pouchGlobal = *(u64*)(g.scene.mainBase + off::PouchGmdGlobal);
    if (!okPtr(gmdIndirect) || !okPtr(pouchGlobal)) { return nullptr; }
    const u64 gmdMgr = *(u64*)gmdIndirect;
    if (!okPtr(gmdMgr)) { return nullptr; }

    u32 mode = *(u32*)(pouchGlobal + 0x188);
    if (mode >= 2) { mode = 0; }
    const u64 handleArg = pouchGlobal + 0xC0ull * mode + 0x58ull;

    alignas(8) u8 structHandle[16] = {};
    const char* name = *(const char**)(g.scene.mainBase + off::GmdEmptyString64);
    auto getStructByIndex = (u32(*)(u64, void*, u64, u32, u32))(g.scene.mainBase + off::GmdGetStructStructByIndex);
    auto getStructString  = (u32(*)(u64, const char**, void*, u32))(g.scene.mainBase + off::GmdGetStructString64);
    if ((getStructByIndex(gmdMgr, structHandle, handleArg, MATERIAL_STRUCT_ARRAY_HASH, slotIndex) & 1u) == 0) { return nullptr; }
    if ((getStructString(gmdMgr, &name, structHandle, POUCH_NAME_FIELD_HASH) & 1u) == 0) { return nullptr; }
    return name;
}

// The trigger: PouchMgr::consumeMaterial(mgr, slotIndex) at entry, on the pouch thread.
HOOK_DEFINE_INLINE(ConsumeMaterialTrigger) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        const u32 slotIndex = (u32)ctx->W[1];
        const char* name = materialSlotName(slotIndex);
        if (name == nullptr) { BVLOG("eat: slot %u name UNRESOLVED", slotIndex); return; }

        CampTier tier = CampTier::CAMP;
        const bool isTrigger = triggerTierFor(name, &tier);
        int dtFlag = 0, dtEnd = 0;
        const bool climbRecent = climbRecentNow(&dtFlag, &dtEnd);
        BVLOG("eat: %s slot=%u dtFlag=%d dtEnd=%d recent=%d trig=%d tier=%u pend=%u lane=%s",
              name, slotIndex, dtFlag, dtEnd, climbRecent ? 1 : 0, isTrigger ? 1 : 0, (u32)tier,
              g.trigger.trigPending, climbRecent ? "cliff" : "Mother Base candidate");
        if (!isTrigger) { return; }
        if (motherbase::shouldRefundAdditionalTrigger(isTrigger, g.trigger.trigPending != 0)) {
            scheduleRefundFor(name, slotIndex, "another placement is already pending");
            queueCue(Cue::Refused);
            BVLOG("eat: additional trigger refunded independently; owned placement unchanged");
            return;
        }

        if (climbRecent && g.scene.player != nullptr) {
            const auto player = gPlayers.snapshot(g.scene.player);
            float d = 0.0f;
            const int nearIdx = nearestActiveSite(
                player.position.x, player.position.y, player.position.z, &d);
            if (nearIdx >= 0 && d < SITE_SEPARATION_METERS) {
                BVLOG("REFUSED: %dcm from site#%u (min %dcm)", cm(d), g.camps.sites[nearIdx].seq, cm(SITE_SEPARATION_METERS));
                scheduleRefundFor(name, slotIndex, "too close to an existing camp");
                queueCue(Cue::Refused);
                return;
            }
        }

        g.trigger.trigTier   = (u8)tier;
        g.trigger.trigGmdIdx = slotIndex;
        { int i = 0; for (; i < 63 && name[i] != '\0'; i++) { g.trigger.trigItemName[i] = name[i]; } g.trigger.trigItemName[i] = '\0'; }
        g.trigger.trigArmedTick = g.scene.tick;
        g.trigger.lastTrigTick  = g.scene.tick;
        g.placement.placeLane     = bivouac::placement::initialLane(climbRecent);
        g.placement.placeStep     = PlaceStep::IDLE;
        g.placement.motherBaseFallbackFromCliff = false;
        g.trigger.trigPending   = 1;
    }
};

void finishPlacementAbort(const char* reason, bool refund) {
    BVLOG("place ABORT (%s): %s%s",
          bivouac::placement::laneName(g.placement.placeLane),
          reason, refund ? "" : " (ordinary eat, no refund, no cue)");
    if (refund) {
        scheduleRefundFor(
            g.trigger.trigItemName, g.trigger.trigGmdIdx, reason);
        queueCue(Cue::Refused);
    }
    g.trigger.trigPending = 0;
    gPlacementCoordinator.reset(g.placement);
}

void commitPlacement(
    const bivouac::feature::PlacementCandidate& candidate) {
    const int siteIndex = findFreeSiteIndex();
    if (siteIndex < 0) {
        finishPlacementAbort(
            "ledger FULL (64 sites) - tear one down", true);
        return;
    }

    Site& site = g.camps.sites[siteIndex];
    site = {};
    site.anchor = candidate.anchor;
    site.nx = candidate.normalX;
    site.nz = candidate.normalZ;
    site.seq = g.camps.nextSeq++;
    site.tier = g.trigger.trigTier;
    site.gapFloor = candidate.floorGap;
    site.gapRoof = candidate.roofGap;
    site.waterMode = candidate.waterMode;
    site.active = true;
    g.persistence.ledgerDirty = true;
    g.map.mapServiceDirty = true;
    g.trigger.trigPending = 0;
    gPlacementCoordinator.reset(g.placement);
    queueCue(Cue::Placed);

    if (candidate.waterMode) {
        BVLOG("PLACED Mother Base #%u tier=%u at (%d,%d,%d)cm facing=(%d,%d)/1000 range=%dcm waterDepth=%d..%dcm attempts=%u waterSamples=%u clearanceSamples=%u - assembling",
              site.seq, static_cast<u32>(site.tier),
              cm(site.anchor.x), cm(site.anchor.y), cm(site.anchor.z),
              static_cast<int>(site.nx * 1000.0f),
              static_cast<int>(site.nz * 1000.0f),
              cm(candidate.anchorDistance),
              cm(candidate.minimumWaterDepth),
              cm(candidate.maximumWaterDepth),
              static_cast<u32>(candidate.candidateAttempts),
              static_cast<u32>(candidate.waterSamples),
              static_cast<u32>(candidate.clearanceSamples));
        return;
    }

    BVLOG("PLACED %s #%u at (%d,%d,%d)cm n=(%d,%d)/1000 seat=%dcm gaps floor=%dcm roof=%dcm - assembling",
          site.tier == static_cast<u8>(CampTier::BIVY)
              ? "bivy"
              : site.tier == static_cast<u8>(CampTier::BASE)
                  ? "basecamp"
                  : "camp",
          site.seq, cm(site.anchor.x), cm(site.anchor.y),
          cm(site.anchor.z),
          static_cast<int>(site.nx * 1000.0f),
          static_cast<int>(site.nz * 1000.0f),
          cm(candidate.sweepSeat),
          cm(site.gapFloor), cm(site.gapRoof));
}

void placementTick(
    const bivouac::engine::PlayerSnapshot& player) {
    const bivouac::feature::PlacementOutcome outcome =
        gPlacementCoordinator.tick(
            g.placement, g.trigger, g.camps, g.scene.mainBase,
            player.position, player.forward, g.scene.tick);
    switch (outcome.kind) {
    case bivouac::feature::PlacementOutcomeKind::None:
        return;
    case bivouac::feature::PlacementOutcomeKind::Abort:
        finishPlacementAbort(outcome.reason, outcome.refund);
        return;
    case bivouac::feature::PlacementOutcomeKind::Commit:
        commitPlacement(outcome.candidate);
        return;
    }
}

constexpr u64 BTN_ZL    = 1ull << 8;
constexpr u64 BTN_ZR    = 1ull << 9;
constexpr u64 BTN_LEFT  = 1ull << 12;
constexpr u64 BTN_RIGHT = 1ull << 14;
constexpr u64 BTN_DOWN  = 1ull << 15;

// Hold ZL+ZR, then tap Left within reach of a camp to tear it down.
void comboTick(void* npadDevice, float linkX, float linkY, float linkZ) {
    const u64 buttons = gNpadReader.read(npadDevice).snapshot().buttons;
    const u64 pressed = buttons & ~g.input.prevButtons;
    g.input.prevButtons = buttons;
    const bool comboArmed = (buttons & BTN_ZL) != 0 && (buttons & BTN_ZR) != 0;
    if (!comboArmed) { return; }

    if (pressed & BTN_LEFT) {
        teardownNearestCamp(linkX, linkY, linkZ, TEARDOWN_REACH_METERS, "combo");
    }
#if BV_PROBE
    if (pressed & BTN_DOWN) {
        if (!g.trigger.trigPending) {
            g.trigger.trigTier = (u8)CampTier::CAMP;
            g.trigger.trigGmdIdx = 0; g.trigger.trigItemName[0] = '\0';
            g.trigger.trigArmedTick = g.scene.tick; g.trigger.lastTrigTick = g.scene.tick;
            g.placement.placeLane = PlacementLane::Cliff;
            g.placement.placeStep = PlaceStep::IDLE;
            g.placement.motherBaseFallbackFromCliff = false;
            g.trigger.trigPending = 1;
            BVLOG("combo: FORCE-PLACE at the faced wall");
        }
    }
    if (pressed & BTN_RIGHT) {
        if (g.spawn.appleDropsPending <= 0) {
            g.spawn.appleDropsPending = GOLDEN_APPLE_DROP_COUNT;
            BVLOG("combo: dropping %d camp-tier items around Link", GOLDEN_APPLE_DROP_COUNT);
        }
    }
#else
    (void)BTN_RIGHT; (void)BTN_DOWN;
#endif
}

void bvTick(void* npadDevice,
            bivouac::runtime::ClimbObservation climbObservation) {
    g.scene.tick++;
    g.scene.player = gPlayers.resolve();

    if (g.scene.player == nullptr) {
        g.scene.firstPlayerTick = 0;
        if (g.scene.nullPlayerTicks < SCENE_GONE_TICKS) {
            g.scene.nullPlayerTicks++;
            if (g.scene.nullPlayerTicks == SCENE_GONE_TICKS) { resetAllRuntime("player unresolvable"); }
        }
        return;
    }
    const int blackoutJustEnded = g.scene.nullPlayerTicks;
    if (blackoutJustEnded > 0) {
        BVLOG("player: transiently unresolvable for %d tick(s) (demo/menu hiccup)", blackoutJustEnded);
    }
    g.scene.nullPlayerTicks = 0;
    if (g.scene.firstPlayerTick == 0) { g.scene.firstPlayerTick = g.scene.tick; }
    const int warmupTicks = (g.map.arrivalSite >= 0) ? ARRIVAL_WARMUP_TICKS : SPAWN_WARMUP_TICKS;
    const bool warmedUp = g.scene.tick - g.scene.firstPlayerTick > warmupTicks;

    const bivouac::engine::PlayerSnapshot player = gPlayers.snapshot(g.scene.player);
    const float linkX = player.position.x;
    const float linkY = player.position.y;
    const float linkZ = player.position.z;
    // The pause menu starves the raycast worker; placement and climb clocks freeze while it is quiet.
    const bool workerAliveThisTick = gRaycasts.observeWorkerForTick();
    if (workerAliveThisTick) { g.scene.gameplayTick++; }

    {
        const u32 heartbeat = g_climbHeartbeat;
        if (heartbeat != g.trigger.lastHeartbeat) {
            g.trigger.lastHeartbeat = heartbeat;
            g.trigger.lastHeartbeatTick = g.scene.tick;
            if (!g.trigger.heartbeatSeen) {
                g.trigger.heartbeatSeen = true;
                BVLOG("climb: heartbeat hook LIVE (first pulse at tick %d)", g.scene.tick);
            }
        }
        const bool pulsing = bivouac::policy::heartbeatClimbing(
            g.scene.tick, g.trigger.lastHeartbeatTick, CLIMB_HEARTBEAT_GRACE_TICKS);
        const bool climbing = bivouac::policy::resolveClimbing(
            climbObservation, pulsing || player.climbing);
        if (climbing) { g.trigger.lastClimbTick = g.scene.gameplayTick; }
        else if (g.trigger.climbFlagPrev) { g.trigger.climbEndTick = g.scene.gameplayTick; }
        if (climbing != g.trigger.climbFlagPrev) {
            BVLOG("climb: %s tick=%d gp=%d hb=%u flag=%u", climbing ? "START" : "END",
                  g.scene.tick, g.scene.gameplayTick, heartbeat, player.climbing ? 1u : 0u);
        }
        g.trigger.climbFlagPrev = climbing;
    }

    updateNearStamps(linkX, linkY, linkZ);

    if (g.trigger.trigPending && !workerAliveThisTick) { g.trigger.trigArmedTick++; }

    const bivouac::menu::Step menuStep =
        bivouac::menu::update(g.menu.menuStarvedTicks, workerAliveThisTick,
                              MENU_MIN_STARVE_TICKS);
    g.menu.menuStarvedTicks = menuStep.starvedTicks;
    const bool menuCloseEdge = menuStep.closed;

    if (warmedUp && g.persistence.sdState == 0 && g.scene.tick >= g.persistence.sdRetryAtTick) {
        const bivouac::persistence::StorageResult mounted = gLedgerStore.mountSdCard();
        if (mounted) {
            g.persistence.sdState = 1;
            BVLOG("sd: mounted");
            if (bivouac::log::sdLogOpen(LEDGER_DIR, LOG_PATH)) { BVLOG("log: writing %s", LOG_PATH); }
            else { BVLOG("log: sd file open FAILED"); }
            if (!g.persistence.ledgerLoaded) { ledgerRead(); }
        } else {
            g.persistence.sdTries++;
            g.persistence.sdRetryAtTick = g.scene.tick + SD_MOUNT_RETRY_TICKS;
            BVLOG("sd: mount failed %x (try %d/%d)", mounted.nativeResult,
                  g.persistence.sdTries, SD_MOUNT_MAX_TRIES);
            if (g.persistence.sdTries >= SD_MOUNT_MAX_TRIES) { g.persistence.sdState = 2; BVLOG("sd: RAM-ONLY MODE (camps won't persist)"); }
        }
    }

    if ((g.scene.tick & 255) == 0) {
        BVLOG("diag tick=%d gp=%d climbHb=%u flag=%u starved=%d near=%d lastClimb=%d climbEnd=%d sd=%u",
              g.scene.tick, g.scene.gameplayTick, g_climbHeartbeat, player.climbing ? 1u : 0u,
              g.menu.menuStarvedTicks, g.map.nearStampCount, g.trigger.lastClimbTick,
              g.trigger.climbEndTick, g.persistence.sdState);
    }
    if ((g.scene.tick & 31) == 0) { bivouac::log::sdLogFlush(); }

    comboTick(npadDevice, linkX, linkY, linkZ);

    if (g.trigger.trigPending) {
        placementTick(player);
    }
    refundTick();

    if (warmedUp) {
        gCampAssembler.pollSpawn(
            g.camps, g.spawn, g.scene.tick);
        gCampAssembler.maintainActors(g.camps, g.scene.tick);
        gCampAssembler.provision(
            g.spawn, {linkX, linkY, linkZ}, g.scene.tick);
        // Hold new spawns during a camp warp's loading screen so the destination deck spawns first.
        const bool warpInFlight = g.map.arrivalSite >= 0
            && g.map.arrival.phase == bivouac::feature::ArrivalPhase::AwaitingArrival;
        if (!warpInFlight) {
            gCampAssembler.schedule(
                g.camps, g.spawn, {linkX, linkY, linkZ}, g.scene.tick);
        }
        // Stamp writes only during live gameplay; the map-close edge reverts user stamp edits.
        if ((g.map.mapServiceDirty && workerAliveThisTick) || menuCloseEdge) {
            gMapStamps.serviceCampStamps(
                g.map, g.camps, g.persistence, g.scene.tick);
        }
        gMapStamps.assistArrival(
            g.map, g.camps, g.scene.player,
            {linkX, linkY, linkZ}, g.scene.tick);
    }
    audioTick(warmedUp);

    if (g.persistence.ledgerDirty && g.persistence.sdState == 1 && g.spawn.spawnSite < 0 && g.scene.tick - g.persistence.ledgerWroteTick >= LEDGER_WRITE_MIN_GAP) {
        ledgerWrite();
    }

#if BV_DIAG
    if (g.scene.tick % HEARTBEAT_TICKS == 0) {
        int activeSites = 0, aliveSlots = 0;
        for (int i = 0; i < MAX_SITES; i++) {
            if (!g.camps.sites[i].active) { continue; }
            activeSites++;
            for (int p = 0; p < CAMP_PROP_COUNT; p++) { if (g.camps.sites[i].slot[p].state == SlotState::ALIVE) { aliveSlots++; } }
        }
        BVLOG("hb tick=%d gp=%d climbHb=%u sites=%d alive-slots=%d sd=%u trig=%u", g.scene.tick, g.scene.gameplayTick, g_climbHeartbeat, activeSites, aliveSlots, g.persistence.sdState, g.trigger.trigPending);
    }
#endif
}

void serviceRaycastWorker(bivouac::runtime::RaycastFn original,
                          const void* from, const void* obj) {
    gRaycasts.noteWorkerCall();
    bool nearLinkCast = false;
    if (from != nullptr) {
        const float* f = (const float*)from;
        void* pl = g.scene.player;
        if (pl != nullptr) {
            const auto player = gPlayers.snapshot(pl);
            const float dx = f[0] - player.position.x;
            const float dy = f[1] - player.position.y;
            const float dz = f[2] - player.position.z;
            nearLinkCast = (dx * dx + dy * dy + dz * dz) < (8.0f * 8.0f);
        } else {
            nearLinkCast = (f[0] > 100.0f || f[0] < -100.0f ||
                            f[2] > 100.0f || f[2] < -100.0f);
        }
    }
    if (nearLinkCast && obj != nullptr && gRaycasts.requestPending()) {
        gRaycasts.prepareWorkerObject(obj);
        original(gRaycasts.requestFrom(), gRaycasts.requestTo(),
                 gRaycasts.workerObject(), nullptr, gRaycasts.requestMask(), 0u);
        gRaycasts.publishWorkerResult();
    }
}

HOOK_DEFINE_TRAMPOLINE(RayCastWorkerHook) {
    static u64 Callback(const void* from, const void* to, const void* obj,
                        const void* outp, u32 mask, u32 flag) {
        const u64 ret = Orig(from, to, obj, outp, mask, flag);
        serviceRaycastWorker(
            [](const void* f, const void* t, const void* o, const void* op,
               u32 m, u32 fl) -> u64 { return Orig(f, t, o, op, m, fl); },
            from, obj);
        return ret;
    }
};

// Runs once per climb-physics frame (the climb speed routine); counts, touches no registers.
HOOK_DEFINE_INLINE(ClimbHeartbeatHook) {
    static void Callback(exl::hook::InlineCtx*) {
        g_climbHeartbeat = g_climbHeartbeat + 1;
    }
};

HOOK_DEFINE_TRAMPOLINE(NpadCalcHook) {
    static void Callback(void* param_1) {
        Orig(param_1);
        bvTick(param_1, bivouac::runtime::ClimbObservation::LocalSensor);
    }
};

// Drives the camp stamp's private icon frame after the vanilla widget tick runs.
HOOK_DEFINE_TRAMPOLINE(StampIconWidgetTickHook) {
    static u64 Callback(u64 widgetCtx) {
        const u64 ret = Orig(widgetCtx);
        const u32 handleIndex = bivouac::presentation::campStampWidgetHandleIndex(widgetCtx);
        const bool nearLink = stampHandleNearLink(handleIndex);
        const bool fullMap = bivouac::menu::fullScreenMenuOpen(
            g.menu.menuStarvedTicks, MENU_MIN_STARVE_TICKS);
        const auto adjusted = bivouac::presentation::adjustCampStampWidget(
            widgetCtx, g.scene.mainBase, fullMap, nearLink);
        if (adjusted.ownedCampStamp && g.map.nearStampCount > 0
            && g.scene.tick - g.map.lastWidgetLogTick >= 120) {
            g.map.lastWidgetLogTick = g.scene.tick;
            BVLOG("map: owned stamp widget handle=%u near[0]=%u nearLink=%d fullMap=%d",
                  handleIndex, g.map.nearHandles[0], nearLink ? 1 : 0, fullMap ? 1 : 0);
        }
        if (adjusted.ownedCampStamp && g.map.widgetLogLines < 200) {
            const std::uint8_t state = static_cast<std::uint8_t>(
                1 | (nearLink ? 2 : 0) | (fullMap ? 4 : 0));
            int slot = -1;
            for (int i = 0; i < bivouac::runtime::MapState::kWidgetLogSlots; i++) {
                if (g.map.widgetSeen[i] == widgetCtx) { slot = i; break; }
                if (g.map.widgetSeen[i] == 0) {
                    g.map.widgetSeen[i] = widgetCtx;
                    g.map.widgetState[i] = 0;
                    slot = i;
                    break;
                }
            }
            if (slot >= 0 && g.map.widgetState[slot] != state) {
                g.map.widgetState[slot] = state;
                g.map.widgetLogLines++;
                BVLOG("map: icon widget 0x%lx handle=%u nearLink=%d fullMap=%d marker=%d fixed=%d "
                      "zoom=%d/1000 vanillaScale=%d/1000 wrote=%d/1000",
                      static_cast<unsigned long>(widgetCtx), handleIndex, nearLink ? 1 : 0,
                      fullMap ? 1 : 0, adjusted.markerFound ? 1 : 0, adjusted.fixedScale ? 1 : 0,
                      static_cast<int>(adjusted.zoom * 1000.0f), static_cast<int>(adjusted.vanillaMarkerScale * 1000.0f),
                      static_cast<int>(adjusted.writtenScale * 1000.0f));
            }
        }
        if (adjusted.frameSelected) {
            static bool logged = false;
            if (!logged) {
                logged = true;
                BVLOG("map: custom stamp icon driven to owned frame");
            }
        }
        return ret;
    }
};

// Map icon screen per-frame pass: refresh our stamps' cached cells and force one re-filter.
HOOK_DEFINE_TRAMPOLINE(MinimapIconPassHook) {
    static u64 Callback(u64 iconScreen) {
        if (!g.map.cellPassSeen) {
            g.map.cellPassSeen = true;
            BVLOG("map: minimap icon pass running");
        }
        const int epoch = g.map.cellEpoch;
        if (epoch != g.map.cellEpochSeen) {
            g.map.cellEpochSeen = epoch;
            bool refreshed = false;
            for (int i = 0; i < bivouac::runtime::kMaximumSites; i++) {
                const Site& s = g.camps.sites[i];
                if (!s.active || s.stampSlot == 0xFFFFu || s.handleIndex == 0xFFFFFFFFu) { continue; }
                const int r = bivouac::presentation::refreshCampStampCell(
                    iconScreen, s.stampSlot, s.handleIndex, s.anchor.x, s.anchor.z);
                if (r > 0) {
                    refreshed = true;
                    BVLOG("map: camp #%u minimap cell refreshed (stamp slot %u)", s.seq, s.stampSlot);
                } else if (r < 0 && g.map.cellMissLogs < 8) {
                    g.map.cellMissLogs++;
                    BVLOG("map: camp #%u minimap cell NOT refreshed - no widget for stamp slot %u (handle %u)",
                          s.seq, s.stampSlot, s.handleIndex);
                }
            }
            if (refreshed) {
                bivouac::presentation::forceMinimapRefilter(iconScreen);
            }
        }
        return Orig(iconScreen);
    }
};

// Map A-press dispatch (W8 = hovered icon type, X19 = screen): a camp stamp warps instead of opening its edit menu.
HOOK_DEFINE_INLINE(MapStampSelectHook) {
    static void Callback(exl::hook::InlineFloatCtx* floatCtx) {
        auto* ctx = reinterpret_cast<exl::hook::InlineCtx*>(
            reinterpret_cast<u8*>(floatCtx) + INLINE_FLOAT_CTX_GPR_OFFSET);
        if (ctx->W[8] != MAP_HOVER_TYPE_STAMP) { return; }
        const u64 screen = ctx->X[19];
        if (!okPtr(screen)) { return; }
        const u32 slot = *(u32*)(screen + MAP_SCREEN_HOVER_INDEX_OFFSET);
        if (!gMapStamps.warpToSelectedStamp(g.map, g.camps, screen, slot)) { return; }
        if (!bivouac::audio::playCue(bivouac::cues::kTravel)) { BVLOG("audio: travel cue refused"); }
        ctx->W[8] = MAP_HOVER_TYPE_NO_ACTION;
    }
};

bool hookWordMatches(ptrdiff_t offset, u32 expected, const char* label) {
    const u32 actual = *reinterpret_cast<const u32*>(g.scene.mainBase + offset);
    if (actual == expected) { return true; }
    BVLOG("%s DISABLED: main+%p word %08x != expected %08x (version/cheat conflict)",
          label, reinterpret_cast<void*>(offset), actual, expected);
    return false;
}

} // namespace

void bivouac::runtime::init(std::uintptr_t mainBase) {
    g.scene.mainBase = mainBase;
    gActors.setMainBase(g.scene.mainBase);
    gGameData.setMainBase(g.scene.mainBase);
    gPlayers.setMainBase(g.scene.mainBase);
    gMapStamps.setMainBase(g.scene.mainBase);
    bivouac::audio::initialize(g.scene.mainBase);
    gCampAssembler.configure(
        kCampProps, CAMP_PROP_COUNT,
        {
            {SEAT_CLEARANCE, BACKER_EXTRA, BACKER_MAX_SHIFT,
             BACKER_TILE_SPACING},
            BACKER_MIN_SPAWN,
            SITE_WANT_RADIUS,
            MAX_LIVE_SITES,
            SPAWN_GAP_TICKS,
            SPAWN_TIMEOUT_TICKS,
            SLOT_RETRY_TICKS,
            SLOT_MAX_FAILS,
            DIED_YOUNG_TICKS,
            HARDEN_DELAY_TICKS,
            SCHEDULER_PERIOD_TICKS,
            TIER_ITEM_CAMP,
            GOLDEN_APPLE_DROP_COUNT,
        });
    gPlacementCoordinator.configure(
        {
            TRIGGER_TTL_TICKS,
            MOTHER_BASE_TRIGGER_TTL_TICKS,
            COLLISION_MASK_SOLID,
            CAST_TIMEOUT_TICKS,
            WALL_CAST_BACKOFF,
            WALL_CAST_LENGTH,
            WALL_CAST_RETRIES,
            WALL_MAX_NORMAL_Y,
            ROOF_CLEARANCE,
            ROOF_TOP_BELOW_LINK,
            GROUND_PROBE_BELOW_DECK,
            GROUND_CLEARANCE,
            {
                SWEEP_RAY_T[0],
                SWEEP_RAY_T[1],
                SWEEP_RAY_T[2],
                SWEEP_RAY_T[3],
                SWEEP_RAY_T[4],
            },
            SWEEP_RAY_COUNT,
            SWEEP_BACKOFF,
            SWEEP_LENGTH,
            SWEEP_MISS_DEPTH,
            SWEEP_DIAG_MISS,
            SEAT_CLEARANCE,
            SEAT_MAX_OUT,
            BACKER_GAP_MAX,
            DECK_HALF_OUT,
            MOTHER_BASE_WATER_QUERIES_PER_TICK,
            MOTHER_BASE_SITE_SEPARATION_METERS,
        });
    BVLOG("exl_main begin base=%p (Bivouac v0.26.0, probe=%d)",
          (void*)g.scene.mainBase, BV_PROBE);
}

void bivouac::runtime::tick(void* npadDevice,
                            ClimbObservation observation) {
    bvTick(npadDevice, observation);
}

void bivouac::runtime::onRaycast(RaycastFn original, const void* from,
                                 const void* object) {
    serviceRaycastWorker(original, from, object);
}

bool bivouac::runtime::removeNearestCamp(float maximumDistance) {
    void* player = gPlayers.resolve();
    if (player == nullptr) return false;
    const auto snapshot = gPlayers.snapshot(player);
    return teardownNearestCamp(snapshot.position.x, snapshot.position.y,
                               snapshot.position.z, maximumDistance, "suite");
}

bivouac::runtime::Status bivouac::runtime::status() {
    int active = 0;
    for (const Site& site : g.camps.sites) {
        if (site.active) ++active;
    }
    return {active, g.persistence.sdState};
}

void bivouac::runtime::installUniqueHooks() {
    ConsumeMaterialTrigger::InstallAtOffset(off::ConsumeMaterial);
    PouchLeafHook::InstallAtOffset(off::PouchCountLeaf);
    StampIconWidgetTickHook::InstallAtOffset(off::StampIconWidgetTick);
    if (hookWordMatches(off::MapHoverDispatch, MAP_HOVER_DISPATCH_WORD, "map stamp travel")) {
        MapStampSelectHook::InstallAtOffset(off::MapHoverDispatch);
    }
    if (hookWordMatches(off::MinimapIconPass, MINIMAP_ICON_PASS_WORD, "minimap cell refresh")) {
        MinimapIconPassHook::InstallAtOffset(off::MinimapIconPass);
    }
}

void bivouac::runtime::install() {
    exl::hook::Initialize();
    init(exl::util::modules::GetTargetStart());

    RayCastWorkerHook::InstallAtOffset(off::RaycastWorker);
    NpadCalcHook::InstallAtOffset(off::NpadCalc);
    if (hookWordMatches(off::ClimbSpeedPost, CLIMB_SPEED_POST_WORD, "climb heartbeat")) {
        ClimbHeartbeatHook::InstallAtOffset(off::ClimbSpeedPost);
    }
    installUniqueHooks();
    BVLOG("hooks installed (raycast, npad, climb heartbeat, consume trigger, pouch leaf, stamp icon, stamp travel, minimap cell)");
}
