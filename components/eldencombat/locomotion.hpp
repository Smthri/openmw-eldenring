#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_LOCOMOTION_H
#define OPENMW_COMPONENTS_ELDENCOMBAT_LOCOMOTION_H

#include <algorithm>
#include <cmath>

namespace EldenCombat
{
    // A reflected left-hand weapon pose needs the opposite source strafe;
    // forward/backward and sprint direction still use the actor's input frame.
    inline unsigned int mirroredLocomotionId(unsigned int animationId)
    {
        for (const unsigned int base : { 20000u, 20100u, 22100u, 320000u, 320100u })
        {
            if (animationId == base + 2)
                return base + 3;
            if (animationId == base + 3)
                return base + 2;
        }
        return animationId;
    }

    struct PlanarMovement
    {
        float mX = 0.f;
        float mY = 0.f;
    };

    inline float movementSpeed(float maximumSpeed, float inputMagnitude, bool strafing, bool eldenActive)
    {
        return maximumSpeed * inputMagnitude * (strafing && !eldenActive ? 0.75f : 1.f);
    }

    // Creature::getRunSpeed deliberately returns its walking speed. Native
    // playback divides by walk velocity, leaving the run/walk root-motion
    // ratio to supply its running pace. Imported poses bypass native root
    // motion, so preserve that ratio in the requested velocity instead.
    // Use the forward clips for every direction, as imported strafing/backing
    // must retain the same travel speed. Missing/invalid keys keep the current
    // speed, including creatures whose running clip falls back to walking.
    inline float creatureRunSpeedMultiplier(float walkVelocity, float runVelocity)
    {
        if (!std::isfinite(walkVelocity) || !std::isfinite(runVelocity)
            || walkVelocity <= 1.f || runVelocity <= 1.f)
            return 1.f;
        const float multiplier = runVelocity / walkVelocity;
        return std::isfinite(multiplier) ? multiplier : 1.f;
    }

    // Compute this from the current facing after Lua has merged actor controls.
    // A target-angle error delayed by one frame can overshoot and oscillate even
    // when both actors are standing still.
    inline float lockOnYawChange(float currentYaw, float targetYaw, float seconds)
    {
        constexpr float pi = 3.14159265358979323846f;
        const float difference = std::remainder(targetYaw - currentYaw, 2.f * pi);
        const float turnLimit = 4.f * pi * std::max(seconds, 0.f);
        return std::clamp(difference, -turnLimit, turnLimit);
    }

    // Ground contact can briefly disappear on steps and uneven terrain. Keep
    // that noise out of the idle/locomotion pose selector without changing the
    // physics contact or the controller's jump and attack grounding checks.
    // Observe every pose update, including updates owned by another action.
    class GroundContactGrace
    {
    public:
        void reset() { mRemaining = 0.f; }

        bool update(bool grounded, float seconds)
        {
            if (grounded)
                mRemaining = 0.12f;
            else if (std::isfinite(seconds) && seconds > 0.f)
                mRemaining = std::max(0.f, mRemaining - seconds);
            return mRemaining > 0.f;
        }

    private:
        float mRemaining = 0.f;
    };

    // The actor follows the aiming frame, while an idle third-person pose can
    // retain the direction in which it last moved. Actor yaw is clockwise;
    // imported pose yaw is counterclockwise, so world yaw is pose minus actor.
    class IdleFacing
    {
    public:
        void reset() { mHaveFacing = false; }

        float update(float actorYaw, float poseYaw, bool moving)
        {
            constexpr float fullTurn = 6.28318530717958647692f;
            if (moving || !mHaveFacing)
            {
                mWorldYaw = std::remainder(poseYaw - actorYaw, fullTurn);
                mHaveFacing = true;
            }
            return std::remainder(mWorldYaw + actorYaw, fullTurn);
        }

    private:
        float mWorldYaw = 0.f;
        bool mHaveFacing = false;
    };

    // Both vectors are world-space fractions of maximum movement speed. Keep
    // heading and magnitude independent: interpolating velocity components
    // would slow a 90-degree turn and stop a 180-degree turn entirely.
    inline PlanarMovement smoothMovement(PlanarMovement current, PlanarMovement target, float duration,
        bool enabled, bool immediateStop, float turningDelay)
    {
        constexpr float epsilon = 0.001f;
        constexpr float pi = 3.14159265358979323846f;
        float targetSpeed = std::hypot(target.mX, target.mY);
        if (targetSpeed > 1.f)
        {
            target.mX /= targetSpeed;
            target.mY /= targetSpeed;
            targetSpeed = 1.f;
        }
        // The caller stores this result even when smoothing is disabled, so
        // toggling it back on never restores an old direction or velocity.
        if (!enabled || (immediateStop && targetSpeed < epsilon))
            return targetSpeed < epsilon ? PlanarMovement{} : target;

        const float currentSpeed = std::hypot(current.mX, current.mY);
        const float seconds = std::max(duration, 0.f);
        const float speed = std::clamp(currentSpeed
            + std::clamp(targetSpeed - currentSpeed, -3.f * seconds, 3.f * seconds), 0.f, 1.f);
        if (speed < epsilon)
            return {};
        float heading = std::atan2(current.mY, current.mX);
        if (targetSpeed >= epsilon)
        {
            const float targetHeading = std::atan2(target.mY, target.mX);
            if (currentSpeed < epsilon)
                heading = targetHeading;
            else
            {
                float delta = std::remainder(targetHeading - heading, 2.f * pi);
                // Either arc is equally short for an exact reversal. Pick a
                // consistent side instead of depending on floating-point signs.
                if (delta <= -pi + 1e-6f)
                    delta = pi;
                const float maxDelta = pi * seconds / std::max(turningDelay, 0.01f);
                heading += std::clamp(delta, -maxDelta, maxDelta);
            }
        }
        return { speed * std::cos(heading), speed * std::sin(heading) };
    }
}

#endif
