// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <nvn/nvn.h>
namespace zonai_survey::atlas_gpu {
void draw(std::uintptr_t base,NVNdevice* device,PFNNVNDEVICEGETPROCADDRESSPROC getProc,
          void* drawContext,void* context,const float* view,const float* projection);
}
