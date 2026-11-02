#ifndef OPENMW_ELDENCOMBAT_DODGECOSTTESTS_H
#define OPENMW_ELDENCOMBAT_DODGECOSTTESTS_H

#include "combat.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace EldenCombat::Tests
{
    inline void configurableDodgeCost()
    {
        const auto check = [](bool value, const char* message) {
            if (!value)
                throw std::runtime_error(message);
        };
        const auto close = [&](float a, float b, const char* message) {
            check(std::abs(a - b) < .001f, message);
        };
        const auto request = [](Controller& controller, Action action) {
            Input input;
            input.mMoveY = action == Action::Backstep ? 0.f : 1.f;
            input.mDodgeDirection = std::array{ 0.f, input.mMoveY };
            input.mCrouching = action == Action::CrouchRoll;
            input.mDodge = { true, true, false };
            controller.update(input, TickSeconds);
            input.mDodge = { false, false, true };
            return controller.update(input, TickSeconds);
        };
        for (Load load : { Load::Light, Load::Medium, Load::Heavy })
        {
            Config config;
            config.mLoad = load;
            config.mStaminaRegen = 0.f;
            for (Action action : { Action::Roll, Action::CrouchRoll, Action::Backstep })
            {
                const auto original = Controller::actionDefinition(action, config);
                for (float multiplier : { 0.f, .5f, 1.f, 2.f, 3.f })
                {
                    Config scaled = config;
                    scaled.mDodgeStaminaMultiplier = multiplier;
                    const auto definition = Controller::actionDefinition(action, scaled);
                    close(definition.mStartCost, original.mStartCost * multiplier,
                        "dodge cost multiplier changed the wrong amount");
                    check(definition.mDuration == original.mDuration && definition.mDistance == original.mDistance
                            && definition.mInvincibleFrom == original.mInvincibleFrom
                            && definition.mInvincibleTo == original.mInvincibleTo
                            && definition.mRootMotion == original.mRootMotion,
                        "dodge cost setting altered distance, timing or invincibility");
                    Controller controller;
                    controller.configure(scaled);
                    controller.setVitals(250.f, 250.f);
                    const auto update = request(controller, action);
                    check(controller.action() == action, "configured dodge failed to start");
                    close(update.mStaminaSpent, definition.mStartCost * 2.5f,
                        "configured dodge failed normalized-fatigue scaling");
                    close(controller.stamina(), 250.f - definition.mStartCost * 2.5f,
                        "configured dodge was charged more than once at start");
                    for (int tick = 0; tick < 500 && controller.action() != Action::Idle; ++tick)
                        controller.update({}, TickSeconds);
                    close(controller.stamina(), 250.f - definition.mStartCost * 2.5f,
                        "configured dodge was charged again while playing");

                    Controller exhausted;
                    exhausted.configure(scaled);
                    exhausted.setVitals(0.f, 100.f);
                    request(exhausted, action);
                    check(exhausted.action() == (multiplier == 0.f ? action : Action::Idle),
                        "zero-cost dodge or exhausted paid-dodge eligibility is wrong");
                    exhausted.reset();
                    exhausted.setVitals(.1f, 100.f);
                    request(exhausted, action);
                    check(exhausted.action() == action, "positive-fatigue dodge compatibility was changed");
                    close(exhausted.stamina(), multiplier == 0.f ? .1f : 0.f,
                        "insufficient positive fatigue must clamp instead of becoming negative");
                }
            }
            Config scaled = config;
            scaled.mDodgeStaminaMultiplier = 3.f;
            for (Action action : { Action::Jump, Action::Light1, Action::Heavy1Charge, Action::GuardCounter })
            {
                const auto before = Controller::actionDefinition(action, config);
                const auto after = Controller::actionDefinition(action, scaled);
                check(before.mStartCost == after.mStartCost && before.mHitCount == after.mHitCount,
                    "dodge-cost setting changed another action's initial cost");
                for (std::size_t i = 0; i < before.mHitCount; ++i)
                    check(before.mHits[i].mStaminaCost == after.mHits[i].mStaminaCost,
                        "dodge-cost setting changed an attack's stamina cost");
            }
        }

        for (float multiplier : { 0.f, .5f, 2.f })
        {
            Config config;
            config.mStaminaRegen = 0.f;
            config.mDodgeStaminaMultiplier = multiplier;
            Controller queued;
            queued.configure(config);
            Input input;
            input.mLight.mPressed = true;
            queued.update(input, TickSeconds);
            for (int tick = 0; tick < 500 && queued.frame() < queued.definition().mDodgeInputFrom; ++tick)
                queued.update({}, TickSeconds);
            request(queued, Action::Roll);
            for (int tick = 0; tick < 500 && queued.action() != Action::Roll; ++tick)
                queued.update({}, TickSeconds);
            check(queued.action() == Action::Roll, "buffered attack cancellation lost the configured dodge");
            Config unscaled = config;
            unscaled.mDodgeStaminaMultiplier = 1.f;
            close(queued.definition().mStartCost,
                Controller::actionDefinition(Action::Roll, unscaled).mStartCost * multiplier,
                "queued dodge bypassed or duplicated the stamina multiplier");
        }
        Config invalid;
        invalid.mDodgeStaminaMultiplier = std::numeric_limits<float>::quiet_NaN();
        Controller validated;
        validated.configure(invalid);
        check(validated.config().mDodgeStaminaMultiplier == 1.f, "invalid dodge multiplier changed default cost");
        invalid.mDodgeStaminaMultiplier = 100.f;
        validated.configure(invalid);
        check(validated.config().mDodgeStaminaMultiplier == 3.f, "dodge multiplier exceeded supported range");
    }
}

#endif
