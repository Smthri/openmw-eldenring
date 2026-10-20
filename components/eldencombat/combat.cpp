#include "combat.hpp"

#ifdef OPENMW_ELDENCOMBAT_IMPORTED_TIMINGS
#include "imported_timings.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iomanip>
#include <sstream>
#include <utility>

namespace EldenCombat
{
    namespace
    {
        constexpr float Never = 9999.f;
        constexpr float SprintHoldFrames = 10.f;
        constexpr float SprintGroundGraceSeconds = 0.12f;
        constexpr float GuardRaiseFrames = 4.f;
        constexpr float GuardCounterFrames = 20.f;
        constexpr float GuardFacingDot = 0.17364818f; // cos(80 degrees)
        constexpr float StickDeadzone = 0.1f;

        struct WeaponProfile
        {
            float mSpeed;
            float mCost;
            float mStance;
            int mChain;
            bool mMultiHit;
        };

        // The generated proprietary action table is absent from the source repository.
        // These are OpenMW fallback profiles; they are not extracted Elden Ring timings.
        WeaponProfile profile(WeaponClass weapon)
        {
            switch (weapon)
            {
                case WeaponClass::Dagger:
                case WeaponClass::Claw:
                    return { 1.35f, 0.7f, 0.65f, 5, false };
                case WeaponClass::Fist:
                    return { 1.25f, 0.75f, 0.8f, 5, false };
                case WeaponClass::ThrustingSword:
                case WeaponClass::CurvedSword:
                    return { 1.15f, 0.9f, 0.9f, 5, false };
                case WeaponClass::Greatsword:
                case WeaponClass::CurvedGreatsword:
                case WeaponClass::GreatSpear:
                case WeaponClass::HeavyThrustingSword:
                case WeaponClass::Halberd:
                case WeaponClass::Reaper:
                    return { 0.85f, 1.3f, 1.6f, 4, false };
                case WeaponClass::GreatHammer:
                case WeaponClass::Greataxe:
                    return { 0.75f, 1.5f, 2.f, 4, false };
                case WeaponClass::ColossalSword:
                case WeaponClass::ColossalWeapon:
                    return { 0.65f, 1.8f, 2.5f, 3, false };
                case WeaponClass::Twinblade:
                    return { 1.1f, 1.f, 0.8f, 5, true };
                case WeaponClass::Hammer:
                case WeaponClass::Axe:
                case WeaponClass::Flail:
                    return { 0.95f, 1.1f, 1.4f, 5, false };
                case WeaponClass::Torch:
                    return { 1.f, 0.85f, 0.65f, 3, false };
                case WeaponClass::Shield:
                    return { 0.95f, 1.f, 1.4f, 4, false };
                default:
                    return { 1.f, 1.f, 1.f, 5, false };
            }
        }

        bool dodgeAction(Action action)
        {
            return action == Action::Roll || action == Action::CrouchRoll || action == Action::Backstep;
        }

        bool leftAction(Action action)
        {
            return action >= Action::LeftLight1 && action <= Action::LeftLight6;
        }

        Action selectAttack(std::initializer_list<Action> choices, const Config& config, bool twoHanded)
        {
#ifdef OPENMW_ELDENCOMBAT_IMPORTED_TIMINGS
            const WeaponClass weapon = leftAction(*choices.begin()) ? config.mLeftWeaponClass : config.mWeaponClass;
            if (importedHasWeaponMoveset(weapon, twoHanded))
            {
                // The source selects the first move this weapon actually has.
                // A missing action in a known moveset must not invent a new attack.
                for (const Action action : choices)
                    if (importedHasAction(action, config, twoHanded))
                        return action;
                return Action::Idle;
            }
#else
            (void)config;
            (void)twoHanded;
#endif
            return *choices.begin();
        }

        bool pairedAction(Action action)
        {
            return action >= Action::PairedLight1 && action <= Action::PairedJump;
        }

        bool lightOpener(Action action)
        {
            return action == Action::RunLight || action == Action::RollAttack || action == Action::BackstepAttack
                || action == Action::CrouchAttack;
        }

        float nonnegative(float value, float fallback = 0.f)
        {
            return std::isfinite(value) ? std::max(0.f, value) : fallback;
        }

        float validatedRollDistance(float value, float fallback)
        {
            return std::clamp(std::isfinite(value) ? value : fallback, 0.1f, 20.f);
        }

        const NativeAttackTiming* nativeAttack(const Config& config)
        {
            if (!config.mNativeAttack)
                return nullptr;
            const auto& timing = *config.mNativeAttack;
            if (timing.mAnimation.empty() || !std::isfinite(timing.mDuration) || timing.mDuration <= 0.f
                || !std::isfinite(timing.mHitFrame) || timing.mHitFrame < 0.f || timing.mHitFrame > timing.mDuration)
                return nullptr;
            return &timing;
        }

        Action nativeAction(Action action, const Config& config)
        {
            if (nativeAttack(config))
            {
                if (action == Action::Heavy1Charge)
                    return Action::Heavy1;
                if (action == Action::Heavy2Charge)
                    return Action::Heavy2;
            }
            return action;
        }

        void configureNativeAttack(ActionDef& definition, Action action, const Config& config)
        {
            const auto* timing = nativeAttack(config);
            if (!timing || !Controller::isAttack(action))
                return;
            Hit hit = definition.mHitCount > 0 ? definition.mHits[0] : Hit{};
            hit.mFrom = hit.mTo = timing->mHitFrame;
            definition.mHits = {};
            definition.mHits[0] = hit;
            definition.mHitCount = 1;
            definition.mAnimation.clear();
            definition.mNativeAnimation = timing->mAnimation;
            definition.mAnimationSpeed = 1.f;
            definition.mDuration = timing->mDuration;
            definition.mInputFrom = definition.mDodgeInputFrom = std::max(0.f, timing->mDuration - 8.f);
            definition.mCancelLight = definition.mCancelHeavy = definition.mCancelLeft = timing->mDuration;
            definition.mCancelDodge = definition.mCancelJump = definition.mCancelGuard
                = definition.mCancelMove = timing->mDuration;
            definition.mInvincibleFrom = definition.mInvincibleTo = 0.f;
            definition.mChargeFrom = definition.mChargeTo = 0.f;
            definition.mDistance = 0.f;
            definition.mRootMotion.clear();
            definition.mRootMotionSpeed = 1.f;
            definition.mLanding.reset();
            definition.mLandingShort.reset();
        }

        void configureDodgeCost(ActionDef& definition, Action action, const Config& config)
        {
            if (dodgeAction(action))
                definition.mStartCost *= std::clamp(nonnegative(config.mDodgeStaminaMultiplier, 1.f), 0.f, 3.f);
        }

        void configureRollDistance(ActionDef& definition, Action action, const Config& config)
        {
            if (!config.mWeightBasedRollDistance || (action != Action::Roll && action != Action::CrouchRoll))
                return;

            const float distance = Controller::rollDistance(config);
            if (!definition.mRootMotion.empty())
            {
                const auto origin = Controller::rootMotionAt(definition, 0.f);
                const auto endpoint = Controller::rootMotionAt(definition, definition.mDuration);
                const float originalDistance = std::hypot(endpoint[0] - origin[0], endpoint[2] - origin[2]);
                if (std::isfinite(originalDistance) && originalDistance > 0.00001f)
                {
                    const float scale = distance / originalDistance;
                    for (auto& sample : definition.mRootMotion)
                    {
                        sample[0] = origin[0] + (sample[0] - origin[0]) * scale;
                        sample[2] = origin[2] + (sample[2] - origin[2]) * scale;
                    }
                }
                else
                    // A stationary or malformed curve cannot supply horizontal travel.
                    // Use the normal fallback curve with the configured distance instead.
                    definition.mRootMotion.clear();
            }
            definition.mDistance = distance;
        }

