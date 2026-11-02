#ifndef OPENMW_ELDENCOMBAT_FLIGHTTESTS_H
#define OPENMW_ELDENCOMBAT_FLIGHTTESTS_H

#include "combat.hpp"

#include <stdexcept>

namespace EldenCombat::Tests
{
    // Run against both fallback and imported definitions. Flight must remain a
    // separate locomotion mode even while native ground contact is still true.
    inline void controlledFlight()
    {
        const auto check = [](bool value, const char* message) {
            if (!value)
                throw std::runtime_error(message);
        };
        const auto noMotion = [&](const Update& update) {
            check(update.mDisplacementX == 0.f && update.mDisplacementY == 0.f,
                "levitation applied combat root displacement");
            check(!update.mJumpStarted, "levitation launched a physical jump");
        };
        Config config;
        config.mStaminaRegen = 0.f;
        for (const bool grounded : { false, true })
        {
            Input flight;
            flight.mFlying = true;
            flight.mGrounded = grounded;
            for (const Action opener : { Action::Light1, Action::Heavy1Charge, Action::LeftLight1,
                     Action::PairedLight1 })
            {
                Config weapons = config;
                weapons.mHasShield = opener != Action::LeftLight1 && opener != Action::PairedLight1;
                weapons.mHasLeftWeapon = !weapons.mHasShield;
                weapons.mPairedWeapons = opener == Action::PairedLight1;
                Controller attacker;
                attacker.configure(weapons);
                Input attack = flight;
                attack.mSprint = true;
                attack.mCrouching = true;
                attack.mMoveY = 1.f;
                attacker.update(attack, TickSeconds);
                if (opener == Action::Light1)
                    attack.mLight = { true, true, false };
                else if (opener == Action::Heavy1Charge)
                    attack.mHeavy = { true, true, false };
                else
                    attack.mGuard = { true, true, false };
                noMotion(attacker.update(attack, TickSeconds));
                check(attacker.action() == opener, "levitation selected a ground-context or jump attack");
                check(!attacker.sprinting(), "levitation retained sprinting");
                attack.clearEdges();
                attack.mMoveY = 0.f;
                if (opener == Action::Light1 || opener == Action::PairedLight1 || opener == Action::LeftLight1)
                {
                    for (int tick = 0; tick < 500 && attacker.frame() < attacker.definition().mInputFrom; ++tick)
                        noMotion(attacker.update(attack, TickSeconds));
                    if (opener == Action::Light1)
                        attack.mLight.mPressed = true;
                    else
                        attack.mGuard.mPressed = true;
                    noMotion(attacker.update(attack, TickSeconds));
                    attack.clearEdges();
                    for (int tick = 0; tick < 500 && attacker.action() == opener; ++tick)
                        noMotion(attacker.update(attack, TickSeconds));
                    const auto next = static_cast<Action>(static_cast<int>(opener) + 1);
                    check(attacker.action() == next, "levitating attack failed to continue its ordinary chain");
                }
                for (int tick = 0; tick < 1000 && attacker.action() != Action::Idle; ++tick)
                    noMotion(attacker.update(attack, TickSeconds));
                check(attacker.action() == Action::Idle, "levitating ordinary attack failed to recover");
            }

            Controller guard;
            guard.setVitals(50.f, 100.f);
            Input blocking = flight;
            blocking.mGuard.mHeld = true;
            for (int tick = 0; tick < 8; ++tick)
                guard.update(blocking, TickSeconds);
            check(guard.guardUp() && guard.stamina() > 50.f, "levitation disabled guard or fatigue regeneration");
            check(guard.receiveHit({}) == HitResult::Blocked, "levitating frontal guard failed to block");
            blocking.mHeavy = { true, true, false };
            guard.update(blocking, TickSeconds);
            blocking.clearEdges();
            for (int tick = 0; tick < 100 && guard.action() == Action::GuardHit; ++tick)
                guard.update(blocking, TickSeconds);
            check(guard.action() == Action::GuardCounter, "levitating block failed to retain its counter");

            Controller movement;
            movement.configure(config);
            Input request = flight;
            request.mMoveY = 1.f;
            request.mSprint = true;
            request.mJump = { true, true, false };
            request.mDodge = { true, true, false };
            noMotion(movement.update(request, TickSeconds));
            check(movement.action() == Action::Idle && !movement.sprinting(),
                "levitation accepted a jump, dodge or sprint");
            request.clearEdges();
            request.mFlying = false;
            request.mGrounded = true;
            request.mSprint = false;
            request.mJump = {};
            request.mDodge = { false, false, true };
            for (int tick = 0; tick < 100; ++tick)
            {
                noMotion(movement.update(request, TickSeconds));
                request.clearEdges();
                check(movement.action() == Action::Idle, "flight input queued a dodge/jump for landing");
            }

            for (const bool dodgeRequest : { false, true })
            {
                Controller buffered;
                buffered.configure(config);
                Input request;
                request.mLight.mPressed = true;
                buffered.update(request, TickSeconds);
                const float inputFrame = dodgeRequest ? buffered.definition().mDodgeInputFrom
                                                     : buffered.definition().mInputFrom;
                for (int tick = 0; tick < 500 && buffered.frame() < inputFrame; ++tick)
                    buffered.update({}, TickSeconds);
                request = {};
                request.mMoveY = 1.f;
                if (dodgeRequest)
                    request.mDodge = { false, true, true };
                else
                    request.mJump.mPressed = true;
                buffered.update(request, TickSeconds);
                check(buffered.action() == Action::Light1, "flight buffer fixture failed to queue before cancel");
                noMotion(buffered.update(flight, TickSeconds));
                for (int tick = 0; tick < 500 && buffered.action() == Action::Light1; ++tick)
                    buffered.update({}, TickSeconds);
                check(buffered.action() == Action::Idle, "levitation retained a preexisting buffered jump/dodge");
            }

            Controller expiring;
            expiring.setVitals(50.f, 100.f);
            expiring.update(blocking, 0.2f);
            check(expiring.guardUp(), "flight expiration fixture failed to raise guard");
            Input falling;
            falling.mGrounded = false;
            falling.mGuard.mHeld = true;
            const float stamina = expiring.stamina();
            expiring.update(falling, TickSeconds);
            check(!expiring.guardUp() && expiring.stamina() == stamina,
                "expired levitation retained airborne guard or regeneration");
            falling.mGrounded = true;
            expiring.update(falling, 0.2f);
            check(expiring.guardUp() && expiring.stamina() > stamina,
                "landing after levitation failed to restore guard or regeneration");
            falling.mGrounded = false;
            falling.mLight.mPressed = true;
            expiring.update(falling, TickSeconds);
            check(expiring.action() == Action::JumpLight, "expired levitation retained ordinary airborne melee");

            // Activating levitation preserves an ordinary swing's clock,
            // sequence and fatigue ownership instead of restarting it.
            Controller groundAttack;
            Controller risingAttack;
            groundAttack.configure(config);
            risingAttack.configure(config);
            Input light;
            light.mLight = { true, true, false };
            groundAttack.update(light, TickSeconds);
            risingAttack.update(light, TickSeconds);
            for (int tick = 0; tick < 120; ++tick)
            {
                groundAttack.update({}, TickSeconds);
                noMotion(risingAttack.update(flight, TickSeconds));
                check(risingAttack.action() == groundAttack.action()
                        && risingAttack.frame() == groundAttack.frame()
                        && risingAttack.attack().mSequence == groundAttack.attack().mSequence
                        && risingAttack.stamina() == groundAttack.stamina(),
                    "levitation canceled, restarted or recharged an ongoing attack");
            }

            Controller rolling;
            rolling.configure(config);
            Input dodge;
            dodge.mMoveY = 1.f;
            dodge.mDodge = { false, true, true };
            rolling.update(dodge, TickSeconds);
            check(rolling.action() == Action::Roll, "flight-entry roll fixture failed to start");
            for (int tick = 0; tick < 200 && rolling.action() != Action::Idle; ++tick)
                noMotion(rolling.update(flight, TickSeconds));
            check(rolling.action() == Action::Idle, "pre-levitation roll failed to finish");

            for (const bool alreadyAirborne : { false, true })
            {
                Controller jumping;
                jumping.configure(config);
                Input jump;
                jump.mJump.mPressed = true;
                jumping.update(jump, TickSeconds);
                if (alreadyAirborne)
                {
                    jump.clearEdges();
                    jump.mGrounded = false;
                    jumping.update(jump, TickSeconds);
                }
                const float stamina = jumping.stamina();
                for (int tick = 0; tick < 1000 && jumping.action() != Action::Idle; ++tick)
                    noMotion(jumping.update(flight, TickSeconds));
                check(jumping.action() == Action::Idle && jumping.stamina() == stamina,
                    "levitation stranded a pending jump or charged it twice");
            }

            for (const float seconds : { TickSeconds, 1.f })
            {
                for (const Action aerial : { Action::JumpLight, Action::JumpHeavy, Action::PairedJump })
                {
                    Config weapons = config;
                    weapons.mHasShield = aerial != Action::PairedJump;
                    weapons.mHasLeftWeapon = !weapons.mHasShield;
                    weapons.mPairedWeapons = !weapons.mHasShield;
                    Controller jumping;
                    jumping.configure(weapons);
                    Input attack;
                    attack.mGrounded = false;
                    if (aerial == Action::JumpLight)
                        attack.mLight.mPressed = true;
                    else if (aerial == Action::JumpHeavy)
                        attack.mHeavy.mPressed = true;
                    else
                        attack.mGuard.mPressed = true;
                    jumping.update(attack, TickSeconds);
                    check(jumping.action() == aerial, "flight-entry aerial swing fixture failed to start");
                    const ActionDef definition = jumping.definition();
                    const auto sequence = jumping.attack().mSequence;
                    const float stamina = jumping.stamina();
                    std::uint32_t seen = 0;
                    for (int tick = 0; tick < 1000 && jumping.action() != Action::Idle; ++tick)
                    {
                        noMotion(jumping.update(flight, seconds));
                        while (const auto hit = jumping.activeHit())
                        {
                            check(hit->mSequence == sequence && (seen & (1u << hit->mHitIndex)) == 0,
                                "levitation repeated an airborne hit or restarted its damage sequence");
                            seen |= 1u << hit->mHitIndex;
                            jumping.markHit();
                        }
                    }
                    check(jumping.action() == Action::Idle && jumping.stamina() == stamina,
                        "levitation stranded an aerial swing or repeated prepaid stamina");
                    check(seen == (1u << definition.mHitCount) - 1u,
                        "levitation discarded the airborne swing's unfinished hits");
                    if (definition.mLanding)
                        check(definition.mLandingShort != nullptr, "flight recovery fixture lacks a short landing");
                }
            }
        }
    }
}

#endif
