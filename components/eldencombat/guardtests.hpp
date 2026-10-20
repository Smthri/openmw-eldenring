#ifndef OPENMW_ELDENCOMBAT_GUARDTESTS_H
#define OPENMW_ELDENCOMBAT_GUARDTESTS_H

#include "guard.hpp"

#include <stdexcept>

namespace EldenCombat::Tests
{
    inline void guardRecoilAnimation()
    {
        const auto check = [](bool value, const char* message) {
            if (!value)
                throw std::runtime_error(message);
        };
        for (const auto weapon : { WeaponClass::StraightSword, WeaponClass::Greatsword,
                 WeaponClass::Spear, WeaponClass::Shield })
            for (const bool twoHanded : { false, true })
                for (const bool keepHeld : { false, true })
                {
                    Controller controller;
                    Config config;
                    config.mWeaponClass = weapon;
                    config.mHasShield = !twoHanded || weapon == WeaponClass::Shield;
                    controller.configure(config);
                    Input input;
                    input.mTwoHanded = twoHanded;
                    input.mGuard.mHeld = true;
                    controller.update(input, .2f);
                    check(controller.guardUp(), "guard animation fixture did not raise its guard");
                    const auto pose = [&] {
                        return guardAnimation(weapon, twoHanded, controller.action(), controller.guarding());
                    };
                    const auto held = pose();
                    check(static_cast<bool>(held), "held guard lacks its equipment pose");
                    check(held.mRightArm.empty() != twoHanded, "guard uses the wrong number of arms");
                    Incoming hit;
                    hit.mFacingDot = 1.f;
                    hit.mStaminaDamage = 1.f;
                    check(controller.receiveHit(hit) == HitResult::Blocked, "guard fixture failed to block");
                    check(controller.action() == Action::GuardHit && !controller.guarding(),
                        "guard recoil fixture retained active defense");
                    input.mGuard.mHeld = keepHeld;
                    controller.applyHitStop(.1f);
                    controller.update(input, .05f);
                    check(pose().mLeftArm == held.mLeftArm && pose().mRightArm == held.mRightArm,
                        "blocked impact/hit-stop dropped the shield or two-handed weapon pose");
                    for (int tick = 0; tick < 200 && controller.action() == Action::GuardHit; ++tick)
                    {
                        const auto recoil = pose();
                        check(recoil.mLeftArm == held.mLeftArm && recoil.mRightArm == held.mRightArm,
                            "releasing the button dropped guard before recoil completed");
                        controller.update(input, TickSeconds);
                    }
                    controller.update(input, TickSeconds);
                    check(controller.action() == Action::Idle && static_cast<bool>(pose()) == keepHeld,
                        "guard recovery did not obey the held/released button");
                    for (const auto action : { Action::GuardBreak, Action::StanceBreak, Action::GuardCounter,
                             Action::Light1, Action::Roll, Action::HurtSmall })
                        check(!guardAnimation(weapon, twoHanded, action, true),
                            "guard overlay hid an interrupting reaction or counterattack");
                }
    }

    inline void attackerGuardDeflection()
    {
        const auto check = [](bool value, const char* message) {
            if (!value)
                throw std::runtime_error(message);
        };
        for (const bool paired : { false, true })
            for (const bool slow : { false, true })
            {
                Controller controller;
                Config config;
                config.mWeaponClass = config.mLeftWeaponClass = WeaponClass::StraightSword;
                config.mHasShield = false;
                config.mHasLeftWeapon = config.mPairedWeapons = paired;
                config.mStaminaRegen = 0.f;
                controller.configure(config);
                Input input;
                (paired ? input.mGuard : input.mLight).mPressed = true;
                controller.update(input, TickSeconds);
                input.clearEdges();
                for (int tick = 0; tick < 300 && !controller.activeHit(); ++tick)
                    controller.update(input, TickSeconds);
                check(controller.activeHit().has_value(), "deflection fixture never reached contact");
                // Queue a follow-up before contact, or cross the remaining hit
                // windows in one slow update. Neither may survive the bounce.
                if (slow)
                    controller.update(input, 1.f);
                else
                {
                    input.mLight.mPressed = true;
                    controller.update(input, TickSeconds);
                    input.clearEdges();
                }
                const auto hit = controller.activeHit();
                check(hit.has_value(), "slow update lost the blocked contact");
                const float stamina = controller.stamina();
                auto stale = *hit;
                ++stale.mSequence;
                check(!controller.deflectHit(stale), "stale contact interrupted an unrelated action");
                check(controller.deflectHit(*hit), "blocked weapon failed to recoil");
                check(controller.action() == Action::Deflected && !controller.activeHit(),
                    "deflection retained an attacking action or crossed hit window");
                check(controller.stamina() == stamina, "deflection charged attacker stamina twice");
                check(!controller.deflectHit(*hit), "same block restarted recoil twice");
                controller.markHit();
                controller.applyHitStop(.1f);
                controller.update(input, .05f);
                check(controller.frame() == 0.f, "impact hit-stop advanced deflection recovery");
                const float recovery = controller.definition().mDuration;
                for (int tick = 0; tick < 300 && controller.action() == Action::Deflected; ++tick)
                {
                    input.mMoveY = 1.f;
                    input.mGuard.mHeld = true;
                    const float before = controller.frame();
                    controller.update(input, TickSeconds);
                    check(!controller.activeHit(), "blocked multi-hit attack dealt another contact during recoil");
                    if (before + 1.f < recovery)
                        check(controller.action() == Action::Deflected,
                            "movement/guard cancelled deflection before recovery");
                }
                check(controller.action() == Action::Idle, "pre-block buffered combo survived deflection");
            }
        for (int context = 0; context != 5; ++context)
        {
            Controller controller;
            Config config;
            config.mWeaponClass = WeaponClass::StraightSword;
            controller.configure(config);
            Input input;
            input.mInCombat = true;
            input.mTwoHanded = context == 0;
            input.mGrounded = context != 1;
            input.mSprint = context == 2;
            input.mCrouching = context == 3;
            input.mMoveY = input.mSprint ? 1.f : 0.f;
            controller.update(input, TickSeconds);
            (context == 4 ? input.mHeavy : input.mLight).mPressed = true;
            controller.update(input, TickSeconds);
            input.clearEdges();
            for (int tick = 0; tick < 300 && !controller.activeHit(); ++tick)
                controller.update(input, TickSeconds);
            const auto hit = controller.activeHit();
            check(hit.has_value(), "strong/contextual deflection fixture never reached contact");
            check(!controller.deflectHit(*hit),
                "two-handed, heavy or contextual attack rebounded like a light chain");
        }
        Controller creature;
        Config native;
        native.mNativeAttack = NativeAttackTiming{ "attack1", 45.f, 20.f };
        creature.configure(native);
        Input input;
        input.mLight.mPressed = true;
        creature.update(input, TickSeconds);
        input.clearEdges();
        for (int tick = 0; tick < 300 && !creature.activeHit(); ++tick)
            creature.update(input, TickSeconds);
        const auto hit = creature.activeHit();
        check(hit.has_value() && !creature.deflectHit(*hit),
            "native creature received humanoid weapon-recoil timing");
    }
}

#endif
