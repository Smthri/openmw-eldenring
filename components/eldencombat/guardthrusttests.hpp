#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_GUARDTHRUSTTESTS_H
#define OPENMW_COMPONENTS_ELDENCOMBAT_GUARDTHRUSTTESTS_H

#include "guard.hpp"

#include <cmath>
#include <stdexcept>

namespace EldenCombat::Tests
{
    inline void guardedSpearAttacks()
    {
        const auto check = [](bool value, const char* message) {
            if (!value)
                throw std::runtime_error(message);
        };
        for (const WeaponClass weapon : { WeaponClass::Spear, WeaponClass::GreatSpear })
            for (const float step : { 1.f / 30.f, 1.f / 60.f, 1.f / 144.f })
            {
                Config config;
                config.mWeaponClass = weapon;
                config.mHasShield = true;
                config.mStaminaRegen = 0.f;
                Controller controller;
                controller.configure(config);
                Input input;
                input.mGuard.mHeld = true;
                controller.update(input, .2f);
                check(controller.guardUp(), "spear shield fixture failed to raise guard");
                input.mLight.mPressed = true;
                controller.update(input, step);
                input.clearEdges();
                check(controller.action() == Action::GuardThrust && controller.guardUp(),
                    "light attack lowered a raised spear shield");
                const auto ordinary = Controller::actionDefinition(Action::Light1, config);
                const auto poke = controller.definition();
                check(poke.mAnimation == ordinary.mAnimation && poke.mAnimationSpeed == ordinary.mAnimationSpeed
                        && poke.mHits[0].mFrom == ordinary.mHits[0].mFrom && poke.mDuration == ordinary.mDuration,
                    "shield poke lost the authored one-handed thrust timing");
                check(poke.mRootMotion.empty() && poke.mHitCount == 1, "shield poke lunges or has extra contacts");
                const auto pose = guardAnimation(weapon, false, controller.action(), controller.guarding());
                check(pose && pose.mRightArm.empty(), "shield overlay erased the attacking right arm");
                for (int tick = 0; tick < 1000 && !controller.activeHit(); ++tick)
                    controller.update(input, step);
                const auto contact = controller.activeHit();
                check(contact && contact->mAction == Action::GuardThrust && !contact->mLeftHand
                        && !contact->mTwoHanded && !contact->mPaired,
                    "shield poke lost its right-hand contact ownership");
                const auto sequence = controller.sequence();
                const float frame = controller.frame();
                Incoming incoming;
                incoming.mStaminaDamage = 4.f;
                const float stamina = controller.stamina();
                check(controller.receiveHit(incoming) == HitResult::Blocked,
                    "incoming frontal contact ignored the shield during a poke");
                check(controller.sequence() == sequence && controller.frame() == frame
                        && controller.activeHit() && controller.guardUp(),
                    "successful shield block cancelled or restarted the spear contact");
                check(std::abs(controller.stamina() - (stamina - 4.f * config.mGuardStaminaTaken)) < .001f,
                    "shield poke block charged the wrong stamina");
                controller.markHit();
                check(!controller.activeHit(), "shield poke can damage twice in one live window");
                check(!controller.deflectHit(*contact), "shield poke received ordinary sword recoil");
                controller.applyHitStop(.1f);
                input.mGuard.mHeld = false;
                controller.update(input, .05f);
                check(!controller.guardUp() && controller.action() == Action::GuardThrust,
                    "releasing shield in hit-stop retained defense or cancelled the thrust");
                controller.applyHitStop(0.f);
                input.mGuard.mHeld = true;
                controller.update(input, .2f);
                check(controller.guardUp(), "spear shield did not raise again after release");
                incoming.mStaminaDamage = 10000.f;
                check(controller.receiveHit(incoming) == HitResult::GuardBroken
                        && controller.action() == Action::GuardBreak && !controller.activeHit(),
                    "guard break retained a shield-poke hit");
            }

        for (int condition = 0; condition < 4; ++condition)
        {
            Config config;
            config.mWeaponClass = condition == 0 ? WeaponClass::StraightSword : WeaponClass::Spear;
            config.mHasShield = condition != 1;
            Controller controller;
            controller.configure(config);
            Input input;
            input.mGuard.mHeld = true;
            input.mTwoHanded = condition == 2;
            input.mGrounded = condition != 3;
            controller.update(input, .2f);
            input.mLight.mPressed = true;
            controller.update(input, TickSeconds);
            check(controller.action() != Action::GuardThrust,
                "non-spear, shieldless, two-handed or airborne attack became a shield poke");
        }

        Config config;
        config.mWeaponClass = WeaponClass::Spear;
        config.mStaminaRegen = 0.f;
        Controller slow;
        slow.configure(config);
        Input input;
        input.mLight.mPressed = input.mGuard.mHeld = true;
        slow.update(input, TickSeconds);
        input.clearEdges();
        check(!slow.guardUp(), "simultaneous shield poke skipped the guard raise delay");
        const auto definition = slow.definition();
        unsigned contacts = 0;
        for (int tick = 0; tick < 10 && slow.action() != Action::Idle; ++tick)
        {
            slow.update(input, .5f);
            if (slow.activeHit())
            {
                ++contacts;
                slow.markHit();
            }
        }
        check(contacts == 1, "slow shield-poke update lost or duplicated a contact");
        check(std::abs(slow.stamina() - (100.f - definition.mStartCost - definition.mHits[0].mStaminaCost)) < .001f,
            "shield poke charged its attack cost more than once");
        check(slow.guardUp(), "shield poke recovery unnecessarily lowered the raised shield");

        for (const bool rear : { false, true })
        {
            Controller interrupted;
            interrupted.configure(config);
            interrupted.update(input, .2f);
            input.mLight.mPressed = true;
            interrupted.update(input, TickSeconds);
            input.clearEdges();
            Incoming incoming;
            incoming.mDamage = 1.f;
            incoming.mUnblockable = !rear;
            incoming.mFacingDot = rear ? -1.f : 1.f;
            check(interrupted.receiveHit(incoming) == HitResult::Hit
                    && interrupted.action() != Action::GuardThrust && !interrupted.activeHit(),
                "shield poke resisted a rear or unblockable interruption");
        }
    }
}

#endif
