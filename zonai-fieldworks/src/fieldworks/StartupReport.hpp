// Copyright (c) HanSoloBlaster, afiaka87
// Switch build only: the module's first log lines, written to the SD card (a console shows no debug output).
#pragma once

namespace fieldworks::report {

inline constexpr char kFile[] = "sdcard:/ZonaiFieldworks/startup-log.txt";

#if FIELDWORKS_SD_REPORT
// Called on every controller update. Writes the kept text when there is more of it.
void tick(unsigned now);
#else
inline void tick(unsigned) {}
#endif

}  // namespace fieldworks::report
