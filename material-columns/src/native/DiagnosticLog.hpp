// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <string_view>
namespace material_columns::native {
void queueDiagnostic(std::string_view text);
#if defined(MATERIAL_COLUMNS_PANEL_HOST_TEST) || defined(MATERIAL_COLUMNS_RELEASE)
inline void pumpDiagnostics(bool = false) {}
#else
void pumpDiagnostics(bool force = false);
#endif
struct SdDiagnosticLogger { void LogRaw(std::string_view text) { queueDiagnostic(text); } };
}
