#include "bossarts.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    using namespace EldenCombat;
    int assertions = 0;

    void check(bool value, const char* message)
    {
        ++assertions;
        if (!value)
            throw std::runtime_error(message);
    }

    void ready(BossArtState& art, Controller& combat)
    {
        for (int i = 0; i < 5; ++i)
            art.update(combat, 1.f, true);
    }

    void selectProfiles()
    {
        check(bossArtForRecord("almalexia_warrior") == &BossArts[1], "Almalexia needs divine flame");
        check(bossArtForRecord("DAGOTH_UR_2") == &BossArts[0], "RefIds are case insensitive");
        check(bossArtForRecord("vivec_god") == &BossArts[2], "Vivec needs divine spear");
        check(bossArtForRecord("imperfect") == &BossArts[3], "Imperfect needs clockwork shock");
        check(bossArtForRecord("bm_frost_giant") == &BossArts[4], "Karstaag needs frost");
        check(bossArtForRecord("BM_HIRCINE_HUNTASPECT") == &BossArts[10], "Guile needs the quick spear art");
        for (std::string_view id : { "bm_hircine_straspect", "bm_hircine_spdaspect" })
            check(bossArtForRecord(id) == &BossArts[5], "Hircine aspect needs hunter art");
        for (std::string_view id : { "", "rat", "gaenor", "dagoth_ur_2_copy", "almalexia guard", "wolf" })
            check(!bossArtForRecord(id), "ordinary/story-lookalike actors must not gain arts");
        for (const auto& art : BossArts)
        {
            check(!art.mId.empty() && !art.mName.empty(), "missing native effect source identity");
            check(art.mTellSeconds >= (art.mSpearThrust ? 0.25f : 0.65f)
                && art.mCooldownSeconds >= 12.f, "art lacks tell or cooldown");
            check(art.mChance > 0.f && art.mChance <= 1.f, "invalid art selection probability");
            check(art.mEffectCount > 0 && art.mEffectCount <= art.mEffects.size(), "invalid effect list");
            for (std::size_t i = 0; i < art.mEffectCount; ++i)
                check(art.mEffects[i].mMagnitude > 0 && art.mEffects[i].mDuration > 0,
                    "native effect must have bounded positive magnitude/duration");
        }
    }

    void interruptedTell()
    {
        Controller combat;
        BossArtState art;
        check(!art.begin(BossArts[0], combat), "new actor needs opening cooldown");
        ready(art, combat);
        check(art.begin(BossArts[0], combat), "ready idle boss should begin tell");
        check(!art.begin(BossArts[1], combat), "a tell cannot replace another tell");
        art.update(combat, 0.f, true);
        check(!art.ready() && !art.launch(combat), "paused tell must not advance");
        art.update(combat, std::numeric_limits<float>::quiet_NaN(), true);
        art.update(combat, -2.f, true);
        check(!art.ready(), "invalid time advanced a tell");
        const float cooldown = art.cooldown();
        art.update(combat, 0.f, false);
        check(!art.active() && art.cooldown() == cooldown, "suspension must cancel without refund");
        check(!art.begin(BossArts[0], combat), "cancelled tell bypassed cooldown");
        for (int i = 0; i < 20; ++i)
            art.update(combat, 1.f, true);
        check(art.begin(BossArts[0], combat), "cooldown never recovered");
        combat.reset(true);
        art.update(combat, 0.f, true);
        check(!art.active(), "teleport/reset left tell active");
        art.reset();
        ready(art, combat);
        check(art.begin(BossArts[0], combat), "fresh tell failed");
        Incoming hit;
        hit.mDamage = 5.f;
        hit.mStanceDamage = 5.f;
        combat.receiveHit(hit);
        art.update(combat, 0.f, true);
        check(!art.active(), "stagger left tell active");
        art.reset();
        check(!art.active() && art.cooldown() == 4.f, "death/load reset retained art state");
    }

    void runSwing(const BossArtProfile& profile, const Config& config, float step, bool contact)
    {
        Controller combat, ordinary;
        combat.configure(config);
        ordinary.configure(config);
        BossArtState art;
        ready(art, combat);
        check(art.begin(profile, combat), "tell did not start");
        float elapsed = 0.f;
        while (!art.ready() && elapsed < 3.f)
        {
            check(!art.launch(combat), "swing started before tell finished");
            art.update(combat, step, true);
            elapsed += std::min(step, 1.f);
        }
        check(elapsed + 0.00001f >= profile.mTellSeconds, "tell was shortened");
        check(art.launch(combat), "prepared art did not launch");
        check(!art.launch(combat), "same tell launched twice");
        Input input;
        if (profile.mSpearThrust)
        {
            input.mSpearThrust = { false, true, true };
            input.mTwoHanded = true;
        }
        else
            input.mHeavy = { true, true, false };
        input.mAIControlled = true;
        input.mInCombat = true;
        int contacts = 0;
        elapsed = 0.f;
        for (int i = 0; i < 3000; ++i)
        {
            if (elapsed >= 0.1f)
                input.mHeavy.mHeld = false;
            const auto update = combat.update(input, step);
            ordinary.update(input, step);
            art.update(combat, update.mAdvancedSeconds, true);
            input.clearEdges();
            elapsed += step;
            check(combat.action() == ordinary.action() && combat.frame() == ordinary.frame(),
                "art changed the native/imported attack timeline");
            check(std::abs(combat.stamina() - ordinary.stamina()) < 0.0001f,
                "art double-charged or refunded native stamina");
            while (const auto hit = combat.activeHit())
            {
                if (contact)
                {
                    if (const auto* proc = art.consume(*hit))
                    {
                        ++contacts;
                        check(proc == &profile, "wrong boss effect was selected");
                        check(!art.consume(*hit), "same contact applied its magic twice");
                        auto secondHit = *hit;
                        ++secondHit.mHitIndex;
                        check(!art.consume(secondHit), "multi-hit attack applied magic twice");
                    }
                }
                combat.markHit();
            }
            while (ordinary.activeHit())
                ordinary.markHit();
            if (combat.action() == Action::Idle && elapsed > step)
                break;
        }
        art.update(combat, 0.f, true);
        check(combat.action() == Action::Idle, "art failed to finish its attack");
        check(!art.active(), "missed/completed art leaked into next attack");
        if (contacts != (contact ? 1 : 0))
            throw std::runtime_error("art contact count: " + std::string(profile.mId)
                + " weapon=" + std::to_string(static_cast<int>(config.mWeaponClass))
                + " native=" + std::to_string(config.mNativeAttack.has_value())
                + " step=" + std::to_string(step) + " contacts=" + std::to_string(contacts));
        check(!art.begin(profile, combat), "completed art bypassed cooldown");
    }

    void unrelatedAttackAndTimeout()
    {
        Controller combat;
        BossArtState art;
        ready(art, combat);
        check(art.begin(BossArts[0], combat), "tell failed");
        art.update(combat, 1.f, true);
        art.update(combat, 1.f, true);
        check(art.launch(combat), "launch failed");
        Attack wrong;
        wrong.mAction = Action::Light1;
        wrong.mSequence = combat.sequence() + 1;
        check(!art.consume(wrong), "unrelated light attack consumed boss magic");
        wrong.mAction = Action::Heavy1;
        wrong.mSequence += 5;
        check(!art.consume(wrong), "later heavy attack consumed old boss magic");
        art.update(combat, 0.5f, true);
        check(!art.active(), "rejected heavy input left a permanent pending art");
    }

    void blockedContact()
    {
        Controller combat;
        BossArtState art;
        ready(art, combat);
        check(art.begin(BossArts[1], combat), "blocked art tell failed");
        art.update(combat, 1.f, true);
        check(art.launch(combat), "blocked art launch failed");
        Input input;
        input.mHeavy = { true, true, false };
        for (int i = 0; i < 500; ++i)
        {
            const auto update = combat.update(input, TickSeconds);
            art.update(combat, update.mAdvancedSeconds, true);
            input.clearEdges();
            input.mHeavy.mHeld = false;
            if (const auto hit = combat.activeHit())
            {
                check(!combat.deflectHit(*hit), "heavy art should retain follow-through when blocked");
                // resolveEldenHit captures Attack before defense. Its blocked
                // contact consumes this reservation without casting the spell;
                // later windows of a multi-hit heavy cannot proc the art.
                check(art.consume(*hit) == &BossArts[1], "block failed to consume its captured art contact");
                check(!art.consume(*hit), "block left a second art proc available");
                return;
            }
        }
        throw std::runtime_error("blocked art never reached contact");
    }

    void quickThrustReservation()
    {
        for (bool contactPending : { false, true })
            for (int interruption = 0; interruption < 4; ++interruption)
            {
                Config config;
                config.mWeaponClass = WeaponClass::Spear;
                Controller combat;
                combat.configure(config);
                BossArtState art;
                ready(art, combat);
                check(art.begin(BossArts[10], combat), "quick-art reservation tell failed");
                art.update(combat, 1.f, true);
                check(art.launch(combat), "quick-art reservation launch failed");
                Input input;
                input.mAIControlled = input.mInCombat = input.mTwoHanded = true;
                input.mSpearThrust = { false, true, true };
                combat.update(input, TickSeconds);
                input.clearEdges();
                if (contactPending)
                {
                    for (int tick = 0; tick < 300 && !combat.activeHit(); ++tick)
                    {
                        const auto update = combat.update(input, TickSeconds);
                        art.update(combat, update.mAdvancedSeconds, true);
                    }
                    check(combat.activeHit().has_value(), "quick-art pending-contact fixture missed its hit");
                }
                const auto captured = combat.attack();
                for (Action wrongAction : { Action::Light1, Action::Heavy1, Action::Heavy1Charge })
                {
                    auto wrong = captured;
                    wrong.mAction = wrongAction;
                    check(!art.consume(wrong) && art.active(), "ordinary swing consumed the quick-art reservation");
                }
                for (int offset : { -1, 1 })
                {
                    auto wrong = captured;
                    wrong.mSequence += offset;
                    check(!art.consume(wrong) && art.active(), "different thrust sequence consumed reserved magic");
                }
                combat.applyHitStop(0.1f);
                const float frame = combat.frame();
                const float cooldown = art.cooldown();
                const auto frozen = combat.update(input, TickSeconds);
                art.update(combat, frozen.mAdvancedSeconds, true);
                check(combat.frame() == frame && art.active() && art.cooldown() == cooldown,
                    "hit-stop advanced or cancelled the active quick-art reservation");
                if (interruption == 0)
                {
                    Incoming hit;
                    hit.mStanceDamage = 1000.f;
                    combat.receiveHit(hit);
                }
                else if (interruption == 1)
                    combat.cancelInputActions();
                else if (interruption == 2)
                    combat.setDead(true);
                else
                    combat.reset(true);
                art.update(combat, 0.f, interruption != 2);
                check(!combat.activeHit() && !art.active(),
                    "interrupted quick thrust retained a pending hit or art reservation");
                check(!art.consume(captured) && art.cooldown() == cooldown,
                    "interrupted quick thrust applied stale magic or refunded its cooldown");
            }
    }

    void quickThrust()
    {
        Config config;
        config.mWeaponClass = WeaponClass::Spear;
        const auto ordinary = Controller::actionDefinition(Action::Light1, config, true);
        const auto thrust = Controller::actionDefinition(Action::SpearThrust, config, true);
        check(thrust.mAnimation == ordinary.mAnimation, "quick thrust must use the existing spear stab");
        check(std::abs(thrust.mDuration * 1.6f - ordinary.mDuration) < 0.001f,
            "quick thrust must complete faster than the ordinary stab");
        check(thrust.mHitCount == 1 && thrust.mChargeFrom == thrust.mChargeTo,
            "quick thrust must have one impact without a charge restart");
        check(std::abs(thrust.mHits[0].mFrom * 1.6f - ordinary.mHits[0].mFrom) < 0.001f,
            "quick thrust impact must follow accelerated visual timing");
        check(thrust.mHits[0].mStaminaCost == ordinary.mHits[0].mStaminaCost
            && thrust.mHits[0].mDamageMultiplier == ordinary.mHits[0].mDamageMultiplier,
            "quick thrust must retain a single native physical hit and stamina cost");
        if (!thrust.mAnimation.empty())
        {
            check(std::abs(thrust.mAnimationSpeed - ordinary.mAnimationSpeed * 1.6f) < 0.001f,
                "quick thrust visual playback must use its contact clock");
            check(thrust.mRootMotion == ordinary.mRootMotion
                && std::abs(thrust.mRootMotionSpeed - ordinary.mRootMotionSpeed * 1.6f) < 0.001f,
                "quick thrust must retain collision-owned root movement at its accelerated rate");
        }
        Input input;
        input.mAIControlled = input.mInCombat = input.mTwoHanded = true;
        input.mSpearThrust = { false, true, true };
        for (int invalid = 0; invalid < 7; ++invalid)
        {
            auto badInput = input;
            auto badConfig = config;
            if (invalid == 0) badInput.mAIControlled = false;
            if (invalid == 1) badInput.mInCombat = false;
            if (invalid == 2) badInput.mGrounded = false;
            if (invalid == 3) badInput.mFlying = true;
            if (invalid == 4) badInput.mTwoHanded = false;
            if (invalid == 5) badConfig.mWeaponClass = WeaponClass::Fist;
            if (invalid == 6) badConfig.mNativeAttack = NativeAttackTiming{ "attack1", 30.f, 10.f };
            Controller combat;
            combat.configure(badConfig);
            combat.update(badInput, TickSeconds);
            check(combat.action() == Action::Idle, "unsupported context started a spear art");
        }
        Controller combat;
        combat.configure(config);
        combat.update(input, TickSeconds);
        check(combat.action() == Action::SpearThrust && combat.attack().mTwoHanded,
            "quick thrust request must start a distinct two-handed attack");
        input.clearEdges();
        check(!input.mSpearThrust.mPressed && !input.mSpearThrust.mReleased,
            "clearing input must consume the art request");
        combat.applyHitStop(0.1f);
        const float frame = combat.frame();
        combat.update(input, TickSeconds);
        check(combat.frame() == frame, "quick thrust ignored native hit stop");
        Incoming incoming;
        incoming.mStanceDamage = 1000.f;
        combat.receiveHit(incoming);
        check(combat.action() != Action::SpearThrust && !combat.activeHit(),
            "a stance break must interrupt the thrust and remove pending damage");
        std::cout << "Quick thrust: tell=" << BossArts[10].mTellSeconds
            << "s, hit=" << thrust.mHits[0].mFrom / AnimationFps
            << "s, duration=" << thrust.mDuration / AnimationFps
            << "s, clip=" << thrust.mAnimation << '\n';
    }
}

