// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "ColumnPanel.hpp"
#include "../pure/StateJournal.hpp"
namespace material_columns::native {
using SavedPreferences = Preferences<maximumColumns>;
bool initializePreferenceStorage(std::uintptr_t mainBase, bool validateOnly = false);
// Called at a material-menu opening with defaults from the current native stock mode. An unchanged profile leaves the caller's process memory intact. A changed profile must discard the previous memory even when its disk is unavailable.
struct PreferenceLoad { bool changed{}, available{}; };
PreferenceLoad loadPreferences(SavedPreferences& value);
bool savePreferences(const SavedPreferences& value);
}
