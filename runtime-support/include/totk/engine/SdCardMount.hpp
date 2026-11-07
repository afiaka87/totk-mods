#pragma once

namespace totk::engine {

// A composed subsdk9 has one nn::fs mount namespace even when several source-linked
// features persist independent ledgers. Mounting the same name twice is an error, so
// every member must share this process-wide result rather than owning the mount itself.
enum class SdCardMountResult {
    MountedNow,
    AlreadyReady,
    Failed,
};

// Mounts `sdcard` at most once after the first success. Failures are not cached: the
// caller retains ownership of retry timing and failure policy.
[[nodiscard]] SdCardMountResult ensureSdCardMounted();

}  // namespace totk::engine
