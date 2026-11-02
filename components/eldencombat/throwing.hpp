#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_THROWING_H
#define OPENMW_COMPONENTS_ELDENCOMBAT_THROWING_H

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

namespace EldenCombat
{
    struct ThrowingAnimation
    {
        std::string_view mClip;
        float mReadyFrame;
        float mReleaseFrame;
    };

    // Throwing daggers and darts use goodsUseAnim 17. TAE event 65 consumes
    // the projectile at frame 40; retain a preparation pose while charging.
    inline constexpr ThrowingAnimation DartThrowAnimation{ "a000_050170", 24.f, 40.f };

    // Native shoot keys continue to own attack strength, projectile release,
    // and recovery. Charge holds the ready pose; an early release may skip
    // part of the native pre-release interval just as the original animation does.
    // The caller clamps predicted time to the active native section's endpoint.
    // Follow-through is explicit because native follow keys can be separated
    // from the release key by unused frames in replacement animation tracks.
    inline std::optional<float> throwingAnimationFrame(float time, float start, float minAttack,
        float maxAttack, float release, float followStart, float followStop, bool followThrough, float endFrame)
    {
        if (!std::isfinite(time) || !std::isfinite(start) || !std::isfinite(minAttack)
            || !std::isfinite(maxAttack) || !std::isfinite(release) || !std::isfinite(followStart)
            || !std::isfinite(followStop) || !std::isfinite(endFrame)
            || start < 0.f || minAttack <= start || maxAttack < minAttack || release <= maxAttack
            || followStart < release || followStop <= followStart || endFrame <= DartThrowAnimation.mReleaseFrame)
            return std::nullopt;

        if (followThrough)
        {
            time = std::clamp(time, followStart, followStop);
            return DartThrowAnimation.mReleaseFrame + (endFrame - DartThrowAnimation.mReleaseFrame)
                * (time - followStart) / (followStop - followStart);
        }

        time = std::clamp(time, start, release);
        if (time <= minAttack)
            return DartThrowAnimation.mReadyFrame * (time - start) / (minAttack - start);
        if (time <= maxAttack)
            return DartThrowAnimation.mReadyFrame;
        return DartThrowAnimation.mReadyFrame + (DartThrowAnimation.mReleaseFrame - DartThrowAnimation.mReadyFrame)
            * (time - maxAttack) / (release - maxAttack);
    }
}

#endif
