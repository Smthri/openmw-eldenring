#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_GUARD_H
#define OPENMW_COMPONENTS_ELDENCOMBAT_GUARD_H

#include "combat.hpp"

#include <algorithm>
#include <cmath>

namespace EldenCombat
{
    inline float guardStaminaDamage(float healthDamage, float guardMultiplier)
    {
        // Native health damage is not a normalized stamina value. In particular,
        // low-damage Morrowind weapons used to cost only 2.75% of the bar to
        // block, recovered almost immediately. The neighboring simulation's
        // light incoming strike uses 30 normalized stamina damage independently
        // of health damage. Retain stronger native impacts and source guard MVs.
        const float damage = std::isfinite(healthDamage) ? std::max(0.f, healthDamage) : 0.f;
        const float multiplier = std::isfinite(guardMultiplier) ? std::max(0.f, guardMultiplier) : 1.f;
        return std::max(5.f, std::max(30.f, damage) * multiplier);
    }

    struct GuardAnimation
    {
        std::string mLeftArm;
        std::string mRightArm;

        explicit operator bool() const { return !mLeftArm.empty(); }
    };

    inline GuardAnimation guardAnimation(WeaponClass weapon, bool twoHanded, Action action, bool guarding)
    {
        // GuardHit deliberately lowers the controller's defensive flag during
        // recoil. The generic recoil clip still needs its equipment's guard
        // arm layer, as in the source recreation; otherwise every blocked hit
        // visually drops the shield/weapon instead of recoiling behind it.
        if (action != Action::GuardHit && !((action == Action::Idle || action == Action::GuardThrust) && guarding))
            return {};
        const std::string clip = stanceAnimation(twoHanded ? weapon : WeaponClass::Shield, twoHanded, 100);
        return { clip, twoHanded ? clip : std::string{} };
    }
}

#endif
