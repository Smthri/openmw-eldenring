#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_GRIP_H
#define OPENMW_COMPONENTS_ELDENCOMBAT_GRIP_H

#include "combat.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace EldenCombat
{
    enum class Grip
    {
        OneHand,
        TwoHandRight,
        TwoHandLeft,
    };

    struct GripPose
    {
        std::string_view mAnimation;
        float mFrame = 0.f;
    };

    // SwapDef from the neighboring recreation's src/sim/extracted.rs. These
    // common grip clips already exist in its bank. Native inventory remains
    // authoritative; advance() reports the one moment to apply the new grip.
    class GripTransition
    {
    public:
        bool start(Grip from, Grip to)
        {
            if (active() || from == to)
                return false;
            mFrom = from;
            mTarget = to;
            mFrame = 0.f;
            mApplied = false;
            return true;
        }

        bool advance(float seconds)
        {
            if (!active() || !std::isfinite(seconds) || seconds <= 0.f)
                return false;
            mFrame = std::min(TotalFrames, mFrame + seconds * AnimationFps);
            if (!mApplied && mFrame >= ApplyFrame)
            {
                mApplied = true;
                return true;
            }
            return false;
        }

        bool active() const { return mFrame < TotalFrames; }
        bool busy() const { return active() && mFrame < StartFrames + FreeFrame; }
        Grip from() const { return mFrom; }
        Grip target() const { return mTarget; }

        GripPose pose() const
        {
            if (!active())
                return {};
            const bool left = mTarget == Grip::TwoHandLeft
                || (mTarget == Grip::OneHand && mFrom == Grip::TwoHandLeft);
            if (mFrame >= StartFrames)
                return { left ? "a000_029020" : "a000_029050", mFrame - StartFrames };
            if (mTarget == Grip::TwoHandRight)
                return { "a000_029060", mFrame };
            if (mTarget == Grip::TwoHandLeft)
                return { "a000_029080", mFrame };
            return { left ? "a000_029010" : "a000_029040", mFrame };
        }

        void reset()
        {
            mFrame = TotalFrames;
            mApplied = false;
        }

    private:
        static constexpr float StartFrames = 5.f;
        static constexpr float ApplyFrame = 3.f;
        static constexpr float FreeFrame = 7.f;
        static constexpr float TotalFrames = 22.f;

        Grip mFrom = Grip::OneHand;
        Grip mTarget = Grip::OneHand;
        float mFrame = TotalFrames;
        bool mApplied = false;
    };
}

#endif
