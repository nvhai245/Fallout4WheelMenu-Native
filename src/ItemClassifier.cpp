#include "ItemClassifier.h"
#include "ConfigUI.h"
#include <RE/T/TESObjectWEAP.h>
#include <RE/T/TESObjectARMO.h>
#include <RE/A/AlchemyItem.h>
#include <RE/B/BGSKeywordForm.h>
#include <RE/B/BGSKeyword.h>
#include <RE/E/ENUM_FORM_ID.h>

#include <fstream>
#include <string>
#include <REX/REX.h>

namespace ItemClassifier {

    CategoryToggles g_Toggles;

    // ==========================================
    // Load excluded FormIDs once for constant-time lookup during classification.
    // ==========================================
    // 把 RE::FormID 换成 std::uint32_t
    std::unordered_set<std::uint32_t> g_ExcludedFormIDs;

    void LoadExclusions() {
        std::ifstream file("Data\\Interface\\WHEEL_MENU\\exclusions_formids.txt");
        if (!file.is_open()) {
            REX::WARN("ItemClassifier: 未找到黑名单 exclusions_formids.txt，跳过加载。");
            return;
        }
        std::string line;
        while (std::getline(file, line)) {
            // 清理空格和回车
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            // 忽略空行和注释行 (以 ; 开头)
            if (line.empty() || line[0] == ';') continue;
            try {
                // 把 RE::FormID 换成 std::uint32_t
                std::uint32_t formId = static_cast<std::uint32_t>(std::stoul(line, nullptr, 16));
                g_ExcludedFormIDs.insert(formId);
            }
            catch (...) {}
        }
        REX::INFO("ItemClassifier: 成功从 TXT 加载了 {} 个黑名单 FormID!", g_ExcludedFormIDs.size());
    }

    bool IsItemAllowed(RE::TESBoundObject* a_obj) {
        if (!a_obj) return false;

        // Reject explicitly excluded forms before any classification work.
        // a_obj->GetFormID() 返回的正好也是 uint32_t
        if (g_ExcludedFormIDs.contains(a_obj->GetFormID())) return false;

        // 关键字 Tag 黑名单拦截
        auto keywordForm = a_obj->As<RE::BGSKeywordForm>();
        if (keywordForm) {
            for (const auto& blacklistedTag : ConfigUI::g_Settings.tagBlacklist) {
                if (keywordForm->HasKeywordString(blacklistedTag.c_str())) {
                    return false;
                }
            }
        }

        switch (a_obj->GetFormType()) {
        case RE::ENUM_FORM_ID::kWEAP: return g_Toggles.allowWeapons;
        case RE::ENUM_FORM_ID::kARMO: return g_Toggles.allowApparel;
        case RE::ENUM_FORM_ID::kALCH: return g_Toggles.allowAid;
        case RE::ENUM_FORM_ID::kAMMO: return g_Toggles.allowAmmo;
        case RE::ENUM_FORM_ID::kMISC: return g_Toggles.allowMisc;
        case RE::ENUM_FORM_ID::kBOOK: return g_Toggles.allowBooks;
        case RE::ENUM_FORM_ID::kNOTE: return g_Toggles.allowNotes;
        case RE::ENUM_FORM_ID::kKEYM: return g_Toggles.allowKeys;
        case RE::ENUM_FORM_ID::kINGR: return g_Toggles.allowIngredients;
        case RE::ENUM_FORM_ID::kCMPO: return g_Toggles.allowComponents;
        case RE::ENUM_FORM_ID::kOMOD: return g_Toggles.allowMods;
        default: return false;
        }
    }

