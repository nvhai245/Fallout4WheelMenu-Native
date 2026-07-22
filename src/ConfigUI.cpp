#include "pch.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <dxgi.h>
#include <Windows.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <detours.h>
#include <RE/B/BSGraphics.h>
#include <RE/C/ControlMap.h>
#include <RE/M/MenuCursor.h>
#include "ConfigUI.h"
#include "Localizer.h"
#include "WheelMenu.h"
#include <SimpleIni.h>
#include <tinyxml2.h>
#include <filesystem>
#include <sstream>

#define _U(str) reinterpret_cast<const char*>(u8##str)

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace ConfigUI {
    Settings g_Settings;
    bool g_ShowConfig = false;
    bool g_Initialized = false;
    bool g_isLayoutTabActive = false;
    bool g_openedByConfig = false;
    bool g_isRecordingHotkey = false;
    bool g_isRecordingWheelHotkey = false;

    using Present_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
    Present_t OriginalPresent = nullptr;
    WNDPROC OriginalWndProc = nullptr;

    // 静态配置与动态缓存分离
    const char* configPath = "Data\\F4SE\\Plugins\\Fallout4WheelMenu.ini";
    const char* cachePath = "Data\\F4SE\\Plugins\\Fallout4WheelMenu_Cache.ini";
    const char* fontFolder = "Data\\F4SE\\Plugins\\Fallout4WheelMenu\\Fonts";

    static ImVector<ImWchar> s_imguiFontRanges;
    static bool g_needsFontReload = false;

    const char* GetFilterDisplayName(const FilterInfo& filter)
    {
        switch (filter.id) {
        case 0x0003F1FF: return _T("Filter.VATSPlayback");
        case 0x0001ED7C: return _T("Filter.Whiteout");
        case 0x00218E64: return _T("Filter.BulletTimeFadeIn");
        case 0x00218E65: return _T("Filter.BulletTimeFadeOut");
        case 0x000E61E4: return _T("Filter.VATSDesat");
        case 0x000FBF9A: return _T("Filter.VATSDesatDOF");
        case 0x00049B5E: return _T("Filter.VATSBaseDOF");
        case 0x001CB0C2: return _T("Filter.DoFTest01");
        case 0x0012BC08: return _T("Filter.PipboyMenu");
        case 0x000711B2: return _T("Filter.VANS");
        case 0x0001ED7D: return _T("Filter.WhiteoutToNormal");
        default: return filter.name.c_str();
        }
    }

    int FindOptionIndex(const char* value, const char* const* options, int optionCount)
    {
        for (int i = 0; i < optionCount; i++) {
            if (value && _stricmp(value, options[i]) == 0) {
                return i;
            }
        }

        return 0;
    }

    bool DrawAttributeCombo(const char* label, tinyxml2::XMLElement* element, const char* attrName, const char* const* values, const char* const* labels, int optionCount)
    {
        int current = FindOptionIndex(element->Attribute(attrName), values, optionCount);
        if (ImGui::Combo(label, &current, labels, optionCount)) {
            element->SetAttribute(attrName, values[current]);
            return true;
        }

        return false;
    }

    const char* StableWindowLabel(const char* text, const char* id)
    {
        static std::string label;
        label = std::string(text) + "###" + id;
        return label.c_str();
    }

    std::string GetDefaultImGuiFontPath()
    {
        // 1. Try bundled Noto Sans SC font first
        std::string bundled = std::string(fontFolder) + "\\NotoSansSC-Regular.ttf";
        if (std::filesystem::exists(bundled)) {
            return bundled;
        }

        // 2. Try any font from the Fonts/ folder
        WIN32_FIND_DATAA ffd;
        for (const char* ext : { "\\*.ttf", "\\*.ttc", "\\*.otf" }) {
            std::string pattern = std::string(fontFolder) + ext;
            HANDLE hFind = FindFirstFileA(pattern.c_str(), &ffd);
            if (hFind != INVALID_HANDLE_VALUE) {
                std::string found = std::string(fontFolder) + "\\" + ffd.cFileName;
                FindClose(hFind);
                return found;
            }
        }

        // 3. Fall back to system Microsoft YaHei font
        char fontPath[MAX_PATH]{};
        GetWindowsDirectoryA(fontPath, MAX_PATH);
        strcat_s(fontPath, "\\Fonts\\msyh.ttc");
        return fontPath;
    }

    bool IsAbsolutePath(const std::string& path)
    {
        return path.size() > 2 && path[1] == ':';
    }

    std::string ResolveImGuiFontPath()
    {
        CreateDirectoryA("Data\\F4SE\\Plugins\\Fallout4WheelMenu", nullptr);
        CreateDirectoryA(fontFolder, nullptr);

        std::string configured = g_Settings.customFontFile;
        if (!configured.empty()) {
            std::filesystem::path customPath(configured);
            if (!IsAbsolutePath(configured)) {
                customPath = std::filesystem::path(fontFolder) / configured;
            }
            if (std::filesystem::exists(customPath)) {
                return customPath.string();
            }
            if (std::filesystem::exists(configured)) {
                return configured;
            }
        }

        return GetDefaultImGuiFontPath();
    }

    const ImWchar* BuildImGuiFontRanges(ImGuiIO& io)
    {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
        builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
        builder.AddRanges(io.Fonts->GetGlyphRangesKorean());
        builder.BuildRanges(&s_imguiFontRanges);
        return s_imguiFontRanges.Data;
    }

    std::vector<std::string> ScanFontFiles()
    {
        std::vector<std::string> fonts;
        WIN32_FIND_DATAA ffd;

        for (const char* ext : { "\\*.ttf", "\\*.ttc", "\\*.otf" }) {
            std::string pattern = std::string(fontFolder) + ext;
            HANDLE hFind = FindFirstFileA(pattern.c_str(), &ffd);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        fonts.push_back(ffd.cFileName);
                    }
                } while (FindNextFileA(hFind, &ffd) != 0);
                FindClose(hFind);
            }
        }

        return fonts;
    }

    void ApplyFontReload()
    {
        if (!g_Initialized) return;

        ImGuiIO& io = ImGui::GetIO();

        // Clear existing font atlas and all loaded fonts
        io.Fonts->Clear();

        // Load the new font from current settings
        std::string fontPath = ResolveImGuiFontPath();
        ImFontConfig font_cfg;
        font_cfg.FontNo = 0;
        if (auto* font = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 18.0f, &font_cfg, BuildImGuiFontRanges(io))) {
            io.FontDefault = font;
        } else {
            // Safety fallback: use ImGui's built-in default font
            io.Fonts->AddFontDefault();
        }

        // Rebuild the font atlas
        io.Fonts->Build();

        // Destroy old GPU font texture; NewFrame() will recreate from the new atlas
        ImGui_ImplDX11_InvalidateDeviceObjects();
    }

    bool IsInitialKeyDown(LPARAM lParam)
    {
        return (lParam & (1LL << 30)) == 0;
    }

    bool IsConfigHotkey(WPARAM wParam, LPARAM lParam)
    {
        if (static_cast<uint32_t>(wParam) == g_Settings.configMenuHotkey) {
            return true;
        }

        const auto scanCode = static_cast<UINT>((lParam >> 16) & 0xFF);
        const auto mappedVk = MapVirtualKeyA(scanCode, MAPVK_VSC_TO_VK_EX);
        return mappedVk == g_Settings.configMenuHotkey;
    }

    std::string GetVirtualKeyName(uint32_t virtualKey)
    {
        UINT scanCode = MapVirtualKeyA(virtualKey, MAPVK_VK_TO_VSC);
        if (virtualKey == VK_LEFT || virtualKey == VK_UP || virtualKey == VK_RIGHT || virtualKey == VK_DOWN ||
            virtualKey == VK_PRIOR || virtualKey == VK_NEXT || virtualKey == VK_END || virtualKey == VK_HOME ||
            virtualKey == VK_INSERT || virtualKey == VK_DELETE || virtualKey == VK_DIVIDE || virtualKey == VK_NUMLOCK) {
            scanCode |= KF_EXTENDED;
        }

        char keyName[64]{};
        if (GetKeyNameTextA(static_cast<LONG>(scanCode << 16), keyName, sizeof(keyName)) > 0) {
            return keyName;
        }
        return std::to_string(virtualKey);
    }

    // ==========================================
    // Visual editor for the wheel XML configuration with live reload.
    // ==========================================
    void DrawCategoryEditor() {
        static tinyxml2::XMLDocument doc;
        static bool s_xmlLoaded = false;
        const char* xmlPath = "Data\\Interface\\WHEEL_MENU\\conf.xml";

        if (!s_xmlLoaded) {
            if (doc.LoadFile(xmlPath) == tinyxml2::XML_SUCCESS) {
                s_xmlLoaded = true;
            }
            else {
                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), _T("Xml.ErrorLoad"));
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), _T("Xml.ErrorLoadHint"));
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), _U("Data\\Interface\\WHEEL_MENU\\conf.xml"));
                if (ImGui::Button(_T("Xml.RetryLoad"))) s_xmlLoaded = false;
                return;
            }
        }

        // 顶部全局操作区
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
        if (ImGui::Button(_T("Xml.SaveHotReload"), ImVec2(-FLT_MIN, 40))) {
            doc.SaveFile(xmlPath);
            WheelMenuNative::WheelMenu::ForceReloadAS3Config(); // 瞬间重载 AS3
        }
        ImGui::PopStyleColor(2);

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        tinyxml2::XMLElement* root = doc.FirstChildElement("wheelmenu");
        if (!root) {
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), _T("Xml.ErrorMissingRoot"));
            return;
        }

        // ==========================================
        // 预定义 C++ 底层物理分类词典
        // ==========================================
        const char* mainCats[] = { "weapons", "apparel", "aid", "ammo", "misc", "notes" };
        const char* mainCatLabels[] = { _T("Xml.MainCategory.Weapons"), _T("Xml.MainCategory.Apparel"), _T("Xml.MainCategory.Aid"), _T("Xml.MainCategory.Ammo"), _T("Xml.MainCategory.Misc"), _T("Xml.MainCategory.Notes") };
        const char* subCats[] = { "ranged", "melee", "thrown", "armor", "clothing", "food", "drink", "chems", "aid", "ammo", "misc", "books", "notes", "keys", "ingredients", "components", "mods" };
        const char* subCatLabels[] = { _T("Xml.SubCategory.Ranged"), _T("Xml.SubCategory.Melee"), _T("Xml.SubCategory.Thrown"), _T("Xml.SubCategory.Armor"), _T("Xml.SubCategory.Clothing"), _T("Xml.SubCategory.Food"), _T("Xml.SubCategory.Drink"), _T("Xml.SubCategory.Chems"), _T("Xml.SubCategory.Aid"), _T("Xml.SubCategory.Ammo"), _T("Xml.SubCategory.Misc"), _T("Xml.SubCategory.Books"), _T("Xml.SubCategory.Notes"), _T("Xml.SubCategory.Keys"), _T("Xml.SubCategory.Ingredients"), _T("Xml.SubCategory.Components"), _T("Xml.SubCategory.Mods") };
        const char* kindTypes[] = { "tag", "category", "maincategory", "regex", "subcategory" };
        const char* kindTypeLabels[] = { _T("Xml.RuleKind.Tag"), _T("Xml.RuleKind.Category"), _T("Xml.RuleKind.MainCategory"), _T("Xml.RuleKind.Regex"), _T("Xml.RuleKind.SubCategory") };
        const char* sideValues[] = { "left", "right" };
        const char* sideLabels[] = { _T("Xml.Side.Left"), _T("Xml.Side.Right") };
        const char* closePolicyLabels[] = { _T("Xml.CloseInherit"), _T("Xml.CloseAfterUse"), _T("Xml.KeepOpenAfterUse") };
        const char* pausePolicyLabels[] = { _T("Xml.PauseInherit"), _T("Xml.DynamicPauseOn"), _T("Xml.DynamicPauseOff") };

        tinyxml2::XMLElement* menuToDelete = nullptr; // 用于记录需要删除的大类
        int menuIdx = 0;

        // 动态遍历并生成所有大类
        for (tinyxml2::XMLElement* menu = root->FirstChildElement("menu"); menu; menu = menu->NextSiblingElement("menu")) {
            ImGui::PushID(menuIdx++);

            const char* mName = menu->Attribute("name");
            if (mName && strcmp(mName, "_m8rFixLoadAll") == 0) { ImGui::PopID(); continue; } // 过滤内置补丁

            char headerLabel[256];
            sprintf_s(headerLabel, "%s %s###menu_%d", _T("Xml.CategoryPrefix"), mName ? mName : _T("Xml.Unnamed"), menuIdx);

            if (ImGui::CollapsingHeader(headerLabel)) {
                ImGui::Indent();

                // --- 1. 基础属性 ---
                char nameBuf[128]; strcpy_s(nameBuf, mName ? mName : "");
                if (ImGui::InputText(_T("Xml.CategoryName"), nameBuf, sizeof(nameBuf))) menu->SetAttribute("name", nameBuf);

                const char* mIcon = menu->Attribute("icon");
                char iconBuf[128]; strcpy_s(iconBuf, mIcon ? mIcon : "");
                if (ImGui::InputText(_T("Xml.CenterIcon"), iconBuf, sizeof(iconBuf))) menu->SetAttribute("icon", iconBuf);

                int currentType = (menu->Attribute("type") && strcmp(menu->Attribute("type"), "right") == 0) ? 1 : 0;
                ImGui::SetNextItemWidth(120);
                if (ImGui::Combo(_T("Xml.ScreenSide"), &currentType, sideLabels, 2)) menu->SetAttribute("type", sideValues[currentType]);

                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                if (ImGui::Button(_T("Xml.DeleteCategory"))) { menuToDelete = menu; }
                ImGui::PopStyleColor();

                bool closeOnUse = !(menu->Attribute("closeOnUse") && _stricmp(menu->Attribute("closeOnUse"), "false") == 0);
                if (ImGui::Checkbox(_T("Xml.CloseAfterUse"), &closeOnUse)) {
                    menu->SetAttribute("closeOnUse", closeOnUse ? "true" : "false");
                }

                bool pauseOnHover = menu->Attribute("pauseOnHover") && _stricmp(menu->Attribute("pauseOnHover"), "true") == 0;
                if (ImGui::Checkbox(_T("Xml.DynamicPauseOnHover"), &pauseOnHover)) {
                    if (pauseOnHover) {
                        menu->SetAttribute("pauseOnHover", "true");
                    } else {
                        menu->DeleteAttribute("pauseOnHover");
                    }
                }

                ImGui::Spacing();

                // --- 2. 匹配规则 (Patterns) ---
                if (ImGui::TreeNode(_T("Xml.ItemMatchRules"))) {
                    tinyxml2::XMLElement* patToDelete = nullptr;
                    int patIdx = 0;
                    for (tinyxml2::XMLElement* pat = menu->FirstChildElement("pattern"); pat; pat = pat->NextSiblingElement("pattern")) {
                        ImGui::PushID(patIdx++);

                        const char* kind = pat->Attribute("kind");
                        const char* val = pat->Attribute("value");

                        int currentKind = FindOptionIndex(kind, kindTypes, IM_ARRAYSIZE(kindTypes));

                        ImGui::SetNextItemWidth(100);
                        if (ImGui::Combo("##kind", &currentKind, kindTypeLabels, IM_ARRAYSIZE(kindTypeLabels))) {
                            pat->SetAttribute("kind", kindTypes[currentKind]);
                        }

                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(180);

                        // 核心：智能判断并渲染“下拉列表”还是“手动输入”
                        if (currentKind == 2) { // maincategory 选项
                            DrawAttributeCombo("##val", pat, "value", mainCats, mainCatLabels, IM_ARRAYSIZE(mainCats));
                        }
                        else if (currentKind == 1 || currentKind == 4) { // category / subcategory 选项
                            DrawAttributeCombo("##val", pat, "value", subCats, subCatLabels, IM_ARRAYSIZE(subCats));
                        }
                        else { // tag 或 regex (保持手写输入框)
                            char valBuf[128]; strcpy_s(valBuf, val ? val : "");
                            if (ImGui::InputTextWithHint("##val", _T("Xml.EnterRule"), valBuf, sizeof(valBuf))) pat->SetAttribute("value", valBuf);
                        }

                        ImGui::SameLine();
                        if (ImGui::Button("[X]")) patToDelete = pat; // 记录待删除的规则
                        ImGui::PopID();
                    }
                    if (patToDelete) menu->DeleteChild(patToDelete); // 执行删除

                    if (ImGui::Button(_T("Xml.AddRule"))) {
                        tinyxml2::XMLElement* newPat = doc.NewElement("pattern");
                        newPat->SetAttribute("kind", "maincategory");
                        newPat->SetAttribute("value", "weapons");
                        menu->InsertEndChild(newPat);
                    }
                    ImGui::TreePop();
                }

                // --- 3. 侧边副分类 (Submenus) ---
                if (ImGui::TreeNode(_T("Xml.SideSubmenus"))) {
                    tinyxml2::XMLElement* subToDelete = nullptr;
                    int subIdx = 0;
                    for (tinyxml2::XMLElement* sub = menu->FirstChildElement("submenu"); sub; sub = sub->NextSiblingElement("submenu")) {
                        ImGui::PushID(subIdx++);

                        // 副类基础信息
                        char sNameBuf[64]; strcpy_s(sNameBuf, sub->Attribute("name") ? sub->Attribute("name") : "");
                        ImGui::SetNextItemWidth(100);
                        if (ImGui::InputTextWithHint("##sName", _T("Xml.Name"), sNameBuf, sizeof(sNameBuf))) sub->SetAttribute("name", sNameBuf);

                        ImGui::SameLine();
                        char sIconBuf[64]; strcpy_s(sIconBuf, sub->Attribute("icon") ? sub->Attribute("icon") : "");
                        ImGui::SetNextItemWidth(100);
                        if (ImGui::InputTextWithHint("##sIcon", _T("Xml.Icon"), sIconBuf, sizeof(sIconBuf))) sub->SetAttribute("icon", sIconBuf);

                        ImGui::SameLine();
                        if (ImGui::Button(_T("Xml.DeleteSubmenu"))) subToDelete = sub;

                        const char* subCloseAttr = sub->Attribute("closeOnUse");
                        int subClosePolicy = 0;
                        if (subCloseAttr) {
                            subClosePolicy = (_stricmp(subCloseAttr, "false") == 0) ? 2 : 1;
                        }
                        ImGui::SetNextItemWidth(180);
                        if (ImGui::Combo(_T("Xml.ClosePolicy"), &subClosePolicy, closePolicyLabels, IM_ARRAYSIZE(closePolicyLabels))) {
                            if (subClosePolicy == 0) {
                                sub->DeleteAttribute("closeOnUse");
                            } else {
                                sub->SetAttribute("closeOnUse", subClosePolicy == 1 ? "true" : "false");
                            }
                        }

                        const char* subPauseAttr = sub->Attribute("pauseOnHover");
                        int subPausePolicy = 0;
                        if (subPauseAttr) {
                            subPausePolicy = (_stricmp(subPauseAttr, "true") == 0) ? 1 : 2;
                        }
                        ImGui::SetNextItemWidth(180);
                        if (ImGui::Combo(_T("Xml.DynamicPausePolicy"), &subPausePolicy, pausePolicyLabels, IM_ARRAYSIZE(pausePolicyLabels))) {
                            if (subPausePolicy == 0) {
                                sub->DeleteAttribute("pauseOnHover");
                            } else {
                                sub->SetAttribute("pauseOnHover", subPausePolicy == 1 ? "true" : "false");
                            }
                        }

                        // 副类匹配规则 (同样支持智能下拉)
                        ImGui::Indent();
                        tinyxml2::XMLElement* sPatToDelete = nullptr;
                        int sPatIdx = 0;
                        for (tinyxml2::XMLElement* spat = sub->FirstChildElement("pattern"); spat; spat = spat->NextSiblingElement("pattern")) {
                            ImGui::PushID(sPatIdx++);

                            const char* sKind = spat->Attribute("kind");
                            const char* sVal = spat->Attribute("value");

                            int currentKind = FindOptionIndex(sKind, kindTypes, IM_ARRAYSIZE(kindTypes));

                            ImGui::SetNextItemWidth(100);
                            if (ImGui::Combo("##skind", &currentKind, kindTypeLabels, IM_ARRAYSIZE(kindTypeLabels))) spat->SetAttribute("kind", kindTypes[currentKind]);

                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(180);

                            // 子类智能下拉
                            if (currentKind == 2) {
                                DrawAttributeCombo("##sval", spat, "value", mainCats, mainCatLabels, IM_ARRAYSIZE(mainCats));
                            }
                            else if (currentKind == 1 || currentKind == 4) {
                                DrawAttributeCombo("##sval", spat, "value", subCats, subCatLabels, IM_ARRAYSIZE(subCats));
                            }
                            else {
                                char valBuf[128]; strcpy_s(valBuf, sVal ? sVal : "");
                                if (ImGui::InputTextWithHint("##sval", _T("Xml.EnterRule"), valBuf, sizeof(valBuf))) spat->SetAttribute("value", valBuf);
                            }

                            ImGui::SameLine();
                            if (ImGui::Button("-")) sPatToDelete = spat;
                            ImGui::PopID();
                        }
                        if (sPatToDelete) sub->DeleteChild(sPatToDelete);

                        if (ImGui::Button(_T("Xml.AddShortRule"))) {
                            tinyxml2::XMLElement* newPat = doc.NewElement("pattern");
                            newPat->SetAttribute("kind", "tag");
                            newPat->SetAttribute("value", "MyTag");
                            sub->InsertEndChild(newPat);
                        }
                        ImGui::Unindent();
                        ImGui::Separator();

                        ImGui::PopID();
                    }
                    if (subToDelete) menu->DeleteChild(subToDelete);

                    if (ImGui::Button(_T("Xml.AddSubmenu"))) {
                        tinyxml2::XMLElement* newSub = doc.NewElement("submenu");
                        newSub->SetAttribute("name", "NewSubmenu");
                        newSub->SetAttribute("icon", "Misc");
                        menu->InsertEndChild(newSub);
                    }

                    ImGui::TreePop();
                }
                ImGui::Unindent();
            }
            ImGui::PopID();
        }

        if (menuToDelete) root->DeleteChild(menuToDelete); // 执行大类删除

        ImGui::Spacing();
        // 添加大类按钮
        if (ImGui::Button(_T("Xml.NewWheelCategory"), ImVec2(-FLT_MIN, 30))) {
            tinyxml2::XMLElement* newMenu = doc.NewElement("menu");
            newMenu->SetAttribute("name", "NewCategory");
            newMenu->SetAttribute("icon", "Misc");
            newMenu->SetAttribute("type", "right");
            newMenu->SetAttribute("closeOnUse", "true");

            // 附赠一个初始规则
            tinyxml2::XMLElement* newPat = doc.NewElement("pattern");
            newPat->SetAttribute("kind", "maincategory");
            newPat->SetAttribute("value", "misc");
            newMenu->InsertEndChild(newPat);

            root->InsertEndChild(newMenu);
        }
    }

    void Save() {
        // Persist static settings to the primary INI.
        CSimpleIniA ini;
        ini.SetUnicode();
        ini.LoadFile(configPath); // 加载已有文件以保留玩家的格式/注释

        ini.SetLongValue("Main", "ConfigHotkey", g_Settings.configMenuHotkey);
        ini.SetLongValue("Main", "WheelHotkey", g_Settings.wheelHotkey);
        ini.SetLongValue("Main", "ActivationMode", g_Settings.activationMode);
        ini.SetValue("Main", "UILanguage", g_Settings.uiLanguage.c_str());
        ini.SetBoolValue("Main", "ToggleMode", g_Settings.toggleMode);
        ini.SetLongValue("Main", "TimeMode", g_Settings.timeMode);
        ini.SetDoubleValue("Main", "SlowMult", g_Settings.slowTimeMult);
        ini.SetBoolValue("Main", "BulletTimeCombatOnly", g_Settings.bulletTimeCombatOnly);
        ini.SetDoubleValue("Main", "Strength", g_Settings.globalFilterStrength);
        ini.SetBoolValue("Main", "AutoRenew", g_Settings.autoRenewFilters);
        ini.SetDoubleValue("Main", "RenewInterval", g_Settings.renewInterval);
        ini.SetDoubleValue("Main", "OffsetX", g_Settings.menuOffsetX);
        ini.SetDoubleValue("Main", "OffsetY", g_Settings.menuOffsetY);
        ini.SetDoubleValue("Main", "Scaling", g_Settings.menuScaling);

        // 联动参数
        ini.SetDoubleValue("Main", "CoreRadius", g_Settings.coreRadius);
        ini.SetDoubleValue("Main", "FavSlotSize", g_Settings.favSlotSize);
        ini.SetDoubleValue("Main", "LayerGap", g_Settings.layerGap);
        ini.SetDoubleValue("Main", "TickLength", g_Settings.tickLength);
        ini.SetDoubleValue("Main", "CategoryRingThickness", g_Settings.categoryRingThickness);
        ini.SetBoolValue("Main", "ShowFavoriteRing", g_Settings.showFavoriteRing);
        ini.SetDoubleValue("Main", "LeftListOffsetX", g_Settings.leftListOffsetX);
        ini.SetDoubleValue("Main", "LeftListOffsetY", g_Settings.leftListOffsetY);
        ini.SetDoubleValue("Main", "LeftListScale", g_Settings.leftListScale);
        ini.SetDoubleValue("Main", "RightListOffsetX", g_Settings.rightListOffsetX);
        ini.SetDoubleValue("Main", "RightListOffsetY", g_Settings.rightListOffsetY);
        ini.SetDoubleValue("Main", "RightListScale", g_Settings.rightListScale);
        ini.SetBoolValue("Main", "ShowSectorsDivider", g_Settings.showSectorsDivider);
        ini.SetBoolValue("Main", "HideEmptyCategories", g_Settings.hideEmptyCategories); // 新增保存
        ini.SetBoolValue("Main", "Gamepad", g_Settings.enableGamepad);
        ini.SetLongValue("Main", "GamepadCenterStick", g_Settings.gamepadCenterStick);
        ini.SetLongValue("Main", "DpadUpAction", g_Settings.dpadUpAction);
        ini.SetLongValue("Main", "DpadDownAction", g_Settings.dpadDownAction);
        ini.SetLongValue("Main", "DpadLeftAction", g_Settings.dpadLeftAction);
        ini.SetLongValue("Main", "DpadRightAction", g_Settings.dpadRightAction);
        ini.SetBoolValue("Main", "FavoriteCloseOnUse", g_Settings.favoriteCloseOnUse);
        ini.SetLongValue("Main", "FavoriteSlotOrder", g_Settings.favoriteSlotOrder);
        ini.SetBoolValue("Main", "SubmenuOpenOnHover", g_Settings.submenuOpenOnHover);
        ini.SetBoolValue("Main", "ShowCloseBadges", g_Settings.showCloseBadges);
        ini.SetBoolValue("Main", "ShowHintBar", g_Settings.showHintBar);
        ini.SetBoolValue("Main", "ShowCenterName", g_Settings.showCenterName);
        ini.SetBoolValue("Main", "ShowConditionBars", g_Settings.showConditionBars);
        ini.SetDoubleValue("Main", "CenterNameFontSize", g_Settings.centerNameFontSize);
        ini.SetValue("Main", "CustomFontFile", g_Settings.customFontFile.c_str());

        // Serialize tag blacklist
        std::string tagBlacklistStr;
        for (size_t i = 0; i < g_Settings.tagBlacklist.size(); ++i) {
            if (i > 0) tagBlacklistStr += ";";
            tagBlacklistStr += g_Settings.tagBlacklist[i];
        }
        ini.SetValue("Main", "TagBlacklist", tagBlacklistStr.c_str());

        for (size_t i = 0; i < g_Settings.filters.size(); ++i) {
            std::string key = "Filter_" + std::to_string(i);
            ini.SetBoolValue("Filters", key.c_str(), g_Settings.filters[i].enabled);
        }

        ini.SaveFile(configPath);
    }

    void Load() {
        // 1. 读取主配置
        CSimpleIniA ini;
        ini.SetUnicode();
        ini.LoadFile(configPath);

        g_Settings.configMenuHotkey = ini.GetLongValue("Main", "ConfigHotkey", 0xDC);
        g_Settings.wheelHotkey = ini.GetLongValue("Main", "WheelHotkey", 0x47);
        g_Settings.activationMode = static_cast<int>(ini.GetLongValue("Main", "ActivationMode", 0));
        if (g_Settings.activationMode < 0 || g_Settings.activationMode > 1) {
            g_Settings.activationMode = 0;
        }
        g_Settings.uiLanguage = ini.GetValue("Main", "UILanguage", "zh_CN");
        WheelMenuNative::L10n::LoadLanguage(g_Settings.uiLanguage);
        g_Settings.toggleMode = ini.GetBoolValue("Main", "ToggleMode", true);
        g_Settings.timeMode = ini.GetLongValue("Main", "TimeMode", 1);
        g_Settings.slowTimeMult = (float)ini.GetDoubleValue("Main", "SlowMult", 0.25);
        g_Settings.bulletTimeCombatOnly = ini.GetBoolValue("Main", "BulletTimeCombatOnly", false);
        g_Settings.globalFilterStrength = (float)ini.GetDoubleValue("Main", "Strength", 0.4);
        g_Settings.autoRenewFilters = ini.GetBoolValue("Main", "AutoRenew", true);
        g_Settings.renewInterval = (float)ini.GetDoubleValue("Main", "RenewInterval", 1.5);
        g_Settings.menuOffsetX = (float)ini.GetDoubleValue("Main", "OffsetX", 0.0);
        g_Settings.menuOffsetY = (float)ini.GetDoubleValue("Main", "OffsetY", 0.0);
        g_Settings.menuScaling = (float)ini.GetDoubleValue("Main", "Scaling", 1.0);

        g_Settings.coreRadius = (float)ini.GetDoubleValue("Main", "CoreRadius", 70.0);
        g_Settings.favSlotSize = (float)ini.GetDoubleValue("Main", "FavSlotSize", 42.0);
        g_Settings.layerGap = (float)ini.GetDoubleValue("Main", "LayerGap", 8.0);
        g_Settings.tickLength = (float)ini.GetDoubleValue("Main", "TickLength", 14.0);
        g_Settings.categoryRingThickness = (float)ini.GetDoubleValue("Main", "CategoryRingThickness", 40.0);
        g_Settings.showFavoriteRing = ini.GetBoolValue("Main", "ShowFavoriteRing", true);
        g_Settings.leftListOffsetX = (float)ini.GetDoubleValue("Main", "LeftListOffsetX", 0.0);
        g_Settings.leftListOffsetY = (float)ini.GetDoubleValue("Main", "LeftListOffsetY", 0.0);
        g_Settings.leftListScale = (float)ini.GetDoubleValue("Main", "LeftListScale", 1.0);
        g_Settings.rightListOffsetX = (float)ini.GetDoubleValue("Main", "RightListOffsetX", 0.0);
        g_Settings.rightListOffsetY = (float)ini.GetDoubleValue("Main", "RightListOffsetY", 0.0);
        g_Settings.rightListScale = (float)ini.GetDoubleValue("Main", "RightListScale", 1.0);
        g_Settings.showSectorsDivider = ini.GetBoolValue("Main", "ShowSectorsDivider", true);
        g_Settings.hideEmptyCategories = ini.GetBoolValue("Main", "HideEmptyCategories", true); // 新增读取
        g_Settings.enableGamepad = ini.GetBoolValue("Main", "Gamepad", true);
        g_Settings.gamepadCenterStick = static_cast<int>(ini.GetLongValue("Main", "GamepadCenterStick", 1));
        if (g_Settings.gamepadCenterStick < 0 || g_Settings.gamepadCenterStick > 1) {
            g_Settings.gamepadCenterStick = 1;
        }
        g_Settings.dpadUpAction = static_cast<int>(ini.GetLongValue("Main", "DpadUpAction", 1));
        g_Settings.dpadDownAction = static_cast<int>(ini.GetLongValue("Main", "DpadDownAction", 1));
        g_Settings.dpadLeftAction = static_cast<int>(ini.GetLongValue("Main", "DpadLeftAction", 1));
        g_Settings.dpadRightAction = static_cast<int>(ini.GetLongValue("Main", "DpadRightAction", 1));
        g_Settings.favoriteCloseOnUse = ini.GetBoolValue("Main", "FavoriteCloseOnUse", true);
        g_Settings.favoriteSlotOrder = static_cast<int>(ini.GetLongValue("Main", "FavoriteSlotOrder", 0));
        if (g_Settings.favoriteSlotOrder < 0 || g_Settings.favoriteSlotOrder > 1) {
            g_Settings.favoriteSlotOrder = 0;
        }
        g_Settings.submenuOpenOnHover = ini.GetBoolValue("Main", "SubmenuOpenOnHover", false);
        g_Settings.showCloseBadges = ini.GetBoolValue("Main", "ShowCloseBadges", true);
        g_Settings.showHintBar = ini.GetBoolValue("Main", "ShowHintBar", true);
        g_Settings.showCenterName = ini.GetBoolValue("Main", "ShowCenterName", true);
        g_Settings.showConditionBars = ini.GetBoolValue("Main", "ShowConditionBars", true);
        g_Settings.centerNameFontSize = (float)ini.GetDoubleValue("Main", "CenterNameFontSize", 24.0);
        g_Settings.customFontFile = ini.GetValue("Main", "CustomFontFile", "");

        // Deserialize tag blacklist
        g_Settings.tagBlacklist.clear();
        const char* tagBlacklistRaw = ini.GetValue("Main", "TagBlacklist", "");
        if (tagBlacklistRaw && tagBlacklistRaw[0]) {
            std::stringstream ss(tagBlacklistRaw);
            std::string tag;
            while (std::getline(ss, tag, ';')) {
                if (!tag.empty()) {
                    g_Settings.tagBlacklist.push_back(tag);
                }
            }
        }

        for (size_t i = 0; i < g_Settings.filters.size(); ++i) {
            std::string key = "Filter_" + std::to_string(i);
            g_Settings.filters[i].enabled = ini.GetBoolValue("Filters", key.c_str(), false);
        }

        // 2. 独立读取动态缓存
        CSimpleIniA cache;
        cache.SetUnicode();
        cache.LoadFile(cachePath);

        g_Settings.pinnedLeft = cache.GetValue("Session", "PinnedLeft", "");
        g_Settings.pinnedRight = cache.GetValue("Session", "PinnedRight", "");
        g_Settings.pinnedLeftFilter = cache.GetValue("Session", "PinnedLeftFilter", "");
        g_Settings.pinnedRightFilter = cache.GetValue("Session", "PinnedRightFilter", "");
    }

    void ToggleConfig() {
        g_ShowConfig = !g_ShowConfig;
        auto cm = RE::ControlMap::GetSingleton();
        auto mc = RE::MenuCursor::GetSingleton();
        ImGuiIO& io = ImGui::GetIO();

        if (g_ShowConfig) {
            if (cm) cm->PushInputContext(RE::UserEvents::INPUT_CONTEXT_ID::kCursor);
            if (mc) mc->RegisterCursor();
            io.MouseDrawCursor = true;

            if (cm) cm->SetIgnoreKeyboardMouse(true);

            io.ClearInputKeys();
            io.InputQueueCharacters.resize(0);

            POINT p;
            if (GetCursorPos(&p) && ScreenToClient(WheelMenuNative::g_GameHWnd, &p)) {
                io.MousePos = ImVec2((float)p.x, (float)p.y);
            }
            // 移除了这里的强制打开轮盘，交给下面的 Tab 状态机去判断！
        }
        else {
            if (cm) {
                cm->PopInputContext(RE::UserEvents::INPUT_CONTEXT_ID::kCursor);
                cm->SetIgnoreKeyboardMouse(false);
            }
            if (mc) mc->UnregisterCursor();
            io.MouseDrawCursor = false;
            g_isRecordingHotkey = false;
            g_isRecordingWheelHotkey = false;

            // 联动关闭：如果 ImGui 关闭时，轮盘是我们强行打开的，连带着一起关掉！
            if (g_openedByConfig && WheelMenuNative::WheelMenu::DesiredState) {
                WheelMenuNative::WheelMenu::DesiredState = false;
                WheelMenuNative::WheelMenu::Toggle(false);
                g_openedByConfig = false;
            }

            Save();
        }
    }

    LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
            if (g_isRecordingWheelHotkey) {
                g_Settings.wheelHotkey = static_cast<uint32_t>(wParam);
                g_isRecordingWheelHotkey = false;
                Save();
                return 0;
            }
            if (g_isRecordingHotkey) {
                g_Settings.configMenuHotkey = (uint32_t)wParam;
                g_isRecordingHotkey = false; Save(); return 0;
            }
            if (IsInitialKeyDown(lParam) && IsConfigHotkey(wParam, lParam)) {
                ToggleConfig(); return 0;
            }
        }

        if (g_ShowConfig) {
            if (ImGui::GetCurrentContext()) {
                ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
            }

            ImGuiIO& io = ImGui::GetIO();
            bool isMouseMsg = (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL;

            // ==========================================
            // Allow mouse input to pass through outside the configuration window.
            // 如果你在“外观”Tab，且鼠标移出了 ImGui 的窗口 (WantCaptureMouse == false)
            // 直接把鼠标事件放行，还给游戏底层！这样就能直接点背后的轮盘了！
            // ==========================================
            if (g_isLayoutTabActive && isMouseMsg && !io.WantCaptureMouse) {
                return CallWindowProc(OriginalWndProc, hWnd, msg, wParam, lParam);
            }

            if (msg == WM_CHAR) return true;

            if (msg == WM_INPUT) {
                RAWINPUT raw;
                UINT dwSize = sizeof(RAWINPUT);
                if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &raw, &dwSize, sizeof(RAWINPUTHEADER)) != (UINT)-1) {
                    if (raw.header.dwType == RIM_TYPEKEYBOARD && (raw.data.keyboard.Flags & RI_KEY_BREAK)) {
                        return CallWindowProc(OriginalWndProc, hWnd, msg, wParam, lParam);
                    }
                }
                return true;
            }

            if (isMouseMsg || (msg >= WM_KEYFIRST && msg <= WM_KEYLAST)) {
                if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
                    return CallWindowProc(OriginalWndProc, hWnd, msg, wParam, lParam);
                }
                return true;
            }
        }

        return CallWindowProc(OriginalWndProc, hWnd, msg, wParam, lParam);
    }

    void Render(IDXGISwapChain* a_swapChain) {
        if (!g_Initialized) {
            auto rd = RE::BSGraphics::GetRendererData();
            if (!rd || !rd->device) return;
            DXGI_SWAP_CHAIN_DESC sd; a_swapChain->GetDesc(&sd);
            HWND hMainWnd = sd.OutputWindow; WheelMenuNative::g_GameHWnd = hMainWnd;
            OriginalWndProc = (WNDPROC)SetWindowLongPtr(hMainWnd, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
            ImGui::CreateContext(); ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; io.MouseDrawCursor = false;
            std::string fontPath = ResolveImGuiFontPath();
            ImFontConfig font_cfg; font_cfg.FontNo = 0;
            if (auto* font = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 18.0f, &font_cfg, BuildImGuiFontRanges(io))) {
                io.FontDefault = font;
            }
            ImGui_ImplWin32_Init(hMainWnd);
            ImGui_ImplDX11_Init(reinterpret_cast<ID3D11Device*>(rd->device), reinterpret_cast<ID3D11DeviceContext*>(rd->context));
            g_Initialized = true;
        }

        // Deferred font reload: always runs, even if config panel is closed
        if (g_needsFontReload) {
            g_needsFontReload = false;
            ApplyFontReload();
        }

        if (g_Initialized && g_ShowConfig) {
            ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(550, 720), ImGuiCond_FirstUseEver);

            bool wasOpened = g_ShowConfig;
            if (ImGui::Begin(StableWindowLabel(_T("Window.Title"), "Fallout4WheelMenuConfig"), &g_ShowConfig, ImGuiWindowFlags_NoCollapse)) {

                // 记录上一帧的状态，用来比对 Tab 是否发生了切换
                static bool s_wasLayoutTabActive = false;
                g_isLayoutTabActive = false; // 默认不在外观 Tab

                if (ImGui::BeginTabBar("ConfigTabs")) {

                    // ------------------------------------------
                    // TAB 1: 常规设置 (不开启轮盘)
                    // ------------------------------------------
                    if (ImGui::BeginTabItem(_T("Tab.General"))) {
                        if (ImGui::CollapsingHeader(_T("Section.Localization"), ImGuiTreeNodeFlags_DefaultOpen)) {
                            auto languages = WheelMenuNative::L10n::GetAvailableLanguages();
                            int currentLang = 0;
                            std::vector<const char*> languageItems;
                            languageItems.reserve(languages.size());
                            for (int i = 0; i < static_cast<int>(languages.size()); ++i) {
                                languageItems.push_back(languages[i].c_str());
                                if (languages[i] == g_Settings.uiLanguage) {
                                    currentLang = i;
                                }
                            }
                            if (ImGui::Combo(_T("General.InterfaceLanguage"), &currentLang, languageItems.data(), static_cast<int>(languageItems.size()))) {
                                g_Settings.uiLanguage = languages[currentLang];
                                WheelMenuNative::L10n::LoadLanguage(g_Settings.uiLanguage);
                                Save();
                            }
                            ImGui::Separator();
                            // Font selection - auto-scan Fonts/ folder
                            auto fontFiles = ScanFontFiles();
                            std::vector<const char*> fontItems;
                            fontItems.reserve(fontFiles.size() + 1);
                            fontItems.push_back(_T("General.DefaultFont"));

                            int currentFontItem = 0;
                            if (!g_Settings.customFontFile.empty()) {
                                for (int i = 0; i < (int)fontFiles.size(); i++) {
                                    if (fontFiles[i] == g_Settings.customFontFile) {
                                        currentFontItem = i + 1;
                                        break;
                                    }
                                }
                            }

                            for (auto& f : fontFiles) {
                                fontItems.push_back(f.c_str());
                            }

                            ImGui::SetNextItemWidth(-FLT_MIN);
                            if (ImGui::Combo(_T("General.CustomFontFile"), &currentFontItem, fontItems.data(), (int)fontItems.size())) {
                                if (currentFontItem == 0) {
                                    g_Settings.customFontFile = "";
                                } else {
                                    g_Settings.customFontFile = fontFiles[currentFontItem - 1];
                                }
                                Save();
                                g_needsFontReload = true;
                            }
                            ImGui::TextWrapped("%s", _T("General.CustomFontHint"));
                            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s", ResolveImGuiFontPath().c_str());
                        }

                        if (ImGui::CollapsingHeader(_T("Section.HotkeysInteraction"), ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::Text(_T("General.SettingsHotkeyFmt"), g_Settings.configMenuHotkey);
                            if (ImGui::Button(g_isRecordingHotkey ? _T("General.WaitingKeyInput") : _T("General.ChangeSettingsHotkey"), ImVec2(200, 0))) {
                                g_isRecordingHotkey = true;
                                g_isRecordingWheelHotkey = false;
                            }
                            ImGui::Separator();

                            const char* activationModes[] = {
                                _T("General.ActivationReplaceFavorites"),
                                _T("General.ActivationIndependent")
                            };
                            ImGui::SetNextItemWidth(260.0f);
                            if (ImGui::Combo(_T("General.ActivationMode"), &g_Settings.activationMode, activationModes, IM_ARRAYSIZE(activationModes))) {
                                Save();
                            }
                            if (g_Settings.activationMode == 1) {
                                const std::string wheelKeyName = GetVirtualKeyName(g_Settings.wheelHotkey);
                                ImGui::Text(_T("General.WheelHotkeyFmt"), wheelKeyName.c_str());
                                if (ImGui::Button(g_isRecordingWheelHotkey ? _T("General.WaitingKeyInput") : _T("General.ChangeWheelHotkey"), ImVec2(200, 0))) {
                                    g_isRecordingWheelHotkey = true;
                                    g_isRecordingHotkey = false;
                                }
                            }

                            ImGui::Separator();
                            ImGui::Checkbox(_T("General.ToggleOpenMode"), &g_Settings.toggleMode);
                            ImGui::TextWrapped("%s", _T("General.ToggleOpenModeHint"));
                            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s",
                                _T(g_Settings.activationMode == 0 ? "General.FavoritesKeyHint" : "General.IndependentKeyHint"));
                            ImGui::Separator();
                            bool favoriteSettingsChanged = false;
                            if (ImGui::Checkbox(_T("General.ShowFavoriteRing"), &g_Settings.showFavoriteRing)) favoriteSettingsChanged = true;
                            if (ImGui::Checkbox(_T("General.FavoriteCloseAfterUse"), &g_Settings.favoriteCloseOnUse)) favoriteSettingsChanged = true;
                            const char* favoriteOrderItems[] = {
                                _T("General.FavoriteOrderClockwise"),
                                _T("General.FavoriteOrderVanillaCross")
                            };
                            ImGui::SetNextItemWidth(220.0f);
                            if (ImGui::Combo(_T("General.FavoriteSlotOrder"), &g_Settings.favoriteSlotOrder, favoriteOrderItems, IM_ARRAYSIZE(favoriteOrderItems))) favoriteSettingsChanged = true;
                            if (ImGui::Checkbox(_T("General.SubmenuOpenOnHover"), &g_Settings.submenuOpenOnHover)) favoriteSettingsChanged = true;
                            if (ImGui::Checkbox(_T("General.ShowCloseBadges"), &g_Settings.showCloseBadges)) favoriteSettingsChanged = true;
                            if (ImGui::Checkbox(_T("General.ShowHintBar"), &g_Settings.showHintBar)) favoriteSettingsChanged = true;
                            if (ImGui::Checkbox(_T("General.ShowCenterName"), &g_Settings.showCenterName)) favoriteSettingsChanged = true;
                            if (ImGui::Checkbox(_T("General.ShowConditionBars"), &g_Settings.showConditionBars)) favoriteSettingsChanged = true;
                            if (favoriteSettingsChanged) {
                                Save();
                                WheelMenuNative::WheelMenu::UpdateLiveLayout(g_Settings.coreRadius, g_Settings.favSlotSize, g_Settings.layerGap, g_Settings.tickLength);
                            }
                        }

                        if (ImGui::CollapsingHeader(_T("Section.Gamepad"), ImGuiTreeNodeFlags_DefaultOpen)) {
                            if (ImGui::Checkbox(_T("General.EnableGamepad"), &g_Settings.enableGamepad)) {
                                Save();
                            }
                            const char* stickItems[] = { _T("General.LeftStick"), _T("General.RightStick") };
                            ImGui::SetNextItemWidth(180.0f);
                            if (ImGui::Combo(_T("General.CenterRingStick"), &g_Settings.gamepadCenterStick, stickItems, IM_ARRAYSIZE(stickItems))) {
                                Save();
                                if (WheelMenuNative::WheelMenu::ActiveMenu) {
                                    WheelMenuNative::WheelMenu::UpdateLiveLayout(g_Settings.coreRadius, g_Settings.favSlotSize, g_Settings.layerGap, g_Settings.tickLength);
                                }
                            }
                            ImGui::Separator();
                            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), _T("General.DpadActions"));
                            const char* dpadItems[] = { _T("General.DpadNone"), _T("General.DpadOpenWheel") };
                            ImGui::SetNextItemWidth(220.0f);
                            ImGui::Combo(_T("General.DpadUp"), &g_Settings.dpadUpAction, dpadItems, IM_ARRAYSIZE(dpadItems));
                            ImGui::SetNextItemWidth(220.0f);
                            ImGui::Combo(_T("General.DpadDown"), &g_Settings.dpadDownAction, dpadItems, IM_ARRAYSIZE(dpadItems));
                            ImGui::SetNextItemWidth(220.0f);
                            ImGui::Combo(_T("General.DpadLeft"), &g_Settings.dpadLeftAction, dpadItems, IM_ARRAYSIZE(dpadItems));
                            ImGui::SetNextItemWidth(220.0f);
                            ImGui::Combo(_T("General.DpadRight"), &g_Settings.dpadRightAction, dpadItems, IM_ARRAYSIZE(dpadItems));
                        }

                        if (ImGui::CollapsingHeader(_T("Section.TimeControl"), ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::RadioButton(_T("Time.Normal"), &g_Settings.timeMode, 0); ImGui::SameLine();
                            ImGui::RadioButton(_T("Time.BulletTime"), &g_Settings.timeMode, 1); ImGui::SameLine();
                            ImGui::RadioButton(_T("Time.FreezeTime"), &g_Settings.timeMode, 2);
                            if (g_Settings.timeMode == 1) {
                                ImGui::SliderFloat(_T("Time.Multiplier"), &g_Settings.slowTimeMult, 0.01f, 1.0f, "%.2f");
                                ImGui::Checkbox(_T("Time.BulletTimeCombatOnly"), &g_Settings.bulletTimeCombatOnly);
                            }
                        }

                        if (ImGui::CollapsingHeader(_T("Section.ImageSpace"), ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::SliderFloat(_T("ImageSpace.GlobalStrength"), &g_Settings.globalFilterStrength, 0.0f, 2.0f, "%.2f");
                            ImGui::Checkbox(_T("ImageSpace.AutoRenew"), &g_Settings.autoRenewFilters);
                            if (g_Settings.autoRenewFilters) {
                                ImGui::SameLine(); ImGui::SliderFloat(_T("ImageSpace.RefreshInterval"), &g_Settings.renewInterval, 0.5f, 5.0f, "%.1fs");
                            }
                            ImGui::Separator();
                            const bool filterListVisible = ImGui::BeginChild("FilterList", ImVec2(0, 200), true);
                            if (filterListVisible) {
                                for (auto& f : g_Settings.filters) ImGui::Checkbox(GetFilterDisplayName(f), &f.enabled);
                            }
                            ImGui::EndChild();
                        }

                        if (ImGui::CollapsingHeader(_T("TagBlacklist.Title"), ImGuiTreeNodeFlags_DefaultOpen)) {
                            // Show current blacklisted tags with delete buttons
                            int deleteIdx = -1;
                            for (int i = 0; i < (int)g_Settings.tagBlacklist.size(); i++) {
                                ImGui::PushID(i);
                                ImGui::BulletText("%s", g_Settings.tagBlacklist[i].c_str());
                                ImGui::SameLine();
                                if (ImGui::SmallButton(_T("TagBlacklist.Delete"))) {
                                    deleteIdx = i;
                                }
                                ImGui::PopID();
                            }
                            if (deleteIdx >= 0) {
                                g_Settings.tagBlacklist.erase(g_Settings.tagBlacklist.begin() + deleteIdx);
                                Save();
                            }

                            // Add new tag
                            static char s_newTagBuf[128] = {};
                            ImGui::SetNextItemWidth(200);
                            if (ImGui::InputTextWithHint("##newTag", _T("TagBlacklist.InputHint"), s_newTagBuf, sizeof(s_newTagBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                                std::string tag(s_newTagBuf);
                                if (!tag.empty()) {
                                    bool isDuplicate = false;
                                    for (auto& t : g_Settings.tagBlacklist) {
                                        if (t == tag) { isDuplicate = true; break; }
                                    }
                                    if (!isDuplicate) {
                                        g_Settings.tagBlacklist.push_back(tag);
                                        Save();
                                    }
                                }
                                s_newTagBuf[0] = '\0';
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(_T("TagBlacklist.Add"))) {
                                std::string tag(s_newTagBuf);
                                if (!tag.empty()) {
                                    bool isDuplicate = false;
                                    for (auto& t : g_Settings.tagBlacklist) {
                                        if (t == tag) { isDuplicate = true; break; }
                                    }
                                    if (!isDuplicate) {
                                        g_Settings.tagBlacklist.push_back(tag);
                                        Save();
                                    }
                                }
                                s_newTagBuf[0] = '\0';
                            }
                            ImGui::TextWrapped(_T("TagBlacklist.Description"));
                        }

                        ImGui::EndTabItem();
                    }

                    // ------------------------------------------
                    // TAB 2: 轮盘外观 (自动联动开启轮盘，并支持穿透)
                    // ------------------------------------------
                    if (ImGui::BeginTabItem(_T("Tab.LayoutAppearance"))) {
                        g_isLayoutTabActive = true;

                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), _T("Layout.DirectControlHint"));
                        ImGui::Spacing();

                        if (ImGui::CollapsingHeader(_T("Section.UIOffset"), ImGuiTreeNodeFlags_DefaultOpen)) {
                            bool transformChanged = false;
                            bool transformReleased = false;

                            if (ImGui::SliderFloat(_T("Layout.OffsetX"), &g_Settings.menuOffsetX, -500.0f, 500.0f, "%.0f")) transformChanged = true;
                            if (ImGui::IsItemDeactivatedAfterEdit()) transformReleased = true;

                            if (ImGui::SliderFloat(_T("Layout.OffsetY"), &g_Settings.menuOffsetY, -500.0f, 500.0f, "%.0f")) transformChanged = true;
                            if (ImGui::IsItemDeactivatedAfterEdit()) transformReleased = true;

                            if (ImGui::SliderFloat(_T("Layout.Scale"), &g_Settings.menuScaling, 0.5f, 2.0f, "%.2f")) transformChanged = true;
                            if (ImGui::IsItemDeactivatedAfterEdit()) transformReleased = true;

                            static ULONGLONG s_lastTransformUpdate = GetTickCount64();
                            if (transformChanged) {
                                const ULONGLONG now = GetTickCount64();
                                if (now - s_lastTransformUpdate > 33) {
                                    WheelMenuNative::WheelMenu::UpdateLiveTransform(g_Settings.menuOffsetX, g_Settings.menuOffsetY, g_Settings.menuScaling);
                                    s_lastTransformUpdate = now;
                                }
                            }

                            if (transformReleased) {
                                WheelMenuNative::WheelMenu::UpdateLiveTransform(g_Settings.menuOffsetX, g_Settings.menuOffsetY, g_Settings.menuScaling);
                                Save();
                            }
                        }

                        if (ImGui::CollapsingHeader(_T("Section.ListLayout"), ImGuiTreeNodeFlags_DefaultOpen)) {
                            bool listLayoutChanged = false;
                            bool listLayoutReleased = false;

                            ImGui::TextUnformatted(_T("Layout.LeftList"));
                            if (ImGui::SliderFloat(_T("Layout.LeftListOffsetX"), &g_Settings.leftListOffsetX, -600.0f, 600.0f, "%.0f")) listLayoutChanged = true;
                            if (ImGui::IsItemDeactivatedAfterEdit()) listLayoutReleased = true;
                            if (ImGui::SliderFloat(_T("Layout.LeftListOffsetY"), &g_Settings.leftListOffsetY, -600.0f, 600.0f, "%.0f")) listLayoutChanged = true;
                            if (ImGui::IsItemDeactivatedAfterEdit()) listLayoutReleased = true;
                            if (ImGui::SliderFloat(_T("Layout.LeftListScale"), &g_Settings.leftListScale, 0.5f, 2.0f, "%.2f")) listLayoutChanged = true;
                            if (ImGui::IsItemDeactivatedAfterEdit()) listLayoutReleased = true;

                            ImGui::Separator();
                            ImGui::TextUnformatted(_T("Layout.RightList"));
                            if (ImGui::SliderFloat(_T("Layout.RightListOffsetX"), &g_Settings.rightListOffsetX, -600.0f, 600.0f, "%.0f")) listLayoutChanged = true;
                            if (ImGui::IsItemDeactivatedAfterEdit()) listLayoutReleased = true;
                            if (ImGui::SliderFloat(_T("Layout.RightListOffsetY"), &g_Settings.rightListOffsetY, -600.0f, 600.0f, "%.0f")) listLayoutChanged = true;
                            if (ImGui::IsItemDeactivatedAfterEdit()) listLayoutReleased = true;
                            if (ImGui::SliderFloat(_T("Layout.RightListScale"), &g_Settings.rightListScale, 0.5f, 2.0f, "%.2f")) listLayoutChanged = true;
                            if (ImGui::IsItemDeactivatedAfterEdit()) listLayoutReleased = true;

                            static ULONGLONG s_lastListLayoutUpdate = GetTickCount64();
                            if (listLayoutChanged) {
                                const ULONGLONG now = GetTickCount64();
                                if (now - s_lastListLayoutUpdate > 33) {
                                    WheelMenuNative::WheelMenu::UpdateLiveListLayout(
                                        g_Settings.leftListOffsetX, g_Settings.leftListOffsetY, g_Settings.leftListScale,
                                        g_Settings.rightListOffsetX, g_Settings.rightListOffsetY, g_Settings.rightListScale);
                                    s_lastListLayoutUpdate = now;
                                }
                            }
                            if (listLayoutReleased) {
                                WheelMenuNative::WheelMenu::UpdateLiveListLayout(
                                    g_Settings.leftListOffsetX, g_Settings.leftListOffsetY, g_Settings.leftListScale,
                                    g_Settings.rightListOffsetX, g_Settings.rightListOffsetY, g_Settings.rightListScale);
                                Save();
                            }
                        }

                        if (ImGui::CollapsingHeader(_T("Section.LiveLayout"), ImGuiTreeNodeFlags_DefaultOpen)) {
                            bool layoutChanged = false;
                            bool isReleased = false;

                            if (ImGui::SliderFloat(_T("Layout.CoreRadius"), &g_Settings.coreRadius, 30.0f, 150.0f, "%.0f")) layoutChanged = true;
                            if (ImGui::IsItemDeactivatedAfterEdit()) isReleased = true;

                            if (ImGui::SliderFloat(_T("Layout.SlotSize"), &g_Settings.favSlotSize, 20.0f, 80.0f, "%.0f")) layoutChanged = true;
                            if (ImGui::IsItemDeactivatedAfterEdit()) isReleased = true;

                            if (ImGui::SliderFloat(_T("Layout.LayerGap"), &g_Settings.layerGap, 0.0f, 30.0f, "%.0f")) layoutChanged = true;
                            if (ImGui::IsItemDeactivatedAfterEdit()) isReleased = true;

                            if (ImGui::SliderFloat(_T("Layout.TickLength"), &g_Settings.tickLength, 0.0f, 30.0f, "%.0f")) layoutChanged = true;
                            if (ImGui::IsItemDeactivatedAfterEdit()) isReleased = true;

                            if (ImGui::SliderFloat(_T("Layout.CategoryRingThickness"), &g_Settings.categoryRingThickness, 20.0f, 90.0f, "%.0f")) layoutChanged = true;
                            if (ImGui::IsItemDeactivatedAfterEdit()) isReleased = true;

                            if (ImGui::SliderFloat(_T("Layout.CenterNameFontSize"), &g_Settings.centerNameFontSize, 12.0f, 48.0f, "%.0f")) layoutChanged = true;
                            if (ImGui::IsItemDeactivatedAfterEdit()) isReleased = true;

                            if (ImGui::Checkbox(_T("Layout.ShowSectorDividers"), &g_Settings.showSectorsDivider)) layoutChanged = true;
                            if (ImGui::IsItemDeactivatedAfterEdit()) isReleased = true;

                            // 新增的开关：同样加入到联动引擎触发条件里
                            if (ImGui::Checkbox(_T("Layout.HideEmptySectors"), &g_Settings.hideEmptyCategories)) layoutChanged = true;
                            if (ImGui::IsItemDeactivatedAfterEdit()) isReleased = true;

                            // 性能节流器 (Throttler)
                            static ULONGLONG s_lastLiveUpdate = GetTickCount64();
                            if (layoutChanged) {
                                ULONGLONG now = GetTickCount64();
                                if (now - s_lastLiveUpdate > 66) {
                                    WheelMenuNative::WheelMenu::UpdateLiveLayout(g_Settings.coreRadius, g_Settings.favSlotSize, g_Settings.layerGap, g_Settings.tickLength);
                                    s_lastLiveUpdate = now;
                                }
                            }

                            if (isReleased) {
                                WheelMenuNative::WheelMenu::UpdateLiveLayout(g_Settings.coreRadius, g_Settings.favSlotSize, g_Settings.layerGap, g_Settings.tickLength);
                                Save();
                            }
                        }
                        ImGui::EndTabItem();
                    }

                    // ------------------------------------------
                    // TAB 3: XML 规则编辑器 (内置微型 IDE)
                    // ------------------------------------------
                    if (ImGui::BeginTabItem(_T("Tab.RulesCategories"))) {
                        g_isLayoutTabActive = false;
                        DrawCategoryEditor();
                        ImGui::EndTabItem();
                    }

                    ImGui::EndTabBar(); // ✅ 正确：在这里结束
                }

                // ==========================================
                // Keep the preview wheel synchronized with the active tab.
                // ==========================================
                if (g_isLayoutTabActive && !s_wasLayoutTabActive) {
                    // 动作 A：刚切入【外观】Tab -> 如果轮盘没开，强行把它打开预览！
                    if (!WheelMenuNative::WheelMenu::DesiredState) {
                        WheelMenuNative::WheelMenu::DesiredState = true;
                        WheelMenuNative::WheelMenu::Toggle(true);
                        g_openedByConfig = true; // 标记：“是我开的，等下我要负责关”
                    }
                }
                else if (!g_isLayoutTabActive && s_wasLayoutTabActive) {
                    // 动作 B：刚离开【外观】Tab -> 如果轮盘是我们强行打开的，立刻撤收关掉！
                    if (g_openedByConfig && WheelMenuNative::WheelMenu::DesiredState) {
                        WheelMenuNative::WheelMenu::DesiredState = false;
                        WheelMenuNative::WheelMenu::Toggle(false);
                        g_openedByConfig = false;
                    }
                }
                s_wasLayoutTabActive = g_isLayoutTabActive;

                ImGui::Separator();

                // 底部的公共按钮
                if (ImGui::Button(_T("Action.HotReloadConf"), ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
                    WheelMenuNative::WheelMenu::ForceReloadAS3Config();
                }
                ImGui::Spacing();

                if (ImGui::Button(_T("Action.SaveClose"), ImVec2(ImGui::GetContentRegionAvail().x, 35))) { ToggleConfig(); }
            }
            ImGui::End(); ImGui::Render(); ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            if (wasOpened && !g_ShowConfig) { g_ShowConfig = true; ToggleConfig(); }
        }
    }

    HRESULT STDMETHODCALLTYPE Present_Hook(IDXGISwapChain* a_swapChain, UINT a_sync, UINT a_flags) {
        Render(a_swapChain); return OriginalPresent(a_swapChain, a_sync, a_flags);
    }

    bool Install() {
        Load(); auto rd = RE::BSGraphics::GetRendererData();
        if (!rd || !rd->renderWindow[0].swapChain) return false;
        auto sc = reinterpret_cast<IDXGISwapChain*>(rd->renderWindow[0].swapChain);
        void** vt = *reinterpret_cast<void***>(sc);
        OriginalPresent = (Present_t)vt[8];
        DetourTransactionBegin(); DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)OriginalPresent, Present_Hook);
        return DetourTransactionCommit() == NO_ERROR;
    }
}
