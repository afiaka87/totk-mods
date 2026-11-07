// SPDX-License-Identifier: GPL-2.0-only
//
// Shared TotK runtime support — diagnostics sink for the UI layer.
// Copyright (C) Clay Mullis
//
// Shared code must not bind to a per-mod Logger (matrix T11/O10). Guard and
// breadcrumb messages route through one settable sink; the default is the debug
// service output, which lands in the Eden log. A mod that owns a richer logger
// installs it once at load time:
//
//     totk::ui::setDiagnosticsSink(&myLoggerAdapter);

#pragma once

#include <cstring>

#include <lib.hpp>  // exlaunch: svcOutputDebugString

namespace totk::ui {

using DiagnosticsSink = void (*)(const char* message);

inline DiagnosticsSink g_diagnosticsSink = nullptr;

inline void setDiagnosticsSink(DiagnosticsSink sink) { g_diagnosticsSink = sink; }

inline void emitDiagnostic(const char* message) {
    if (g_diagnosticsSink != nullptr) {
        g_diagnosticsSink(message);
        return;
    }
    svcOutputDebugString(message, strlen(message));
}

} // namespace totk::ui