        float motionFraction(float frame, const ActionDef& definition)
        {
            // OpenMW uses its own physics and animation assets. This interpolation is a
            // fallback displacement curve rather than an Elden Ring root-motion sample.
            const float end = std::min(definition.mDuration, 18.f);
            if (end <= 0.f)
                return 0.f;
            const float progress = std::clamp(frame / end, 0.f, 1.f);
            return progress * progress * (3.f - 2.f * progress);
        }
    }

    void Input::resetKeepingGrip()
    {
        const bool twoHanded = mTwoHanded;
        const bool twoHandedLeft = mTwoHandedLeft;
        *this = {};
        mTwoHanded = twoHanded;
        mTwoHandedLeft = twoHandedLeft;
    }

    void Input::clearEdges()
    {
        for (Button* button : { &mDodge, &mLight, &mHeavy, &mGuard, &mJump, &mCritical, &mSpearThrust })
        {
            button->mPressed = false;
            button->mReleased = false;
        }
        mDodgeDirection.reset();
    }

    Controller::Controller()
        : mDef(actionDefinition(Action::Idle, mConfig))
    {
    }

    float poiseFromStats(const PoiseStats& stats)
    {
        const float endurance = nonnegative(stats.mEndurance);
        const float strength = nonnegative(stats.mStrength);
        float poise = std::max(0.f, endurance - 40.f) * 0.12f
            + std::max(0.f, strength - 40.f) * 0.06f;
        if (stats.mCreature)
        {
            const float health = nonnegative(stats.mHealth);
            // Some small creatures have unusually high Endurance. Their actual
            // health keeps rats and similar weak enemies easy to interrupt.
            poise *= std::min(1.f, health / 100.f);
            poise += std::min(20.f, std::sqrt(std::max(0.f, health - 100.f)) * 0.5f);
            poise += std::min(10.f, std::max(0.f, nonnegative(stats.mLevel) - 10.f) * 0.15f);
        }
        else
            poise += std::min(20.f, nonnegative(stats.mArmorRating) * 0.2f);
        return std::clamp(poise, 0.f, 45.f);
    }

    void Controller::configure(const Config& config)
    {
        const float previousMaximumStance = mConfig.mMaximumStance;
        const float previousMaximumPoise = mConfig.mMaximumPoise;
        mConfig = config;
        mConfig.mMaximumStamina = nonnegative(config.mMaximumStamina, 100.f);
        mConfig.mMaximumStance = std::max(1.f, nonnegative(config.mMaximumStance, 60.f));
        mConfig.mMaximumPoise = nonnegative(config.mMaximumPoise);
        mConfig.mPoiseRecoveryDelay = std::clamp(nonnegative(config.mPoiseRecoveryDelay, 2.5f), 0.25f, 10.f);
        mConfig.mStaminaRegen = nonnegative(config.mStaminaRegen, 45.f);
        mConfig.mGuardRegenMultiplier = nonnegative(config.mGuardRegenMultiplier, 0.5f);
        mConfig.mSprintDrain = nonnegative(config.mSprintDrain, 11.f);
        mConfig.mGuardStaminaTaken = nonnegative(config.mGuardStaminaTaken, 0.55f);
        mConfig.mSpeedMultiplier = std::clamp(nonnegative(config.mSpeedMultiplier, 1.f), 0.1f, 5.f);
        mConfig.mLeftSpeedMultiplier = std::clamp(nonnegative(config.mLeftSpeedMultiplier, 1.f), 0.1f, 5.f);
        mConfig.mLightRollDistance = validatedRollDistance(config.mLightRollDistance, 4.33f);
        mConfig.mMediumRollDistance = validatedRollDistance(config.mMediumRollDistance, 3.65f);
        mConfig.mHeavyRollDistance = validatedRollDistance(config.mHeavyRollDistance, 3.31f);
        mConfig.mDodgeStaminaMultiplier = std::clamp(nonnegative(config.mDodgeStaminaMultiplier, 1.f), 0.f, 3.f);
        mConfig.mPostHitProtectionSeconds = nonnegative(config.mPostHitProtectionSeconds);
        mStamina = std::clamp(mStamina, 0.f, mConfig.mMaximumStamina);
        mStance = std::clamp(mStance * mConfig.mMaximumStance / std::max(1.f, previousMaximumStance), 0.f,
            mConfig.mMaximumStance);
        // The world adapter configures every frame and before contacts. Preserve
        // the remaining fraction when equipment/stat modifiers change.
        mPoise = previousMaximumPoise > 0.f
            ? std::clamp(mPoise * mConfig.mMaximumPoise / previousMaximumPoise, 0.f, mConfig.mMaximumPoise)
            : mConfig.mMaximumPoise;
        // An equipment change does not alter the definition of an action already underway.
    }

    void Controller::setVitals(float currentStamina, float maximumStamina)
    {
        mConfig.mMaximumStamina = nonnegative(maximumStamina);
        mStamina = std::clamp(nonnegative(currentStamina), 0.f, mConfig.mMaximumStamina);
    }

    bool Controller::isAttack(Action action)
    {
        return (action >= Action::Light1 && action <= Action::GuardThrust) || action == Action::Critical;
    }

    Load Controller::loadFromRatio(float ratio, float lightThreshold, float heavyThreshold)
    {
        lightThreshold = std::clamp(std::isfinite(lightThreshold) ? lightThreshold : 0.3f, 0.f, 1.f);
        heavyThreshold = std::clamp(std::isfinite(heavyThreshold) ? heavyThreshold : 0.7f, 0.f, 1.f);
        if (lightThreshold > heavyThreshold)
            std::swap(lightThreshold, heavyThreshold);
        if (!std::isfinite(ratio))
            return Load::Heavy;
        ratio = std::max(0.f, ratio);
        return ratio < lightThreshold ? Load::Light : (ratio < heavyThreshold ? Load::Medium : Load::Heavy);
    }

    float Controller::rollDistance(const Config& config)
    {
        switch (config.mLoad)
        {
            case Load::Light:
                return validatedRollDistance(config.mLightRollDistance, 4.33f);
            case Load::Heavy:
                return validatedRollDistance(config.mHeavyRollDistance, 3.31f);
            default:
                return validatedRollDistance(config.mMediumRollDistance, 3.65f);
        }
    }

    std::array<float, 3> Controller::rootMotionAt(const ActionDef& definition, float frame)
    {
        if (definition.mRootMotion.empty() || !std::isfinite(frame))
            return {};
        const std::size_t last = definition.mRootMotion.size() - 1;
        const float at = std::clamp(frame * nonnegative(definition.mRootMotionSpeed, 1.f), 0.f,
            static_cast<float>(last));
        const std::size_t from = static_cast<std::size_t>(at);
        const std::size_t to = std::min(from + 1, last);
        const float blend = at - static_cast<float>(from);
        std::array<float, 3> result;
        for (std::size_t axis = 0; axis < result.size(); ++axis)
            result[axis] = definition.mRootMotion[from][axis]
                + (definition.mRootMotion[to][axis] - definition.mRootMotion[from][axis]) * blend;
        return result;
    }

    std::string stanceAnimation(WeaponClass weapon, bool twoHanded, unsigned int animationId)
    {
        unsigned int category = 0;
#ifdef OPENMW_ELDENCOMBAT_IMPORTED_TIMINGS
        category = importedStanceCategory(weapon, twoHanded);
#else
        (void)weapon;
        (void)twoHanded;
#endif
        std::ostringstream name;
        name << 'a' << std::setfill('0') << std::setw(3) << category << '_' << std::setw(6) << animationId;
        return name.str();
    }

