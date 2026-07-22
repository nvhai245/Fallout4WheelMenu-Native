#include "WheelMenu.h"
#include "ConfigUI.h"
#include "ItemClassifier.h"
#include "Localizer.h"
#include <ConditionSystem/DurabilityAPI.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <RE/B/ButtonEvent.h>
#include <RE/B/BSGraphics.h>
#include <RE/B/BSScaleformTranslator.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/S/Setting.h>
#include <RE/A/ActorEquipManager.h>      
#include <RE/B/BGSObjectInstance.h>      
#include <RE/T/TESBoundObject.h>         
#include <RE/T/TESForm.h>
#include <RE/T/TESObjectWEAP.h>          
#include <RE/B/BGSKeywordForm.h>         
#include <RE/C/ControlMap.h> 
#include <RE/B/BSTimer.h>
#include <RE/T/TESImageSpaceModifier.h>
#include <RE/I/ImageSpaceModifierInstanceForm.h>
#include <RE/M/Main.h>
#include <RE/P/PlayerControls.h>
#include <RE/E/ExtraHealth.h>
#include <RE/E/ExtraCharge.h>
#include <RE/T/TESHealthForm.h>
#include <RE/P/PlayerInputHandler.h>
#include <RE/P/PipboyManager.h>
#include <RE/A/AlchemyItem.h>
#include <RE/A/ActorValue.h>
#include <RE/A/ActorValueInfo.h>
#include <RE/E/EffectArchetypes.h>
#include <RE/U/UIUtils.h>
#include <RE/F/FavoritesManager.h>
#include <RE/T/ThumbstickEvent.h>
#include <RE/E/ExtraFavorite.h>
#include <RE/T/TESAmmo.h>
#include <RE/G/GUN_STATE.h>
#include <RE/C/COMPILER_NAME.h>
#include <RE/C/ConcreteFormFactory.h>
#include <RE/S/Script.h>
#include <RE/S/ScriptCompiler.h>
// Keep ConsoleLog after Script.h; some CommonLibF4 layouts are order-sensitive here.
#include <RE/C/ConsoleLog.h>

#include <F4SE/F4SE.h> 
#include <detours.h> 
#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>
#include <SimpleIni.h>
#include <fstream>
#include <format>
#include <sstream>
#include <string>

namespace WheelMenuNative {

    HWND g_GameHWnd = nullptr;

