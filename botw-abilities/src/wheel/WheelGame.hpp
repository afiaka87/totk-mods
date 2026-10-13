// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/engine/CodeCheck.hpp"
#include <cstddef>
#include <cstdint>

// TotK 1.2.1 (9B4E4365) game routines and wheel-screen layout used by the abilities page.
namespace botw_abilities::wheel::game {
namespace offset {
inline constexpr std::uintptr_t kNpadCalc = 0x2A267BC;
// game::ui::UIShortCutMainItemScreen, the hold-L ability wheel.
inline constexpr std::uintptr_t kWheelOpen = 0x1B1DC94;   // openStartImpl_
inline constexpr std::uintptr_t kWheelUpdate = 0x1B1E65C; // updateImpl_
inline constexpr std::uintptr_t kWheelExit = 0x19AD3F4;   // doOutBefore
inline constexpr std::uintptr_t kWheelClose = 0x1B1EEB8;  // closeEndImpl_
// One wedge (UIShortCutMainItemCirclePartsScreen) and the caption.
inline constexpr std::uintptr_t kRequestIcon = 0x1ACC35C;
inline constexpr std::uintptr_t kUnloadIcon = 0x1ACC39C;
inline constexpr std::uintptr_t kSetValid = 0x1B1E514;
inline constexpr std::uintptr_t kSetCheck = 0x1B1E534;
inline constexpr std::uintptr_t kPouchName = 0x1A89340;
inline constexpr std::uintptr_t kFindMessage = 0xB51940;
inline constexpr std::uintptr_t kLayoutSetMessage = 0xB519B0;
} // namespace offset

namespace field {
inline constexpr std::ptrdiff_t kLayout = 0x28, kWedgeCount = 0x1A8, kWedges = 0x1B0,
                                kIconLoader = 0x1B8, kCursor = 0x23C, kIds = 0x270;
// Wedge: animators, icon path buffer and icon request flags (bit0 requested, bit1 loading).
inline constexpr std::ptrdiff_t kInvalidAnim = 0x48, kCheckAnim = 0x50, kIconPattern = 0x58,
                                kIconPath = 0x98, kIconFlags = 0x128;
inline constexpr std::ptrdiff_t kAnimFrame = 0x20, kSetFrameSlot = 0xD0;
} // namespace field

// The caption text handed to the layout: a view of UTF-16 text.
struct MessageString {
    const char16_t *text = nullptr;
    std::uint32_t length = 0;
    std::int64_t index = -1;
};
static_assert(sizeof(MessageString) == 24);

using RequestIcon = bool (*)(std::uintptr_t wedge, const char *const *path, std::uintptr_t loader);
using UnloadIcon = void (*)(std::uintptr_t wedge);
using SetFlag = void (*)(std::uintptr_t wedge, bool on);
using SetFrame = void (*)(std::uintptr_t animator, float frame);
using PouchName = bool (*)(MessageString *out, const char *const *actor, bool);
using FindMessage = bool (*)(MessageString *out, const char *const *file, const char *const *label,
                             bool);
using LayoutSetMessage = void (*)(std::uintptr_t layout, const char *pane,
                                  const MessageString *text, bool, void *);

// Native ability IDs 2..8 name their caption by key-item actor; 1 (Map) uses a reference label.
inline const char *abilityActor(std::int32_t id) {
    static constexpr const char *kActors[]{"Obj_UltraHand",       "Obj_OneTouchBond",
                                           "Obj_Tooreroof",       "Obj_ReverseRecorder",
                                           "Obj_AutoBuilder",     "Obj_Camera",
                                           "Obj_AmiiboItem"};
    return id >= 2 && id <= 8 ? kActors[id - 2] : nullptr;
}

// Entries the abilities already hooked start with a jump, which the check accepts.
inline constexpr totk::engine::CodeSite kSites[]{
    {offset::kNpadCalc, 0xD102C3FF, 0xA9057BFD},
    {offset::kWheelOpen, 0xD10283FF, 0xFD001BE8},
    {offset::kWheelUpdate, 0xD10203FF, 0x6D032BEB},
    {offset::kWheelExit, 0xA9BE7BFD, 0xA9014FF4},
    {offset::kWheelClose, 0xA9BE7BFD, 0xA9014FF4},
    {offset::kRequestIcon, 0xA9BE7BFD, 0xA9014FF4},
    {offset::kUnloadIcon, 0xF9404408, 0x91018000},
    {offset::kSetValid, 0xF9402400, 0x7200003F},
    {offset::kSetCheck, 0xF9402800, 0x7200003F},
    {offset::kPouchName, 0xA9BD7BFD, 0xA90157FC},
    {offset::kFindMessage, 0xD10103FF, 0xA9027BFD},
    {offset::kLayoutSetMessage, 0xA9BD7BFD, 0xF9000BF5},
};
} // namespace botw_abilities::wheel::game
