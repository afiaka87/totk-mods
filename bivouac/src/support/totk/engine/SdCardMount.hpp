// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

namespace totk::engine {

enum class SdCardMountResult {
    MountedNow,
    AlreadyReady,
    Failed,
};

// Mounts "sdcard" once per process; failures are not cached, so callers own retry timing.
[[nodiscard]] SdCardMountResult ensureSdCardMounted();

}  // namespace totk::engine
