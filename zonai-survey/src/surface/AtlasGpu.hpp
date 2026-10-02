// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <nvn/nvn.h>
#include "SurveyGameProfiles.hpp"
namespace zonai_survey::atlas_gpu {
// Scene-pass labels, used only where no interface draw is mapped.
void draw(std::uintptr_t base,NVNdevice* device,PFNNVNDEVICEGETPROCADDRESSPROC getProc,
          void* drawContext,void* context,const float* view,const float* projection,
          float sceneWidth,float sceneHeight);
struct UiTarget { void* drawContext; void* target; void* viewport; void* restore; };
void drawUi(std::uintptr_t base,NVNdevice* device,PFNNVNDEVICEGETPROCADDRESSPROC getProc,
            const profiles::UiSite& site,const UiTarget& ui,const float* view,const float* projection);
}