    ActionDef Controller::actionDefinition(Action action, const Config& config, bool twoHanded)
    {
        if (leftAction(action) && config.mSpeedMultiplier != config.mLeftSpeedMultiplier)
        {
            Config left = config;
            left.mSpeedMultiplier = left.mLeftSpeedMultiplier
                = std::clamp(nonnegative(config.mLeftSpeedMultiplier, 1.f), 0.1f, 5.f);
            return actionDefinition(action, left, twoHanded);
        }
        action = nativeAction(action, config);
        if (action == Action::GuardThrust)
        {
            // The supplied bank has no separate shield-poke action. Keep the
            // authored one-handed spear thrust and its cost/contact clock,
            // with a shield-arm overlay supplied by guardAnimation().
            ActionDef result = actionDefinition(Action::Light1, config, false);
            result.mHitCount = std::min<std::size_t>(result.mHitCount, 1);
            result.mRootMotion.clear();
            result.mDistance = 0.f;
            return result;
        }
        if (action == Action::SpearThrust)
        {
            // An authored quick thrust using the existing two-handed spear
            // opener. The same speed scales pose, contact and root motion.
            // No charge restart, extra damage callback, or new asset is needed.
            Config thrust = config;
            thrust.mWeaponClass = WeaponClass::Spear;
            thrust.mNativeAttack.reset();
            thrust.mSpeedMultiplier = std::clamp(nonnegative(config.mSpeedMultiplier, 1.f) * 1.6f, 0.1f, 5.f);
            ActionDef result = actionDefinition(Action::Light1, thrust, true);
            result.mHitCount = std::min<std::size_t>(result.mHitCount, 1);
            result.mInputFrom = result.mDodgeInputFrom = result.mDuration;
            result.mCancelLight = result.mCancelHeavy = result.mCancelLeft = result.mCancelMove
                = result.mCancelGuard = result.mCancelJump = result.mCancelDodge = result.mDuration;
            return result;
        }
        if (action == Action::Deflected)
        {
            ActionDef result;
            // A repelled weapon has no damage, travel or new stamina charge.
            // Its recovery cannot be cancelled by the interrupted combo.
            result.mDuration = 18.f;
            result.mInputFrom = result.mDodgeInputFrom = 12.f;
            result.mCancelLight = result.mCancelHeavy = result.mCancelLeft = result.mCancelMove
                = result.mCancelGuard = result.mCancelJump = result.mCancelDodge = result.mDuration;
            return result;
        }
#ifdef OPENMW_ELDENCOMBAT_IMPORTED_TIMINGS
        if (std::optional<ActionDef> imported = importedActionDefinition(action, config, twoHanded))
        {
            configureRollDistance(*imported, action, config);
            configureDodgeCost(*imported, action, config);
            configureNativeAttack(*imported, action, config);
            return std::move(*imported);
        }
#endif
        ActionDef result;
        result.mDuration = 60.f;
        result.mInputFrom = 10.f;
        result.mDodgeInputFrom = 10.f;
        result.mCancelLight = 25.f;
        result.mCancelHeavy = 25.f;
        result.mCancelLeft = 25.f;
        result.mCancelDodge = 28.f;
        result.mCancelJump = 30.f;
        result.mCancelGuard = 30.f;
        result.mCancelMove = 30.f;

        if (isAttack(action))
        {
            Hit hit{ 11.f, 13.f, 1.f, 12.f, 10.f, 1.f, 0.05f };
            const WeaponProfile weapon = profile(leftAction(action) ? config.mLeftWeaponClass : config.mWeaponClass);
            switch (action)
            {
                case Action::Light2:
                    result.mDuration = 62.f;
                    hit.mFrom = 13.f;
                    hit.mTo = 15.f;
                    break;
                case Action::Light3:
                case Action::Light4:
                case Action::Light5:
                case Action::Light6:
                    hit.mDamageMultiplier = 1.05f + 0.025f * (static_cast<int>(action) - static_cast<int>(Action::Light3));
                    break;
                case Action::Heavy1Charge:
                case Action::Heavy2Charge:
                    result.mDuration = 90.f;
                    result.mChargeFrom = 8.f;
                    result.mChargeTo = 30.f;
                    result.mInputFrom = 40.f;
                    result.mDodgeInputFrom = 40.f;
                    result.mCancelLight = result.mCancelHeavy = result.mCancelLeft = 65.f;
                    result.mCancelDodge = result.mCancelJump = result.mCancelGuard = result.mCancelMove = 65.f;
                    result.mStrength = 1.f;
                    hit = { 48.f, 52.f, 1.6f, 30.f, 30.f, 1.6f, 0.1f };
                    break;
                case Action::Heavy1:
                case Action::Heavy2:
                    result.mDuration = 68.f;
                    result.mStrength = 0.75f;
                    result.mCancelLight = result.mCancelHeavy = result.mCancelLeft = 35.f;
                    result.mCancelDodge = result.mCancelJump = result.mCancelGuard = result.mCancelMove = 38.f;
                    hit = { 18.f, 21.f, 1.25f, 20.f, 20.f, 1.25f, 0.075f };
                    break;
                case Action::GuardCounter:
                    result.mStrength = 0.9f;
                    result.mCancelLight = result.mCancelHeavy = result.mCancelLeft = 38.f;
                    result.mCancelDodge = result.mCancelJump = result.mCancelGuard = result.mCancelMove = 40.f;
                    hit = { 20.f, 23.f, 1.5f, 20.f, 30.f, 1.5f, 0.08f };
                    break;
                case Action::RunLight:
                case Action::RollAttack:
                case Action::BackstepAttack:
                case Action::CrouchAttack:
                    hit.mFrom = 9.f;
                    hit.mTo = 12.f;
                    hit.mDamageMultiplier = 1.05f;
                    break;
                case Action::RunHeavy:
                case Action::JumpHeavy:
                    result.mStrength = 0.8f;
                    hit = { 15.f, 18.f, 1.35f, 20.f, 25.f, 1.35f, 0.08f };
                    break;
                case Action::JumpLight:
                case Action::PairedJump:
                    hit = { 8.f, 11.f, 1.1f, 12.f, 15.f, 1.1f, 0.06f };
                    break;
                case Action::Critical:
                    result.mDuration = 75.f;
                    result.mInputFrom = result.mDodgeInputFrom = 65.f;
                    result.mCancelLight = result.mCancelHeavy = result.mCancelLeft = 75.f;
                    result.mCancelDodge = result.mCancelJump = result.mCancelGuard = result.mCancelMove = 75.f;
                    result.mInvincibleTo = 60.f;
                    result.mStrength = 1.f;
                    hit = { 28.f, 30.f, 3.f, 0.f, 60.f, 3.f, 0.12f };
                    break;
                default:
                    break;
            }
            if (action == Action::Heavy2 || action == Action::Heavy2Charge)
                hit.mDamageMultiplier += 0.05f;
            hit.mStaminaCost *= weapon.mCost;
            hit.mStanceDamage *= weapon.mStance;
            // Distinct two-handed fallback timings; no invented two-handing damage bonus.
            const float speed = std::clamp(weapon.mSpeed * config.mSpeedMultiplier * (twoHanded ? 0.95f : 1.f), 0.1f, 5.f);
            result.mDuration /= speed;
            result.mInputFrom /= speed;
            result.mDodgeInputFrom /= speed;
            result.mCancelLight /= speed;
            result.mCancelHeavy /= speed;
            result.mCancelLeft /= speed;
            result.mCancelDodge /= speed;
            result.mCancelJump /= speed;
            result.mCancelGuard /= speed;
            result.mCancelMove /= speed;
            result.mChargeFrom /= speed;
            result.mChargeTo /= speed;
            hit.mFrom /= speed;
            hit.mTo /= speed;
            result.mHits[0] = hit;
            result.mHitCount = 1;
            if ((weapon.mMultiHit || pairedAction(action)) && action != Action::Critical)
            {
                result.mHits[0].mDamageMultiplier *= 0.6f;
                result.mHits[0].mStaminaCost *= 0.5f;
                result.mHits[0].mStanceDamage *= 0.6f;
                result.mHits[1] = result.mHits[0];
                result.mHits[1].mFrom += 4.f / speed;
                result.mHits[1].mTo += 4.f / speed;
                result.mHitCount = 2;
            }
            configureNativeAttack(result, action, config);
            return result;
        }

        switch (action)
        {
            case Action::Idle:
                return {};
            case Action::Roll:
            case Action::CrouchRoll:
            {
                const bool heavy = config.mLoad == Load::Heavy;
                result.mDuration = heavy ? 44.f : (config.mLoad == Load::Light ? 32.f : 36.f);
                result.mInputFrom = result.mDodgeInputFrom = 10.f;
                result.mCancelLight = result.mCancelHeavy = result.mCancelLeft = 18.f;
                result.mCancelDodge = result.mCancelJump = heavy ? 32.f : 24.f;
                result.mCancelMove = result.mCancelGuard = heavy ? 34.f : 26.f;
                result.mInvincibleTo = heavy ? 12.f : 13.f;
                result.mStartCost = 12.f;
                result.mDistance = config.mLoad == Load::Light ? 4.33f : (heavy ? 3.31f : 3.65f);
                if (action == Action::CrouchRoll)
                    result.mDistance *= 3.57f / 3.65f;
                break;
            }
            case Action::Backstep:
                result.mDuration = 30.f;
                result.mInputFrom = result.mDodgeInputFrom = 7.f;
                result.mCancelLight = result.mCancelHeavy = result.mCancelLeft = 12.f;
                result.mCancelDodge = result.mCancelJump = 18.f;
                result.mCancelMove = result.mCancelGuard = 20.f;
                result.mInvincibleTo = 7.f;
                result.mStartCost = 8.f;
                result.mDistance = 2.5f;
                break;
            case Action::Jump:
                result.mDuration = 30.f;
                result.mInputFrom = result.mDodgeInputFrom = 0.f;
                result.mCancelLight = result.mCancelHeavy = result.mCancelLeft = 6.f;
                result.mCancelDodge = result.mCancelJump = result.mCancelGuard = result.mCancelMove = 30.f;
                result.mStartCost = 10.f;
                break;
            case Action::GuardHit:
                result.mDuration = 30.f;
                result.mInputFrom = result.mDodgeInputFrom = 0.f;
                result.mCancelLight = result.mCancelHeavy = result.mCancelLeft = 8.f;
                result.mCancelDodge = result.mCancelJump = result.mCancelGuard = result.mCancelMove = 10.f;
                break;
            case Action::HurtSmall:
                result.mDuration = 34.f;
                result.mCancelMove = 15.f;
                break;
            case Action::HurtMiddle:
                result.mDuration = 57.f;
                result.mCancelMove = 28.f;
                break;
            case Action::HurtLarge:
                result.mDuration = 80.f;
                result.mCancelMove = 42.f;
                break;
            case Action::Knockdown:
                result.mDuration = 120.f;
                result.mCancelMove = 90.f;
                result.mInvincibleFrom = 8.f;
                result.mInvincibleTo = 70.f;
                break;
            case Action::GuardBreak:
                result.mDuration = 60.f;
                result.mCancelMove = 48.f;
                break;
            case Action::StanceBreak:
                result.mDuration = 48.f;
                result.mCancelMove = 48.f;
                break;
            case Action::Dead:
                result.mDuration = Never;
                result.mInvincibleTo = Never;
                break;
            default:
                break;
        }
        if (action >= Action::GuardBreak)
        {
            result.mInputFrom = result.mDodgeInputFrom = std::max(0.f, result.mCancelMove - 8.f);
            result.mCancelLight = result.mCancelHeavy = result.mCancelLeft = result.mCancelMove;
            result.mCancelDodge = result.mCancelJump = result.mCancelGuard = result.mCancelMove;
            if (action == Action::Knockdown)
            {
                result.mCancelDodge = 65.f;
                result.mDodgeInputFrom = 57.f;
            }
        }
        configureRollDistance(result, action, config);
        configureDodgeCost(result, action, config);
        return result;
    }

