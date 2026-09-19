// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include "../BivouacPolicy.hpp"
#include "../feature/CampGeometry.hpp"

#include <types.h>

namespace bivouac::runtime::config {

using CampTier = bivouac::policy::CampTier;
constexpr const char* TIER_ITEM_CAMP = bivouac::policy::kCampItem;

constexpr u32 MOTION_FIXED_STATIC = 0;
constexpr u32 MOTION_KEYFRAMED    = 1;
constexpr u32 MOTION_DYNAMIC      = 2;

// Ticks are npad ticks (about 40 Hz); climb clocks count gameplay ticks only; distances are meters.
constexpr int   CLIMB_RECENT_TICKS  = 33;
constexpr int   CLIMB_HEARTBEAT_GRACE_TICKS = 4;
constexpr int   TRIGGER_COOLDOWN_TICKS = 33;
constexpr int   CLIMB_MENU_GRACE_TICKS = 40;
constexpr int   GOLDEN_APPLE_DROP_COUNT = 10;
constexpr int   TRIGGER_TTL_TICKS      = 160;
// 3D spacing, also the near-camp minimap radius; must stay inside the minimap's stamp range.
constexpr float SITE_SEPARATION_METERS = 160.0f;
constexpr int   MOTHER_BASE_TRIGGER_TTL_TICKS = 330;
constexpr u32   MOTHER_BASE_WATER_QUERIES_PER_TICK = 8;
constexpr float MOTHER_BASE_SITE_SEPARATION_METERS = 250.0f;
// Ledger decode keeps the old spacing so camps saved under it still load.
constexpr float LEDGER_OVERLAP_METERS = 20.0f;

constexpr float WALL_CAST_BACKOFF   = 1.0f;
constexpr float WALL_CAST_LENGTH    = 4.0f;
constexpr int   WALL_CAST_RETRIES   = 3;
constexpr float WALL_MAX_NORMAL_Y   = 0.6f;
constexpr float ROOF_CLEARANCE      = 5.0f;
constexpr float ROOF_TOP_BELOW_LINK = 2.0f;
constexpr float SWEEP_RAY_T[5]      = { -4.0f, -2.0f, 0.0f, 2.0f, 4.0f };
constexpr int   SWEEP_RAY_COUNT     = 5;
constexpr float SWEEP_BACKOFF       = 4.0f;
constexpr float SWEEP_LENGTH        = 12.0f;
constexpr float SWEEP_MISS_DEPTH    = -7.0f;
constexpr float SWEEP_DIAG_MISS     = 99.99f;
constexpr float GROUND_PROBE_BELOW_DECK = 1.0f;
constexpr float GROUND_CLEARANCE    = 0.25f;
constexpr float SEAT_CLEARANCE      = 0.10f;
constexpr float SEAT_MAX_OUT        = 2.5f;
constexpr float BACKER_EXTRA        = 0.5f;
constexpr float BACKER_TILE_SPACING = 3.5f;
constexpr float BACKER_MAX_SHIFT    = 6.0f;
constexpr float BACKER_MIN_SPAWN    = 0.25f;
constexpr float BACKER_DROP         = 0.03f;
constexpr float BACKER_GAP_MAX      = 5.4f;
constexpr int   CAST_TIMEOUT_TICKS  = 8;

// AsbObj_StoneRectangle_B_LL_01 extents: 4.08 out of the wall x 8.1 along it x 0.47 thick.
constexpr float DECK_HALF_OUT   = 2.04f;
constexpr float DECK_HALF_THICK = 0.235f;
constexpr float DECK_OUT_SPAN   = 2.0f * DECK_HALF_OUT;
constexpr float DECK_HALF_ALONG = 4.05f;
constexpr float DECK_ALONG_SPAN = 2.0f * DECK_HALF_ALONG;

constexpr int   MAX_SITES        = 64;
constexpr int   MAX_PROPS        = 56;
constexpr int   MAX_LIVE_SITES   = 3;
constexpr float SITE_WANT_RADIUS = 140.0f;
constexpr int   SPAWN_GAP_TICKS      = 4;
constexpr int   SPAWN_TIMEOUT_TICKS  = 110;
constexpr int   SPAWN_WARMUP_TICKS   = 33;
constexpr int   SLOT_MAX_FAILS       = 5;
constexpr int   SLOT_RETRY_TICKS     = 22;
constexpr int   DIED_YOUNG_TICKS     = 330;
constexpr int   REFUND_DELAY_TICKS   = 12;
constexpr int   REFUND_QUEUE_CAP     = 4;
constexpr int   HARDEN_DELAY_TICKS   = 3;
constexpr int   SCENE_GONE_TICKS     = 5;
constexpr int   SCHEDULER_PERIOD_TICKS = 4;
constexpr int   HEARTBEAT_TICKS      = 1024;

constexpr const char* LEDGER_DIR  = "sdcard:/totk_bivouac";
constexpr const char* LEDGER_PATH = "sdcard:/totk_bivouac/sites.bin";
constexpr const char* LOG_PATH    = "sdcard:/totk_bivouac/bivouac.log";
constexpr int SD_MOUNT_RETRY_TICKS = 512;
constexpr int SD_MOUNT_MAX_TRIES   = 5;
constexpr int LEDGER_WRITE_MIN_GAP = 11;

constexpr u32 COLLISION_MASK_SOLID = 0x20;

using PropDef = bivouac::feature::PropDefinition;
constexpr u8 DEP_NONE = 0xFF;
constexpr u8 TM_CAMP = 1u << 0;
constexpr u8 TM_BIVY = 1u << 1;
constexpr u8 TM_BASE = 1u << 2;
constexpr u8 TM_CB   = TM_CAMP | TM_BASE;
constexpr u8 TM_ALL  = TM_CAMP | TM_BIVY | TM_BASE;

// Columns: name, t (along wall), n (out of wall), lift, yawS, yawC, freeze, expectBody, backerLevel, backerTile, dependsOn (row index), tiers, radius; camp uses the base's middle 3x3 (11 of 27 slabs).
constexpr PropDef kCampProps[] = {
    { "AsbObj_StoneRectangle_B_LL_01",          0.0f,  0.0f,                  -DECK_HALF_THICK,               0.0f,  1.0f, 1, 1, 0, 0, DEP_NONE, TM_ALL,  140.0f },
    { "AsbObj_WoodRectangle_A_LL_01",           0.0f,  0.0f,                   ROOF_CLEARANCE,                0.0f,  1.0f, 1, 1, 0, 0, DEP_NONE, TM_ALL,  140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",          0.0f,  DECK_OUT_SPAN,         -DECK_HALF_THICK,               0.0f,  1.0f, 1, 1, 0, 0, DEP_NONE, TM_ALL,  140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",          0.0f,  2.0f * DECK_OUT_SPAN,  -DECK_HALF_THICK,               0.0f,  1.0f, 1, 1, 0, 0, DEP_NONE, TM_CB,   140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",          0.0f,  0.0f,                  -DECK_HALF_THICK - BACKER_DROP, 0.0f,  1.0f, 1, 1, 1, 0, DEP_NONE, TM_ALL,  140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",          0.0f,  0.0f,                  -DECK_HALF_THICK - BACKER_DROP, 0.0f,  1.0f, 1, 1, 1, 1, DEP_NONE, TM_ALL,  140.0f },
    { "AsbObj_WoodRectangle_A_LL_01",           0.0f,  0.0f,                   ROOF_CLEARANCE - BACKER_DROP,  0.0f,  1.0f, 1, 1, 2, 0, DEP_NONE, TM_ALL,  140.0f },
    { "AsbObj_WoodRectangle_A_LL_01",           0.0f,  0.0f,                   ROOF_CLEARANCE - BACKER_DROP,  0.0f,  1.0f, 1, 1, 2, 1, DEP_NONE, TM_ALL,  140.0f },
    { "Item_CookSetOnFire",                     1.5f,  0.6f,                   0.408f,                        0.0f,  1.0f, 0, 1, 0, 0, DEP_NONE, TM_ALL,   70.0f },
    { "FldObj_TravelerTrace_Bed_A_01",          0.22f, 0.04f,                  0.0f,                          1.0f,  0.0f, 0, 0, 0, 0, DEP_NONE, TM_CB,    70.0f },
    { "FldObj_MercenaryBivouacFlagStand_A_01", -3.5f,  1.5f + DECK_OUT_SPAN,   0.345f,                        0.0f, -1.0f, 0, 0, 0, 0, DEP_NONE, TM_CAMP, 140.0f },
    { "FldObj_MercenaryBivouacFlagStand_A_01", -(2.0f * DECK_ALONG_SPAN + 3.5f), 1.5f + 6.0f * DECK_OUT_SPAN, 0.345f, 0.0f, -1.0f, 0, 0, 0, 0, 38, TM_BASE, 140.0f },
    { "LightBallBud_Large",                     3.4f,  1.6f + DECK_OUT_SPAN,   0.02f,                         0.0f,  1.0f, 0, 0, 0, 0, DEP_NONE, TM_CAMP, 140.0f },
    { "LightBallBud_Large",                     2.0f * DECK_ALONG_SPAN + 3.4f, 1.6f + 6.0f * DECK_OUT_SPAN, 0.02f, 0.0f, 1.0f, 0, 0, 0, 0, 43, TM_BASE, 140.0f },
    { "AsbObj_StoneSquare_B_M_01",             -6.07f, DECK_OUT_SPAN,         -0.2167f,                       0.0f,  1.0f, 1, 1, 0, 0, 2,        TM_CB,   140.0f },
    { "FldObj_TravelerTraceTent_A_S_01",       -6.07f, DECK_OUT_SPAN,          0.322f,                        1.0f,  0.0f, 0, 0, 0, 0, 14,       TM_CB,    70.0f },
    { "Obj_BarrelOld_A_01",                    -3.2f, -1.0f,                   0.02f,                         0.0f,  1.0f, 0, 1, 0, 0, DEP_NONE, TM_CB,    70.0f },
    { "Obj_BarrelOld_A_01",                    -3.35f, 0.0f,                   0.02f,                         0.0f,  1.0f, 0, 1, 0, 0, DEP_NONE, TM_CB,    70.0f },
    { "SpObj_SpringPiston_A_01",                2.7f,  4.9f,                   0.03f,                         0.0f,  1.0f, 0, 1, 0, 0, 2,        TM_BASE,  70.0f },
    { "AsbObj_StoneRectangle_B_LL_01",         -DECK_ALONG_SPAN,  2.0f * DECK_OUT_SPAN,   -DECK_HALF_THICK,               0.0f,  1.0f, 1, 1, 0, 0, 3,        TM_CB,   140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",          DECK_ALONG_SPAN,  2.0f * DECK_OUT_SPAN,   -DECK_HALF_THICK,               0.0f,  1.0f, 1, 1, 0, 0, 3,        TM_CB,   140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",         -DECK_ALONG_SPAN,  3.0f * DECK_OUT_SPAN,   -DECK_HALF_THICK,               0.0f,  1.0f, 1, 1, 0, 0, 19,       TM_CB,   140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",          0.0f,             3.0f * DECK_OUT_SPAN,   -DECK_HALF_THICK,               0.0f,  1.0f, 1, 1, 0, 0, 3,        TM_CB,   140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",          DECK_ALONG_SPAN,  3.0f * DECK_OUT_SPAN,   -DECK_HALF_THICK,               0.0f,  1.0f, 1, 1, 0, 0, 20,       TM_CB,   140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",         -DECK_ALONG_SPAN,  4.0f * DECK_OUT_SPAN,   -DECK_HALF_THICK,               0.0f,  1.0f, 1, 1, 0, 0, 21,       TM_CB,   140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",          0.0f,             4.0f * DECK_OUT_SPAN,   -DECK_HALF_THICK,               0.0f,  1.0f, 1, 1, 0, 0, 22,       TM_CB,   140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",          DECK_ALONG_SPAN,  4.0f * DECK_OUT_SPAN,   -DECK_HALF_THICK,               0.0f,  1.0f, 1, 1, 0, 0, 23,       TM_CB,   140.0f },
    { "AsbObj_WoodStick_A_LL_01",              -2.0f * DECK_ALONG_SPAN, 2.0f * DECK_OUT_SPAN, 0.233f,                   0.0f,  1.0f, 0, 0, 0, 0, 34,       TM_BASE,  70.0f },
    { "AsbObj_WoodRectangle_A_M_01",            2.0f * DECK_ALONG_SPAN, 2.0f * DECK_OUT_SPAN, 0.138f,                   0.0f,  1.0f, 0, 0, 0, 0, 39,       TM_BASE,  70.0f },
    { "AsbObj_StoneSquare_B_M_01",             -2.0f * DECK_ALONG_SPAN, 3.0f * DECK_OUT_SPAN, 0.210f,                   0.0f,  1.0f, 0, 0, 0, 0, 35,       TM_BASE,  70.0f },
    { "AsbObj_StoneSquare_B_M_01",              2.0f * DECK_ALONG_SPAN, 3.0f * DECK_OUT_SPAN, 0.210f,                   0.0f,  1.0f, 0, 0, 0, 0, 40,       TM_BASE,  70.0f },
    { "AsbObj_MetalRectangle_A_M_01",          -2.0f * DECK_ALONG_SPAN, 4.0f * DECK_OUT_SPAN, 0.137f,                   0.0f,  1.0f, 0, 0, 0, 0, 36,       TM_BASE,  70.0f },
    { "AsbObj_MetalRectangle_A_M_01",           2.0f * DECK_ALONG_SPAN, 4.0f * DECK_OUT_SPAN, 0.137f,                   0.0f,  1.0f, 0, 0, 0, 0, 41,       TM_BASE,  70.0f },
    { "Npc_TripMaster_Bivouac",                -DECK_ALONG_SPAN + 2.2f, 2.0f * DECK_OUT_SPAN + 1.0f, 0.02f,              0.0f, -1.0f, 0, 0, 0, 0, 19,       TM_BASE,  70.0f },
    { "AsbObj_StoneRectangle_B_LL_01",         -2.0f * DECK_ALONG_SPAN,  2.0f * DECK_OUT_SPAN, -DECK_HALF_THICK,  0.0f,  1.0f, 1, 1, 0, 0, 19,       TM_BASE, 140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",         -2.0f * DECK_ALONG_SPAN,  3.0f * DECK_OUT_SPAN, -DECK_HALF_THICK,  0.0f,  1.0f, 1, 1, 0, 0, 34,       TM_BASE, 140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",         -2.0f * DECK_ALONG_SPAN,  4.0f * DECK_OUT_SPAN, -DECK_HALF_THICK,  0.0f,  1.0f, 1, 1, 0, 0, 35,       TM_BASE, 140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",         -2.0f * DECK_ALONG_SPAN,  5.0f * DECK_OUT_SPAN, -DECK_HALF_THICK,  0.0f,  1.0f, 1, 1, 0, 0, 36,       TM_BASE, 140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",         -2.0f * DECK_ALONG_SPAN,  6.0f * DECK_OUT_SPAN, -DECK_HALF_THICK,  0.0f,  1.0f, 1, 1, 0, 0, 37,       TM_BASE, 140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",          2.0f * DECK_ALONG_SPAN,  2.0f * DECK_OUT_SPAN, -DECK_HALF_THICK,  0.0f,  1.0f, 1, 1, 0, 0, 20,       TM_BASE, 140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",          2.0f * DECK_ALONG_SPAN,  3.0f * DECK_OUT_SPAN, -DECK_HALF_THICK,  0.0f,  1.0f, 1, 1, 0, 0, 39,       TM_BASE, 140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",          2.0f * DECK_ALONG_SPAN,  4.0f * DECK_OUT_SPAN, -DECK_HALF_THICK,  0.0f,  1.0f, 1, 1, 0, 0, 40,       TM_BASE, 140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",          2.0f * DECK_ALONG_SPAN,  5.0f * DECK_OUT_SPAN, -DECK_HALF_THICK,  0.0f,  1.0f, 1, 1, 0, 0, 41,       TM_BASE, 140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",          2.0f * DECK_ALONG_SPAN,  6.0f * DECK_OUT_SPAN, -DECK_HALF_THICK,  0.0f,  1.0f, 1, 1, 0, 0, 42,       TM_BASE, 140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",         -DECK_ALONG_SPAN,         5.0f * DECK_OUT_SPAN, -DECK_HALF_THICK,  0.0f,  1.0f, 1, 1, 0, 0, 24,       TM_BASE, 140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",          0.0f,                    5.0f * DECK_OUT_SPAN, -DECK_HALF_THICK,  0.0f,  1.0f, 1, 1, 0, 0, 25,       TM_BASE, 140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",          DECK_ALONG_SPAN,         5.0f * DECK_OUT_SPAN, -DECK_HALF_THICK,  0.0f,  1.0f, 1, 1, 0, 0, 26,       TM_BASE, 140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",         -DECK_ALONG_SPAN,         6.0f * DECK_OUT_SPAN, -DECK_HALF_THICK,  0.0f,  1.0f, 1, 1, 0, 0, 44,       TM_BASE, 140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",          0.0f,                    6.0f * DECK_OUT_SPAN, -DECK_HALF_THICK,  0.0f,  1.0f, 1, 1, 0, 0, 45,       TM_BASE, 140.0f },
    { "AsbObj_StoneRectangle_B_LL_01",          DECK_ALONG_SPAN,         6.0f * DECK_OUT_SPAN, -DECK_HALF_THICK,  0.0f,  1.0f, 1, 1, 0, 0, 46,       TM_BASE, 140.0f },
};
constexpr int CAMP_PROP_COUNT = int(sizeof(kCampProps) / sizeof(kCampProps[0]));
static_assert(CAMP_PROP_COUNT == 50);
static_assert(CAMP_PROP_COUNT <= MAX_PROPS);

} // namespace bivouac::runtime::config
