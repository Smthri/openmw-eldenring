#ifndef OPENMW_ELDENCOMBAT_POISETESTS_H
#define OPENMW_ELDENCOMBAT_POISETESTS_H

#include "combat.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace EldenCombat::Tests
{
    // Shared controller regressions must pass with fallback and imported timings.
    inline void enemyPoise()
    {
        const auto check = [](bool value, const char* message) {
            if (!value)
                throw std::runtime_error(message);
        };
        const auto close = [&](float a, float b, const char* message) {
            check(std::abs(a - b) < .001f, message);
        };
        const auto advance = [](Controller& controller, float seconds, Input input = {}) {
            for (int tick = 0; tick < static_cast<int>(std::ceil(seconds / TickSeconds)); ++tick)
                controller.update(input, TickSeconds);
        };
        const auto start = [&](Controller& controller) {
            Input input;
            input.mLight.mPressed = true;
            controller.update(input, TickSeconds);
            check(controller.action() == Action::Light1, "poise fixture failed to start an attack");
        };
        const PoiseStats imperfect{ 100.f, 100.f, 0.f, 2000.f, 80.f, true };
        const PoiseStats rat{ 70.f, 20.f, 0.f, 23.f, 2.f, true };
        close(poiseFromStats(imperfect), 40.8f, "Imperfect poise must remain moderate");
        check(poiseFromStats(rat) < 1.f, "rat Endurance incorrectly grants large-creature poise");
        check(poiseFromStats({ 50.f, 50.f, 0.f, 38.f, 3.f, true }) < 1.f,
            "weak skeleton should remain easy to stagger");
        const float unarmored = poiseFromStats({ 50.f, 50.f });
        const float armored = poiseFromStats({ 50.f, 50.f, 50.f });
        check(unarmored < 5.f && armored > 10.f && armored < 20.f,
            "NPC armor should grant modest resistance without protecting weak unarmored enemies");
        check(poiseFromStats({ 10000.f, 10000.f, 10000.f, 100000.f, 1000.f, true }) == 45.f,
            "native creature poise must be capped");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        check(poiseFromStats({ nan, nan, nan, nan, nan, true }) == 0.f,
            "invalid native stats produced poise");

        Config config;
        config.mMaximumPoise = poiseFromStats(imperfect);
        config.mAllowStanceBreak = false;
        config.mStaminaRegen = 0.f;
        Incoming light;
        light.mDamage = 10.f;
        light.mStanceDamage = 10.f;
        light.mLevel = HurtLevel::Small;
        Controller strong;
        strong.configure(config);
        start(strong);
        const auto attackSequence = strong.sequence();
        for (int hit = 0; hit < 4; ++hit)
        {
            const float frame = strong.frame();
            check(strong.receiveHit(light) == HitResult::Hit, "poise must not suppress health damage");
            check(strong.action() == Action::Light1 && strong.sequence() == attackSequence,
                "weak contact interrupted a strong enemy's attack");
            close(strong.frame(), frame, "absorbed stagger restarted the attack clock");
            strong.configure(config);
        }
        close(strong.poise(), .8f, "per-frame configure refilled damaged poise");
        for (int tick = 0; tick < 500 && !strong.activeHit(); ++tick)
            strong.update({}, TickSeconds);
        check(strong.activeHit().has_value(), "strong enemy could not reach its hit despite resisting contacts");
        Incoming tiny = light;
        tiny.mStanceDamage = .1f;
        const auto pending = *strong.activeHit();
        strong.receiveHit(tiny);
        check(strong.activeHit() && strong.activeHit()->mSequence == pending.mSequence
                && strong.activeHit()->mHitIndex == pending.mHitIndex,
            "absorbed stagger discarded the enemy's pending contact");
        strong.receiveHit(light);
        check(strong.action() == Action::HurtSmall, "accumulated light hits must eventually stagger a strong enemy");
        close(strong.poise(), config.mMaximumPoise, "stagger must refill the separate poise pool");
        const auto staggerSequence = strong.sequence();
        advance(strong, .05f);
        strong.receiveHit(light);
        check(strong.sequence() == staggerSequence && strong.frame() > 0.f,
            "one subsequent weak contact restarted a strong enemy's stagger");

        Controller buffered;
        buffered.configure(config);
        start(buffered);
        for (int tick = 0; tick < 500 && buffered.frame() < buffered.definition().mInputFrom; ++tick)
            buffered.update({}, TickSeconds);
        Input followup;
        followup.mLight.mPressed = true;
        buffered.update(followup, TickSeconds);
        buffered.receiveHit(light);
        for (int tick = 0; tick < 500 && buffered.action() == Action::Light1; ++tick)
            buffered.update({}, TickSeconds);
        check(buffered.action() == Action::Light2, "absorbed stagger discarded the buffered attack continuation");

        Controller weak;
        Config weakConfig = config;
        weakConfig.mMaximumPoise = poiseFromStats(rat);
        weak.configure(weakConfig);
        start(weak);
        weak.receiveHit(light);
        check(weak.action() == Action::HurtSmall, "weak enemy no longer staggers on an ordinary hit");
        Controller equal;
        weakConfig.mMaximumPoise = light.mStanceDamage;
        equal.configure(weakConfig);
        equal.receiveHit(light);
        check(equal.action() == Action::HurtSmall, "poise threshold equality failed to stagger");
        Controller disabled;
        disabled.configure(Config{});
        start(disabled);
        disabled.receiveHit(light);
        check(disabled.action() == Action::HurtSmall, "disabled poise changed legacy hit reactions");

        Controller heavy;
        heavy.configure(config);
        start(heavy);
        Incoming impact = light;
        impact.mStanceDamage = 30.f;
        impact.mLevel = HurtLevel::Large;
        heavy.receiveHit(impact);
        check(heavy.action() == Action::Light1, "moderate boss poise should absorb one heavy impact");
        heavy.receiveHit(impact);
        check(heavy.action() == Action::HurtLarge, "successive heavy blows failed to interrupt boss poise");
        for (const bool critical : { false, true })
        {
            Controller forced;
            forced.configure(config);
            start(forced);
            impact = light;
            impact.mCritical = critical;
            impact.mLevel = critical ? HurtLevel::Large : HurtLevel::Knockdown;
            forced.receiveHit(impact);
            check(forced.action() == (critical ? Action::HurtLarge : Action::Knockdown),
                "poise masked a critical or explicit knockdown");
        }
        Controller stance;
        Config stanceConfig = config;
        stanceConfig.mAllowStanceBreak = true;
        stanceConfig.mMaximumStance = 10.f;
        stance.configure(stanceConfig);
        check(stance.receiveHit(light) == HitResult::StanceBroken && stance.criticalReady(),
            "poise blocked the separate stance-break critical mechanic");

        Controller recovery;
        config.mPoiseRecoveryDelay = .5f;
        recovery.configure(config);
        recovery.receiveHit(light);
        advance(recovery, .25f);
        close(recovery.poise(), config.mMaximumPoise - 10.f, "poise recovered before its delay");
        recovery.receiveHit(light);
        advance(recovery, .25f);
        close(recovery.poise(), config.mMaximumPoise - 20.f, "new contact did not restart recovery delay");
        Incoming harmless = light;
        harmless.mDamage = 0.f;
        recovery.receiveHit(harmless);
        close(recovery.poise(), config.mMaximumPoise - 20.f, "zero-damage hit depleted poise");
        recovery.applyHitStop(1.f);
        advance(recovery, .3f);
        close(recovery.poise(), config.mMaximumPoise,
            "poise did not recover in elapsed time or harmless contact prolonged recovery");
        recovery.receiveHit(light);
        recovery.cancelInputActions();
        close(recovery.poise(), config.mMaximumPoise - 10.f, "menu cancellation restored poise");
        recovery.reset();
        close(recovery.poise(), config.mMaximumPoise, "load/suspension reset did not restore poise");
        recovery.setDead(true);
        check(recovery.poise() == 0.f && recovery.receiveHit(light) == HitResult::Ignored,
            "death retained active poise state");
        recovery.setDead(false);
        close(recovery.poise(), config.mMaximumPoise, "resurrection reused old poise damage");
        Config changed = config;
        recovery.receiveHit(light);
        changed.mMaximumPoise *= .5f;
        recovery.configure(changed);
        close(recovery.poise(), (config.mMaximumPoise - 10.f) * .5f, "stat change refilled poise fraction");

        Controller guard;
        guard.configure(config);
        Input block;
        block.mGuard.mHeld = true;
        advance(guard, .2f, block);
        check(guard.receiveHit(light) == HitResult::Blocked, "poise guard fixture did not block");
        close(guard.poise(), config.mMaximumPoise, "blocked hit consumed poise");
        Controller dodging;
        dodging.configure(config);
        Input dodge;
        dodge.mMoveY = 1.f;
        dodge.mDodge.mPressed = true;
        dodging.update(dodge, TickSeconds);
        dodge.mDodge = { false, false, true };
        dodging.update(dodge, TickSeconds);
        for (int tick = 0; tick < 100 && !dodging.invincible(); ++tick)
            dodging.update({}, TickSeconds);
        check(dodging.receiveHit(light) == HitResult::Dodged, "poise dodge fixture missed invincibility");
        close(dodging.poise(), config.mMaximumPoise, "dodged hit consumed poise");
    }
}

#endif
