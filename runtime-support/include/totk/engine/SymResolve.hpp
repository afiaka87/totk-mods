// SPDX-License-Identifier: GPL-2.0-only
//
// Shared TotK runtime support — runtime symbol resolution helpers.
// Copyright (C) aquacluck and the totk-lotuskit contributors
//   <https://github.com/aquacluck/totk-lotuskit>
//
// Vendored VERBATIM from totk-lotuskit (src/program/exlaunch.hpp): the
// EXL_SYM_RESOLVE helper the shared TextWriter uses to resolve engine functions
// by name against the runtime symbol table. Canonicalized from nine byte-identical
// per-mod copies (F-B2); the syms tables themselves stay per-build
// (vendor/totk-sdk/syms/output/TOTK_<version> on the include path).

#pragma once
#include <string>

// exlaunch
#include <lib.hpp>

inline ptrdiff_t EXL_SYM_OFFSET(std::string sym) {
    auto search = exl::reloc::GetLookupTable().FindByName(sym);
    if (search == nullptr) { return 0; }
    return search->m_Offset;
}

template<class T>
inline T EXL_SYM_RESOLVE(std::string sym) {
    auto search = exl::reloc::GetLookupTable().FindByName(sym);
    if (search == nullptr) { return nullptr; }
    return reinterpret_cast<T>(exl::util::modules::GetTargetStart() + search->m_Offset); // TODO other modules
}