    const char* GetItemCategory(RE::TESBoundObject* a_obj, RE::TBO_InstanceData* a_instData) {
        if (!a_obj) return Categories::UNKNOWN;

        auto formType = a_obj->GetFormType();

        // Classify weapons using native types and Quickkey behavior keywords.
        if (formType == RE::ENUM_FORM_ID::kWEAP) {
            auto keywordForm = a_obj->As<RE::BGSKeywordForm>();
            bool isMelee = false;
            bool isThrown = false;

            if (keywordForm) {
                // Detect thrown weapons first so Molotov cocktails are not misclassified.
                if (keywordForm->HasKeywordString("WeaponTypeExplosive") ||
                    keywordForm->HasKeywordString("ObjectTypeGrenade") ||
                    keywordForm->HasKeywordString("ObjectTypeMine") ||
                    keywordForm->HasKeywordString("AnimsGrenade") ||
                    keywordForm->HasKeywordString("AnimsMine") ||
                    keywordForm->HasKeywordString("WeaponTypeGrenade") ||
                    keywordForm->HasKeywordString("WeaponTypeThrown")) {
                    isThrown = true;
                }
                // Detect melee and unarmed weapons through native and Quickkey keywords.
                else if (keywordForm->HasKeywordString("QuickkeyMelee") ||
                    keywordForm->HasKeywordString("WeaponTypeMelee1H") ||
                    keywordForm->HasKeywordString("WeaponTypeMelee2H") ||
                    keywordForm->HasKeywordString("WeaponTypeUnarmed")) {
                    isMelee = true;
                }
                // Detect guns and heavy weapons after thrown and melee checks.
                // 这里我们其实不需要显式设置 isRanged = true，因为下面有兜底，
                // 但如果未来你想把重武器(HeavyGun)单独分出一个扇区，就可以在这里加逻辑！
            }

            // 严格按层级返回大类
            if (isThrown) return Categories::WEAPONS_THROWN;
            if (isMelee) return Categories::WEAPONS_MELEE;

            // Unknown weapon records fall back to ranged so incomplete mod keywords
            // do not hide otherwise usable weapons.
            return Categories::WEAPONS_RANGED;
        }

        // 2. 辐射76级服装/护甲物理分离
        if (formType == RE::ENUM_FORM_ID::kARMO) {
            auto armo = a_obj->As<RE::TESObjectARMO>();
            if (armo) {
                // 检查物理抗性：如果有改装InstanceData，优先读它
                auto rating = armo->armorData.rating;
                if (a_instData) {
                    // kARMO instance data uses the armor-specific instance type.
                    rating = static_cast<RE::TESObjectARMO::InstanceData*>(a_instData)->rating;
                }

                // 检查关键字：是否有装甲类或者动力甲的关键字
                bool hasArmorKeyword = false;
                auto keywordForm = a_obj->As<RE::BGSKeywordForm>();
                if (keywordForm) {
                    if (keywordForm->HasKeywordString("ArmorTypeLight") ||
                        keywordForm->HasKeywordString("ArmorTypeHeavy") ||
                        keywordForm->HasKeywordString("ArmorTypeSturdy") ||
                        keywordForm->HasKeywordString("ArmorTypePower")) {
                        hasArmorKeyword = true;
                    }
                }

                // 如果有护甲值或者防具关键字，就是 Armor；否则就是纯净的 Clothing
                if (rating > 0 || hasArmorKeyword) return Categories::ARMOR;
                else return Categories::CLOTHING;
            }
        }

        // 3. 辅助品精确分离 (药、水、食物)
        if (formType == RE::ENUM_FORM_ID::kALCH) {
            auto keywordForm = a_obj->As<RE::BGSKeywordForm>();
            if (keywordForm) {
                if (keywordForm->HasKeywordString("ObjectTypeFood")) return Categories::FOOD;
                if (keywordForm->HasKeywordString("ObjectTypeDrink")) return Categories::DRINK;
                if (keywordForm->HasKeywordString("ObjectTypeChem")) return Categories::CHEMS;
            }
            return Categories::AID; // 兜底：比如治疗针、隐形小子
        }

        // 4. 其他类型秒出结果
        switch (formType) {
        case RE::ENUM_FORM_ID::kAMMO: return Categories::AMMO;
        case RE::ENUM_FORM_ID::kMISC: return Categories::MISC;
        case RE::ENUM_FORM_ID::kBOOK: return Categories::BOOKS;
        case RE::ENUM_FORM_ID::kNOTE: return Categories::NOTES;
        case RE::ENUM_FORM_ID::kKEYM: return Categories::KEYS;
        case RE::ENUM_FORM_ID::kINGR: return Categories::INGREDIENTS;
        case RE::ENUM_FORM_ID::kCMPO: return Categories::COMPONENTS;
        case RE::ENUM_FORM_ID::kOMOD: return Categories::MODS;
        default: break;
        }

        return Categories::UNKNOWN;
    }
}
