#pragma once

#include <cstdint>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TBO_InstanceData.h>
#include <unordered_set> // 新增

namespace ItemClassifier {
    // 物理原生大类常量定义 (严格对应 AS3 里的 category 规则)
    namespace Categories {
        inline const char* WEAPONS_RANGED = "ranged";
        inline const char* WEAPONS_MELEE = "melee";
        inline const char* WEAPONS_THROWN = "thrown";
        inline const char* ARMOR = "armor";           // 辐射76级分离：提供防御的护甲
        inline const char* CLOTHING = "clothing";     // 辐射76级分离：纯外观衣服
        inline const char* FOOD = "food";
        inline const char* DRINK = "drink";
        inline const char* CHEMS = "chems";
        inline const char* AID = "aid";               // 其他辅助品（比如血包、隐形小子）
        inline const char* AMMO = "ammo";
        inline const char* MISC = "misc";
        inline const char* BOOKS = "books";
        inline const char* NOTES = "notes";
        inline const char* KEYS = "keys";
        inline const char* INGREDIENTS = "ingredients";
        inline const char* COMPONENTS = "components"; // 零件垃圾
        inline const char* MODS = "mods";             // 改造配件
        inline const char* UNKNOWN = "unknown";
    }

    // 特性开关 (Feature Toggles)
    // 目前轮盘默认只允许武器和辅助品进入，未来如果有需求，改 true 即可！
    struct CategoryToggles {
        bool allowWeapons = true;
        bool allowApparel = true;   // 我们目前把服装护甲设为 true，方便测试辐射76功能！
        bool allowAid = true;
        bool allowAmmo = true;
        bool allowMisc = true;
        bool allowBooks = true;
        bool allowNotes = true;
        bool allowKeys = true;
        bool allowIngredients = true;
        bool allowComponents = true;
        bool allowMods = true;
    };

    extern CategoryToggles g_Toggles;

    // ==========================================
    // 新增：C++ 级别的全局黑名单缓存
    // ==========================================
    // 把 RE::FormID 换成 std::uint32_t
    extern std::unordered_set<std::uint32_t> g_ExcludedFormIDs;

    void LoadExclusions(); // 在游戏启动时读取 txt 的函数

    bool IsItemAllowed(RE::TESBoundObject* a_obj);
    const char* GetItemCategory(RE::TESBoundObject* a_obj, RE::TBO_InstanceData* a_inst);
}