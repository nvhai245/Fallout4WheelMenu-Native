#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace ConfigUI {
    struct FilterInfo {
        uint32_t id;
        std::string name;
        bool enabled;
    };

    struct Settings {
        uint32_t configMenuHotkey = 0xDC;  // \ (仅保留设置菜单的呼出键)
        uint32_t wheelHotkey = 0x47;       // G
        int activationMode = 0;            // 0=replace Favorites, 1=independent hotkey

        int timeMode = 1;
        float slowTimeMult = 0.25f;
        bool bulletTimeCombatOnly = false;
        float globalFilterStrength = 0.4f;

        bool autoRenewFilters = true;
        float renewInterval = 1.5f;
        float menuOffsetX = 0.0f;
        float menuOffsetY = 0.0f;
        float menuScaling = 1.0f;

        // 新增：涟漪联动布局的 4 大控制参数
        float coreRadius = 70.0f;
        float favSlotSize = 42.0f;
        float layerGap = 8.0f;
        float tickLength = 14.0f;
        float categoryRingThickness = 40.0f;
        bool showFavoriteRing = true;
        float leftListOffsetX = 0.0f;
        float leftListOffsetY = 0.0f;
        float leftListScale = 1.0f;
        float rightListOffsetX = 0.0f;
        float rightListOffsetY = 0.0f;
        float rightListScale = 1.0f;
        bool enableGamepad = true;
        int gamepadCenterStick = 1; // 0=left, 1=right (控制收藏环的摇杆)
        int dpadUpAction = 1;       // 0=None, 1=OpenWheelMenu
        int dpadDownAction = 1;
        int dpadLeftAction = 1;
        int dpadRightAction = 1;
        bool showSectorsDivider = true; // 默认开启分割线
        bool hideEmptyCategories = true; // 新增：自动隐藏无物品的分类
        bool favoriteCloseOnUse = true;
        int favoriteSlotOrder = 0; // 0: clockwise, 1: vanilla cross
        bool submenuOpenOnHover = false;
        bool showCloseBadges = true;
        bool showHintBar = true;
        bool showCenterName = true;
        bool showConditionBars = true;
        float centerNameFontSize = 24.0f;
        
        bool toggleMode = true; // true: 点击开启，false: 按住开启
        std::string uiLanguage = "zh_CN";
        std::string customFontFile = "";

        // ==========================================
        // 新增：持久化动态缓存变量 (专门给 Cache 使用)
        // ==========================================
        std::string pinnedLeft = "";
        std::string pinnedRight = "";
        std::string pinnedLeftFilter = "";
        std::string pinnedRightFilter = "";

        std::vector<std::string> tagBlacklist;

        std::vector<FilterInfo> filters;

        Settings() {
            filters = {
                { 0x0003F1FF, "Filter.VATSPlayback", true },
                { 0x0001ED7C, "Filter.Whiteout", false },
                { 0x00218E64, "Filter.BulletTimeFadeIn", false },
                { 0x00218E65, "Filter.BulletTimeFadeOut", false },
                { 0x000E61E4, "Filter.VATSDesat", false },
                { 0x000FBF9A, "Filter.VATSDesatDOF", false },
                // 👇 补回丢失的 5 个滤镜！
                { 0x00049B5E, "Filter.VATSBaseDOF", false },
                { 0x001CB0C2, "Filter.DoFTest01", false },
                { 0x0012BC08, "Filter.PipboyMenu", false },
                { 0x000711B2, "Filter.VANS", false },
                { 0x0001ED7D, "Filter.WhiteoutToNormal", false }
            };
        }
    };

    extern Settings g_Settings;
    extern bool g_ShowConfig;
    extern bool g_Initialized;

    // ==========================================
    // 新增：追踪 ImGui 调试面板的当前状态
    // ==========================================
    extern bool g_isLayoutTabActive; // 记录当前是否处于“外观排版”Tab
    extern bool g_openedByConfig;    // 记录轮盘是不是被 ImGui 强行打开的

    extern bool g_isRecordingHotkey;
    extern bool g_isRecordingWheelHotkey;

    bool Install();
    void Load();
    void Save();
}