int main()
{
    try
    {
        selectProfiles();
        interruptedTell();
        unrelatedAttackAndTimeout();
        blockedContact();
        quickThrust();
        quickThrustReservation();
        for (const auto& art : BossArts)
            for (const float step : { 1.f / 30.f, 1.f / 60.f, 1.f / 144.f, 1.f })
                for (const bool contact : { false, true })
                {
                    Config humanoid;
                    if (art.mSpearThrust)
                    {
                        humanoid.mWeaponClass = WeaponClass::Spear;
                        runSwing(art, humanoid, step, contact);
                        continue;
                    }
                    runSwing(art, humanoid, step, contact);
                    Config native;
                    native.mWeaponClass = WeaponClass::Fist;
                    native.mNativeAttack = NativeAttackTiming{ "attack2", 52.f, 29.f };
                    runSwing(art, native, step, contact);
                    native.mNativeAttack = NativeAttackTiming{ "attack1", 8.f, 0.f };
                    runSwing(art, native, step, contact);
                }
        for (int weapon = 0; weapon <= static_cast<int>(WeaponClass::Shield); ++weapon)
        {
            Config config;
            config.mWeaponClass = static_cast<WeaponClass>(weapon);
            runSwing(BossArts[1], config, 1.f / 60.f, true);
        }
        std::cout << "Boss art controller checks passed: " << assertions << " assertions\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
