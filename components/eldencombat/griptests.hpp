#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_GRIPTESTS_H
#define OPENMW_COMPONENTS_ELDENCOMBAT_GRIPTESTS_H

#include "grip.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace EldenCombat::Tests
{
    inline void gripTransitions()
    {
        const auto check = [](bool value, const char* message) {
            if (!value)
                throw std::runtime_error(message);
        };
        for (const Grip grip : { Grip::OneHand, Grip::TwoHandRight, Grip::TwoHandLeft })
        {
            Input input;
            input.mTwoHanded = grip != Grip::OneHand;
            input.mTwoHandedLeft = grip == Grip::TwoHandLeft;
            input.mLight = input.mHeavy = input.mGuard = input.mDodge = input.mJump
                = input.mCritical = input.mSpearThrust = { true, true, true };
            input.mMoveX = .5f;
            input.mMoveY = -.5f;
            input.mDodgeDirection = std::array{ 1.f, 0.f };
            input.mHandsBusy = input.mSprint = input.mLockedOn = input.mCriticalAvailable = input.mAIControlled = true;
            input.resetKeepingGrip();
            check(input.mTwoHanded == (grip != Grip::OneHand) && input.mTwoHandedLeft == (grip == Grip::TwoHandLeft),
                "input reset lost the selected armament grip");
            for (const Button button : { input.mLight, input.mHeavy, input.mGuard, input.mDodge, input.mJump,
                     input.mCritical, input.mSpearThrust })
                check(!button.mHeld && !button.mPressed && !button.mReleased,
                    "input reset retained an action button");
            check(input.mMoveX == 0.f && input.mMoveY == 0.f && !input.mDodgeDirection && !input.mHandsBusy
                    && !input.mSprint && !input.mLockedOn && !input.mCriticalAvailable && !input.mAIControlled,
                "input reset retained transient movement or action context");
        }
        for (const Grip from : { Grip::OneHand, Grip::TwoHandRight, Grip::TwoHandLeft })
            for (const Grip to : { Grip::OneHand, Grip::TwoHandRight, Grip::TwoHandLeft })
            {
                GripTransition swap;
                check(!swap.active() && !swap.busy() && swap.pose().mAnimation.empty(),
                    "new grip transition is already busy");
                check(swap.start(from, to) == (from != to), "grip transition accepted a no-op");
                if (from == to)
                    continue;
                check(!swap.start(to, from), "second grip request replaced an active transition");
                check(!swap.advance(-1.f) && !swap.advance(std::numeric_limits<float>::quiet_NaN()),
                    "invalid time applied a grip");
                const bool left = to == Grip::TwoHandLeft || (to == Grip::OneHand && from == Grip::TwoHandLeft);
                const std::string_view start = to == Grip::TwoHandRight ? "a000_029060"
                    : to == Grip::TwoHandLeft ? "a000_029080" : left ? "a000_029010" : "a000_029040";
                check(swap.pose().mAnimation == start, "grip selected the wrong source transition");
                check(!swap.advance(2.f / AnimationFps), "grip applied before source frame three");
                check(swap.advance(1.01f / AnimationFps), "grip did not apply at source frame three");
                check(swap.target() == to && swap.from() == from, "grip transition lost its hands");
                check(!swap.advance(2.f / AnimationFps), "grip applied twice");
                check(swap.pose().mAnimation == (left ? "a000_029020" : "a000_029050")
                        && std::abs(swap.pose().mFrame - .01f) < .001f,
                    "grip completion clip did not start after five frames");
                check(swap.busy(), "grip unlocked controls before recovery");
                swap.advance(7.f / AnimationFps);
                check(!swap.busy() && swap.active(), "grip did not unlock at completion frame seven");
                swap.advance(10.f / AnimationFps);
                check(!swap.active() && !swap.busy() && swap.pose().mAnimation.empty(),
                    "grip clip outlasted its 22-frame source sequence");
                check(swap.start(to, from) && swap.advance(1.f), "slow update lost the grip apply event");
                check(!swap.advance(1.f), "slow update duplicated the grip apply event");
                check(swap.start(from, to), "finished grip blocked a new transition");
                swap.reset();
                check(!swap.active() && !swap.advance(1.f), "cancelled grip changed equipment afterward");
            }

        Config config;
        config.mWeaponClass = WeaponClass::StraightSword;
        config.mLeftWeaponClass = WeaponClass::Dagger;
        const auto right = Controller::actionDefinition(Action::Light1, config);
        const auto left = Controller::actionDefinition(Action::LeftLight1, config);
        config.mLeftSpeedMultiplier = 2.f;
        const auto fastLeft = Controller::actionDefinition(Action::LeftLight1, config);
        check(std::abs(fastLeft.mDuration * 2.f - left.mDuration) < .001f
                && std::abs(fastLeft.mHits[0].mFrom * 2.f - left.mHits[0].mFrom) < .001f,
            "off-hand weapon speed did not scale its own timing");
        check(Controller::actionDefinition(Action::Light1, config).mDuration == right.mDuration,
            "off-hand weapon speed changed right-hand timing");
        config.mLeftSpeedMultiplier = std::numeric_limits<float>::quiet_NaN();
        check(std::isfinite(Controller::actionDefinition(Action::LeftLight1, config).mDuration),
            "invalid off-hand speed broke attack timing");

        Controller controller;
        Input input;
        input.mHandsBusy = true;
        input.mLight.mPressed = true;
        input.mGuard.mHeld = true;
        controller.update(input, .2f);
        check(controller.action() == Action::Idle && !controller.guarding(),
            "grip swap started an attack or blocked with occupied hands");
        input.clearEdges();
        controller.update(input, .2f);
        input.mHandsBusy = false;
        input.mTwoHanded = true;
        controller.update(input, TickSeconds);
        check(controller.action() == Action::Light1 && controller.attack().mTwoHanded,
            "grip recovery lost the buffered attack or used the old grip");
    }

    inline void leftGripRootMotion()
    {
        const auto check = [](bool value, const char* message) {
            if (!value)
                throw std::runtime_error(message);
        };
        for (const auto weapon : { WeaponClass::StraightSword, WeaponClass::Greatsword, WeaponClass::Shield })
        {
            std::array<float, 2> lateral{}, forward{};
            for (int hand = 0; hand < 2; ++hand)
            {
                Config config;
                config.mWeaponClass = weapon;
                Controller controller;
                controller.configure(config);
                Input input;
                input.mTwoHanded = true;
                input.mTwoHandedLeft = hand != 0;
                input.mLight.mPressed = true;
                controller.update(input, TickSeconds);
                input.clearEdges();
                while (controller.action() != Action::Idle)
                {
                    const auto update = controller.update(input, TickSeconds);
                    lateral[hand] += update.mDisplacementX;
                    forward[hand] += update.mDisplacementY;
                }
            }
            const float sign = weapon == WeaponClass::Shield ? 1.f : -1.f;
            check(std::abs(lateral[1] - sign * lateral[0]) < .0001f && std::abs(forward[1] - forward[0]) < .0001f,
                "left grip root motion does not follow its reflected attack pose");
        }
        // Current extracted attack tracks contain forward travel only. Supply
        // an asymmetric fixture to exercise reflection and its captured hand
        // independently of that bank's presently zero lateral coordinates.
        for (const auto weapon : { WeaponClass::StraightSword, WeaponClass::Shield })
            for (const bool left : { false, true })
            {
                Config config;
                config.mWeaponClass = weapon;
                Controller controller;
                controller.configure(config);
                Input input;
                input.mTwoHanded = true;
                input.mTwoHandedLeft = left;
                input.mLight.mPressed = true;
                controller.update(input, TickSeconds);
                input.clearEdges();
                auto& definition = const_cast<ActionDef&>(controller.definition());
                definition.mRootMotion = { { 0.f, 0.f, 0.f }, { .1f, 0.f, .2f }, { .2f, 0.f, .4f } };
                definition.mRootMotionSpeed = 1.f;
                // Subsequent adapter configuration must not reinterpret an
                // already committed contact/pose as a different hand or type.
                input.mTwoHandedLeft = !left;
                config.mWeaponClass = weapon == WeaponClass::Shield ? WeaponClass::StraightSword : WeaponClass::Shield;
                controller.configure(config);
                const auto update = controller.update(input, 1.f / AnimationFps);
                const float expected = left && weapon != WeaponClass::Shield ? .1f : -.1f;
                check(std::abs(update.mDisplacementX - expected) < .0001f
                        && std::abs(update.mDisplacementY - .2f) < .0001f,
                    "asymmetric root-motion fixture ignored the captured attack hand");
            }
        for (const bool left : { false, true })
        {
            Controller controller;
            Input input;
            input.mTwoHanded = true;
            input.mTwoHandedLeft = left;
            input.mMoveX = 1.f;
            input.mDodge = { true, true, false };
            controller.update(input, TickSeconds);
            input.mDodge = { false, false, true };
            controller.update(input, TickSeconds);
            input.clearEdges();
            float right = 0.f;
            while (controller.action() != Action::Idle)
                right += controller.update(input, TickSeconds).mDisplacementX;
            check(right > 0.f, "left grip reflected the player's dodge input direction");
        }
    }
}

#endif
