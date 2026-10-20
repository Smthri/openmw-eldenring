#ifndef OPENMW_ELDENCOMBAT_NATIVEATTACKTESTS_H
#define OPENMW_ELDENCOMBAT_NATIVEATTACKTESTS_H

#include "combat.hpp"
#include "nativeattack.hpp"
#include "creatureposes.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace EldenCombat::Tests
{
    inline void nativeCreatureAttacks()
    {
        const auto check = [](bool value, const char* message) {
            if (!value)
                throw std::runtime_error(message);
        };
        const auto close = [&](float a, float b, const char* message) {
            check(std::abs(a - b) < .001f, message);
        };
        const auto begin = [&](Controller& controller, bool heavy = false) {
            Input input;
            if (heavy)
                input.mHeavy = { true, true, false };
            else
                input.mLight = { true, true, false };
            controller.update(input, TickSeconds);
            check(controller.action() == (heavy ? Action::Heavy1 : Action::Light1),
                "native attack started the wrong controller action");
        };
        for (const char* model : { "meshes\\r\\AshSlave.NIF", "r/ashzombie.nif", "r/ashvampire.nif",
                 "r/dagothr.nif", "r/cr_draugr.nif", "r/draugrlord.nif" })
            check(humanoidCreatureModel(model), "known upright native humanoid was not admitted");
        for (const char* model : { "Meshes\\R\\xAshSlave.NIF", "r/xashzombie.nif", "r/xashvampire.nif",
                 "r/xdagothr.nif", "r/xcr_draugr.nif", "r/xdraugrlord.nif" })
            check(humanoidCreatureModel(model), "animated native humanoid mesh was not admitted");
        for (const char* model : { "r/rust rat.nif", "r/wolf_black.nif", "r/guar.nif", "r/duskyalit.nif",
                 "r/leastkagouti.nif", "r/minescrib.nif", "r/bonelord.nif", "r/wingedtwilight.nif",
                 "r/fabricant_imperfect.nif", "r/ascendedsleeper.nif", "r/mod_ashslave.nif",
                 "r/ashslave.nif.extra", "custom/r/ashslave.nif", "custom/r/xashslave.nif",
                 "r/xxashslave.nif", "r/xashslave.nif.extra", "" })
            check(!humanoidCreatureModel(model), "unmatched creature inherited human poses");
        check(hircineGuileModel("BM_hircine_huntaspect", "Meshes\\R\\Hircine.NIF"),
            "Guile's case-insensitive native record/model was rejected");
        check(hircineGuileModel("BM_hircine_huntaspect", "Meshes\\R\\xHircine.NIF"),
            "Guile's runtime animated model was rejected");
        for (const char* id : { "bm_hircine", "bm_hircine2", "bm_hircine_straspect", "bm_hircine_spdaspect", "" })
        {
            check(!hircineGuileModel(id, "r/hircine.nif"), "non-Guile Hircine form inherited spear poses");
            check(!hircineGuileModel(id, "r/xhircine.nif"), "animated non-Guile Hircine form inherited spear poses");
        }
        for (const char* model : { "r/Hircine_Bear_larger.NIF", "r/HircineWolf.NIF", "custom/r/hircine.nif",
                 "r/xHircine_Bear_larger.NIF", "r/xHircineWolf.NIF", "custom/r/xhircine.nif",
                 "r/xxhircine.nif", "r/xhircine.nif.extra", "" })
            check(!hircineGuileModel("bm_hircine_huntaspect", model), "replacement nonhumanoid model inherited spear poses");
        // Source key offsets are independent of a model's bipedal flag and
        // import eligibility. Keep the visible native clip and one native hit
        // on the same clock when an imported skeleton is unavailable.
        const auto keys = nativeAttackTiming("attack2", 37.f, 39.f, 38.25f);
        check(keys && keys->mAnimation == "attack2", "valid native creature keys were rejected");
        close(keys->mDuration, 60.f, "native key duration included the source timeline offset");
        close(keys->mHitFrame, 37.5f, "native key impact included the source timeline offset");
        const auto noHit = nativeAttackTiming("attack1", 12.f, 13.f, -1.f);
        check(noHit && noHit->mHitFrame == 0.f, "missing native hit key lost contact at start");
        check(!nativeAttackTiming("attack1", 3.f, 3.f, 3.f)
                && !nativeAttackTiming("attack1", 4.f, 3.f, 3.f)
                && !nativeAttackTiming("attack1", 2.f, 3.f, 1.f)
                && !nativeAttackTiming("attack1", 2.f, 3.f, 4.f)
                && !nativeAttackTiming("attack1", -1.f, 3.f, 2.f)
                && !nativeAttackTiming("", 1.f, 3.f, 2.f),
            "invalid native timeline was admitted");
        for (const float invalid : { std::numeric_limits<float>::quiet_NaN(),
                 std::numeric_limits<float>::infinity() })
            check(!nativeAttackTiming("attack1", invalid, 3.f, 2.f)
                    && !nativeAttackTiming("attack1", 1.f, invalid, 2.f)
                    && !nativeAttackTiming("attack1", 1.f, 3.f, invalid),
                "non-finite native text key reached the controller");
        check(!nativeAttackTiming("attack1", 0.f, std::numeric_limits<float>::max(), 1.f),
            "native seconds-to-frames conversion overflowed");
        Config config;
        config.mWeaponClass = WeaponClass::Fist;
        config.mStaminaRegen = 0.f;
        // Synthetic slow creature attacks. Production timing comes from each
        // selected model; exact installed-game checks remain local fixtures.
        for (const NativeAttackTiming timing : { NativeAttackTiming{ "attack1", 54.f, 27.f },
                 NativeAttackTiming{ "attack2", 47.f, 29.f }, NativeAttackTiming{ "attack3", 81.f, 41.f } })
        {
            config.mNativeAttack = timing;
            for (float speed : { .25f, 1.f, 3.f })
            {
                config.mSpeedMultiplier = speed;
                const auto definition = Controller::actionDefinition(Action::Light1, config);
                check(definition.mNativeAnimation == timing.mAnimation && definition.mAnimation.empty(),
                    "native timing retained an imported pose or lost its native group");
                check(definition.mAnimationSpeed == 1.f && definition.mDuration == timing.mDuration,
                    "weapon/fist speed accelerated the native creature animation");
                check(definition.mHitCount == 1 && definition.mHits[0].mFrom == timing.mHitFrame
                        && definition.mHits[0].mTo == timing.mHitFrame,
                    "native contact does not match its sole authored hit key");
                check(definition.mCancelLight == timing.mDuration && definition.mCancelHeavy == timing.mDuration
                        && definition.mCancelLeft == timing.mDuration && definition.mCancelDodge == timing.mDuration
                        && definition.mCancelJump == timing.mDuration && definition.mCancelGuard == timing.mDuration
                        && definition.mCancelMove == timing.mDuration,
                    "native recovery can be canceled before the clip ends");
            }
            for (const float seconds : { 1.f / 30.f, 1.f / 60.f, 1.f / 144.f, .37f, 1.f })
            {
                Controller controller;
                controller.configure(config);
                begin(controller);
                const auto sequence = controller.sequence();
                float elapsed = 0.f;
                unsigned hits = 0;
                // Held movement/guard and repeated attack requests must not
                // truncate recovery, even while a follow-up is buffered.
                Input input;
                input.mMoveY = 1.f;
                input.mGuard.mHeld = true;
                input.mLight.mPressed = true;
                for (int tick = 0; tick < 1000 && controller.sequence() == sequence; ++tick)
                {
                    elapsed += seconds;
                    controller.update(input, seconds);
                    if (elapsed * AnimationFps < timing.mDuration - .01f)
                        check(controller.sequence() == sequence, "native attack combo/movement skipped recovery");
                    while (const auto hit = controller.activeHit())
                    {
                        if (hit->mSequence == sequence)
                        {
                            check(elapsed * AnimationFps >= timing.mHitFrame - .01f,
                                "creature damaged its target before the visible native hit key");
                            ++hits;
                        }
                        controller.markHit();
                    }
                }
                check(controller.sequence() != sequence, "native recovery never completed");
                check(hits == 1, "native swing missed or duplicated its single contact");
                check(elapsed * AnimationFps >= timing.mDuration - .01f,
                    "native creature began its next action before authored recovery ended");
            }
        }

        config.mNativeAttack = NativeAttackTiming{ "attack1", 54.f, 27.f };
        for (const auto weapon : { WeaponClass::Fist, WeaponClass::Twinblade, WeaponClass::Greatsword })
        {
            config.mWeaponClass = weapon;
            for (const auto action : { Action::Light1, Action::Heavy1Charge, Action::PairedLight1,
                     Action::JumpHeavy, Action::Critical })
            {
                Config ordinary = config;
                ordinary.mNativeAttack.reset();
                const auto source = Controller::actionDefinition(
                    action == Action::Heavy1Charge ? Action::Heavy1 : action, ordinary);
                const auto native = Controller::actionDefinition(action, config);
                check(native.mHitCount == 1, "native swing inherited an imported multi-hit attack");
                close(native.mStrength, source.mStrength, "native timing changed attack strength");
                close(native.mHits[0].mDamageMultiplier, source.mHits[0].mDamageMultiplier,
                    "native timing changed the selected hit's damage multiplier");
                close(native.mHits[0].mStaminaCost, source.mHits[0].mStaminaCost,
                    "native timing changed the selected hit's stamina cost");
                close(native.mHits[0].mStanceDamage, source.mHits[0].mStanceDamage,
                    "native timing changed the selected hit's stance damage");
                check(native.mRootMotion.empty() && !native.mLanding && !native.mLandingShort
                        && native.mChargeFrom == 0.f && native.mChargeTo == 0.f
                        && native.mInvincibleFrom == 0.f && native.mInvincibleTo == 0.f,
                    "native swing inherited incompatible imported motion or phase rules");
            }
        }

        config.mWeaponClass = WeaponClass::Fist;
        for (const bool release : { false, true })
        {
            Controller heavy;
            heavy.configure(config);
            begin(heavy, true);
            const auto sequence = heavy.sequence();
            Input input;
            input.mHeavy.mHeld = !release;
            input.mHeavy.mReleased = release;
            for (int tick = 0; tick < 60; ++tick)
            {
                heavy.update(input, TickSeconds);
                input.clearEdges();
                check(heavy.action() == Action::Heavy1 && heavy.sequence() == sequence,
                    "native heavy release/hold restarted the visible attack");
            }
            close(heavy.frame(), 30.f, "native heavy release/hold changed its animation clock");
        }

        Controller captured;
        captured.configure(config);
        begin(captured);
        Config next = config;
        next.mNativeAttack = NativeAttackTiming{ "attack3", 81.f, 41.f };
        captured.configure(next);
        check(captured.definition().mNativeAnimation == "attack1" && captured.definition().mDuration == 54.f
                && captured.definition().mHits[0].mFrom == 27.f,
            "configuring the next animation retimed an active native swing");
        next.mNativeAttack.reset();
        captured.configure(next);
        check(captured.definition().mNativeAnimation == "attack1",
            "a contact-time configuration reset discarded the captured native attack");

        Controller crossed;
        Config first = config;
        first.mNativeAttack = NativeAttackTiming{ "attack1", 12.f, 10.f };
        crossed.configure(first);
        begin(crossed);
        const auto oldSequence = crossed.sequence();
        crossed.update({}, 5.f / AnimationFps);
        Config second = config;
        second.mNativeAttack = NativeAttackTiming{ "attack2", 47.f, 29.f };
        crossed.configure(second);
        Input chain;
        chain.mLight.mPressed = true;
        crossed.update(chain, .5f);
        check(crossed.sequence() != oldSequence && crossed.definition().mNativeAnimation == "attack2",
            "crossed native contact fixture did not begin its different follow-up group");
        check(crossed.activeHit() && crossed.activeHit()->mSequence == oldSequence
                && crossed.activeHit()->mNativeAnimation == "attack1",
            "retained native impact inherited its successor's attack group/damage type");
        crossed.markHit();
        check(!crossed.activeHit(), "retained old native contact was processed twice");

        Controller paused;
        config.mMaximumPoise = 40.f;
        config.mAllowStanceBreak = false;
        paused.configure(config);
        begin(paused);
        paused.update({}, .5f);
        const float before = paused.frame();
        paused.update({}, 0.f);
        close(paused.frame(), before, "paused native attack advanced");
        paused.applyHitStop(.2f);
        paused.update({}, .1f);
        close(paused.frame(), before, "native attack clock ignored hit-stop");
        Incoming light;
        light.mDamage = 10.f;
        light.mStanceDamage = 10.f;
        light.mLevel = HurtLevel::Small;
        const auto sequence = paused.sequence();
        paused.receiveHit(light);
        check(paused.sequence() == sequence && paused.definition().mNativeAnimation == "attack1",
            "poise absorption interrupted a native creature swing");
        bool landed = false;
        for (int tick = 0; tick < 200 && !landed; ++tick)
        {
            paused.update({}, TickSeconds);
            if (paused.activeHit())
            {
                check(paused.frame() >= 27.f - .001f, "paused native attack hit early after resuming");
                landed = true;
            }
        }
        check(landed, "native swing lost its contact after hit-stop and poise absorption");

        // Native groups without a hit key use their start key; exact stop-key
        // impacts must also survive an update that crosses into idle recovery.
        for (float hitFrame : { 0.f, 6.f })
        {
            Config boundaryConfig = config;
            boundaryConfig.mNativeAttack = NativeAttackTiming{ "attack1", 6.f, hitFrame };
            Controller boundary;
            boundary.configure(boundaryConfig);
            begin(boundary);
            const auto sequence = boundary.sequence();
            if (hitFrame == 0.f)
                check(boundary.activeHit() && boundary.activeHit()->mSequence == sequence,
                    "missing native hit key did not strike at animation start");
            else
                check(!boundary.activeHit(), "stop-key native contact fired on start");
            unsigned contacts = 0;
            if (boundary.activeHit())
            {
                ++contacts;
                boundary.markHit();
            }
            boundary.update({}, .5f);
            if (boundary.activeHit())
            {
                check(boundary.activeHit()->mSequence == sequence,
                    "native crossed contact changed ownership after clip completion");
                ++contacts;
                boundary.markHit();
            }
            check(contacts == 1 && !boundary.activeHit(), "boundary native contact was duplicated or skipped");
        }

        Config ordinary;
        ordinary.mWeaponClass = WeaponClass::Fist;
        const auto fallback = Controller::actionDefinition(Action::Heavy1Charge, ordinary);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();
        for (const auto invalid : { NativeAttackTiming{ "", 54.f, 27.f },
                 NativeAttackTiming{ "attack1", 0.f, 0.f }, NativeAttackTiming{ "attack1", -1.f, 0.f },
                 NativeAttackTiming{ "attack1", nan, 27.f }, NativeAttackTiming{ "attack1", infinity, 27.f },
                 NativeAttackTiming{ "attack1", 54.f, -1.f }, NativeAttackTiming{ "attack1", 54.f, 55.f },
                 NativeAttackTiming{ "attack1", 54.f, nan }, NativeAttackTiming{ "attack1", 54.f, infinity } })
        {
            Config invalidConfig = ordinary;
            invalidConfig.mNativeAttack = invalid;
            const auto definition = Controller::actionDefinition(Action::Heavy1Charge, invalidConfig);
            check(definition.mNativeAnimation.empty() && definition.mAnimation == fallback.mAnimation
                    && definition.mDuration == fallback.mDuration && definition.mHits[0].mFrom == fallback.mHits[0].mFrom,
                "malformed native timing damaged the existing fallback behavior");
            Controller controller;
            controller.configure(invalidConfig);
            Input heavy;
            heavy.mHeavy = { true, true, false };
            controller.update(heavy, TickSeconds);
            check(controller.action() == Action::Heavy1Charge, "invalid native timing altered charge semantics");
        }
    }
}

#endif