    void Controller::spend(float normalizedCost, Update* result)
    {
        const float before = mStamina;
        mStamina = std::max(0.f, mStamina - nonnegative(normalizedCost) * mConfig.mMaximumStamina / 100.f);
        if (result)
            result->mStaminaSpent += before - mStamina;
    }

    bool Controller::leftButtonAttacks(const Input& input) const
    {
        return !input.mTwoHanded && !mConfig.mHasShield && mConfig.mHasLeftWeapon;
    }

    bool Controller::canGuardThrust(const Input& input) const
    {
        return !input.mHandsBusy && input.mGuard.mHeld && mConfig.mHasShield && !input.mTwoHanded && !mConfig.mNativeAttack
            && (input.mGrounded || input.mFlying)
            && (mConfig.mWeaponClass == WeaponClass::Spear || mConfig.mWeaponClass == WeaponClass::GreatSpear);
    }

    void Controller::readButtons(const Input& input, float frames)
    {
        const auto buffer = [&](Request request) {
            mBuffer = BufferedRequest{
                request, request == Request::Dodge ? input.mDodgeDirection : std::nullopt };
        };
        const bool listening = mAction == Action::Idle || mAction == Action::Jump || mFrame >= mDef.mInputFrom;
        const bool dodgeListening = mAction == Action::Idle || mAction == Action::Jump || mFrame >= mDef.mDodgeInputFrom;
        if (input.mDodge.mPressed)
        {
            mDodgeHoldFrames = 0.f;
            mDodgeArmed = !input.mFlying;
        }
        if (input.mDodge.mHeld)
            mDodgeHoldFrames += frames;
        if (input.mDodge.mReleased)
        {
            if (!input.mFlying && mDodgeArmed && mDodgeHoldFrames < SprintHoldFrames && dodgeListening)
                buffer(Request::Dodge);
            mDodgeArmed = false;
            mDodgeHoldFrames = 0.f;
            mSprintSpent = false;
        }
        if (!input.mSprint && !input.mDodge.mHeld)
            mSprintSpent = false;
        if (!listening)
            return;
        // One slot, newest input wins, matching the Rust state machine.
        if (input.mJump.mPressed && !input.mFlying)
            buffer(Request::Jump);
        if (input.mHeavy.mPressed)
            buffer(Request::Heavy);
        if (input.mGuard.mPressed && leftButtonAttacks(input))
            buffer(Request::Left);
        if (input.mLight.mPressed)
            buffer(Request::Light);
        if (input.mCritical.mPressed && input.mCriticalAvailable)
            buffer(Request::Critical);
        // A boss art can only start from idle. Do not buffer one into an
        // unrelated recovery, change its contextual opener, or chain it.
        if (input.mSpearThrust.mPressed && input.mAIControlled && input.mInCombat
            && input.mGrounded && !input.mFlying && input.mTwoHanded
            && mConfig.mWeaponClass == WeaponClass::Spear && !mConfig.mNativeAttack
            && mAction == Action::Idle)
            buffer(Request::SpearThrust);
    }

    void Controller::start(Action action, const Input& input, Update* result)
    {
        action = nativeAction(action, mConfig);
        const bool resumeGuard = (mAction == Action::GuardHit || mAction == Action::GuardThrust)
            && action == Action::Idle;
        const bool thrustGuard = action == Action::GuardThrust && canGuardThrust(input);
        if (action == Action::Jump && result)
            result->mJumpMovementMultiplier = mSprinting ? 1.504f : 1.f;
        mAction = action;
        mTwoHanded = input.mTwoHanded;
        mTwoHandedLeft = input.mTwoHanded && input.mTwoHandedLeft;
        mMirrorAttackRootMotion = isAttack(action) && mTwoHandedLeft && mConfig.mWeaponClass != WeaponClass::Shield;
        mDef = actionDefinition(action, mConfig, mTwoHanded);
        mJumpWasAirborne = !input.mGrounded && (action == Action::Jump || mDef.mLanding);
        mFrame = 0.f;
        mPaidHits = 0;
        mLandedHits = 0;
        mDeferredHits = 0;
        ++mSequence;
        mSprinting = false;
        mSprintGroundGraceRemaining = 0.f;
        // Guard recoil has its own recovery; a block does not keep the guard
        // active after its button is released or through the recoil window.
        mGuarding = thrustGuard || (resumeGuard && canGuardThrust(input));
        if (action != Action::GuardHit && !resumeGuard && !thrustGuard)
            mGuardFrames = 0.f;
        spend(mDef.mStartCost, result);
        // Jump swings pay as soon as they are initiated, as the source's AirAttack does.
        if (action == Action::JumpLight || action == Action::JumpHeavy || action == Action::PairedJump)
        {
            for (std::size_t index = 0; index < mDef.mHitCount; ++index)
            {
                spend(mDef.mHits[index].mStaminaCost, result);
                mPaidHits |= 1u << index;
            }
        }
        if (action == Action::GuardCounter)
            mGuardCounterFrames = 0.f;
        if (!mDef.mNativeAnimation.empty() && mDef.mHits[0].mFrom == 0.f)
        {
            // Creatures without an explicit hit key strike on the native start
            // key. Keep that event in this update, including a zero-length window.
            if ((mPaidHits & 1u) == 0)
            {
                spend(mDef.mHits[0].mStaminaCost, result);
                mPaidHits |= 1u;
            }
            collectHits(-1.f);
        }
        if (result)
        {
            result->mActionChanged = true;
            result->mAttackStarted |= isAttack(action);
            result->mJumpStarted |= action == Action::Jump;
        }
    }

