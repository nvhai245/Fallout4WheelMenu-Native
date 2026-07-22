#pragma once

#include <RE/Fallout.h>

#include <cstdint>

namespace ConditionSystem::DurabilityAPI
{
    // ConditionSystemFramework stores durability at one tenth of its displayed
    // percentage so the game does not apply its normal health behavior.
    constexpr float ENGINE_DEFAULT_HEALTH_THRESHOLD = 0.9f;
    constexpr float ARMOR_MIN_ENGINE_HEALTH = 0.0001f;
    constexpr float ARMOR_MIN_REAL_PERCENT = 0.001f;

    inline float DecompressHealth(float engineHealth)
    {
        return engineHealth * 10.0f;
    }

    inline float CompressHealth(float realPercent)
    {
        float result = realPercent * 0.1f;
        if (result >= ENGINE_DEFAULT_HEALTH_THRESHOLD) {
            result = ENGINE_DEFAULT_HEALTH_THRESHOLD - 0.0001f;
        }
        return result;
    }

    inline float GetItemHealthPercent(RE::TESBoundObject*, RE::ExtraDataList* extra)
    {
        if (!extra) {
            return -1.0f;
        }

        if (auto healthExtra = extra->GetByType<RE::ExtraHealth>()) {
            if (healthExtra->health >= ENGINE_DEFAULT_HEALTH_THRESHOLD) {
                return -1.0f;
            }
            return DecompressHealth(healthExtra->health);
        }

        if (auto chargeExtra = extra->GetByType<RE::ExtraCharge>()) {
            const float charge = chargeExtra->charge;
            return charge > 1.0f ? charge / 100.0f : charge;
        }

        return -1.0f;
    }

    inline float GetStackHealthPercent(RE::TESBoundObject* object, RE::BGSInventoryItem::Stack* stack)
    {
        if (!stack) {
            return -1.0f;
        }
        return GetItemHealthPercent(object, stack->extra.get());
    }

    using GetDurabilityPercentFn = float (*)(RE::TESBoundObject*, RE::BGSInventoryItem::Stack*);

    struct Interface
    {
        std::uint32_t version;
        GetDurabilityPercentFn GetDurabilityPercent;
    };

    constexpr std::uint32_t kMessage_RequestInterface = 'CDRI';
    constexpr std::uint32_t kMessage_ProvideInterface = 'CDRO';
}
