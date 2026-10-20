#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_GUARD_H
#define OPENMW_COMPONENTS_ELDENCOMBAT_GUARD_H

#include "combat.hpp"

namespace EldenCombat
{
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
