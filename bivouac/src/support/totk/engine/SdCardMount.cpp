// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "totk/engine/SdCardMount.hpp"

#include <nn/fs.h>

namespace nn::fs {
nn::Result MountSdCard(char const* mount);
}  // namespace nn::fs

namespace totk::engine {
namespace {

bool gSdCardReady = false;

}  // namespace

SdCardMountResult ensureSdCardMounted() {
    if (gSdCardReady) return SdCardMountResult::AlreadyReady;
    const nn::Result result = nn::fs::MountSdCard("sdcard");
    if (result.IsFailure()) return SdCardMountResult::Failed;
    gSdCardReady = true;
    return SdCardMountResult::MountedNow;
}

}  // namespace totk::engine
