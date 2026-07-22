#include "pch.h"
#include "WheelMenu.h"
#include "ConfigUI.h"

// Compatibility shim for the Fallout 4 1.10.163 F4SE interface.
namespace OGSupport
{
    static F4SE::Impl::F4SEInterface RestoreLoadInterface;

    [[nodiscard]] inline static const char* F4SEAPI F4SEGetSaveFolderName() noexcept
    {
        return "Fallout4";
    }

    void Init(const F4SE::LoadInterface* a_f4se)
    {
        if (a_f4se->RuntimeVersion() <= F4SE::RUNTIME_1_10_163) {
            memcpy(&RestoreLoadInterface, a_f4se, 48);
            (((F4SE::Impl::F4SEInterface*)(&RestoreLoadInterface))->GetSaveFolderName) = F4SEGetSaveFolderName;
            F4SE::Init((const F4SE::LoadInterface*)(&RestoreLoadInterface));
        }
        else {
            F4SE::Init(a_f4se);
        }
    }
}

F4SE_EXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
    if (!a_f4se || !a_info) return false;
    a_info->infoVersion = F4SE::PluginInfo::kVersion;
    a_info->name = "Fallout4WheelMenu";
    a_info->version = 1;
    return true;
}

static bool Initialize(const F4SE::LoadInterface* a_f4se) {
    static std::once_flag once;
    std::call_once(once, [&]() {
        OGSupport::Init(a_f4se);
        REL::GetTrampoline().create(128);

        std::thread([]() {
            auto renderWindow = RE::BSGraphics::GetCurrentRendererWindow();
            while (!renderWindow || !renderWindow->swapChain) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                renderWindow = RE::BSGraphics::GetCurrentRendererWindow();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1500));

            WheelMenuNative::WheelMenu::Register();

            WheelMenuNative::WheelMenu::SetupInputHijack();

            ItemClassifier::LoadExclusions();

            if (ConfigUI::Install()) {
                REX::INFO("Fallout4WheelMenu: ImGui config center, input hook, and swap-chain hook installed.");
            }
            }).detach();
        });
    return true;
}

F4SE_EXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
    return Initialize(a_f4se);
}
