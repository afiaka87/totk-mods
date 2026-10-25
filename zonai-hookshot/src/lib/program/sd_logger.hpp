// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include <lib/log/ilogger.hpp>
#include <string_view>

namespace zonai_hookshot::log {

// Copies each log line into RAM; sdLogService writes them to sdmc:/zonai-hookshot/hookshot.log.
struct SdFileLogger : public exl::log::ILogger {
    virtual void LogRaw(std::string_view string) final;
};

// Mounts the SD card once startup has settled, then appends buffered lines. Call once per
// controller tick; concurrent calls return immediately.
void sdLogService();

}  // namespace zonai_hookshot::log
