#pragma once
#include "fs.h"
namespace nn { using Result=fs::Result; }
namespace nn::settings { struct LanguageCode { char code[8]; }; }
namespace nn::oe { settings::LanguageCode GetDesiredLanguage(); }