    Action Controller::nextLight() const
    {
        if (!mFlying && (!mGrounded || mAction == Action::Jump))
            return Action::JumpLight;
        if (!mFlying && (mAction == Action::Roll || mAction == Action::CrouchRoll))
            return selectAttack({ Action::RollAttack, Action::Light1 }, mConfig, mTwoHanded);
        if (!mFlying && mAction == Action::Backstep)
            return selectAttack({ Action::BackstepAttack, Action::Light1 }, mConfig, mTwoHanded);
        if (lightOpener(mAction))
            return selectAttack({ Action::Light2, Action::Light1 }, mConfig, mTwoHanded);
#ifdef OPENMW_ELDENCOMBAT_IMPORTED_TIMINGS
        if (importedActionDefinition(Action::Light1, mConfig, mTwoHanded))
        {
            if (mAction >= Action::Light1 && mAction < Action::Light6)
            {
                const Action next = static_cast<Action>(static_cast<int>(mAction) + 1);
                if (importedActionDefinition(next, mConfig, mTwoHanded))
                    return next;
            }
            return Action::Light1;
        }
#endif
        const int chain = profile(mConfig.mWeaponClass).mChain;
        if (mAction >= Action::Light1 && static_cast<int>(mAction) < static_cast<int>(Action::Light1) + chain - 1)
            return static_cast<Action>(static_cast<int>(mAction) + 1);
        return Action::Light1;
    }

    Action Controller::nextHeavy() const
    {
        if (guardCounterReady())
            return selectAttack({ Action::GuardCounter, Action::Heavy1Charge }, mConfig, mTwoHanded);
        if (!mFlying && (!mGrounded || mAction == Action::Jump))
            return Action::JumpHeavy;
        if (mAction == Action::Heavy1 || mAction == Action::Heavy1Charge)
            return selectAttack({ Action::Heavy2Charge, Action::Heavy1Charge }, mConfig, mTwoHanded);
        return selectAttack({ Action::Heavy1Charge, Action::Heavy1 }, mConfig, mTwoHanded);
    }

    bool Controller::pairedWeaponsActive(bool twoHanded) const
    {
        if (!mConfig.mPairedWeapons || twoHanded)
            return false;
#ifdef OPENMW_ELDENCOMBAT_IMPORTED_TIMINGS
        if (importedHasWeaponMoveset(mConfig.mWeaponClass, false))
            return importedHasAction(Action::PairedLight1, mConfig, false);
#endif
        return true;
    }

    Action Controller::nextLeft() const
    {
        if (pairedWeaponsActive(mTwoHanded))
        {
            if (!mFlying && (!mGrounded || mAction == Action::Jump))
                return Action::PairedJump;
            if (!mFlying && (mAction == Action::Roll || mAction == Action::CrouchRoll))
                return selectAttack({ Action::PairedRoll, Action::PairedLight1 }, mConfig, mTwoHanded);
            if (!mFlying && mAction == Action::Backstep)
                return selectAttack({ Action::PairedBackstep, Action::PairedLight1 }, mConfig, mTwoHanded);
            if (mAction >= Action::PairedLight1 && mAction < Action::PairedLight6)
            {
                const Action next = static_cast<Action>(static_cast<int>(mAction) + 1);
#ifdef OPENMW_ELDENCOMBAT_IMPORTED_TIMINGS
                if (importedActionDefinition(Action::PairedLight1, mConfig, mTwoHanded))
                    return importedActionDefinition(next, mConfig, mTwoHanded) ? next : Action::PairedLight1;
#endif
                if (static_cast<int>(next) < static_cast<int>(Action::PairedLight1) + profile(mConfig.mWeaponClass).mChain)
                    return next;
            }
            return selectAttack({ Action::PairedLight1 }, mConfig, mTwoHanded);
        }
        if (mAction >= Action::LeftLight1 && mAction < Action::LeftLight6)
        {
            const Action next = static_cast<Action>(static_cast<int>(mAction) + 1);
#ifdef OPENMW_ELDENCOMBAT_IMPORTED_TIMINGS
            if (importedActionDefinition(Action::LeftLight1, mConfig, mTwoHanded))
                return importedActionDefinition(next, mConfig, mTwoHanded) ? next : Action::LeftLight1;
#endif
            if (static_cast<int>(next) < static_cast<int>(Action::LeftLight1) + profile(mConfig.mLeftWeaponClass).mChain)
                return next;
        }
        return selectAttack({ Action::LeftLight1 }, mConfig, mTwoHanded);
    }

    void Controller::startRequest(const BufferedRequest& request, const Input& input, Update& result)
    {
        auto startAttack = [&](Action action) {
            if (action != Action::Idle)
                start(action, input, &result);
        };
        auto startLeftAttack = [&] {
            startAttack(mAction == Action::Idle && input.mGrounded && !input.mFlying && mSprinting
                    && pairedWeaponsActive(mTwoHanded)
                    ? selectAttack({ Action::PairedRun, Action::PairedLight1 }, mConfig, mTwoHanded)
                    : nextLeft());
        };
        switch (request.mRequest)
        {
            case Request::Dodge:
            {
                if (!input.mGrounded || input.mFlying)
                    return;
                const auto direction = request.mDodgeDirection.value_or(std::array{ input.mMoveX, input.mMoveY });
                const float length = std::hypot(direction[0], direction[1]);
                const bool moving = length >= StickDeadzone;
                if (moving)
                {
                    mDodgeX = direction[0] / length;
                    mDodgeY = direction[1] / length;
                    start(input.mCrouching ? Action::CrouchRoll : Action::Roll, input, &result);
                }
                else
                {
                    mDodgeX = 0.f;
                    mDodgeY = -1.f;
                    start(Action::Backstep, input, &result);
                }
                break;
            }
            case Request::Jump:
                if (input.mGrounded && !input.mFlying)
                    start(Action::Jump, input, &result);
                break;
            case Request::Light:
                startAttack(canGuardThrust(input) && !mSprinting
                        && (mAction == Action::Idle || mAction == Action::GuardThrust || mAction == Action::GuardHit)
                        ? Action::GuardThrust
                        : mAction == Action::Idle && input.mGrounded && !input.mFlying && mSprinting
                        ? selectAttack({ Action::RunLight, Action::Light1 }, mConfig, mTwoHanded)
                        : (mAction == Action::Idle && input.mGrounded && !input.mFlying && input.mCrouching
                                ? selectAttack({ Action::CrouchAttack, Action::RollAttack, Action::Light1 }, mConfig,
                                    mTwoHanded)
                                : nextLight()));
                break;
            case Request::Heavy:
                startAttack(mAction == Action::Idle && input.mGrounded && !input.mFlying && mSprinting
                        ? selectAttack({ Action::RunHeavy, Action::Heavy1Charge }, mConfig, mTwoHanded)
                        : nextHeavy());
                break;
            case Request::Left:
                startLeftAttack();
                break;
            case Request::Critical:
                if (input.mCriticalAvailable)
                    start(Action::Critical, input, &result);
                break;
            case Request::SpearThrust:
                if (mAction == Action::Idle && input.mAIControlled && input.mInCombat
                    && input.mGrounded && !input.mFlying && input.mTwoHanded
                    && mConfig.mWeaponClass == WeaponClass::Spear && !mConfig.mNativeAttack)
                    start(Action::SpearThrust, input, &result);
                break;
        }
    }

