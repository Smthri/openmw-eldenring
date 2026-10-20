#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_PAIREDTESTS_H
#define OPENMW_COMPONENTS_ELDENCOMBAT_PAIREDTESTS_H

#include "combat.hpp"

#ifdef OPENMW_ELDENCOMBAT_IMPORTED_TIMINGS
#include "imported_timings.hpp"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace EldenCombat::Tests
{
    namespace PairedDetail
    {
        inline void check(bool value, const char* message)
        {
            if (!value)
                throw std::runtime_error(message);
        }

        inline Config config()
        {
            Config result;
            result.mWeaponClass = result.mLeftWeaponClass = WeaponClass::StraightSword;
            result.mHasShield = false;
            result.mHasLeftWeapon = result.mPairedWeapons = true;
            result.mStaminaRegen = 0.f;
            return result;
        }

        inline void toFrame(Controller& controller, Input input, float target)
        {
            input.clearEdges();
            const auto action = controller.action();
            for (int tick = 0; controller.frame() + .0001f < target && tick < 2000; ++tick)
            {
                const float seconds = std::min(TickSeconds, (target - controller.frame()) / AnimationFps);
                controller.update(input, seconds);
                check(controller.action() == action, "paired fixture changed action before its target frame");
            }
            check(std::abs(controller.frame() - target) < .001f, "paired fixture missed its target frame");
        }

        inline void followUp(Controller& controller, Input input, Action expected, bool pairedButton = true)
        {
            const auto original = controller.action();
            const auto definition = controller.definition();
            const float cancel = pairedButton ? definition.mCancelLeft : definition.mCancelLight;
            check(definition.mInputFrom < cancel,
                "paired fixture has no buffered-input interval");
            toFrame(controller, input, definition.mInputFrom);
            (pairedButton ? input.mGuard : input.mLight).mPressed = true;
            controller.update(input, TickSeconds);
            input.clearEdges();
            check(controller.action() == original, "paired follow-up skipped its source cancellation window");
            toFrame(controller, input, cancel - .01f);
            controller.setVitals(100.f, 100.f);
            controller.update(input, .02f / AnimationFps);
            check(controller.action() == expected, "attack button ignored its source cancellation window");
        }
    }

    inline void pairedAttackButtons()
    {
        using namespace PairedDetail;
        const Config paired = config();
        for (const bool pairedButton : { false, true })
            for (const bool ai : { false, true })
                for (const float speed : { .75f, 1.f, 1.3f })
                {
                    Config equipment = paired;
                    equipment.mSpeedMultiplier = speed;
                    // Both chains follow the primary weapon's speed. The
                    // paired button adds the off-hand's authored contacts.
                    equipment.mLeftSpeedMultiplier = 2.f;
                    Controller controller;
                    controller.configure(equipment);
                    Input input;
                    input.mAIControlled = ai;
                    (pairedButton ? input.mGuard : input.mLight).mPressed = true;
                    controller.update(input, TickSeconds);
                    input.clearEdges();
                    const auto first = pairedButton ? Action::PairedLight1 : Action::Light1;
                    check(controller.action() == first && controller.attack().mPaired == pairedButton,
                        "matching swords did not keep primary and paired buttons distinct");
                    int length = 5; // Documented fallback straight-sword chains.
#ifdef OPENMW_ELDENCOMBAT_IMPORTED_TIMINGS
                    length = 0;
                    for (int index = 0; index < 6; ++index)
                    {
                        const auto action = static_cast<Action>(static_cast<int>(first) + index);
                        if (!importedHasAction(action, equipment, false))
                            break;
                        ++length;
                    }
                    check(length > 1, "imported sword fixture has no complete chain");
#endif
                    for (int index = 0; index < length; ++index)
                    {
                        const auto definition = Controller::actionDefinition(controller.action(), equipment);
                        check(controller.definition().mAnimation == definition.mAnimation
                                && controller.definition().mAnimationSpeed == definition.mAnimationSpeed
                                && controller.definition().mDuration == definition.mDuration,
                            "sword attack lost its authored pose or primary weapon speed");
                        const auto next = index + 1 == length ? first
                            : static_cast<Action>(static_cast<int>(first) + index + 1);
                        followUp(controller, input, next, pairedButton);
                    }
                }

        // Switching buttons during recovery uses the requested attack's
        // cancellation window, including different light/paired source flags.
        for (const bool beganPaired : { false, true })
        {
            Controller controller;
            controller.configure(paired);
            Input input;
            (beganPaired ? input.mGuard : input.mLight).mPressed = true;
            controller.update(input, TickSeconds);
            input.clearEdges();
            followUp(controller, input, beganPaired ? Action::Light1 : Action::PairedLight1, !beganPaired);
        }

        for (const bool pairedButton : { false, true })
            for (int context = 0; context < 5; ++context)
            {
                Controller controller;
                controller.configure(paired);
                Input input;
                if (context == 0)
                {
                    input.mSprint = true;
                    input.mMoveY = 1.f;
                    controller.update(input, TickSeconds);
                    check(controller.sprinting(), "sword running fixture did not sprint");
                    (pairedButton ? input.mGuard : input.mLight).mPressed = true;
                    controller.update(input, TickSeconds);
                    check(controller.action() == (pairedButton ? Action::PairedRun : Action::RunLight),
                        "running attack confused primary and paired buttons");
                }
                else if (context == 1 || context == 2)
                {
                    input.mMoveY = context == 1 ? 1.f : 0.f;
                    input.mDodge = { false, true, true };
                    controller.update(input, TickSeconds);
                    const auto dodge = context == 1 ? Action::Roll : Action::Backstep;
                    check(controller.action() == dodge, "sword dodge fixture failed");
                    input = {};
                    const auto next = pairedButton
                        ? (context == 1 ? Action::PairedRoll : Action::PairedBackstep)
                        : (context == 1 ? Action::RollAttack : Action::BackstepAttack);
                    followUp(controller, input, next, pairedButton);
                }
                else
                {
                    input.mGrounded = context != 3;
                    input.mCrouching = context == 4;
                    (pairedButton ? input.mGuard : input.mLight).mPressed = true;
                    controller.update(input, TickSeconds);
                    auto expected = context == 3
                        ? (pairedButton ? Action::PairedJump : Action::JumpLight)
                        : (pairedButton ? Action::PairedLight1 : Action::CrouchAttack);
#ifdef OPENMW_ELDENCOMBAT_IMPORTED_TIMINGS
                    if (expected == Action::CrouchAttack && !importedHasAction(expected, paired, false))
                        expected = importedHasAction(Action::RollAttack, paired, false) ? Action::RollAttack : Action::Light1;
#endif
                    check(controller.action() == expected,
                        "airborne/crouched attack confused primary and paired buttons");
                }
            }

        for (int condition = 0; condition < 7; ++condition)
        {
            Config equipment = paired;
            Input input;
            if (condition == 0)
            {
                equipment.mPairedWeapons = false;
                equipment.mLeftWeaponClass = WeaponClass::Dagger;
            }
            else if (condition == 1)
                equipment.mHasLeftWeapon = equipment.mPairedWeapons = false;
            else if (condition == 2)
                equipment.mHasShield = true;
            else if (condition == 3 || condition == 4)
            {
                input.mTwoHanded = true;
                input.mTwoHandedLeft = condition == 4;
            }
            else if (condition == 5)
                input.mAIControlled = true;
            else
                input.mHeavy = { true, true, false };
            Controller controller;
            controller.configure(equipment);
            input.mLight.mPressed = condition != 6;
            controller.update(input, TickSeconds);
            check(controller.action() == (condition == 6 ? Action::Heavy1Charge : Action::Light1)
                    && !controller.attack().mPaired,
                "attack mapping changed mixed/shield/single/two-hand/AI/heavy primary input");
            if (condition == 3 || condition == 4)
                check(controller.attack().mTwoHanded && controller.attack().mLeftHand == input.mTwoHandedLeft,
                    "first primary attack selected the previous grip's hand");
        }

        for (const bool pairedButton : { false, true })
            for (const bool previousLeft : { false, true })
            {
                Controller controller;
                controller.configure(paired);
                Input input;
                input.mTwoHanded = true;
                input.mTwoHandedLeft = previousLeft;
                controller.update(input, TickSeconds);
                input.mTwoHanded = input.mTwoHandedLeft = false;
                (pairedButton ? input.mGuard : input.mLight).mPressed = true;
                controller.update(input, TickSeconds);
                check(controller.action() == (pairedButton ? Action::PairedLight1 : Action::Light1)
                        && !controller.attack().mTwoHanded,
                    "returning to one-hand grip changed the requested attack button");
            }

        // A primary press during the grip animation remains a primary request.
        // Its eventual hand belongs to the selected grip at action start.
        for (const bool toTwoHanded : { false, true })
            for (const bool left : { false, true })
            {
                Controller controller;
                controller.configure(paired);
                Input input;
                input.mTwoHanded = !toTwoHanded;
                input.mTwoHandedLeft = left && input.mTwoHanded;
                input.mHandsBusy = true;
                input.mLight.mPressed = true;
                controller.update(input, .1f);
                check(controller.action() == Action::Idle, "primary attack began during grip recovery");
                input.clearEdges();
                input.mTwoHanded = toTwoHanded;
                input.mTwoHandedLeft = left && input.mTwoHanded;
                controller.update(input, .1f);
                input.mHandsBusy = false;
                controller.update(input, TickSeconds);
                check(controller.action() == Action::Light1 && !controller.attack().mPaired
                        && controller.attack().mTwoHanded == toTwoHanded
                        && controller.attack().mLeftHand == (toTwoHanded && left),
                    "grip recovery lost or misrouted the buffered primary attack");
            }
        for (const bool addOffhand : { false, true })
        {
            Config equipment = paired;
            equipment.mPairedWeapons = equipment.mHasLeftWeapon = !addOffhand;
            Controller controller;
            controller.configure(equipment);
            Input input;
            input.mHandsBusy = input.mLight.mPressed = true;
            controller.update(input, TickSeconds);
            input.clearEdges();
            equipment.mPairedWeapons = equipment.mHasLeftWeapon = addOffhand;
            controller.configure(equipment);
            input.mHandsBusy = false;
            controller.update(input, TickSeconds);
            check(controller.action() == Action::Light1 && !controller.attack().mPaired,
                "changing the off-hand replaced a buffered primary attack");
        }
        for (const bool matching : { false, true })
        {
            Config equipment = paired;
            equipment.mPairedWeapons = matching;
            equipment.mLeftWeaponClass = matching ? WeaponClass::StraightSword : WeaponClass::Dagger;
            Controller controller;
            controller.configure(equipment);
            Input input;
            input.mHandsBusy = input.mGuard.mPressed = true;
            controller.update(input, TickSeconds);
            check(controller.action() == Action::Idle, "off-hand attack began during grip recovery");
            input.clearEdges();
            input.mHandsBusy = false;
            controller.update(input, TickSeconds);
            check(controller.action() == (matching ? Action::PairedLight1 : Action::LeftLight1)
                    && controller.attack().mPaired == matching,
                "buffered off-hand button lost its paired or mixed-weapon attack");
        }
        for (const bool shield : { false, true })
        {
            Config equipment = paired;
            equipment.mHasShield = shield;
            Controller controller;
            controller.configure(equipment);
            Input input;
            input.mTwoHanded = !shield;
            input.mGuard = { true, true, false };
            controller.update(input, .2f);
            check(controller.action() == Action::Idle && controller.guardUp(),
                "off-hand attack mapping replaced shield/two-hand guarding");
        }

#ifdef OPENMW_ELDENCOMBAT_IMPORTED_TIMINGS
        for (int weapon = static_cast<int>(WeaponClass::Dagger); weapon <= static_cast<int>(WeaponClass::Shield); ++weapon)
        {
            Config equipment = paired;
            equipment.mWeaponClass = equipment.mLeftWeaponClass = static_cast<WeaponClass>(weapon);
            if (!importedHasAction(Action::Light1, equipment, false))
                continue;
            Controller controller;
            controller.configure(equipment);
            Input input;
            input.mLight.mPressed = true;
            controller.update(input, TickSeconds);
            check(controller.action() == Action::Light1 && !controller.attack().mPaired,
                "equipping a matching off-hand replaced an imported primary weapon attack");
        }
#endif
    }

    inline void pairedContactOwnership()
    {
        using namespace PairedDetail;
        for (const bool left : { false, true })
        {
            Controller controller;
            controller.configure(config());
            Input input;
            input.mTwoHanded = true;
            input.mTwoHandedLeft = left;
            input.mLight.mPressed = true;
            controller.update(input, TickSeconds);
            auto& definition = const_cast<ActionDef&>(controller.definition());
            definition.mHits[0].mLeftHand = true;
            definition.mHits[0].mHasHandAssignment = true;
            check(controller.attack().mTwoHanded && controller.attack().mLeftHand == left,
                "authored physical hand replaced the selected two-handed inventory item");
        }
        for (const float speed : { .75f, 1.f, 1.3f })
            for (const float step : { 1.f / 30.f, 1.f / 60.f, 1.f / 144.f, .137f, .5f })
                for (const bool assigned : { false, true })
                {
                    Config equipment = config();
                    equipment.mSpeedMultiplier = speed;
                    Controller controller;
                    controller.configure(equipment);
                    Input input;
                    input.mGuard.mPressed = true;
                    controller.update(input, TickSeconds);
                    input.clearEdges();
                    check(controller.action() == Action::PairedLight1, "paired ownership fixture used a primary swing");
                    // Deliberately reverse the first two contacts and repeat a
                    // right-hand contact: explicit ownership must beat parity.
                    auto& definition = const_cast<ActionDef&>(controller.definition());
                    definition.mHitCount = 4;
                    definition.mDuration = 40.f / speed;
                    definition.mStartCost = 0.f;
                    const std::array<bool, 4> hands{ true, false, false, true };
                    for (std::size_t index = 0; index < hands.size(); ++index)
                    {
                        definition.mHits[index] = { (5.f + 3.f * index) / speed, (6.f + 3.f * index) / speed,
                            1.f, 3.f + index, 10.f, 1.f, .05f, assigned && hands[index], assigned };
                    }
                    std::array<bool, MaximumHits> seen{};
                    unsigned count = 0;
                    float paid = 0.f;
                    for (int tick = 0; tick < 1000 && (tick == 0 || controller.action() != Action::Idle); ++tick)
                    {
                        paid += controller.update(input, step).mStaminaSpent;
                        while (const auto hit = controller.activeHit())
                        {
                            check(hit->mHitIndex < hands.size() && !seen[hit->mHitIndex],
                                "paired contact was invalid or delivered twice");
                            seen[hit->mHitIndex] = true;
                            check(hit->mPaired && hit->mLeftHand == (assigned ? hands[hit->mHitIndex] : hit->mHitIndex % 2 != 0),
                                "paired contact ignored explicit hand ownership or fallback alternation");
                            ++count;
                            controller.markHit();
                        }
                    }
                    check(count == hands.size(), "slow paired update lost a crossed contact");
                    check(std::abs(paid - 18.f) < .001f && std::abs(controller.stamina() - 82.f) < .001f,
                        "paired contacts charged stamina more than once");
                }

#ifdef OPENMW_ELDENCOMBAT_IMPORTED_TIMINGS
        for (const auto weapon : { WeaponClass::Fist, WeaponClass::Claw })
            for (const bool left : { false, true })
            {
                Config equipment = config();
                equipment.mWeaponClass = weapon;
                Controller controller;
                controller.configure(equipment);
                Input input;
                input.mTwoHanded = true;
                input.mTwoHandedLeft = left;
                input.mLight.mPressed = true;
                controller.update(input, TickSeconds);
                input.clearEdges();
                const auto definition = controller.definition();
                bool physicalLeft = false;
                unsigned contacts = 0;
                for (int tick = 0; tick < 2000 && controller.action() != Action::Idle; ++tick)
                {
                    controller.update(input, TickSeconds);
                    while (const auto hit = controller.activeHit())
                    {
                        physicalLeft |= definition.mHits[hit->mHitIndex].mLeftHand;
                        check(hit->mTwoHanded && hit->mLeftHand == left,
                            "imported two-handed fist/claw contact selected the other inventory item");
                        ++contacts;
                        controller.markHit();
                    }
                }
                check(physicalLeft && contacts == definition.mHitCount,
                    "two-handed fist/claw fixture missed its authored left physical hand");
            }
        for (int weapon = static_cast<int>(WeaponClass::Dagger); weapon <= static_cast<int>(WeaponClass::Shield); ++weapon)
            for (const float step : { 1.f / 60.f, .137f, .5f })
            {
                Config equipment = config();
                equipment.mWeaponClass = equipment.mLeftWeaponClass = static_cast<WeaponClass>(weapon);
                if (!importedHasAction(Action::PairedLight1, equipment, false))
                    continue;
                Controller controller;
                controller.configure(equipment);
                Input input;
                input.mGuard.mPressed = true;
                controller.update(input, TickSeconds);
                input.clearEdges();
                const auto definition = controller.definition();
                std::array<bool, MaximumHits> seen{};
                float cost = definition.mStartCost;
                for (std::size_t index = 0; index < definition.mHitCount; ++index)
                {
                    check(definition.mHits[index].mHasHandAssignment,
                        "imported paired hit lacks its extracted hand assignment");
                    cost += definition.mHits[index].mStaminaCost;
                }
                for (int tick = 0; tick < 2000 && controller.action() != Action::Idle; ++tick)
                {
                    controller.update(input, step);
                    while (const auto hit = controller.activeHit())
                    {
                        check(hit->mHitIndex < definition.mHitCount && !seen[hit->mHitIndex],
                            "imported paired contact was lost or repeated");
                        seen[hit->mHitIndex] = true;
                        check(hit->mLeftHand == definition.mHits[hit->mHitIndex].mLeftHand
                                && hit->mDamageMultiplier == definition.mHits[hit->mHitIndex].mDamageMultiplier,
                            "imported paired hit lost its extracted hand or damage value");
                        controller.markHit();
                    }
                }
                for (std::size_t index = 0; index < definition.mHitCount; ++index)
                    check(seen[index], "imported paired hit window was skipped");
                check(std::abs(controller.stamina() - std::max(0.f, 100.f - cost)) < .001f,
                    "imported paired timing charged the wrong total stamina");
            }
#endif
    }

    inline void pairedJumpContacts()
    {
#ifdef OPENMW_ELDENCOMBAT_IMPORTED_TIMINGS
        using namespace PairedDetail;
        for (const float speed : { .75f, 1.f, 1.3f })
            for (int airborneHits = 0; airborneHits <= 3; ++airborneHits)
            {
                Config equipment = config();
                equipment.mSpeedMultiplier = speed;
                Controller controller;
                controller.configure(equipment);
                Input input;
                input.mGrounded = false;
                input.mGuard.mPressed = true;
                controller.update(input, TickSeconds);
                input.clearEdges();
                check(controller.action() == Action::PairedJump, "paired jump fixture selected a grounded action");
                const ActionDef air = controller.definition();
                check(air.mLanding && air.mHitCount >= 2, "paired jump lost one weapon's airborne contacts");
                check(air.mLanding->mHitCount == air.mHitCount,
                    "paired sword jump air and landing phases disagree on contacts");
                float cost = air.mStartCost;
                float end = 0.f;
                std::array<unsigned, 2> expected{}, seen{};
                for (std::size_t index = 0; index < air.mHitCount; ++index)
                {
                    const auto& hit = air.mHits[index];
                    check(hit.mHasHandAssignment, "paired jump lacks extracted airborne hand ownership");
                    ++expected[hit.mLeftHand];
                    cost += hit.mStaminaCost;
                    end = std::max(end, hit.mTo);
                }
                check(expected[0] > 0 && expected[1] > 0, "paired sword jump uses only one weapon");
                const float prepaid = controller.stamina();
                check(std::abs(prepaid - std::max(0.f, 100.f - cost)) < .001f,
                    "paired jump did not prepay each authored airborne contact exactly once");
                const auto sequence = controller.sequence();
                const auto consume = [&]() {
                    const auto hit = controller.activeHit();
                    check(hit && hit->mSequence == sequence && hit->mPaired,
                        "paired landing created a new damage sequence");
                    check(++seen[hit->mLeftHand] <= expected[hit->mLeftHand],
                        "paired landing repeated a weapon's consumed contact");
                    controller.markHit();
                };
                // 0: touchdown before contact; 1: only the first weapon hit;
                // 2: both hit in air; 3: the active window ended without a hit.
                if (airborneHits == 1 || airborneHits == 2)
                {
                    const unsigned wanted = airborneHits == 1 ? 1u : static_cast<unsigned>(air.mHitCount);
                    for (int tick = 0; seen[0] + seen[1] < wanted && tick < 2000; ++tick)
                    {
                        while (controller.activeHit() && seen[0] + seen[1] < wanted)
                            consume();
                        if (seen[0] + seen[1] < wanted)
                            controller.update(input, TickSeconds);
                    }
                    check(seen[0] + seen[1] == wanted, "paired airborne weapon never reached its hit window");
                }
                else if (airborneHits == 3)
                    toFrame(controller, input, end + .01f);
                input.mGrounded = true;
                controller.update(input, TickSeconds);
                for (int tick = 0; tick < 2000; ++tick)
                {
                    while (controller.activeHit())
                        consume();
                    if (controller.action() == Action::Idle)
                        break;
                    controller.update(input, TickSeconds);
                }
                check(controller.action() == Action::Idle, "paired jump did not recover after landing");
                check((airborneHits == 3 && seen[0] + seen[1] == 0) || (airborneHits != 3 && seen == expected),
                    "paired touchdown lost an unconsumed hand or replayed an expired contact");
                check(std::abs(controller.stamina() - prepaid) < .001f,
                    "paired landing charged its prepaid contacts again");
            }
#endif
    }

    inline void pairedMisses()
    {
        using namespace PairedDetail;
        for (const float step : { TickSeconds, .137f, .5f })
        {
            Controller controller;
            controller.configure(config());
            Input input;
            input.mGuard.mPressed = true;
            controller.update(input, TickSeconds);
            input.clearEdges();
            auto& definition = const_cast<ActionDef&>(controller.definition());
            definition.mDuration = 24.f;
            definition.mHitCount = 2;
            definition.mHits[0] = { 2.f, 20.f, 1.f, 10.f, 10.f, 1.f, .05f, false, true };
            definition.mHits[1] = { 2.f, 20.f, 1.f, 14.f, 10.f, 1.f, .05f, true, true };
            while (!controller.activeHit())
                controller.update(input, step);
            const auto first = controller.activeHit();
            check(first && !first->mLeftHand && first->mHitIndex == 0,
                "paired miss fixture selected the wrong first contact");
            controller.deferHit();
            const auto second = controller.activeHit();
            check(second && second->mLeftHand && second->mHitIndex == 1
                    && second->mSequence == first->mSequence,
                "a missed right weapon suppressed the overlapping left contact");
            controller.markHit();
            check(!controller.activeHit(), "deferred miss repeated within the same contact pass");
            const float paid = controller.stamina();
            controller.update(input, TickSeconds);
            const auto retry = controller.activeHit();
            check(retry && retry->mHitIndex == first->mHitIndex && retry->mSequence == first->mSequence,
                "a live paired miss did not retry on the next update");
            check(controller.stamina() == paid, "retrying a missed paired window charged stamina twice");
            controller.markHit();
            check(!controller.activeHit(), "a connected paired hand damaged twice");
            controller.update(input, TickSeconds);
            check(!controller.activeHit() && std::abs(controller.stamina() - 76.f) < .001f,
                "paired retry replayed damage or changed the authored per-hand costs");
        }

        // A long update can finish a swing before the adapter receives either
        // contact. Deferring that old sequence must not consume or suppress a
        // later swing's same-numbered hit.
        Controller controller;
        controller.configure(config());
        Input input;
        input.mGuard.mPressed = true;
        controller.update(input, TickSeconds);
        input.clearEdges();
        auto& definition = const_cast<ActionDef&>(controller.definition());
        definition.mDuration = 10.f;
        definition.mHitCount = 2;
        definition.mHits[0] = { 2.f, 4.f, 1.f, 10.f, 10.f, 1.f, .05f, false, true };
        definition.mHits[1] = { 2.f, 4.f, 1.f, 14.f, 10.f, 1.f, .05f, true, true };
        const auto sequence = controller.sequence();
        controller.update(input, .5f);
        check(controller.action() == Action::Idle && controller.sequence() != sequence,
            "slow paired miss fixture did not finish its attack");
        check(controller.activeHit() && controller.activeHit()->mSequence == sequence,
            "slow update lost the previous sequence's crossed contact");
        controller.deferHit();
        check(controller.activeHit() && controller.activeHit()->mLeftHand,
            "deferring an expired contact hid the other crossed hand");
        controller.markHit();
        check(!controller.activeHit() && std::abs(controller.stamina() - 76.f) < .001f,
            "crossed paired contacts repeated or charged stamina twice");
        controller.update(input, TickSeconds);
        check(!controller.activeHit(), "an expired deferred miss retried after its update");
        input.mGuard.mPressed = true;
        controller.update(input, TickSeconds);
        input.clearEdges();
        for (int tick = 0; !controller.activeHit() && tick < 2000; ++tick)
            controller.update(input, TickSeconds);
        check(controller.activeHit() && controller.activeHit()->mHitIndex == 0
                && controller.activeHit()->mSequence != sequence,
            "an old deferred contact suppressed the next attack's first hit");
    }
}

#endif
