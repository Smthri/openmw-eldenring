#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_BOW_H
#define OPENMW_COMPONENTS_ELDENCOMBAT_BOW_H

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace EldenCombat
{
    inline constexpr std::string_view BowDrawAnimation = "a044_036010";
    inline constexpr std::string_view BowHoldAnimation = "a044_036020";
    inline constexpr std::string_view BowReleaseAnimation = "a044_036000";
    inline constexpr std::string_view BowIdleAnimation = "a014_000000";

    inline std::string bowStanceAnimation(unsigned animationId)
    {
        if (animationId > 999999)
            return {};
        return "a014_" + std::to_string(1000000 + animationId).substr(1);
    }

    // Older native tracks play release and follow-through separately; newer
    // tracks play both in AttackEnd. A state name alone cannot identify recoil.
    enum class NativeBowSection
    {
        Windup,
        Release,
        FollowThrough,
        ReleaseAndFollow
    };

    struct NativeBowTiming
    {
        float mStart;
        float mMinAttack;
        float mMaxAttack;
        float mRelease;
        float mFollowStart;
        float mFollowStop;
    };

    struct BowAnimationLengths
    {
        float mDraw;
        float mHold;
        float mRelease;
    };

    struct BowPose
    {
        std::string_view mClip;
        float mFrame;
        bool mLoop;
        // Fade the drawing hand onto the native bow's animated string nock.
        // Release lets the renderer ease its last correction out after detach.
        float mDrawWeight;
    };

    // The source draw ends at the loop's fully drawn pose. TAE event 2 fires
    // the arrow at release clip frame zero, which also matches the held pose.
    // Native text keys own charge strength, arrow attachment, launch and ammo.
    // holdFrame advances independently after the native windup clock stops.
    inline std::optional<BowPose> bowAnimationPose(float time, NativeBowSection section,
        const NativeBowTiming& timing, const BowAnimationLengths& lengths, float holdFrame)
    {
        for (float value : { time, timing.mStart, timing.mMinAttack, timing.mMaxAttack, timing.mRelease,
                 timing.mFollowStart, timing.mFollowStop, lengths.mDraw, lengths.mHold, lengths.mRelease, holdFrame })
            if (!std::isfinite(value))
                return std::nullopt;
        if (timing.mStart < 0.f || timing.mMinAttack <= timing.mStart || timing.mMaxAttack < timing.mMinAttack
            || timing.mRelease <= timing.mMaxAttack || timing.mFollowStart < timing.mRelease
            || timing.mFollowStop <= timing.mFollowStart || lengths.mDraw <= 0.f || lengths.mHold <= 0.f
            || lengths.mRelease <= 0.f || holdFrame < 0.f)
            return std::nullopt;

        if (section == NativeBowSection::Windup)
            time = std::min(time, timing.mMaxAttack);
        else if (section == NativeBowSection::Release)
            time = std::min(time, timing.mRelease);

        if (section != NativeBowSection::FollowThrough)
        {
            if (time < timing.mMinAttack)
            {
                const float progress
                    = std::clamp((time - timing.mStart) / (timing.mMinAttack - timing.mStart), 0.f, 1.f);
                return BowPose{ BowDrawAnimation, lengths.mDraw * progress, false,
                    progress * progress * (3.f - 2.f * progress) };
            }
            if (time < timing.mRelease)
                return BowPose{ BowHoldAnimation, std::fmod(holdFrame, lengths.mHold), true, 1.f };
        }

        // Continuous native playback includes the interval before follow start;
        // recoil must begin at the actual release key, without a two-frame wait.
        const float from = section == NativeBowSection::FollowThrough ? timing.mFollowStart : timing.mRelease;
        const float progress = section == NativeBowSection::Release ? 0.f
            : std::clamp((time - from) / (timing.mFollowStop - from), 0.f, 1.f);
        return BowPose{ BowReleaseAnimation, lengths.mRelease * progress, false, 0.f };
    }
}

#endif