    float Controller::cancelFrame(Request request) const
    {
        switch (request)
        {
            case Request::Light:
            case Request::Critical:
                return mDef.mCancelLight;
            case Request::SpearThrust:
                return mDef.mDuration;
            case Request::Heavy:
                return mDef.mCancelHeavy;
            case Request::Left:
                return mDef.mCancelLeft;
            case Request::Dodge:
                return mDef.mCancelDodge;
            case Request::Jump:
                return mDef.mCancelJump;
        }
        return Never;
    }

    Attack Controller::makeAttack(std::size_t hitIndex) const
    {
        const Hit& hit = mDef.mHits[hitIndex];
        // The fallback paired movesets alternate hands. Imported definitions can
        // explicitly identify a hand; sequence parity is an estimate when absent.
        // Both physical hands use the selected inventory item in a two-hand
        // grip (notably the extracted fist/claw movesets).
        const bool leftHand = mTwoHanded ? mTwoHandedLeft
            : hit.mLeftHand || leftAction(mAction)
                || (!hit.mHasHandAssignment && pairedAction(mAction) && hitIndex % 2 != 0);
        return { mAction, mSequence, hitIndex, mDef.mStrength, hit.mDamageMultiplier, hit.mStanceDamage,
            hit.mGuardDamage, hit.mHitStop, mTwoHanded, leftHand, pairedAction(mAction), mDef.mNativeAnimation };
    }

    Attack Controller::attack() const
    {
        return mDef.mHitCount > 0 ? makeAttack(0) : Attack{};
    }

    void Controller::collectHits(float previousFrame)
    {
        for (std::size_t index = 0; index < mDef.mHitCount; ++index)
        {
            const Hit& hit = mDef.mHits[index];
            if (mFrame < hit.mFrom || previousFrame >= hit.mTo || (mLandedHits & (1u << index)) != 0)
                continue;
            bool alreadyAvailable = false;
            for (std::size_t i = 0; i < mAvailableHitCount; ++i)
                alreadyAvailable |= mAvailableHits[i].mSequence == mSequence && mAvailableHits[i].mHitIndex == index;
            if (!alreadyAvailable && mAvailableHitCount < mAvailableHits.size())
                mAvailableHits[mAvailableHitCount++] = makeAttack(index);
        }
    }

    void Controller::landAttack(Update& result, bool preserveCrossedHits)
    {
        float hitEnd = 0.f;
        for (std::size_t index = 0; index < mDef.mHitCount; ++index)
            hitEnd = std::max(hitEnd, mDef.mHits[index].mTo);
        const bool finished = mFrame >= hitEnd;
        const bool connected = mLandedHits != 0;
        const float airborneFrame = mFrame;
        // Keep ownership while replacing the definition that owns this phase.
        const auto landing = finished && mDef.mLandingShort ? mDef.mLandingShort : mDef.mLanding;
        std::uint32_t landedHits = connected || finished ? ~std::uint32_t{ 0 } : 0;
        const auto assigned = [](const ActionDef& definition) {
            return std::all_of(definition.mHits.begin(), definition.mHits.begin() + definition.mHitCount,
                [](const Hit& hit) { return hit.mHasHandAssignment; });
        };
        if (!finished && assigned(mDef) && assigned(*landing))
        {
            // Air and landing phases can list the hands in different orders.
            // Match each hand's contacts in order so an airborne right hit does
            // not consume the left hit, or replay after touchdown. An expired
            // airborne window also stays spent even when it missed its target.
            landedHits = 0;
            for (std::size_t target = 0; target < landing->mHitCount; ++target)
            {
                const bool left = landing->mHits[target].mLeftHand;
                std::size_t ordinal = 0;
                for (std::size_t earlier = 0; earlier < target; ++earlier)
                    ordinal += landing->mHits[earlier].mLeftHand == left;
                for (std::size_t source = 0; source < mDef.mHitCount; ++source)
                {
                    if (mDef.mHits[source].mLeftHand != left)
                        continue;
                    if (ordinal > 0)
                    {
                        --ordinal;
                        continue;
                    }
                    if ((mLandedHits & (1u << source)) != 0 || airborneFrame >= mDef.mHits[source].mTo)
                        landedHits |= 1u << target;
                    break;
                }
            }
        }
        mDef = *landing;
        mFrame = finished ? 0.f
                          : std::min(airborneFrame, mDef.mHitCount > 0 ? mDef.mHits[0].mFrom : 0.f);
        // This is the same prepaid swing and damage sequence.
        mPaidHits = ~std::uint32_t{ 0 };
        mLandedHits = landedHits;
        if (!preserveCrossedHits)
            mAvailableHitCount = 0;
        mJumpWasAirborne = false;
        result.mActionChanged = true;
    }

