#pragma once
#include "pch.h"
#include "ItemClassifier.h"
#include <variant>
#include <unordered_map>
#include <map>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <Scaleform/Scaleform.h>
#include <Scaleform/G/GFx_Value.h>
#include <Scaleform/G/GFx_Movie.h>
#include <Scaleform/G/GFx_FunctionHandler.h>

namespace WheelMenuNative {
    extern HWND g_GameHWnd;

    // Blocks the vanilla Favorites menu when replacement mode is active.
    class WheelInputHijacker : public RE::BSInputEventUser {
    public:
        static WheelInputHijacker* GetSingleton() {
            static WheelInputHijacker singleton;
            return &singleton;
        }
        virtual bool ShouldHandleEvent(const RE::InputEvent* a_event) override;
        virtual void OnButtonEvent(const RE::ButtonEvent* a_event) override;
        virtual void OnThumbstickEvent(const RE::ThumbstickEvent* a_event) override;
    };

    namespace Categories {
        inline const char* MELEE = "MeleeWeapons";
        inline const char* RANGED = "RangedWeapons";
        inline const char* EXPLOSIVE = "Explosives";
        inline const char* FOOD = "Food";
        inline const char* ALCOHOL = "Alcohol";
        inline const char* DRINK = "Drink";
        inline const char* CHEMS = "Chems";
        inline const char* INGESTIBLE = "Ingestible";
    }

    class WheelMenu : public RE::GameMenuBase {
    public:
        static constexpr auto MENU_NAME = "Fallout4WheelMenu";
        static constexpr auto SWF_PATH = "Interface\\WheelMenu.swf";

        static inline bool DesiredState = false; // 独立自治的状态锁！

        WheelMenu();
        virtual ~WheelMenu();

        static RE::IMenu* CreateMenu(const RE::UIMessage&);
        static void Register();
        static void SetupInputHijack();
        static void Toggle(bool a_show);
        static void SetDynamicPause(bool a_active);

        static void ForceReloadAS3Config();
        // Sends live layout geometry to the Scaleform UI.
        static void UpdateLiveLayout(float coreRadius, float slotSize, float gap, float tick);
        static void UpdateLiveTransform(float offsetX, float offsetY, float scaling);
        static void UpdateLiveListLayout(float leftOffsetX, float leftOffsetY, float leftScale,
            float rightOffsetX, float rightOffsetY, float rightScale);

        static inline WheelMenu* ActiveMenu = nullptr;
        // Tracks the inventory item currently highlighted by the UI.
        static inline std::uint32_t s_hoveredUID = 0;

        void OnWheelMenuInit();
        void OnWheelMenuSelect(std::uint32_t a_uid, bool a_close);
        void OnWheelMenuClose();
        void HandleControllerInput(const char* a_action);
        void HandleCenterRingInput(float a_angleDeg);
        void HandleRightStickInput(float a_angleDeg);

        virtual RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& a_message) override;
        virtual void AdvanceMovie(float a_interval, std::uint64_t a_currentTime) override;

    private:
        void PushInventoryData();
        const char* GetItemCategory(RE::TESBoundObject* a_obj);

        bool _isInitTriggered = false;
        std::unordered_map<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>> _itemMap;
    };
    bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm);
}
