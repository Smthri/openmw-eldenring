#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_NATIVEATTACK_H
#define OPENMW_COMPONENTS_ELDENCOMBAT_NATIVEATTACK_H

#include "combat.hpp"

#include <cmath>
#include <optional>
#include <string>
#include <utility>

namespace EldenCombat
{
    // Missing hit keys follow OpenMW's native creature rule: contact at start.
    // Keep invalid/reversed timelines out of the controller's captured action.
    inline std::optional<NativeAttackTiming> nativeAttackTiming(
        std::string group, float startSeconds, float stopSeconds, float hitSeconds)
    {
        if (group.empty() || !std::isfinite(startSeconds) || !std::isfinite(stopSeconds)
            || !std::isfinite(hitSeconds) || startSeconds < 0.f || stopSeconds <= startSeconds
            || (hitSeconds >= 0.f && (hitSeconds < startSeconds || hitSeconds > stopSeconds)))
            return {};
        const float duration = (stopSeconds - startSeconds) * AnimationFps;
        const float impact = (hitSeconds < 0.f ? 0.f : hitSeconds - startSeconds) * AnimationFps;
        if (!std::isfinite(duration) || !std::isfinite(impact))
            return {};
        return NativeAttackTiming{ std::move(group), duration, impact };
    }
}
#endif
