// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstdint>

#include "HookshotRuntime.hpp"

namespace arrowbound::arrow_mode {

inline constexpr const char* kCarrierActor = "Obj_CaveWellHonor_00";

void initialize(std::uintptr_t mainBase);
void service(HookshotRuntime& runtime);
void onWorldReset(std::uint32_t worldGeneration);

bool enabled();
void setCodeOnlyReady(bool ready);
std::uintptr_t selectionDeal(std::uintptr_t original);
bool replaceMessage(void* output, const char* const* table, const char* const* key);

void beginSelection(std::uint32_t category, std::uint32_t index);
void endSelection(void* screen);
bool beginAction(int action);
void refreshSelectedSlot(void* screen);
bool isCarrierSlot(const void* options);
void filterSerializedSave(void* manager, std::uint32_t fileIndex);

}  // namespace arrowbound::arrow_mode
