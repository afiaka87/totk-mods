#pragma once
#include <lib/log/logger_mgr.hpp>

struct LinkedStickLogSink {
    void LogRaw(std::string_view text) {
        char line[exl::setting::LogBufferSize + 1];
        auto size = std::min(text.size(), sizeof(line) - 1);
        std::memcpy(line, text.data(), size);
        if (!size || line[size - 1] != '\n') line[size++] = '\n';
        svcOutputDebugString(line, size);
    }
};