    void Controller::tick(const Input& input, float seconds, Update& result)
    {
        if (mAction == Action::Dead)
            return;
        const float frames = seconds * AnimationFps;
        mGrounded = input.mGrounded;
        mFlying = input.mFlying;
        if (mFlying)
        {
            mSprinting = false;
            mSprintGroundGraceRemaining = 0.f;
            mDodgeArmed = false;
            if (mBuffer && (mBuffer->mRequest == Request::Jump || mBuffer->mRequest == Request::Dodge))
                mBuffer.reset();
        }
        if ((mAction == Action::Jump || mDef.mLanding) && !mGrounded)
            mJumpWasAirborne = true;
        mSprintGroundGraceRemaining = input.mGrounded && !mFlying ? SprintGroundGraceSeconds
            : std::max(0.f, mSprintGroundGraceRemaining - seconds);
        readButtons(input, frames);
        if (mAction == Action::GuardThrust)
        {
            // Guard follows the live shield/button, including during hit-stop.
            // Releasing it never cancels an already committed spear contact.
            mGuarding = canGuardThrust(input);
            mGuardFrames = mGuarding ? mGuardFrames + (mHitStopSeconds > 0.f ? 0.f : frames) : 0.f;
        }
        mPoiseRecoveryRemaining = std::max(0.f, mPoiseRecoveryRemaining - seconds);
        if (mPoiseRecoveryRemaining <= 0.f)
            mPoise = mConfig.mMaximumPoise;
        if (mHitStopSeconds > 0.f)
        {
            mHitStopSeconds = std::max(0.f, mHitStopSeconds - seconds);
            result.mFrozen = true;
            return;
        }
        result.mAdvancedSeconds += seconds;
        mGuardCounterFrames = std::max(0.f, mGuardCounterFrames - frames);
        mCriticalSeconds = std::max(0.f, mCriticalSeconds - seconds);
        mSinceHitSeconds += seconds;
        if (mSinceHitSeconds >= 5.f)
            mStance = mConfig.mMaximumStance;

        // Native jump height can bring the actor down before the imported
        // jump clip finishes. Resume ground controls immediately, while
        // keeping jump attacks' recovery and pending launch pulses intact.
        // The airborne latch also retains a landing observed during hit-stop.
        if (mAction == Action::Jump && mJumpWasAirborne && mGrounded && !mFlying)
            start(Action::Idle, input, &result);
        else if (mDef.mLanding && mJumpWasAirborne && mGrounded && !mFlying)
            landAttack(result);

        bool regenerate = false;
        if (mAction == Action::Idle)
        {
            // Opener selection needs the requested grip even on the first
            // update after equipping it; start() snapshots it for the attack.
            mTwoHanded = input.mTwoHanded;
            mTwoHandedLeft = input.mTwoHanded && input.mTwoHandedLeft;
            if (mBuffer && !input.mHandsBusy)
            {
                const BufferedRequest request = *mBuffer;
                mBuffer.reset();
                if (mStamina > 0.f || (request.mRequest == Request::Dodge && mConfig.mDodgeStaminaMultiplier == 0.f))
                {
                    startRequest(request, input, result);
                    if (mAction != Action::Idle)
                        return;
                }
            }
            const bool moving = std::hypot(input.mMoveX, input.mMoveY) >= StickDeadzone;
            // Native ground contact can flicker at steps and terrain seams.
            // Keep established sprint momentum/pose through a brief gap, but
            // never start a sprint in the air or relax jump/attack grounding.
            const bool sprintGrounded = input.mGrounded || (mSprinting && mSprintGroundGraceRemaining > 0.f);
            mSprinting = (input.mSprint || (input.mDodge.mHeld && mDodgeHoldFrames >= SprintHoldFrames))
                && moving && sprintGrounded && !mFlying && !mSprintSpent && !input.mHandsBusy && mStamina > 0.f;
            if (mSprinting && input.mInCombat)
            {
                spend(mConfig.mSprintDrain * seconds, &result);
                if (mStamina <= 0.f)
                {
                    mSprinting = false;
                    mSprintSpent = true;
                }
            }
            mGuarding = !input.mHandsBusy && input.mGuard.mHeld && !mSprinting && (mGrounded || mFlying)
                && (mConfig.mHasShield || input.mTwoHanded) && !leftButtonAttacks(input);
            mGuardFrames = mGuarding ? mGuardFrames + frames : 0.f;
            regenerate = !mSprinting && (mGrounded || mFlying);
        }
        else
        {
            // Releasing during the charge window replaces the charge with its ordinary swing.
            if (mDef.mChargeTo > mDef.mChargeFrom && mFrame >= mDef.mChargeFrom && mFrame < mDef.mChargeTo
                && !input.mHeavy.mHeld)
            {
                start(mAction == Action::Heavy2Charge ? Action::Heavy2 : Action::Heavy1, input, &result);
                return;
            }
            const float previousFrame = mFrame;
            mFrame += frames;
            for (std::size_t index = 0; index < mDef.mHitCount; ++index)
            {
                if ((mPaidHits & (1u << index)) == 0 && mFrame >= mDef.mHits[index].mFrom)
                {
                    spend(mDef.mHits[index].mStaminaCost, &result);
                    mPaidHits |= 1u << index;
                }
            }
            collectHits(previousFrame);
            if (!mDef.mRootMotion.empty() && mGrounded && !mFlying && mAction != Action::Jump)
            {
                const auto before = rootMotionAt(mDef, previousFrame);
                const auto after = rootMotionAt(mDef, mFrame);
                float right = -(after[0] - before[0]);
                float forward = after[2] - before[2];
                if (dodgeAction(mAction))
                {
                    // Imported rolls use the canonical Front clip. Backstep's clip is
                    // already authored backward, so undo that sign before rotating it.
                    if (mAction == Action::Backstep)
                    {
                        right = -right;
                        forward = -forward;
                    }
                    result.mDisplacementX += right * mDodgeY + forward * mDodgeX;
                    result.mDisplacementY += -right * mDodgeX + forward * mDodgeY;
                }
                else
                {
                    result.mDisplacementX += mMirrorAttackRootMotion ? -right : right;
                    result.mDisplacementY += forward;
                }
            }
            else if (dodgeAction(mAction) && mGrounded && !mFlying)
            {
                const float distance = mDef.mDistance * (motionFraction(mFrame, mDef) - motionFraction(previousFrame, mDef));
                result.mDisplacementX += mDodgeX * distance;
                result.mDisplacementY += mDodgeY * distance;
            }
            // Levitation can start during an aerial swing. Finish its authored
            // phase, then recover without waiting indefinitely for touchdown.
            // Slow updates may cross the last hit and the end of the clip in
            // one call; retain those hits for the adapter to consume afterward.
            if (mFlying && mDef.mLanding && mFrame >= mDef.mDuration)
                landAttack(result, true);
            // An imported aerial swing holds its pose until native physics
            // reports landing. Ground recovery and buffered cancels begin there.
            const bool airborneAttack = static_cast<bool>(mDef.mLanding);
            // Replacing Jump before physics consumes its launch pulse would
            // cancel that pulse and strand an aerial attack on the ground.
            const bool waitingForTakeoff = mAction == Action::Jump && !mJumpWasAirborne && !mFlying;
            if (!airborneAttack && !waitingForTakeoff && !input.mHandsBusy
                && mBuffer && mFrame >= cancelFrame(mBuffer->mRequest))
            {
                const BufferedRequest request = *mBuffer;
                mBuffer.reset();
                if (mStamina > 0.f || (request.mRequest == Request::Dodge && mConfig.mDodgeStaminaMultiplier == 0.f))
                {
                    const std::uint64_t previousSequence = mSequence;
                    startRequest(request, input, result);
                    if (mSequence != previousSequence)
                        return;
                }
            }
            regenerate = !airborneAttack && mFrame >= mDef.mCancelMove && (mGrounded || mFlying);
            if (!airborneAttack
                && ((input.mGuard.mHeld && !leftButtonAttacks(input) && mFrame >= mDef.mCancelGuard)
                    || (std::hypot(input.mMoveX, input.mMoveY) >= StickDeadzone && mFrame >= mDef.mCancelMove)
                    || mFrame >= mDef.mDuration))
                start(Action::Idle, input, &result);
        }
        if (regenerate)
        {
            const float before = mStamina;
            const float multiplier = mGuarding ? mConfig.mGuardRegenMultiplier : 1.f;
            mStamina = std::min(mConfig.mMaximumStamina,
                mStamina + mConfig.mStaminaRegen * multiplier * seconds * mConfig.mMaximumStamina / 100.f);
            result.mStaminaRegenerated += mStamina - before;
        }
    }

    Update Controller::update(const Input& input, float seconds)
    {
        Update result;
        mAvailableHitCount = 0;
        mDeferredHits = 0;
        // Protection follows simulation time, including hit-stop, rather than
        // animation time. Paused menus supply zero time and retain the remainder.
        advanceHitProtection(seconds);
        if (std::isfinite(seconds) && seconds > 0.f)
        {
            // Substeps preserve input/cancel decisions and do not skip narrow hit windows.
            Input stepInput = input;
            float remaining = std::min(seconds, 1.f);
            while (remaining > 0.f)
            {
                // End a frozen slice exactly when its timer expires so the
                // remainder advances animation and root motion in this update.
                const float elapsed = std::min({ remaining, TickSeconds,
                    mHitStopSeconds > 0.f ? mHitStopSeconds : TickSeconds });
                tick(stepInput, elapsed, result);
                stepInput.clearEdges();
                remaining -= elapsed;
            }
        }
        result.mAction = mAction;
        result.mFrame = mFrame;
        result.mDuration = mDef.mDuration;
        result.mMovementLocked = mAction != Action::Idle && mFrame < mDef.mCancelMove;
        result.mMoveX = dodgeAction(mAction) ? mDodgeX : input.mMoveX;
        result.mMoveY = dodgeAction(mAction) ? mDodgeY : input.mMoveY;
        result.mMovementMultiplier = mSprinting ? 1.504f : (mGuarding ? 0.65f : 1.f);
        result.mFrozen = result.mFrozen && result.mAdvancedSeconds == 0.f;
        if (result.mFrozen)
            result.mMovementMultiplier = 0.f;
        return result;
    }

    std::optional<Attack> Controller::activeHit() const
    {
        if (mAvailableHitCount > 0)
            return mAvailableHits[0];
        for (std::size_t index = 0; index < mDef.mHitCount; ++index)
        {
            const Hit& hit = mDef.mHits[index];
            if (mFrame >= hit.mFrom && mFrame < hit.mTo && ((mLandedHits | mDeferredHits) & (1u << index)) == 0)
                return makeAttack(index);
        }
        return std::nullopt;
    }

    void Controller::markHit()
    {
        const std::optional<Attack> active = activeHit();
        if (!active)
            return;
        if (active->mSequence == mSequence)
            mLandedHits |= 1u << active->mHitIndex;
        if (mAvailableHitCount > 0)
        {
            std::move(mAvailableHits.begin() + 1, mAvailableHits.begin() + mAvailableHitCount, mAvailableHits.begin());
            --mAvailableHitCount;
        }
    }

