#include "aitactics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    using namespace EldenCombat;

    constexpr float AttackDelay = 1.2f;

    void check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void close(float actual, float expected, const char* message)
    {
        check(std::abs(actual - expected) < 0.001f, message);
    }

    Input press(AiAttackButton button)
    {
        Input input;
        switch (button)
        {
            case AiAttackButton::Light:
                input.mLight = { true, true, false };
                break;
            case AiAttackButton::Heavy:
                input.mHeavy = { true, true, false };
                break;
            case AiAttackButton::Left:
                input.mGuard = { true, true, false };
                break;
        }
        return input;
    }

    void advance(Controller& controller, AiTactics& tactics, const Input& input = {}, float seconds = TickSeconds)
    {
        controller.update(input, seconds);
        tactics.update(controller, seconds, AttackDelay);
    }

    void finish(Controller& controller, AiTactics& tactics)
    {
        for (int tick = 0; controller.action() != Action::Idle && tick < 10000; ++tick)
            advance(controller, tactics);
        check(controller.action() == Action::Idle, "action did not finish");
    }

    void attackPauseStartsAfterRecovery()
    {
        Controller controller;
        AiTactics tactics;
        advance(controller, tactics, press(AiAttackButton::Light));
        check(tactics.comboCount() == 1, "opener did not consume one combo attack");
        check(!tactics.attackReady(), "another opener was permitted during a swing");
        finish(controller, tactics);
        close(tactics.attackCooldown(), AttackDelay, "attack animation consumed the pause between chains");
        check(tactics.comboCount() == 0, "idle did not reset the combo count");
        advance(controller, tactics, {}, AttackDelay * 0.5f);
        check(!tactics.attackReady(), "attack pause ended early");
        close(tactics.attackCooldown(), AttackDelay * 0.5f, "idle did not advance the attack pause");
        advance(controller, tactics, {}, AttackDelay * 0.5f + TickSeconds);
        check(tactics.attackReady(), "attack pause did not finish");
        advance(controller, tactics, press(AiAttackButton::Light));
        check(tactics.comboCount() == 1, "next chain inherited the previous combo count");
    }

    void finiteCombination(const Config& config, AiAttackButton button, int maximumCombo, float seconds = TickSeconds)
    {
        Controller controller;
        controller.configure(config);
        AiTactics tactics;
        advance(controller, tactics, press(button), seconds);
        int requests = 0;
        int largestCombo = tactics.comboCount();
        for (int tick = 0; controller.action() != Action::Idle && tick < 20000; ++tick)
        {
            Input input;
            // Ordinary pursuit movement also permits recovery cancellation.
            input.mMoveY = 0.25f;
            if (tactics.canCombo(controller, button, maximumCombo))
            {
                input = press(button);
                input.mMoveY = 0.25f;
                tactics.markComboRequested(controller);
                ++requests;
                check(!tactics.canCombo(controller, button, maximumCombo), "same swing queued a second follow-up");
            }
            advance(controller, tactics, input, seconds);
            largestCombo = std::max(largestCombo, tactics.comboCount());
            check(largestCombo <= maximumCombo, "combo exceeded the configured attack limit");
        }
        check(controller.action() == Action::Idle, "combination did not finish");
        check(requests == maximumCombo - 1, "combination missed a follow-up window");
        check(largestCombo == maximumCombo, "requested follow-up did not become an attack");
        check(tactics.comboCount() == 0, "completed combination did not reset");
        close(tactics.attackCooldown(), AttackDelay, "combination did not receive its full recovery pause");
    }

    void combinations()
    {
        Config config;
        finiteCombination(config, AiAttackButton::Light, 1);
        finiteCombination(config, AiAttackButton::Light, 3);
        finiteCombination(config, AiAttackButton::Light, 6);
        finiteCombination(config, AiAttackButton::Heavy, 3);

        config.mHasShield = false;
        config.mHasLeftWeapon = true;
        config.mLeftWeaponClass = WeaponClass::StraightSword;
        finiteCombination(config, AiAttackButton::Left, 3);
        config.mPairedWeapons = true;
        finiteCombination(config, AiAttackButton::Left, 3);

        config = {};
        config.mSpeedMultiplier = 0.25f;
        finiteCombination(config, AiAttackButton::Light, 3);
        config.mSpeedMultiplier = 5.f;
        finiteCombination(config, AiAttackButton::Light, 3, 1.f / 15.f);
    }

    void slowWeaponsQueueNearTheirCancel()
    {
        Config config;
        config.mSpeedMultiplier = 0.25f;
        Controller controller;
        controller.configure(config);
        AiTactics tactics;
        advance(controller, tactics, press(AiAttackButton::Light));
        const auto definition = controller.definition();
        for (int tick = 0; controller.frame() < definition.mInputFrom && tick < 10000; ++tick)
            advance(controller, tactics);
        check(controller.frame() < definition.mCancelLight - 8.f, "test weapon lacks a long input/cancel interval");
        check(!tactics.canCombo(controller, AiAttackButton::Light, 3), "slow weapon buffered at the early input window");
        for (int tick = 0; !tactics.canCombo(controller, AiAttackButton::Light, 3) && tick < 10000; ++tick)
            advance(controller, tactics);
        check(controller.action() != Action::Idle, "slow weapon missed its combo window");
        check(controller.frame() >= definition.mInputFrom, "combo queued before inputs were accepted");
        check(controller.frame() >= definition.mCancelLight - 8.f, "combo queued too far ahead of its cancel");
        const auto sequence = controller.sequence();
        tactics.markComboRequested(controller);
        advance(controller, tactics, press(AiAttackButton::Light));
        for (int tick = 0; controller.sequence() == sequence && tick < 10000; ++tick)
            advance(controller, tactics);
        check(Controller::isAttack(controller.action()) && tactics.comboCount() == 2,
            "slow weapon's buffered follow-up failed to execute");
    }

    void interruptionKeepsThePause()
    {
        Controller controller;
        AiTactics tactics;
        advance(controller, tactics, press(AiAttackButton::Light));
        Incoming incoming;
        incoming.mDamage = 1.f;
        check(controller.receiveHit(incoming) == HitResult::Hit, "test hit did not interrupt the attack");
        tactics.update(controller, 0.f, AttackDelay);
        check(tactics.comboCount() == 0, "interruption retained the old combo");
        close(tactics.attackCooldown(), AttackDelay, "interruption omitted the attack pause");
        advance(controller, tactics, {}, 0.2f);
        check(controller.action() != Action::Idle, "test hit reaction ended unexpectedly early");
        close(tactics.attackCooldown(), AttackDelay, "hit reaction consumed the attack pause");
        finish(controller, tactics);
        check(tactics.attackCooldown() >= AttackDelay - TickSeconds - 0.001f,
            "hit reaction left no opening before the next attack");

        Input dodge;
        dodge.mMoveX = 1.f;
        dodge.mDodge = { false, true, true };
        advance(controller, tactics, dodge);
        check(controller.action() == Action::Roll, "test dodge did not roll");
        const float beforeRoll = tactics.attackCooldown();
        advance(controller, tactics, {}, 0.2f);
        close(tactics.attackCooldown(), beforeRoll, "roll consumed the pause between attack chains");
    }

    void reactionDelayAndOneAttempt()
    {
        Controller opponent;
        AiTactics tactics;
        opponent.update(press(AiAttackButton::Light), TickSeconds);
        tactics.observeThreat(opponent, TickSeconds);
        check(!tactics.consumeReaction(0.25f), "NPC reacted immediately on attack detection");
        for (int tick = 0; tick < 12; ++tick)
        {
            opponent.update({}, TickSeconds);
            tactics.observeThreat(opponent, TickSeconds);
        }
        check(!tactics.consumeReaction(0.25f), "NPC reacted before its configured delay");
        for (int tick = 0; tick < 5; ++tick)
        {
            opponent.update({}, TickSeconds);
            tactics.observeThreat(opponent, TickSeconds);
        }
        check(tactics.consumeReaction(0.25f), "NPC did not become ready after its reaction delay");
        check(!tactics.consumeReaction(0.f), "same attack rerolled its defensive decision");
        for (int tick = 0; tick < 10; ++tick)
        {
            opponent.update({}, TickSeconds);
            tactics.observeThreat(opponent, TickSeconds);
            check(!tactics.consumeReaction(0.f), "continued attack rearmed a consumed defense");
        }
        while (opponent.action() != Action::Idle)
        {
            opponent.update({}, TickSeconds);
            tactics.observeThreat(opponent, TickSeconds);
        }
        check(!tactics.consumeReaction(0.f), "idle opponent offered a defensive reaction");
        opponent.update(press(AiAttackButton::Light), TickSeconds);
        tactics.observeThreat(opponent, TickSeconds);
        check(tactics.consumeReaction(0.f), "next opponent attack did not rearm defensive choice");
    }

    void chargeReleaseIsOneAttack()
    {
        Controller controller;
        AiTactics tactics;
        const Input held = press(AiAttackButton::Heavy);
        advance(controller, tactics, held);
        tactics.observeThreat(controller, TickSeconds);
        check(controller.action() == Action::Heavy1Charge, "heavy attack did not start charging");
        check(tactics.consumeReaction(0.f), "heavy charge did not offer an initial reaction");
        Input charge;
        charge.mHeavy.mHeld = true;
        for (int tick = 0; controller.frame() < controller.definition().mChargeFrom && tick < 10000; ++tick)
        {
            advance(controller, tactics, charge);
            tactics.observeThreat(controller, TickSeconds);
        }
        advance(controller, tactics);
        tactics.observeThreat(controller, TickSeconds);
        check(controller.action() == Action::Heavy1, "heavy release did not become its ordinary swing");
        check(tactics.comboCount() == 1, "charge release counted as a second combo attack");
        check(!tactics.consumeReaction(0.f), "charge release rerolled the same defensive decision");
    }

    void fullyChargedHeavyCanContinue()
    {
        Controller controller;
        AiTactics tactics;
        advance(controller, tactics, press(AiAttackButton::Heavy));
        bool passedChargeWindow = false;
        bool queuedContinuation = false;
        int largestCombo = tactics.comboCount();
        for (int tick = 0; controller.action() != Action::Idle && tick < 10000; ++tick)
        {
            Input input;
            input.mMoveY = 0.25f;
            if (tactics.comboCount() == 1)
            {
                input.mHeavy.mHeld = controller.frame() <= controller.definition().mChargeTo;
                if (controller.frame() > controller.definition().mChargeTo)
                {
                    check(controller.action() == Action::Heavy1Charge,
                        "full charge should retain its action through the swing and recovery");
                    passedChargeWindow = true;
                }
            }
            const bool charging = (controller.action() == Action::Heavy1Charge
                                      || controller.action() == Action::Heavy2Charge)
                && controller.frame() < controller.definition().mChargeTo;
            if (!charging && tactics.canCombo(controller, AiAttackButton::Heavy, 2))
            {
                input = press(AiAttackButton::Heavy);
                tactics.markComboRequested(controller);
                queuedContinuation = true;
            }
            advance(controller, tactics, input);
            largestCombo = std::max(largestCombo, tactics.comboCount());
        }
        check(controller.action() == Action::Idle, "charged heavy combination did not finish");
        check(passedChargeWindow && queuedContinuation && largestCombo == 2,
            "fully charged heavy could not continue into its planned second attack");
        close(tactics.attackCooldown(), AttackDelay, "charged heavy chain omitted its recovery pause");
    }

    void lightCombinationHeavyFinisher()
    {
        Controller controller;
        AiTactics tactics;
        advance(controller, tactics, press(AiAttackButton::Light));
        int requests = 0;
        int largestCombo = tactics.comboCount();
        bool sawHeavy = false;
        for (int tick = 0; controller.action() != Action::Idle && tick < 10000; ++tick)
        {
            Input input;
            input.mMoveY = 0.25f;
            const auto next = tactics.comboCount() == 2 ? AiAttackButton::Heavy : AiAttackButton::Light;
            if (tactics.canCombo(controller, next, 3))
            {
                input = press(next);
                tactics.markComboRequested(controller);
                ++requests;
            }
            advance(controller, tactics, input);
            largestCombo = std::max(largestCombo, tactics.comboCount());
            sawHeavy |= controller.action() == Action::Heavy1 || controller.action() == Action::Heavy1Charge;
        }
        check(controller.action() == Action::Idle && sawHeavy, "light chain omitted its planned heavy finisher");
        check(requests == 2 && largestCombo == 3, "heavy finisher exceeded the three-attack combination limit");
        close(tactics.attackCooldown(), AttackDelay, "heavy finisher omitted its recovery pause");
    }

    void meleeFacingCone()
    {
        constexpr float pi = 3.14159265358979323846f;
        for (float direction : { -1.f, 1.f })
        {
            check(meleeAttackFacing(0.f, direction * pi / 6.f), "turning toward a nearby target prevents a windup");
            check(meleeAttackFacing(0.f, direction * (pi / 3.f - .001f)), "forward edge cannot start a windup");
            check(!meleeAttackFacing(0.f, direction * (pi / 3.f + .001f)), "windup starts outside the forward cone");
            check(!meleeAttackFacing(0.f, direction * pi), "AI starts a swing facing away from its target");
            check(meleeAttackFacing(direction * (pi - .1f), -direction * (pi - .1f)),
                "yaw wrap prevents a forward-facing windup");
        }
        check(!meleeAttackFacing(std::numeric_limits<float>::quiet_NaN(), 0.f), "invalid actor yaw permits attacks");
        check(!meleeAttackFacing(0.f, std::numeric_limits<float>::infinity()), "invalid target yaw permits attacks");
        check(rangedAttackFacing(0.f, pi / 9.f, 0.f, pi / 9.f), "tracking prevents a native ranged windup");
        check(!rangedAttackFacing(0.f, pi / 5.f, 0.f, 0.f), "ranged windup starts outside its yaw cone");
        check(!rangedAttackFacing(0.f, 0.f, 0.f, pi / 5.f), "ranged windup starts outside its pitch cone");
        check(!rangedAttackFacing(0.f, 0.f, 0.f, std::numeric_limits<float>::quiet_NaN()),
            "invalid pitch permits ranged attacks");
    }

    void circlingTargetDoesNotStarveAttacks()
    {
        constexpr float pi = 3.14159265358979323846f;
        // Use the native smoothTurn rate/easing at base creature speed. These
        // targets stay in range and visible while orbiting continuously. Their
        // steady tracking lag never reaches the old 3-degree completion gate.
        for (int fps : { 30, 60, 144 })
        {
            for (float angularSpeed : { -4.f, -2.5f, 2.5f, 4.f })
            {
                Controller controller;
                AiTactics tactics;
                float actorYaw = 0.f;
                float targetYaw = 0.f;
                const float seconds = 1.f / fps;
                int openers = 0;
                int continuations = 0;
                int oldReadyFrames = 0;
                int rangedReadyFrames = 0;
                for (int tick = 0; tick < fps * 24; ++tick)
                {
                    targetYaw = std::remainder(targetYaw + angularSpeed * seconds, 2.f * pi);
                    const float error = std::remainder(targetYaw - actorYaw, 2.f * pi);
                    const bool oldFacing = std::abs(error) < 3.f * pi / 180.f;
                    if (tick >= fps * 4 && oldFacing)
                        ++oldReadyFrames;
                    if (tick >= fps * 4 && rangedAttackFacing(actorYaw, targetYaw, 0.f, .1f))
                        ++rangedReadyFrames;
                    const float limit = 5.f * pi * seconds * std::min(std::abs(error) / pi + .1f, .5f);
                    const float turn = oldFacing ? 0.f : std::clamp(error, -limit, limit);

                    Input input;
                    input.mInCombat = true;
                    if (meleeAttackFacing(actorYaw, targetYaw))
                    {
                        if (controller.action() == Action::Idle && tactics.attackReady())
                        {
                            input.mLight = { true, true, false };
                        }
                        else if (tactics.canCombo(controller, AiAttackButton::Light, 3))
                        {
                            input.mLight = { true, true, false };
                            tactics.markComboRequested(controller);
                        }
                    }
                    const bool wasAttacking = Controller::isAttack(controller.action());
                    const auto sequence = controller.sequence();
                    advance(controller, tactics, input, seconds);
                    if (Controller::isAttack(controller.action()) && controller.sequence() != sequence)
                    {
                        if (wasAttacking)
                            ++continuations;
                        else
                            ++openers;
                    }
                    if (wasAttacking && !Controller::isAttack(controller.action()))
                        close(tactics.attackCooldown(), AttackDelay, "circling removed the pause between chains");
                    actorYaw = std::remainder(actorYaw + turn, 2.f * pi);
                }
                check(oldReadyFrames == 0, "circling regression does not reproduce the old turn-completion starvation");
                check(openers >= 3, "continuous circling starves AI attack openers");
                check(continuations >= 3, "continuous circling prevents AI combo continuations");
                check(rangedReadyFrames >= fps * 10, "continuous circling starves native spell/projectile windups");
            }
        }
    }
}

int main()
{
    try
    {
        attackPauseStartsAfterRecovery();
        combinations();
        slowWeaponsQueueNearTheirCancel();
        interruptionKeepsThePause();
        reactionDelayAndOneAttempt();
        chargeReleaseIsOneAttack();
        fullyChargedHeavyCanContinue();
        lightCombinationHeavyFinisher();
        meleeFacingCone();
        circlingTargetDoesNotStarveAttacks();
        std::cout << "Elden combat AI tests passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Elden combat AI test failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
