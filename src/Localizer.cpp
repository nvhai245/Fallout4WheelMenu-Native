#include "pch.h"
#include "Localizer.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unordered_map>

namespace WheelMenuNative::L10n
{
    namespace
    {
        constexpr const char* kLangDir = "Data\\F4SE\\Plugins\\Fallout4WheelMenu\\lang\\";
        std::unordered_map<std::string, std::string> g_dictionary;
        std::string g_currentLanguage = "zh_CN";
    }

    void LoadLanguage(const std::string& langCode)
    {
        g_currentLanguage = langCode.empty() ? "zh_CN" : langCode;
        g_dictionary.clear();

        const std::string path = std::string(kLangDir) + g_currentLanguage + ".json";
        if (!std::filesystem::exists(path)) {
            REX::WARN("[Fallout4WheelMenu] Missing language file: {}. Translation keys will be shown.", path);
            return;
        }

        try {
            std::ifstream file(path);
            nlohmann::json j = nlohmann::json::parse(file, nullptr, true, true);
            for (auto& [key, value] : j.items()) {
                if (value.is_string()) {
                    g_dictionary[key] = value.get<std::string>();
                }
            }
            REX::INFO("[Fallout4WheelMenu] Loaded language file: {}", g_currentLanguage);
        }
        catch (const std::exception& e) {
            REX::ERROR("[Fallout4WheelMenu] Failed to parse language file {}: {}", path, e.what());
        }
    }

    const char* Get(const char* key)
    {
        auto it = g_dictionary.find(key);
        return it != g_dictionary.end() ? it->second.c_str() : key;
    }

    std::string GetCurrentLanguage()
    {
        return g_currentLanguage;
    }

    std::vector<std::string> GetAvailableLanguages()
    {
        std::vector<std::string> languages;
        if (std::filesystem::exists(kLangDir)) {
            for (const auto& entry : std::filesystem::directory_iterator(kLangDir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    languages.push_back(entry.path().stem().string());
                }
            }
        }

        if (languages.empty()) {
            languages.push_back("zh_CN");
            languages.push_back("en_US");
        }

        return languages;
    }
}