    void Controller::deferHit()
    {
        const std::optional<Attack> active = activeHit();
        if (!active)
            return;
        if (active->mSequence == mSequence)
            mDeferredHits |= 1u << active->mHitIndex;
        if (mAvailableHitCount > 0)
        {
            std::move(mAvailableHits.begin() + 1, mAvailableHits.begin() + mAvailableHitCount, mAvailableHits.begin());
            --mAvailableHitCount;
        }
    }

    bool Controller::invincible() const
    {
        return mAction == Action::Dead || (mFrame >= mDef.mInvincibleFrom && mFrame < mDef.mInvincibleTo);
    }

    bool Controller::guardUp() const
    {
        return mGuarding && mGuardFrames >= GuardRaiseFrames;
    }

    HitResult Controller::receiveHit(const Incoming& hit)
    {
        if (mAction == Action::Dead || mPostHitProtectionRemaining > 0.f)
            return HitResult::Ignored;
        if (!hit.mCritical && invincible())
            return HitResult::Dodged;
        if (hit.mLow && !mGrounded && hit.mAirborneClearance > 0.35f)
            return HitResult::Jumped;
        Input input;
        input.mTwoHanded = mTwoHanded;
        input.mTwoHandedLeft = mTwoHandedLeft;
        if (!hit.mUnblockable && !hit.mCritical && guardUp() && hit.mFacingDot >= GuardFacingDot)
        {
            // Incoming stamina damage is normalized; fatigue costs scale to the actor's maximum.
            spend(hit.mStaminaDamage * mConfig.mGuardStaminaTaken);
            if (mStamina <= 0.f)
            {
                start(Action::GuardBreak, input);
                mCriticalSeconds = 1.6f;
                mBuffer.reset();
                mAvailableHitCount = 0;
                return HitResult::GuardBroken;
            }
            mGuardCounterFrames = GuardCounterFrames;
            // Shield pokes may absorb a block while the right arm finishes its
            // thrust. Preserve its clock, prepaid hit and buffered follow-up;
            // guard break above still interrupts everything immediately.
            if (mAction == Action::GuardThrust)
                return HitResult::Blocked;
            start(Action::GuardHit, input);
            mAvailableHitCount = 0;
            return HitResult::Blocked;
        }

        protectAfterHit(hit.mDamage);
        mSinceHitSeconds = 0.f;
        if (mConfig.mAllowStanceBreak && !hit.mCritical)
        {
            mStance -= nonnegative(hit.mStanceDamage);
            if (mStance <= 0.f)
            {
                mStance = mConfig.mMaximumStance;
                mPoise = mConfig.mMaximumPoise;
                mPoiseRecoveryRemaining = mConfig.mPoiseRecoveryDelay;
                mCriticalSeconds = 1.6f;
                mBuffer.reset();
                mAvailableHitCount = 0;
                start(Action::StanceBreak, input);
                return HitResult::StanceBroken;
            }
        }
        if (mConfig.mMaximumPoise > 0.f)
        {
            const bool damaging = nonnegative(hit.mDamage) > 0.f;
            const float impact = damaging ? nonnegative(hit.mStanceDamage) : 0.f;
            if (damaging)
                mPoiseRecoveryRemaining = mConfig.mPoiseRecoveryDelay;
            mPoise = std::max(0.f, mPoise - impact);
            if (!hit.mCritical && hit.mLevel != HurtLevel::Knockdown && mPoise > 0.f)
                // Damage and stance loss still apply. Preserve the attack's
                // frame, queued follow-up and crossed hit windows so an enemy
                // can finish its swing through a weak hit.
                return HitResult::Hit;
            mPoise = mConfig.mMaximumPoise;
        }
        mBuffer.reset();
        mAvailableHitCount = 0;
        // Like the source simulation, aerial hits do not force a ground reaction.
        if (mGrounded)
        {
            Action action = Action::HurtMiddle;
            switch (hit.mLevel)
            {
                case HurtLevel::Small:
                    action = Action::HurtSmall;
                    break;
                case HurtLevel::Middle:
                    break;
                case HurtLevel::Large:
                    action = Action::HurtLarge;
                    break;
                case HurtLevel::Knockdown:
                    action = Action::Knockdown;
                    break;
            }
            start(action, input);
        }
        return HitResult::Hit;
    }

    bool Controller::deflectHit(const Attack& hit)
    {
        const auto pending = activeHit();
        if (!pending || pending->mSequence != hit.mSequence || pending->mHitIndex != hit.mHitIndex
            || mAction == Action::Dead || hit.mTwoHanded || !hit.mNativeAnimation.empty())
            return false;
        // Rebound ordinary one-handed light chains. Two-handed, heavy and
        // contextual attacks keep their stronger follow-through. Native
        // creature swings retain authored recovery, never a humanoid recoil.
        const Action action = pending->mAction;
        if (!((action >= Action::Light1 && action <= Action::Light6)
                || (action >= Action::LeftLight1 && action <= Action::LeftLight6)
                || (action >= Action::PairedLight1 && action <= Action::PairedLight6)))
            return false;
        Input input;
        input.mGrounded = mGrounded;
        input.mFlying = mFlying;
        input.mTwoHanded = hit.mTwoHanded;
        input.mTwoHandedLeft = hit.mTwoHanded && hit.mLeftHand;
        clearRequests();
        mAvailableHitCount = 0;
        start(Action::Deflected, input);
        return true;
    }

    void Controller::applyHitStop(float seconds)
    {
        mHitStopSeconds = std::max(mHitStopSeconds, nonnegative(seconds));
    }

    void Controller::protectAfterHit(float damage)
    {
        if (mAction != Action::Dead && nonnegative(damage) > 0.f && mPostHitProtectionRemaining <= 0.f)
            mPostHitProtectionRemaining = mConfig.mPostHitProtectionSeconds;
    }

    void Controller::advanceHitProtection(float seconds)
    {
        mPostHitProtectionRemaining = std::max(0.f, mPostHitProtectionRemaining - nonnegative(seconds));
        if (mPostHitProtectionRemaining < 1e-6f)
            mPostHitProtectionRemaining = 0.f;
    }

    void Controller::clearRequests()
    {
        mBuffer.reset();
        mDodgeArmed = false;
        mDodgeHoldFrames = 0.f;
        mSprintSpent = false;
    }

    void Controller::cancelInputActions()
    {
        clearRequests();
        mAvailableHitCount = 0;
        mSprinting = false;
        mSprintGroundGraceRemaining = 0.f;
        mGuarding = false;
        mGuardFrames = 0.f;
        if (isAttack(mAction))
        {
            Input input;
            input.mTwoHanded = mTwoHanded;
            input.mTwoHandedLeft = mTwoHandedLeft;
            start(Action::Idle, input);
        }
    }

    bool Controller::consumeCritical()
    {
        if (!criticalReady())
            return false;
        mCriticalSeconds = 0.f;
        mStance = mConfig.mMaximumStance;
        return true;
    }

    void Controller::setDead(bool dead)
    {
        if (dead)
        {
            Input input;
            start(Action::Dead, input);
            mBuffer.reset();
            mAvailableHitCount = 0;
            mHitStopSeconds = 0.f;
            mPostHitProtectionRemaining = 0.f;
            mCriticalSeconds = 0.f;
            mPoise = 0.f;
            mPoiseRecoveryRemaining = 0.f;
        }
        else if (mAction == Action::Dead)
            reset();
    }

    void Controller::reset(bool preserveHitProtection)
    {
        const Config config = mConfig;
        const std::uint64_t nextSequence = mSequence + 1;
        const float protection = preserveHitProtection ? mPostHitProtectionRemaining : 0.f;
        *this = Controller();
        configure(config);
        mStamina = mConfig.mMaximumStamina;
        mStance = mConfig.mMaximumStance;
        // Observers use this generation to detect state changes. Do not reuse a
        // prior action's generation after equipment changes or resurrection.
        mSequence = nextSequence;
        mPostHitProtectionRemaining = protection;
    }
}
