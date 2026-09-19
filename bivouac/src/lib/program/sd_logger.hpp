// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include <lib/log/ilogger.hpp>
#include <string_view>

namespace bivouac::log {

// Mirrors every log line into a RAM buffer; the mod's tick drains it to the SD card.
struct SdFileLogger : public exl::log::ILogger {
    virtual void LogRaw(std::string_view string) final;
};

// Opens (truncating) the SD log once the card is mounted. Safe to call again; no-op when open.
bool sdLogOpen(const char* directory, const char* path);
// Writes buffered lines to the file. Call from one thread only (the mod's tick).
void sdLogFlush();
bool sdLogIsOpen();

} // namespace bivouac::log
