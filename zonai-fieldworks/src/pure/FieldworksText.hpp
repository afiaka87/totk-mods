// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
// Generated from the mod's text table; do not edit.
#pragma once
#include "totk/text/Language.hpp"

namespace fieldworks::text {
// The switch page caption is the member name, then on or off, then any notes in order (pure/SwitchFile.hpp
// switchCaption). The whole caption fits 63 UTF-16 units in every language (tests/switch_file_test.cpp).
struct Switches {
    const char16_t *on, *off, *afterAction, *couldNotStart, *newAreas;
};
inline constexpr const char *kSwitchesKeys[]{"on", "off", "after_action", "could_not_start", "new_areas"};
inline constexpr const char16_t *Switches::*kSwitchesFields[]{&Switches::on, &Switches::off, &Switches::afterAction, &Switches::couldNotStart, &Switches::newAreas};
// One row per language, in totk::text::kLanguageCodes order.
inline constexpr Switches kSwitches[totk::text::kLanguageCount]{
    // JPja
    {u"\uff1a\u30aa\u30f3",
     u"\uff1a\u30aa\u30d5",
     u"\uff08\u4eca\u306e\u52d5\u4f5c\u304c\u7d42\u308f\u3063\u305f\u3089\uff09",
     u"\uff08\u958b\u59cb\u3067\u304d\u307e\u305b\u3093\u3067\u3057\u305f\uff09",
     u"\uff08\u65b0\u3057\u304f\u8aad\u307f\u8fbc\u3093\u3060\u5834\u6240\uff09"},
    // USen
    {u": On",
     u": Off",
     u" when the current action ends",
     u" (could not start)",
     u" (newly loaded areas)"},
    // USes
    {u": Activado",
     u": Desactivado",
     u" al terminar la acci\u00f3n actual",
     u" (no se pudo iniciar)",
     u" (zonas reci\u00e9n cargadas)"},
    // USfr
    {u" : Activ\u00e9",
     u" : D\u00e9sactiv\u00e9",
     u" \u00e0 la fin de l\u2019action en cours",
     u" (d\u00e9marrage impossible)",
     u" (zones nouvellement charg\u00e9es)"},
    // USpt
    {u": Ativado",
     u": Desativado",
     u" ao terminar a a\u00e7\u00e3o atual",
     u" (n\u00e3o foi poss\u00edvel iniciar)",
     u" (\u00e1reas rec\u00e9m-carregadas)"},
    // EUen
    {u": On",
     u": Off",
     u" when the current action ends",
     u" (could not start)",
     u" (newly loaded areas)"},
    // EUes
    {u": Activado",
     u": Desactivado",
     u" al terminar la acci\u00f3n actual",
     u" (no se pudo iniciar)",
     u" (zonas reci\u00e9n cargadas)"},
    // EUfr
    {u" : Activ\u00e9",
     u" : D\u00e9sactiv\u00e9",
     u" \u00e0 la fin de l\u2019action en cours",
     u" (d\u00e9marrage impossible)",
     u" (zones nouvellement charg\u00e9es)"},
    // EUde
    {u": An",
     u": Aus",
     u" nach der aktuellen Aktion",
     u" (Start fehlgeschlagen)",
     u" (neu geladene Gebiete)"},
    // EUit
    {u": Attivato",
     u": Disattivato",
     u" al termine dell\u2019azione in corso",
     u" (avvio non riuscito)",
     u" (aree appena caricate)"},
    // EUpt (no TotK version offers it; USpt)
    {u": Ativado",
     u": Desativado",
     u" ao terminar a a\u00e7\u00e3o atual",
     u" (n\u00e3o foi poss\u00edvel iniciar)",
     u" (\u00e1reas rec\u00e9m-carregadas)"},
    // EUnl
    {u": Aan",
     u": Uit",
     u" na de huidige actie",
     u" (kon niet starten)",
     u" (nieuw geladen gebieden)"},
    // EUru
    {u": \u0412\u043a\u043b.",
     u": \u0412\u044b\u043a\u043b.",
     u" \u043f\u043e\u0441\u043b\u0435 \u0442\u0435\u043a\u0443\u0449\u0435\u0433\u043e \u0434\u0435\u0439\u0441\u0442\u0432\u0438\u044f",
     u" (\u043d\u0435 \u0443\u0434\u0430\u043b\u043e\u0441\u044c \u0437\u0430\u043f\u0443\u0441\u0442\u0438\u0442\u044c)",
     u" (\u0432\u043d\u043e\u0432\u044c \u0437\u0430\u0433\u0440\u0443\u0436\u0435\u043d\u043d\u044b\u0435 \u043e\u0431\u043b\u0430\u0441\u0442\u0438)"},
    // KRko
    {u": \ucf1c\uc9d0",
     u": \uaebc\uc9d0",
     u" (\ud604\uc7ac \ub3d9\uc791\uc774 \ub05d\ub098\uba74)",
     u" (\uc2dc\uc791\ud560 \uc218 \uc5c6\uc74c)",
     u" (\uc0c8\ub85c \ubd88\ub7ec\uc628 \uc9c0\uc5ed)"},
    // CNzh
    {u"\uff1a\u5f00",
     u"\uff1a\u5173",
     u"\uff08\u5f53\u524d\u52a8\u4f5c\u7ed3\u675f\u540e\uff09",
     u"\uff08\u65e0\u6cd5\u542f\u52a8\uff09",
     u"\uff08\u65b0\u8f7d\u5165\u7684\u533a\u57df\uff09"},
    // TWzh
    {u"\uff1a\u958b",
     u"\uff1a\u95dc",
     u"\uff08\u76ee\u524d\u52d5\u4f5c\u7d50\u675f\u5f8c\uff09",
     u"\uff08\u7121\u6cd5\u555f\u52d5\uff09",
     u"\uff08\u65b0\u8f09\u5165\u7684\u5340\u57df\uff09"},
};
inline const Switches &switches() { return kSwitches[totk::text::gameTextLanguage()]; }

}  // namespace fieldworks::text
