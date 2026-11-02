#ifndef OPENMW_MWPHYSICS_ACTORPASSTHROUGH_H
#define OPENMW_MWPHYSICS_ACTORPASSTHROUGH_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace MWPhysics
{
    // Only uninterrupted, physically obstructed movement counts. Runtime actor
    // identities are never reused, so a removed blocker cannot transfer progress.
    class ActorPassThrough
    {
    public:
        static constexpr float PushDuration = 1.5f;

        void reset()
        {
            mBlockingActor = 0;
            mBlockedSeconds = 0.f;
        }

        void update(std::uint64_t blockingActor, float seconds)
        {
            if (blockingActor == 0 || !std::isfinite(seconds) || seconds <= 0.f)
            {
                reset();
                return;
            }
            if (blockingActor != mBlockingActor)
            {
                reset();
                mBlockingActor = blockingActor;
            }
            mBlockedSeconds = std::min(PushDuration, mBlockedSeconds + seconds);
        }

        std::uint64_t readyActor() const
        {
            return mBlockedSeconds + 0.00001f >= PushDuration ? mBlockingActor : 0;
        }
        std::uint64_t blockingActor() const { return mBlockingActor; }
        float blockedSeconds() const { return mBlockedSeconds; }

    private:
        std::uint64_t mBlockingActor = 0;
        float mBlockedSeconds = 0.f;
    };

    inline bool ignoresActorPair(std::uint64_t firstId, std::uint64_t firstPassThroughPlayerId,
        std::uint64_t secondId, std::uint64_t secondPassThroughPlayerId)
    {
        return firstId != 0 && secondId != 0 && firstId != secondId
            && (firstPassThroughPlayerId == secondId || secondPassThroughPlayerId == firstId);
    }

    inline bool isBlockedActorMovement(std::uint64_t blockingActor, float requestedDistance, float forwardDistance)
    {
        return blockingActor != 0 && std::isfinite(requestedDistance) && std::isfinite(forwardDistance)
            && requestedDistance > 0.0001f && forwardDistance <= requestedDistance * 0.25f;
    }

    // A small clearance keeps the pair disabled at the initial contact margin
    // and until the player's whole collision hull has safely left the actor.
    inline bool separatedActorBounds(const std::array<float, 3>& firstMin, const std::array<float, 3>& firstMax,
        const std::array<float, 3>& secondMin, const std::array<float, 3>& secondMax)
    {
        constexpr float clearance = 4.f;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            if (firstMax[axis] + clearance < secondMin[axis] || secondMax[axis] + clearance < firstMin[axis])
                return true;
        }
        return false;
    }
}

#endif
