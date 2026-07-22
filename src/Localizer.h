#pragma once
#include "pch.h"

namespace WheelMenuNative::L10n
{
    void LoadLanguage(const std::string& langCode = "zh_CN");
    const char* Get(const char* key);
    std::string GetCurrentLanguage();
    std::vector<std::string> GetAvailableLanguages();
}

#define _T(key) WheelMenuNative::L10n::Get(key)