    // Performance trace points are compiled out for release builds.
    static std::chrono::time_point<std::chrono::high_resolution_clock> s_openStartTime;
    static void LogPerf(const char* stepName) {
        constexpr bool enablePerfLogging = false;
        if constexpr (enablePerfLogging) {
            const auto now = std::chrono::high_resolution_clock::now();
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_openStartTime).count();
            REX::INFO("[PerfTimer] +{} ms: {}", ms, stepName);
        }
    }

    class F4SEEventHook : public Scaleform::GFx::FunctionHandler {
    public:
        void Call(const Params& a_params) override {
            if (a_params.argCount == 0 || !a_params.args[0].IsString()) return;
            std::string eventName = a_params.args[0].GetString();
            if (eventName == "WheelMenuInit") {
                LogPerf("【节点 4】AS3 虚拟机启动完毕，向 C++ 请求初始化数据");
                RE::UIUtils::PlayMenuSound("UIMenuOK");
                if (WheelMenu::ActiveMenu) WheelMenu::ActiveMenu->OnWheelMenuInit();
            }
            else if (eventName == "WheelMenuSelect") {
                if (a_params.argCount >= 2) {
                    std::uint32_t uid = 0;
                    if (a_params.args[1].IsUInt()) uid = a_params.args[1].GetUInt();
                    else if (a_params.args[1].IsInt()) uid = static_cast<std::uint32_t>(a_params.args[1].GetInt());
                    else if (a_params.args[1].IsNumber()) uid = static_cast<std::uint32_t>(a_params.args[1].GetNumber());
                    bool close = true;
                    if (a_params.argCount >= 3 && a_params.args[2].IsBoolean()) close = a_params.args[2].GetBoolean();
                    if (WheelMenu::ActiveMenu) WheelMenu::ActiveMenu->OnWheelMenuSelect(uid, close);
                }
            }
            else if (eventName == "WheelMenuClose") {
                if (WheelMenu::ActiveMenu) WheelMenu::ActiveMenu->OnWheelMenuClose();
            }
            else if (eventName == "WheelMenuHoverUpdate") {
                if (a_params.argCount >= 2) {
                    if (a_params.args[1].IsUInt()) WheelMenu::s_hoveredUID = a_params.args[1].GetUInt();
                    else if (a_params.args[1].IsInt()) WheelMenu::s_hoveredUID = static_cast<std::uint32_t>(a_params.args[1].GetInt());
                    else if (a_params.args[1].IsNumber()) WheelMenu::s_hoveredUID = static_cast<std::uint32_t>(a_params.args[1].GetNumber());
                }
            }
            else if (eventName == "WheelMenuDynamicPauseUpdate") {
                if (a_params.argCount >= 2 && a_params.args[1].IsBoolean()) {
                    WheelMenu::SetDynamicPause(a_params.args[1].GetBoolean());
                }
            }
        }
    };

    class F4SEDirListingHook : public Scaleform::GFx::FunctionHandler {
    public:
        void Call(const Params& a_params) override {
            if (a_params.argCount >= 2 && a_params.args[0].IsString() && a_params.args[1].IsString()) {
                std::string path = a_params.args[0].GetString();
                std::string match = a_params.args[1].GetString();

                Scaleform::GFx::Value retArray;
                a_params.movie->CreateArray(&retArray);

                std::string ext = match;
                if (!ext.empty() && ext[0] == '*') ext = ext.substr(1);

                int idx = 0;
                try {
                    for (const auto& entry : std::filesystem::directory_iterator(path)) {
                        if (entry.is_regular_file()) {
                            std::string filename = entry.path().filename().string();
                            if (ext.empty() || (filename.length() >= ext.length() && filename.compare(filename.length() - ext.length(), ext.length(), ext) == 0)) {
                                Scaleform::GFx::Value obj;
                                a_params.movie->CreateObject(&obj);
                                obj.SetMember("name", Scaleform::GFx::Value(filename.c_str()));
                                retArray.SetElement(idx++, obj);
                            }
                        }
                    }
                }
                catch (...) {}

                if (a_params.retVal) *a_params.retVal = retArray;
            }
        }
    };

    class F4SEAllowTextInputHook : public Scaleform::GFx::FunctionHandler {
    public:
        void Call(const Params&) override {}
    };

    class F4SEPlaySoundHook : public Scaleform::GFx::FunctionHandler {
    public:
        void Call(const Params& a_params) override {
            if (a_params.argCount < 1 || !a_params.args[0].IsString()) return;
            auto soundName = a_params.args[0].GetString();
            if (!soundName || soundName[0] == '\0') return;
            RE::UIUtils::PlayMenuSound(soundName);
        }
    };

    class F4SEUpdatePinHook : public Scaleform::GFx::FunctionHandler {
    public:
        void Call(const Params& a_params) override {
            if (a_params.argCount >= 4) {
                std::string pL = a_params.args[0].IsString() ? a_params.args[0].GetString() : "";
                std::string pR = a_params.args[1].IsString() ? a_params.args[1].GetString() : "";
                std::string fL = a_params.args[2].IsString() ? a_params.args[2].GetString() : "";
                std::string fR = a_params.args[3].IsString() ? a_params.args[3].GetString() : "";

                ConfigUI::g_Settings.pinnedLeft = pL;
                ConfigUI::g_Settings.pinnedRight = pR;
                ConfigUI::g_Settings.pinnedLeftFilter = fL;
                ConfigUI::g_Settings.pinnedRightFilter = fR;

                std::thread([pL, pR, fL, fR]() {
                    CSimpleIniA cacheIni;
                    cacheIni.SetUnicode();
                    const char* cachePath = "Data\\F4SE\\Plugins\\Fallout4WheelMenu_Cache.ini";

                    cacheIni.LoadFile(cachePath);
                    cacheIni.SetValue("Session", "PinnedLeft", pL.c_str());
                    cacheIni.SetValue("Session", "PinnedRight", pR.c_str());
                    cacheIni.SetValue("Session", "PinnedLeftFilter", fL.c_str());
                    cacheIni.SetValue("Session", "PinnedRightFilter", fR.c_str());

                    cacheIni.SaveFile(cachePath);
                    }).detach();
            }
        }
    };

    namespace {
        constexpr float kTimeMultiplierEpsilon = 0.001f;

        bool s_menuTimeStateCaptured = false;
        bool s_menuAppliedGlobalTimeMultiplier = false;
        bool s_menuAppliedFreezeTime = false;
        float s_savedGlobalTimeMultiplier = 1.0f;
        float s_appliedGlobalTimeMultiplier = 1.0f;
        bool s_savedFreezeTime = false;

        bool NearlyEqual(float a_lhs, float a_rhs)
        {
            return std::fabs(a_lhs - a_rhs) <= kTimeMultiplierEpsilon;
        }

        void CaptureMenuTimeState()
        {
            if (s_menuTimeStateCaptured) {
                return;
            }

            s_savedGlobalTimeMultiplier = RE::BSTimer::QGlobalTimeMultiplier();
            if (auto main = RE::Main::GetSingleton()) {
                s_savedFreezeTime = main->freezeTime;
            }
            else {
                s_savedFreezeTime = false;
            }

            s_menuAppliedGlobalTimeMultiplier = false;
            s_menuAppliedFreezeTime = false;
            s_menuTimeStateCaptured = true;
        }

        bool IsPlayerInCombat()
        {
            auto player = RE::PlayerCharacter::GetSingleton();
            return player && (player->playerInCombat || player->activeCombatantCount > 0);
        }

        bool ShouldApplyBulletTimeMode()
        {
            return ConfigUI::g_Settings.timeMode == 1 &&
                (!ConfigUI::g_Settings.bulletTimeCombatOnly || IsPlayerInCombat());
        }

        void ApplyMenuTimeMode()
        {
            CaptureMenuTimeState();

            if (ShouldApplyBulletTimeMode()) {
                if (auto ui = RE::UI::GetSingleton()) {
                    const float configuredMult = ConfigUI::g_Settings.slowTimeMult;
                    const float targetMult = (std::min)(s_savedGlobalTimeMultiplier, configuredMult);
                    ui->uiTimer.SetGlobalTimeMultiplier(targetMult, true);
                    s_appliedGlobalTimeMultiplier = targetMult;
                    s_menuAppliedGlobalTimeMultiplier = true;
                }
            }
            else if (ConfigUI::g_Settings.timeMode == 2) {
                if (auto main = RE::Main::GetSingleton()) {
                    main->freezeTime = true;
                    s_menuAppliedFreezeTime = true;
                }
            }
        }

        void RestoreGameplayTimeDuringMenu()
        {
            if (!s_menuTimeStateCaptured || !s_menuAppliedGlobalTimeMultiplier) {
                return;
            }

            if (auto ui = RE::UI::GetSingleton()) {
                ui->uiTimer.SetGlobalTimeMultiplier(s_savedGlobalTimeMultiplier, true);
                s_menuAppliedGlobalTimeMultiplier = false;
            }
        }

        void RefreshCombatScopedBulletTime()
        {
            if (!s_menuTimeStateCaptured || ConfigUI::g_Settings.timeMode != 1) {
                return;
            }

            if (s_menuAppliedGlobalTimeMultiplier && !ShouldApplyBulletTimeMode()) {
                RestoreGameplayTimeDuringMenu();
            }
        }

        void ReleaseMenuTimeMode()
        {
            if (!s_menuTimeStateCaptured) {
                return;
            }

            if (s_menuAppliedGlobalTimeMultiplier) {
                const float currentMult = RE::BSTimer::QGlobalTimeMultiplier();
                if (NearlyEqual(currentMult, s_appliedGlobalTimeMultiplier)) {
                    if (auto ui = RE::UI::GetSingleton()) {
                        ui->uiTimer.SetGlobalTimeMultiplier(s_savedGlobalTimeMultiplier, true);
                    }
                }
            }

            if (s_menuAppliedFreezeTime) {
                if (auto main = RE::Main::GetSingleton()) {
                    if (main->freezeTime) {
                        main->freezeTime = s_savedFreezeTime;
                    }
                }
            }

            s_menuTimeStateCaptured = false;
            s_menuAppliedGlobalTimeMultiplier = false;
            s_menuAppliedFreezeTime = false;
            s_savedGlobalTimeMultiplier = 1.0f;
            s_appliedGlobalTimeMultiplier = 1.0f;
            s_savedFreezeTime = false;
        }

        bool IsPlayerAimingDownSights()
        {
            auto player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                return false;
            }

            return player->gunState == RE::GUN_STATE::kSighted ||
                player->gunState == RE::GUN_STATE::kFireSighted;
        }

        bool IsGameplayHUDOnly()
        {
            auto ui = RE::UI::GetSingleton();
            if (!ui) {
                return false;
            }

            if (ui->menuMode != 0) {
                return false;
            }

            if (auto main = RE::Main::GetSingleton(); main && main->inMenuMode) {
                return false;
            }

            return true;
        }

        bool HasSlowTimeEffect(RE::TESForm* a_form)
        {
            auto alchemyItem = a_form ? a_form->As<RE::AlchemyItem>() : nullptr;
            if (!alchemyItem) {
                return false;
            }

            for (auto effect : alchemyItem->listOfEffects) {
                if (effect && effect->effectSetting &&
                    effect->effectSetting->data.archetype == RE::EffectArchetypes::ArchetypeID::kSlowTime) {
                    return true;
                }
            }

            return false;
        }

        bool s_dynamicPauseActive = false;
        bool s_dynamicPauseCaptured = false;
        bool s_savedDynamicPauseFreezeTime = false;
        bool s_suppressNextKeepOpenAlchemyHide = false;
        std::chrono::steady_clock::time_point s_ignoreNativeHideUntil{};
        std::vector<RE::NiPointer<RE::ImageSpaceModifierInstanceForm>> s_menuFilterInstances;

        std::string FormatFormForDiag(RE::TESForm* a_form)
        {
            if (!a_form) {
                return "none";
            }

            std::string name;
            auto fullName = RE::TESFullName::GetFullName(*a_form);
            if (!fullName.empty()) {
                name = std::string(fullName);
            }

            auto editorID = a_form->GetFormEditorID();
            return std::format(
                "{:08X}/{} editor='{}' name='{}'",
                a_form->GetFormID(),
                a_form->GetFormTypeString(),
                editorID ? editorID : "",
                name);
        }

        void LogAidAnimState(
            const char* a_stage,
            RE::PlayerCharacter* a_player,
            RE::TESForm* a_form,
            std::uint32_t a_uid,
            std::uint32_t a_stackID,
            bool a_close,
            bool a_slowTime)
        {
            auto alchemy = a_form ? a_form->As<RE::AlchemyItem>() : nullptr;
            auto currentEffect = a_player ? a_player->currentAlchemyEffect : nullptr;
            auto stimpakTarget = a_player ? a_player->stimpakTarget : nullptr;
            auto pipboyMgr = RE::PipboyManager::GetSingleton();
            auto main = RE::Main::GetSingleton();
            auto ui = RE::UI::GetSingleton();
            const auto now = std::chrono::steady_clock::now();
            const auto suppressMs = s_ignoreNativeHideUntil > now ?
                std::chrono::duration_cast<std::chrono::milliseconds>(s_ignoreNativeHideUntil - now).count() :
                0LL;

            REX::INFO(
                "[FWM-AidAnimDiag] stage={} uid={} stack={} close={} slowTime={} form={} isALCH={} isFood={} isMedicine={} isPoison={} effectCount={} currentAlchemyEffect={} stimpakTarget={} pipboyItemAnimOnClose={} pipboyCloseQueued={} animationObjectAction={} currentAlchemyIsPoison={} dynamicPause={} freezeTime={} inMenuMode={} uiMenuMode={} suppressNextHide={} suppressMs={}",
                a_stage ? a_stage : "unknown",
                a_uid,
                a_stackID,
                a_close,
                a_slowTime,
                FormatFormForDiag(a_form),
                alchemy != nullptr,
                alchemy ? alchemy->IsFood() : false,
                alchemy ? alchemy->IsMedicine() : false,
                alchemy ? alchemy->IsPoison() : false,
                alchemy ? alchemy->listOfEffects.size() : 0,
                FormatFormForDiag(currentEffect),
                FormatFormForDiag(stimpakTarget),
                pipboyMgr ? FormatFormForDiag(pipboyMgr->itemAnimOnClose) : "none",
                pipboyMgr ? pipboyMgr->pipboyMenuCloseQueued : false,
                a_player ? a_player->animationObjectAction.underlying() : -1,
                a_player ? a_player->currentAlchemyIsPoison : false,
                s_dynamicPauseActive,
                main ? main->freezeTime : false,
                main ? main->inMenuMode : false,
                ui ? ui->menuMode : 0,
                s_suppressNextKeepOpenAlchemyHide,
                suppressMs);
        }

        void ApplyDynamicPause()
        {
            if (s_dynamicPauseActive) {
                return;
            }

            auto main = RE::Main::GetSingleton();
            if (!main) {
                return;
            }

            s_savedDynamicPauseFreezeTime = main->freezeTime;
            s_dynamicPauseCaptured = true;
            main->freezeTime = true;
            s_dynamicPauseActive = true;
            REX::INFO("[Fallout4WheelMenu] Dynamic pause enabled");
        }

        void ReleaseDynamicPause()
        {
            if (!s_dynamicPauseActive) {
                return;
            }

            if (auto main = RE::Main::GetSingleton(); main && s_dynamicPauseCaptured) {
                main->freezeTime = s_savedDynamicPauseFreezeTime;
            }

            s_dynamicPauseActive = false;
            s_dynamicPauseCaptured = false;
            s_savedDynamicPauseFreezeTime = false;
            REX::INFO("[Fallout4WheelMenu] Dynamic pause disabled");
        }
    }

    // 老版本策略：接收所有按钮事件，由 OnButtonEvent 自行过滤
    // 这样不会干扰游戏对 Scaleform 的鼠标事件路由
        bool WheelInputHijacker::ShouldHandleEvent(const RE::InputEvent* a_event) {
        return a_event && (a_event->eventType == RE::INPUT_EVENT_TYPE::kButton ||
                           a_event->eventType == RE::INPUT_EVENT_TYPE::kThumbstick);
    }

    namespace {
        void SetGameplayHandlersEnabled(bool a_enabled)
        {
            if (auto pc = RE::PlayerControls::GetSingleton()) {
                if (pc->lookHandler) {
                    reinterpret_cast<RE::PlayerInputHandler*>(pc->lookHandler)->inputEventHandlingEnabled = a_enabled;
                }
                if (pc->togglePOVHandler) {
                    reinterpret_cast<RE::PlayerInputHandler*>(pc->togglePOVHandler)->inputEventHandlingEnabled = a_enabled;
                }
                if (pc->attackHandler) {
                    reinterpret_cast<RE::PlayerInputHandler*>(pc->attackHandler)->inputEventHandlingEnabled = a_enabled;
                }
                if (pc->readyWeaponHandler) {
                    reinterpret_cast<RE::PlayerInputHandler*>(pc->readyWeaponHandler)->inputEventHandlingEnabled = a_enabled;
                }
            }
        }

        void CleanupMenuRuntimeState()
        {
            ReleaseDynamicPause();
            ReleaseMenuTimeMode();
            SetGameplayHandlersEnabled(true);
        }

        void StopMenuFilterInstances()
        {
            for (auto& instance : s_menuFilterInstances) {
                if (instance) {
                    static_cast<RE::ImageSpaceModifierInstance*>(instance.get())->Stop();
                }
            }
            s_menuFilterInstances.clear();
        }

        void PruneExpiredMenuFilterInstances()
        {
            std::erase_if(s_menuFilterInstances, [](const auto& instance) {
                return !instance || instance->IsExpired();
            });
        }

        void TriggerMenuFilters(bool a_restartExisting)
        {
            if (a_restartExisting) {
                StopMenuFilterInstances();
            }
            else {
                PruneExpiredMenuFilterInstances();
            }

            for (const auto& f : ConfigUI::g_Settings.filters) {
                if (!f.enabled) {
                    continue;
                }

                auto imad = RE::TESForm::GetFormByID<RE::TESImageSpaceModifier>(f.id);
                if (!imad) {
                    continue;
                }

                uint32_t* flags = reinterpret_cast<uint32_t*>(&imad->data.animatable);
                *flags |= 0x04;

                auto instance = RE::ImageSpaceModifierInstanceForm::Trigger(imad, ConfigUI::g_Settings.globalFilterStrength, nullptr);
                if (instance) {
                    s_menuFilterInstances.emplace_back(instance);
                }
            }
        }

        bool ExecuteSilentScriptCommand(std::string_view a_command, RE::TESObjectREFR* a_target)
        {
            if (a_command.empty() || !a_target) {
                return false;
            }

            auto log = RE::ConsoleLog::GetSingleton();
            const auto oldBuffer = log ? RE::BSString(log->buffer) : RE::BSString();

            auto factory = RE::ConcreteFormFactory<RE::Script>::GetFormFactory();
            auto script = factory ? factory->Create() : nullptr;
            if (!script) {
                REX::WARN("[FWM-AidAnimDiag] script_command failed: no script factory");
                return false;
            }

            RE::ScriptCompiler compiler;
            script->SetText(a_command);
            script->CompileAndRun(&compiler, RE::COMPILER_NAME::kSystemWindow, a_target);

            const bool compiled = script->header.isCompiled;
            if (log) {
                log->buffer = oldBuffer;
            }

            delete script;
            REX::INFO("[FWM-AidAnimDiag] script_command command='{}' compiled={}", a_command, compiled);
            return compiled;
        }

    }

    WheelMenu::WheelMenu() : RE::GameMenuBase() {
        menuFlags.set(RE::UI_MENU_FLAGS::kUsesMenuContext);
        menuFlags.set(RE::UI_MENU_FLAGS::kUsesCursor);
        depthPriority = RE::UI_DEPTH_PRIORITY::kStandard;
        ActiveMenu = this;
        this->inputContext = RE::UserEvents::INPUT_CONTEXT_ID::kQuickContainerMenu;

        ApplyMenuTimeMode();

        // 禁用游戏操作 handler，防止鼠标事件泄漏到游戏层面
        SetGameplayHandlersEnabled(false);
    }

    void WheelMenu::SetDynamicPause(bool a_active) {
        if (a_active) {
            ApplyDynamicPause();
        }
        else {
            ReleaseDynamicPause();
        }
    }

    WheelMenu::~WheelMenu() {
        if (ActiveMenu == this) ActiveMenu = nullptr;

        CleanupMenuRuntimeState();

        StopMenuFilterInstances();

        if (auto controlMap = RE::ControlMap::GetSingleton()) {
            controlMap->SetTextEntryMode(false);
            controlMap->PopInputContext(RE::UserEvents::INPUT_CONTEXT_ID::kQuickContainerMenu);
        }

        // 🛑 动画队列已移除：PlayItemAnimOnClose 会导致无第三人称动画的物品产生"捡起"动画并卡住
    }

    // 🎮 摇杆事件：收藏环摇杆由配置决定，主分类/深层导航自动使用另一根摇杆
    void WheelInputHijacker::OnThumbstickEvent(const RE::ThumbstickEvent* a_event) {
        if (!a_event) return;

        auto ui = RE::UI::GetSingleton();
        bool isOpen = ui && ui->GetMenuOpen(WheelMenu::MENU_NAME);
        if (!isOpen || !WheelMenu::ActiveMenu) return;
        if (!ConfigUI::g_Settings.enableGamepad) return;

        const auto favoriteStick = ConfigUI::g_Settings.gamepadCenterStick == 0 ?
            RE::ThumbstickEvent::THUMBSTICK_ID::kLeft :
            RE::ThumbstickEvent::THUMBSTICK_ID::kRight;
        const auto categoryStick = favoriteStick == RE::ThumbstickEvent::THUMBSTICK_ID::kLeft ?
            RE::ThumbstickEvent::THUMBSTICK_ID::kRight :
            RE::ThumbstickEvent::THUMBSTICK_ID::kLeft;

        if (!ConfigUI::g_Settings.showFavoriteRing &&
            (a_event->idCode == favoriteStick || a_event->idCode == categoryStick)) {
            constexpr float kCategoryStickDeadZone = 0.4f;
            if (std::abs(a_event->xValue) < kCategoryStickDeadZone &&
                std::abs(a_event->yValue) < kCategoryStickDeadZone) {
                return;
            }

            float angleDeg = std::atan2(a_event->yValue, a_event->xValue) * (180.0f / 3.14159265f);
            WheelMenu::ActiveMenu->HandleRightStickInput(angleDeg);
            const_cast<RE::ThumbstickEvent*>(a_event)->handled = RE::InputEvent::HANDLED_RESULT::kStop;
            return;
        }

        // 收藏环角度导航
        if (a_event->idCode == favoriteStick) {
            constexpr float kFavoriteStickDeadZone = 0.3f;
            if (std::abs(a_event->xValue) < kFavoriteStickDeadZone &&
                std::abs(a_event->yValue) < kFavoriteStickDeadZone) {
                return;
            }

            float angleDeg = std::atan2(a_event->yValue, a_event->xValue) * (180.0f / 3.14159265f);
            WheelMenu::ActiveMenu->HandleCenterRingInput(angleDeg);

            const_cast<RE::ThumbstickEvent*>(a_event)->handled = RE::InputEvent::HANDLED_RESULT::kStop;
        }
        // 主分类/深层导航：layer 0 = 扇区指向，深层 = up/down
        // 节流策略：C++ 不做节流，事件全部转发给 AS3，由 AS3 根据当前 layer 自适应：
        //   - Layer 0：每事件即时更新角度（MenuContainer 内部对同扇区去重）
        //   - Layer 1/2：AS3 内部按时间窗口节流，避免列表滚动过快
        else if (a_event->idCode == categoryStick) {
            constexpr float kCategoryStickDeadZone = 0.4f;

            if (std::abs(a_event->xValue) < kCategoryStickDeadZone &&
                std::abs(a_event->yValue) < kCategoryStickDeadZone) {
                return;
            }

            float angleDeg = std::atan2(a_event->yValue, a_event->xValue) * (180.0f / 3.14159265f);
            WheelMenu::ActiveMenu->HandleRightStickInput(angleDeg);

            const_cast<RE::ThumbstickEvent*>(a_event)->handled = RE::InputEvent::HANDLED_RESULT::kStop;
        }
    }

    void WheelInputHijacker::OnButtonEvent(const RE::ButtonEvent* a_event) {
        if (!a_event) return;

        auto& userEvent = a_event->QUserEvent();
        bool isDown = (a_event->value > 0.0f);
        uint32_t currentKeyCode = a_event->idCode;

        auto ui = RE::UI::GetSingleton();
        bool isOpen = ui && ui->GetMenuOpen(WheelMenu::MENU_NAME);

        // 🎮 按键状态追踪：必须在 RT/LT 检测之前，防止同一个扳机产生两个事件
        std::uint32_t keyHash = (static_cast<std::uint32_t>(a_event->device.get()) << 16) | currentKeyCode;
        static std::unordered_map<std::uint32_t, bool> s_keyState;
        bool wasDown = s_keyState[keyHash];
        s_keyState[keyHash] = isDown;
        bool justPressed = isDown && !wasDown;

        // 🎮 RT/LT 扳机检测：必须在粗过滤之前！不然扳机事件会被跳过
        // 使用 justPressed 防止原始事件和映射事件各触发一次（导致快速选择两个扇区）
        if (isOpen && justPressed) {
            auto bsCode = a_event->GetBSButtonCode();
            if (bsCode == RE::BS_BUTTON_CODE::kDPAD_Left) {
                if (WheelMenu::ActiveMenu) {
                    WheelMenu::ActiveMenu->HandleControllerInput("trigger_left");
                }
                const_cast<RE::ButtonEvent*>(a_event)->handled = RE::InputEvent::HANDLED_RESULT::kStop;
                return;
            }
            if (bsCode == RE::BS_BUTTON_CODE::kDPAD_Right) {
                if (WheelMenu::ActiveMenu) {
                    WheelMenu::ActiveMenu->HandleControllerInput("trigger_right");
                }
                const_cast<RE::ButtonEvent*>(a_event)->handled = RE::InputEvent::HANDLED_RESULT::kStop;
                return;
            }
            if (bsCode == RE::BS_BUTTON_CODE::kDPAD_Up) {
                if (WheelMenu::ActiveMenu) {
                    WheelMenu::ActiveMenu->HandleControllerInput("up");
                }
                const_cast<RE::ButtonEvent*>(a_event)->handled = RE::InputEvent::HANDLED_RESULT::kStop;
                return;
            }
            if (bsCode == RE::BS_BUTTON_CODE::kDPAD_Down) {
                if (WheelMenu::ActiveMenu) {
                    WheelMenu::ActiveMenu->HandleControllerInput("down");
                }
                const_cast<RE::ButtonEvent*>(a_event)->handled = RE::InputEvent::HANDLED_RESULT::kStop;
                return;
            }
            if (bsCode == RE::BS_BUTTON_CODE::kLTrigger) {
                // LT 现在是层深导航（发送 left 对应 AS3 的左=深层/右=浅层逻辑）
                if (WheelMenu::ActiveMenu) {
                    WheelMenu::ActiveMenu->HandleControllerInput("left");
                }
                const_cast<RE::ButtonEvent*>(a_event)->handled = RE::InputEvent::HANDLED_RESULT::kStop;
                return;
            }
            if (bsCode == RE::BS_BUTTON_CODE::kRTrigger) {
                // RT 现在是层深导航（发送 right 对应 AS3 的右=深层/左=浅层逻辑）
                if (WheelMenu::ActiveMenu) {
                    WheelMenu::ActiveMenu->HandleControllerInput("right");
                }
                const_cast<RE::ButtonEvent*>(a_event)->handled = RE::InputEvent::HANDLED_RESULT::kStop;
                return;
            }
            if (bsCode == RE::BS_BUTTON_CODE::kAButton || bsCode == RE::BS_BUTTON_CODE::kEnter) {
                // A 按钮 → 确认/使用（BS_BUTTON_CODE 比较绕过 userEvent 字符串过滤器）
                if (WheelMenu::ActiveMenu) {
                    WheelMenu::ActiveMenu->HandleControllerInput("accept");
                }
                const_cast<RE::ButtonEvent*>(a_event)->handled = RE::InputEvent::HANDLED_RESULT::kStop;
                return;
            }
            if (bsCode == RE::BS_BUTTON_CODE::kXButton) {
                // X 按钮 → 固定/取消固定当前主分类，复用鼠标右键的永久记忆固定逻辑
                if (WheelMenu::ActiveMenu) {
                    WheelMenu::ActiveMenu->HandleControllerInput("pin_category");
                }
                const_cast<RE::ButtonEvent*>(a_event)->handled = RE::InputEvent::HANDLED_RESULT::kStop;
                return;
            }
        }

        const bool isNativeFavoritesKey = (userEvent == "Quickkeys" || userEvent == "Favorites");
        const bool isQuickSlotKey = (userEvent == "QuickkeyUp" || userEvent == "QuickkeyDown" ||
            userEvent == "QuickkeyLeft" || userEvent == "QuickkeyRight");
        const bool replacesFavorites = ConfigUI::g_Settings.activationMode == 0;
        const bool isIndependentKey = !replacesFavorites &&
            a_event->device == RE::INPUT_DEVICE::kKeyboard &&
            static_cast<std::uint32_t>(a_event->GetBSButtonCode()) == ConfigUI::g_Settings.wheelHotkey;
        const bool isWheelActivationKey = replacesFavorites ? (isNativeFavoritesKey || isQuickSlotKey) : isIndependentKey;
        const bool isOpenNavigationKey = isOpen && isQuickSlotKey;
        const bool suppressNativeFavoritesWhileOpen = isOpen && isNativeFavoritesKey;
        bool isCancelKey = (userEvent == "Cancel");
        bool isAcceptKey = (userEvent == "Activate" || userEvent == "Accept");

        static uint32_t s_closingKeyId = 0xFFFFFFFF;
        bool isHandlingCloseRelease = (!isDown && s_closingKeyId != 0xFFFFFFFF && currentKeyCode == s_closingKeyId);

        // 独立模式在菜单关闭时完全放行原版收藏菜单和快捷槽事件。
        if (!isWheelActivationKey && !isOpenNavigationKey && !suppressNativeFavoritesWhileOpen &&
            !(isCancelKey && isOpen) && !(isAcceptKey && isOpen) && !isHandlingCloseRelease) return;

        // 🎮 上下文放行（必须在任何 handled=kStop 之前！）：
        // 收藏环把十字键重映射成了它自己的功能，并会消费(kStop)十字键事件。
        // 但菜单关闭、且当前处于「需要十字键的游戏上下文」时（对话中 / 玩家死亡），
        // 绝不能消费这些事件——否则会连带吃掉对话选项的十字键选择
        // （玩家反馈「手柄无法用十字键选对话」的根因就在这里）。
        // 此时直接原样放行给游戏，不 kStop、不开环。
        // 注意：这段必须放在 D-Pad disabled 过滤(下方)和无条件 kStop 之前。
        if (replacesFavorites && !isOpen && !isHandlingCloseRelease) {
            auto guardPlayer = RE::PlayerCharacter::GetSingleton();
            auto guardAvManager = RE::ActorValue::GetSingleton();
            if (guardPlayer && guardAvManager && guardAvManager->health &&
                guardPlayer->GetActorValue(*(guardAvManager->health)) <= 0.0f) {
                return;  // 玩家死亡：放行
            }
            if (ui && ui->GetMenuOpen("DialogueMenu")) {
                return;  // 对话中：放行十字键给对话菜单
            }
        }

        // 🎮 D-Pad 过滤：菜单关闭时，对每个方向检查配置
        // 必须在 handled=kStop 之前，这样 disabled 的方向才能被正确拦截
        if (!isOpen && !isHandlingCloseRelease) {
            if (userEvent == "QuickkeyUp" && ConfigUI::g_Settings.dpadUpAction == 0) {
                const_cast<RE::ButtonEvent*>(a_event)->handled = RE::InputEvent::HANDLED_RESULT::kStop;
                return;
            }
            if (userEvent == "QuickkeyDown" && ConfigUI::g_Settings.dpadDownAction == 0) {
                const_cast<RE::ButtonEvent*>(a_event)->handled = RE::InputEvent::HANDLED_RESULT::kStop;
                return;
            }
            if (userEvent == "QuickkeyLeft" && ConfigUI::g_Settings.dpadLeftAction == 0) {
                const_cast<RE::ButtonEvent*>(a_event)->handled = RE::InputEvent::HANDLED_RESULT::kStop;
                return;
            }
            if (userEvent == "QuickkeyRight" && ConfigUI::g_Settings.dpadRightAction == 0) {
                const_cast<RE::ButtonEvent*>(a_event)->handled = RE::InputEvent::HANDLED_RESULT::kStop;
                return;
            }
        }

        const_cast<RE::ButtonEvent*>(a_event)->handled = RE::InputEvent::HANDLED_RESULT::kStop;

        // 🎮 手柄方向键：菜单打开时转发到 AS3 深度导航状态机
        if (isOpen && isDown && justPressed) {
            const char* dirAction = nullptr;
            // D-Pad 左/右现在是切换分区（发送 trigger_left/trigger_right）
            if (userEvent == "QuickkeyLeft") dirAction = "trigger_left";
            else if (userEvent == "QuickkeyRight") dirAction = "trigger_right";
            else if (userEvent == "QuickkeyUp") dirAction = "up";
            else if (userEvent == "QuickkeyDown") dirAction = "down";
            if (dirAction) {
                if (WheelMenu::ActiveMenu) {
                    WheelMenu::ActiveMenu->HandleControllerInput(dirAction);
                }
                return;
            }
        }

        if (isAcceptKey && isOpen && isDown && justPressed) {
            if (WheelMenu::ActiveMenu) {
                WheelMenu::ActiveMenu->HandleControllerInput("accept");
            }
            return;
        }

        if (isHandlingCloseRelease) {
            s_closingKeyId = 0xFFFFFFFF;
            return;
        }

        if (isCancelKey && isOpen && isDown && justPressed) {
            s_closingKeyId = currentKeyCode;
            WheelMenu::DesiredState = false;
            WheelMenu::Toggle(false);
            return;
        }

        if (isWheelActivationKey) {
            if (!isOpen) {
                auto player = RE::PlayerCharacter::GetSingleton();
                auto avManager = RE::ActorValue::GetSingleton();
                if (player && avManager && avManager->health) {
                    if (player->GetActorValue(*(avManager->health)) <= 0.0f) return;
                }
                if (ui && ui->GetMenuOpen("DialogueMenu")) return;
            }

            if (ConfigUI::g_Settings.toggleMode) {
                if (justPressed) {
                    bool targetState = !isOpen;
                    if (targetState) {
                        s_openStartTime = std::chrono::high_resolution_clock::now();
                        LogPerf("【节点 1】物理快捷键按下！发送菜单开启指令...");
                    }
                    WheelMenu::DesiredState = targetState;
                    WheelMenu::Toggle(targetState);
                }
            }
            else {
                if (isDown != WheelMenu::DesiredState) {
                    if (isDown && !isOpen) {
                        s_openStartTime = std::chrono::high_resolution_clock::now();
                        LogPerf("【节点 1】物理快捷键按下(按住模式)！发送菜单开启指令...");
                    }
                    WheelMenu::DesiredState = isDown;

                    if (!isDown) {
                        if (WheelMenu::ActiveMenu && WheelMenu::s_hoveredUID != 0) {
                            WheelMenu::ActiveMenu->OnWheelMenuSelect(WheelMenu::s_hoveredUID, true);
                            WheelMenu::s_hoveredUID = 0;
                        }
                        else {
                            WheelMenu::Toggle(false);
                        }
                    }
                    else {
                        WheelMenu::Toggle(true);
                    }
                }
            }
        }
    }

    void WheelMenu::SetupInputHijack() {
        std::thread([]() {
            RE::MenuControls* mc = nullptr;
            while (!(mc = RE::MenuControls::GetSingleton())) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            mc->handlers.insert(mc->handlers.begin(), WheelInputHijacker::GetSingleton());
            REX::INFO("WheelMenu: Global input handler installed; Favorites replacement is configuration-driven.");
            }).detach();
    }

    static float s_equipResumeTimer = 0.0f;

    void WheelMenu::AdvanceMovie(float a_interval, std::uint64_t a_currentTime) {
        RE::GameMenuBase::AdvanceMovie(a_interval, a_currentTime);
        static auto s_lastTime = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        float delta = std::chrono::duration<float>(now - s_lastTime).count();
        s_lastTime = now;

        RefreshCombatScopedBulletTime();

        if (s_equipResumeTimer > 0.0f) {
            s_equipResumeTimer -= delta;
            if (s_equipResumeTimer <= 0.0f) {
                if (ConfigUI::g_Settings.timeMode == 1) {
                    ApplyMenuTimeMode();
                }
            }
        }

        static float s_renewTimer = 0.0f;
        if (ConfigUI::g_Settings.autoRenewFilters) {
            s_renewTimer += delta;
            if (s_renewTimer >= ConfigUI::g_Settings.renewInterval) {
                s_renewTimer = 0.0f;
                TriggerMenuFilters(false);
            }
        }
        else { s_renewTimer = 0.0f; }

        if (uiMovie) {
            Scaleform::GFx::Value root1;
            if (uiMovie->GetVariable(&root1, "root1")) {
                if (!_isInitTriggered) {
                    _isInitTriggered = true;
                    Scaleform::GFx::Value f4seObj, sendFunc, dirFunc, allowFunc, playSoundFunc;
                    uiMovie->CreateObject(&f4seObj);

                    static F4SEEventHook s_hook;
                    uiMovie->CreateFunction(&sendFunc, &s_hook);
                    f4seObj.SetMember("SendExternalEvent", sendFunc);

                    static F4SEDirListingHook s_dirHook;
                    uiMovie->CreateFunction(&dirFunc, &s_dirHook);
                    f4seObj.SetMember("GetDirectoryListing", dirFunc);

                    static F4SEAllowTextInputHook s_allowHook;
                    uiMovie->CreateFunction(&allowFunc, &s_allowHook);
                    f4seObj.SetMember("AllowTextInput", allowFunc);

                    static F4SEPlaySoundHook s_playSoundHook;
                    uiMovie->CreateFunction(&playSoundFunc, &s_playSoundHook);
                    f4seObj.SetMember("PlaySound", playSoundFunc);

                    static F4SEUpdatePinHook s_pinHook;
                    Scaleform::GFx::Value pinFunc;
                    uiMovie->CreateFunction(&pinFunc, &s_pinHook);
                    f4seObj.SetMember("UpdatePinnedState", pinFunc);

                    Scaleform::GFx::Value args[1] = { f4seObj };
                    root1.Invoke("onF4SEObjCreated", nullptr, args, 1);
                }
            }
        }
    }

    void WheelMenu::OnWheelMenuInit() {
        if (!uiMovie) return;

        LogPerf("【节点 5】开始遍历玩家背包并打包数据 (PushInventoryData)");
        PushInventoryData();
        LogPerf("【节点 6】背包数据打包完毕，开始发送最后排版指令...");

        Scaleform::GFx::Value root1;
        if (!uiMovie->GetVariable(&root1, "root1")) return;

        Scaleform::GFx::Value menuPos, posStrct, posData;
        uiMovie->CreateObject(&menuPos); uiMovie->CreateObject(&posStrct); uiMovie->CreateObject(&posData);
        posData.SetMember("offsetX", Scaleform::GFx::Value(ConfigUI::g_Settings.menuOffsetX));
        posData.SetMember("offsetY", Scaleform::GFx::Value(ConfigUI::g_Settings.menuOffsetY));
        posData.SetMember("scaling", Scaleform::GFx::Value(ConfigUI::g_Settings.menuScaling));
        posStrct.SetMember("__data__", posData);
        menuPos.SetMember("__struct__", posStrct);
        root1.SetMember("menuPos", menuPos);

        Scaleform::GFx::Value argsPin[4];
        argsPin[0] = Scaleform::GFx::Value(ConfigUI::g_Settings.pinnedLeft.c_str());
        argsPin[1] = Scaleform::GFx::Value(ConfigUI::g_Settings.pinnedRight.c_str());
        argsPin[2] = Scaleform::GFx::Value(ConfigUI::g_Settings.pinnedLeftFilter.c_str());
        argsPin[3] = Scaleform::GFx::Value(ConfigUI::g_Settings.pinnedRightFilter.c_str());
        root1.Invoke("syncPinnedStates", nullptr, argsPin, 4);

        Scaleform::GFx::Value args[17];
        args[0] = static_cast<double>(ConfigUI::g_Settings.coreRadius);
        args[1] = static_cast<double>(ConfigUI::g_Settings.favSlotSize);
        args[2] = static_cast<double>(ConfigUI::g_Settings.layerGap);
        args[3] = static_cast<double>(ConfigUI::g_Settings.tickLength);
        args[4] = static_cast<bool>(ConfigUI::g_Settings.showSectorsDivider);
        args[5] = static_cast<bool>(ConfigUI::g_Settings.hideEmptyCategories); // 新增参数
        args[6] = static_cast<bool>(ConfigUI::g_Settings.favoriteCloseOnUse);
        args[7] = static_cast<double>(ConfigUI::g_Settings.favoriteSlotOrder);
        args[8] = static_cast<bool>(ConfigUI::g_Settings.submenuOpenOnHover);
        args[9] = static_cast<double>(ConfigUI::g_Settings.gamepadCenterStick);
        args[10] = static_cast<bool>(ConfigUI::g_Settings.showCloseBadges);
        args[11] = static_cast<bool>(ConfigUI::g_Settings.showHintBar);
        args[12] = static_cast<double>(ConfigUI::g_Settings.categoryRingThickness);
        args[13] = static_cast<bool>(ConfigUI::g_Settings.showFavoriteRing);
        args[14] = static_cast<bool>(ConfigUI::g_Settings.showCenterName);
        args[15] = static_cast<double>(ConfigUI::g_Settings.centerNameFontSize);
        args[16] = static_cast<bool>(ConfigUI::g_Settings.showConditionBars);

        root1.Invoke("updateLiveLayout", nullptr, args, 17);

        Scaleform::GFx::Value listArgs[6];
        listArgs[0] = static_cast<double>(ConfigUI::g_Settings.leftListOffsetX);
        listArgs[1] = static_cast<double>(ConfigUI::g_Settings.leftListOffsetY);
        listArgs[2] = static_cast<double>(ConfigUI::g_Settings.leftListScale);
        listArgs[3] = static_cast<double>(ConfigUI::g_Settings.rightListOffsetX);
        listArgs[4] = static_cast<double>(ConfigUI::g_Settings.rightListOffsetY);
        listArgs[5] = static_cast<double>(ConfigUI::g_Settings.rightListScale);
        root1.Invoke("updateLiveListLayout", nullptr, listArgs, 6);

        LogPerf("【节点 7】所有指令发送完毕！AS3 正式开始最终绘制！");
    }

    void WheelMenu::PushInventoryData() {
        Scaleform::GFx::Value root1;
        if (!uiMovie->GetVariable(&root1, "root1")) return;
        auto player = RE::PlayerCharacter::GetSingleton();
        if (player && player->inventoryList) {
            Scaleform::GFx::Value invArray;
            uiMovie->CreateArray(&invArray);
            _itemMap.clear();
            std::uint32_t uidCounter = 1, arrayIdx = 0, equippedPrimaryUID = 0, equippedGrenadeUID = 0;

            struct AggregatedItem {
                RE::TESBoundObject* obj; RE::TBO_InstanceData* instance; std::string displayName;
                std::uint32_t totalCount; bool isAnyEquipped; std::uint32_t firstStackID;
                std::int32_t favSlot;
                float condition; // 新增：耐久度或电量百分比
            };
            std::map<std::pair<RE::TESBoundObject*, RE::TBO_InstanceData*>, AggregatedItem> groupMap;

            for (auto& item : player->inventoryList->data) {
                if (!item.object) continue;

                if (ItemClassifier::IsItemAllowed(item.object)) {
                    std::uint32_t stackID = 0;
                    auto* currentStack = item.stackData.get();
                    while (currentStack) {
                        auto* instData = item.GetInstanceData(stackID);

                        std::int32_t slot = -1;
                        float condition = -1.0f; // 默认 -1 代表无耐久

                        if (currentStack->extra) {
                            auto extraData = currentStack->extra->GetByType(RE::EXTRA_DATA_TYPE::kFavorite);
                            if (extraData) {
                                auto extraFav = static_cast<RE::ExtraFavorite*>(extraData);
                                slot = extraFav->quickkeyIndex;
                            }

                            // Fusion cores and other charged items expose ExtraCharge directly.
                            auto extraCharge = static_cast<RE::ExtraCharge*>(currentStack->extra->GetByType(RE::EXTRA_DATA_TYPE::kCharge));
                            if (extraCharge) {
                                condition = extraCharge->charge / 100.0f;
                                if (condition > 1.0f) condition = 1.0f;
                            }
                            else {
                                float realDurability = ConditionSystem::DurabilityAPI::GetStackHealthPercent(item.object, currentStack);
                                if (realDurability >= 0.0f) {
                                    condition = realDurability;
                                    if (condition > 10.0f) condition = 10.0f;
                                    if (condition < 0.0f) condition = 0.0f;
                                }
                            }
                        }

                        auto key = std::make_pair(item.object, instData);
                        if (groupMap.find(key) == groupMap.end()) {
                            const char* name = item.GetDisplayFullName(stackID);
                            groupMap[key] = { item.object, instData, name ? name : "", currentStack->GetCount(), currentStack->IsEquipped(), stackID, slot, condition };
                        }
                        else {
                            groupMap[key].totalCount += currentStack->GetCount();
                            if (currentStack->IsEquipped()) groupMap[key].isAnyEquipped = true;
                            if (slot != -1) groupMap[key].favSlot = slot;
                            if (condition >= 0.0f) groupMap[key].condition = condition; // 更新最新耐久
                        }
                        currentStack = currentStack->nextStack.get(); stackID++;
                    }
                }
            }

            for (auto const& [key, agg] : groupMap) {
                std::uint32_t currentUID = uidCounter++;
                _itemMap[currentUID] = { agg.obj->formID, agg.firstStackID };
                Scaleform::GFx::Value var, strct, data, itemObj;
                uiMovie->CreateObject(&itemObj); uiMovie->CreateObject(&var); uiMovie->CreateObject(&strct); uiMovie->CreateObject(&data);

                const char* finalCategory = ItemClassifier::GetItemCategory(agg.obj, agg.instance);

                const char* mainCat = "unknown";
                switch (agg.obj->GetFormType()) {
                case RE::ENUM_FORM_ID::kWEAP: mainCat = "weapons"; break;
                case RE::ENUM_FORM_ID::kARMO: mainCat = "apparel"; break;
                case RE::ENUM_FORM_ID::kALCH: mainCat = "aid"; break;
                case RE::ENUM_FORM_ID::kAMMO: mainCat = "ammo"; break;
                case RE::ENUM_FORM_ID::kMISC: mainCat = "misc"; break;
                default: break;
                }

                if (agg.isAnyEquipped) {
                    if (strcmp(finalCategory, ItemClassifier::Categories::WEAPONS_THROWN) == 0) equippedGrenadeUID = currentUID;
                    else if (agg.obj->GetFormType() == RE::ENUM_FORM_ID::kWEAP) equippedPrimaryUID = currentUID;
                }

                data.SetMember("id", Scaleform::GFx::Value(static_cast<double>(currentUID)));
                data.SetMember("Name", Scaleform::GFx::Value(agg.displayName.c_str()));
                data.SetMember("Description", Scaleform::GFx::Value(""));
                data.SetMember("count", Scaleform::GFx::Value(static_cast<double>(agg.totalCount)));
                data.SetMember("Equipped", Scaleform::GFx::Value(agg.isAnyEquipped));

                std::string ammoNameStr = "";
                uint32_t ammoCount = 0;

                if (agg.obj->GetFormType() == RE::ENUM_FORM_ID::kWEAP) {
                    auto weap = agg.obj->As<RE::TESObjectWEAP>();
                    if (weap) {
                        RE::TESAmmo* ammo = nullptr;
                        if (agg.instance) {
                            auto weapInst = static_cast<RE::TESObjectWEAP::InstanceData*>(agg.instance);
                            ammo = weapInst->ammo;
                        }
                        if (!ammo) ammo = weap->weaponData.ammo;

                        if (ammo) {
                            ammoNameStr = ammo->GetFullName();
                            player->GetItemCount(ammoCount, ammo, false);
                        }
                    }
                }

                data.SetMember("AmmoName", Scaleform::GFx::Value(ammoNameStr.c_str()));
                data.SetMember("AmmoCount", Scaleform::GFx::Value(static_cast<double>(ammoCount)));

                data.SetMember("MainCategory", Scaleform::GFx::Value(mainCat));
                data.SetMember("SubCategory", Scaleform::GFx::Value(finalCategory));

                data.SetMember("Category", Scaleform::GFx::Value(finalCategory));
                data.SetMember("FavoriteSlot", Scaleform::GFx::Value(static_cast<double>(agg.favSlot)));
                data.SetMember("Condition", Scaleform::GFx::Value(static_cast<double>(agg.condition))); // 发送耐久数据

                strct.SetMember("__data__", data); var.SetMember("__struct__", strct); itemObj.SetMember("__var__", var);
                invArray.SetElement(arrayIdx++, itemObj);
            }
            root1.SetMember("inventoryItems", invArray);

            // 修复：发送数组而不是单个值，避免手雷被主武器覆盖
            Scaleform::GFx::Value equippedIds;
            uiMovie->CreateArray(&equippedIds);
            int eqIdx = 0;
            if (equippedGrenadeUID != 0) equippedIds.SetElement(eqIdx++, Scaleform::GFx::Value(static_cast<double>(equippedGrenadeUID)));
            if (equippedPrimaryUID != 0) equippedIds.SetElement(eqIdx++, Scaleform::GFx::Value(static_cast<double>(equippedPrimaryUID)));
            root1.SetMember("equippedItemIds", equippedIds);
        }
    }

    void WheelMenu::OnWheelMenuSelect(std::uint32_t a_uid, bool a_close) {
        auto it = _itemMap.find(a_uid);
        if (it != _itemMap.end()) {
            std::uint32_t formID = it->second.first, stackID = it->second.second;
            auto player = RE::PlayerCharacter::GetSingleton();
            auto form = RE::TESForm::GetFormByID(formID);
            auto equipMgr = RE::ActorEquipManager::GetSingleton();
            const bool selectedSlowTimeItem = HasSlowTimeEffect(form);

            LogAidAnimState("select_begin", player, form, a_uid, stackID, a_close, selectedSlowTimeItem);

            // 🔧 如果使用后菜单将关闭，先恢复 gameplay handler
            // 这样物品使用可能触发的 HUD 动画能正常使用鼠标
            if (a_close) {
                SetGameplayHandlersEnabled(true);
            }

            if (selectedSlowTimeItem && a_close) {
                LogAidAnimState("slow_time_close_before_equip", player, form, a_uid, stackID, a_close, selectedSlowTimeItem);
                RestoreGameplayTimeDuringMenu();
                WheelMenu::DesiredState = false;
                Toggle(false);
            } else if (selectedSlowTimeItem) {
                LogAidAnimState("slow_time_keep_open_before_equip", player, form, a_uid, stackID, a_close, selectedSlowTimeItem);
                RestoreGameplayTimeDuringMenu();
                ReleaseDynamicPause();
            }
            if (player && form && equipMgr) {
                auto boundObj = form->As<RE::TESBoundObject>();
                if (boundObj) {
                    RE::TBO_InstanceData* instData = nullptr;
                    if (player->inventoryList) {
                        for (auto& item : player->inventoryList->data) {
                            if (item.object && item.object->formID == formID) {
                                instData = item.GetInstanceData(stackID); break;
                            }
                        }
                    }
                    RE::BGSObjectInstance objInst{ boundObj, instData };
                    const bool keepOpenAlchemy = !a_close && form->As<RE::AlchemyItem>();
                    if (keepOpenAlchemy) {
                        LogAidAnimState("before_equip_keep_open", player, form, a_uid, stackID, a_close, selectedSlowTimeItem);
                        RestoreGameplayTimeDuringMenu();
                        ReleaseDynamicPause();
                        s_suppressNextKeepOpenAlchemyHide = true;
                        s_ignoreNativeHideUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(900);
                        equipMgr->EquipObject(player, objInst, stackID, 1, nullptr, false, false, false, false, false);
                    } else {
                        LogAidAnimState("before_equip", player, form, a_uid, stackID, a_close, selectedSlowTimeItem);
                        equipMgr->EquipObject(player, objInst, stackID, 1, nullptr, false, false, false, false, false);
                    }
                    LogAidAnimState("after_equip", player, form, a_uid, stackID, a_close, selectedSlowTimeItem);
                }
                else {
                    LogAidAnimState("skip_no_bound_object", player, form, a_uid, stackID, a_close, selectedSlowTimeItem);
                }
            }
            if (selectedSlowTimeItem && a_close) {
                LogAidAnimState("slow_time_return_after_equip", player, form, a_uid, stackID, a_close, selectedSlowTimeItem);
                return;
            }
            if (!a_close && ConfigUI::g_Settings.timeMode == 1 && !selectedSlowTimeItem) {
                RestoreGameplayTimeDuringMenu();
                s_equipResumeTimer = 0.5f;
            }
        }
        if (a_close) {
            auto player = RE::PlayerCharacter::GetSingleton();
            LogAidAnimState("close_after_select", player, nullptr, a_uid, 0, a_close, false);
            WheelMenu::DesiredState = false;
            Toggle(false);
        }
    }

    void WheelMenu::OnWheelMenuClose() {
        auto player = RE::PlayerCharacter::GetSingleton();
        LogAidAnimState("as_close_request", player, nullptr, 0, 0, true, false);
        WheelMenu::DesiredState = false; Toggle(false);
    }

    void WheelMenu::HandleControllerInput(const char* a_action) {
        if (!uiMovie || !a_action || !a_action[0]) return;

        Scaleform::GFx::Value root1;
        if (!uiMovie->GetVariable(&root1, "root1")) return;

        Scaleform::GFx::Value args[1];
        args[0] = Scaleform::GFx::Value(a_action);
        root1.Invoke("handleControllerInput", nullptr, args, 1);
    }

    void WheelMenu::HandleCenterRingInput(float a_angleDeg) {
        if (!uiMovie) return;

        Scaleform::GFx::Value root1;
        if (!uiMovie->GetVariable(&root1, "root1")) return;

        Scaleform::GFx::Value angleVal(a_angleDeg);
        root1.Invoke("handleCenterRingInput", nullptr, &angleVal, 1);
    }

    void WheelMenu::HandleRightStickInput(float a_angleDeg) {
        if (!uiMovie) return;

        Scaleform::GFx::Value root1;
        if (!uiMovie->GetVariable(&root1, "root1")) return;

        Scaleform::GFx::Value angleVal(a_angleDeg);
        root1.Invoke("handleRightStickInput", nullptr, &angleVal, 1);
    }

    RE::UI_MESSAGE_RESULTS WheelMenu::ProcessMessage(RE::UIMessage& a_message) {
        if (a_message.type == RE::UI_MESSAGE_TYPE::kHide) {
            auto player = RE::PlayerCharacter::GetSingleton();
            LogAidAnimState("process_hide", player, nullptr, 0, 0, !WheelMenu::DesiredState, false);
            if (WheelMenu::DesiredState &&
                s_suppressNextKeepOpenAlchemyHide &&
                std::chrono::steady_clock::now() < s_ignoreNativeHideUntil) {
                s_suppressNextKeepOpenAlchemyHide = false;
                s_ignoreNativeHideUntil = {};
                LogAidAnimState("process_hide_keep_open_suppressed", player, nullptr, 0, 0, false, false);
                return RE::UI_MESSAGE_RESULTS::kIgnore;
            }
            s_suppressNextKeepOpenAlchemyHide = false;
            s_ignoreNativeHideUntil = {};
            if (WheelMenu::DesiredState) {
                LogAidAnimState("process_hide_external_forced_cleanup", player, nullptr, 0, 0, true, false);
                WheelMenu::DesiredState = false;
                CleanupMenuRuntimeState();
            }
        }
        if (a_message.type == RE::UI_MESSAGE_TYPE::kShow) {
            auto player = RE::PlayerCharacter::GetSingleton();
            LogAidAnimState("process_show", player, nullptr, 0, 0, false, false);
            if (!WheelMenu::DesiredState) {
                LogAidAnimState("process_show_blocked_desired_closed", player, nullptr, 0, 0, false, false);
                return RE::UI_MESSAGE_RESULTS::kHandled;
            }
        }
        return RE::GameMenuBase::ProcessMessage(a_message);
    }

    void WheelMenu::Toggle(bool a_show) {
        auto ui = RE::UI::GetSingleton();
        if (!ui) return;

        if (a_show) {
            auto player = RE::PlayerCharacter::GetSingleton();
            LogAidAnimState("toggle_show_request", player, nullptr, 0, 0, false, false);
            if (!IsGameplayHUDOnly()) {
                return;
            }

            if (IsPlayerAimingDownSights()) {
                return;
            }

            ApplyMenuTimeMode();

            TriggerMenuFilters(true);
            RE::UIMessageQueue::GetSingleton()->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kShow);
        }
        else {
            auto player = RE::PlayerCharacter::GetSingleton();
            LogAidAnimState("toggle_hide_request", player, nullptr, 0, 0, true, false);
            CleanupMenuRuntimeState();

            StopMenuFilterInstances();
            RE::UIMessageQueue::GetSingleton()->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kHide);
        }
    }

    RE::IMenu* WheelMenu::CreateMenu(const RE::UIMessage&) {
        LogPerf("【节点 2】游戏底层开始创建菜单载体...");
        auto menu = new WheelMenu();
        auto scaleformMgr = RE::BSScaleformManager::GetSingleton();
        if (scaleformMgr) {
            LogPerf("【节点 3】准备从硬盘读取并加载 WheelMenu.swf...");
            if (auto translator = scaleformMgr->GetTranslator()) {
                const auto languageSetting = RE::GetINISetting("sLanguage:General");
                const auto language = languageSetting ? languageSetting->GetString() : "EN";
                REX::INFO("[Fallout4WheelMenu] Registering Scaleform translations: WheelMenu_{}.txt", language);
                translator->AddTranslationsMod("WheelMenu");
            }
            scaleformMgr->LoadMovieEx(*menu, SWF_PATH, "root1");
        }

        if (!WheelMenu::DesiredState) {
            RE::UIMessageQueue::GetSingleton()->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kHide);
        }
        return menu;
    }

    void WheelMenu::Register() {
        auto ui = RE::UI::GetSingleton();
        if (ui) {
            RE::BSAutoWriteLock l{ RE::UI::GetMenuMapRWLock() };
            ui->RegisterMenu(MENU_NAME, CreateMenu);
        }
    }

    void WheelMenu::ForceReloadAS3Config() {
        if (ActiveMenu && ActiveMenu->uiMovie) {
            Scaleform::GFx::Value root1;
            if (ActiveMenu->uiMovie->GetVariable(&root1, "root1")) {
                root1.Invoke("forceReloadConfig", nullptr, nullptr, 0);
            }
        }
    }

    void WheelMenu::UpdateLiveLayout(float coreRadius, float slotSize, float gap, float tick) {
        if (ActiveMenu && ActiveMenu->uiMovie) {
            Scaleform::GFx::Value root1;
            if (ActiveMenu->uiMovie->GetVariable(&root1, "root1")) {
                Scaleform::GFx::Value args[17];
                args[0] = static_cast<double>(coreRadius);
                args[1] = static_cast<double>(slotSize);
                args[2] = static_cast<double>(gap);
                args[3] = static_cast<double>(tick);
                args[4] = static_cast<bool>(ConfigUI::g_Settings.showSectorsDivider);
                args[5] = static_cast<bool>(ConfigUI::g_Settings.hideEmptyCategories); // 新增参数
                args[6] = static_cast<bool>(ConfigUI::g_Settings.favoriteCloseOnUse);
                args[7] = static_cast<double>(ConfigUI::g_Settings.favoriteSlotOrder);
                args[8] = static_cast<bool>(ConfigUI::g_Settings.submenuOpenOnHover);
                args[9] = static_cast<double>(ConfigUI::g_Settings.gamepadCenterStick);
                args[10] = static_cast<bool>(ConfigUI::g_Settings.showCloseBadges);
                args[11] = static_cast<bool>(ConfigUI::g_Settings.showHintBar);
                args[12] = static_cast<double>(ConfigUI::g_Settings.categoryRingThickness);
                args[13] = static_cast<bool>(ConfigUI::g_Settings.showFavoriteRing);
                args[14] = static_cast<bool>(ConfigUI::g_Settings.showCenterName);
                args[15] = static_cast<double>(ConfigUI::g_Settings.centerNameFontSize);
                args[16] = static_cast<bool>(ConfigUI::g_Settings.showConditionBars);

                root1.Invoke("updateLiveLayout", nullptr, args, 17);
            }
        }
    }

    void WheelMenu::UpdateLiveTransform(float offsetX, float offsetY, float scaling) {
        if (ActiveMenu && ActiveMenu->uiMovie) {
            Scaleform::GFx::Value root1;
            if (ActiveMenu->uiMovie->GetVariable(&root1, "root1")) {
                Scaleform::GFx::Value args[3];
                args[0] = static_cast<double>(offsetX);
                args[1] = static_cast<double>(offsetY);
                args[2] = static_cast<double>(scaling);
                root1.Invoke("updateLiveTransform", nullptr, args, 3);
            }
        }
    }

    void WheelMenu::UpdateLiveListLayout(float leftOffsetX, float leftOffsetY, float leftScale,
        float rightOffsetX, float rightOffsetY, float rightScale) {
        if (ActiveMenu && ActiveMenu->uiMovie) {
            Scaleform::GFx::Value root1;
            if (ActiveMenu->uiMovie->GetVariable(&root1, "root1")) {
                Scaleform::GFx::Value args[6];
                args[0] = static_cast<double>(leftOffsetX);
                args[1] = static_cast<double>(leftOffsetY);
                args[2] = static_cast<double>(leftScale);
                args[3] = static_cast<double>(rightOffsetX);
                args[4] = static_cast<double>(rightOffsetY);
                args[5] = static_cast<double>(rightScale);
                root1.Invoke("updateLiveListLayout", nullptr, args, 6);
            }
        }
    }

    static void ToggleNative(std::monostate) {
        bool target = !WheelMenu::DesiredState;
        if (target) {
            s_openStartTime = std::chrono::high_resolution_clock::now();
            LogPerf("【节点 1】Papyrus 脚本调用！发送菜单开启指令...");
        }
        WheelMenu::DesiredState = target;
        WheelMenu::Toggle(WheelMenu::DesiredState);
    }

    bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm) {
        a_vm->BindNativeMethod("F4WheelMenuNative", "Toggle", ToggleNative); return true;
    }
}
