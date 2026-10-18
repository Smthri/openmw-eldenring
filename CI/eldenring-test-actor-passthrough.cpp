#include <apps/openmw/mwphysics/actorpassthrough.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    unsigned int assertions = 0;
    constexpr std::uint64_t player = 1;
    constexpr std::uint64_t firstActor = 2;
    constexpr std::uint64_t secondActor = 3;

    void check(bool condition, const char* message)
    {
        ++assertions;
        if (!condition)
            throw std::runtime_error(message);
    }

    void checkReset(const MWPhysics::ActorPassThrough& state)
    {
        check(state.blockingActor() == 0, "cleared push retained its actor");
        check(state.readyActor() == 0, "cleared push retained permission to pass through");
        check(state.blockedSeconds() == 0.f, "cleared push retained its elapsed time");
    }

    void sustainedContact()
    {
        MWPhysics::ActorPassThrough state;
        checkReset(state);
        state.update(firstActor, 1.49f);
        check(state.readyActor() == 0, "brief contact allowed passing through an actor");
        check(state.blockingActor() == firstActor, "push did not remember the obstructing actor");
        state.update(firstActor, 0.01f);
        check(state.readyActor() == firstActor, "sustained push did not allow passing through its actor");
        check(state.blockedSeconds() == MWPhysics::ActorPassThrough::PushDuration,
            "ready push time did not stop at the activation threshold");
        for (int frame = 0; frame < 100; ++frame)
            state.update(firstActor, 0.5f);
        check(state.readyActor() == firstActor, "continued push lost its ready actor");
        check(state.blockedSeconds() == MWPhysics::ActorPassThrough::PushDuration,
            "continued push accumulated unbounded elapsed time");
        state.reset();
        checkReset(state);
        state.update(secondActor, 0.01f);
        check(state.readyActor() == 0, "explicit reset leaked readiness into the next actor");
    }

    void frameRates()
    {
        for (const int fps : { 15, 30, 60, 120, 144, 240 })
        {
            MWPhysics::ActorPassThrough state;
            const int activationFrame = static_cast<int>(std::ceil(MWPhysics::ActorPassThrough::PushDuration * fps));
            for (int frame = 1; frame <= activationFrame; ++frame)
            {
                state.update(firstActor, 1.f / fps);
                check(state.readyActor() == (frame == activationFrame ? firstActor : 0),
                    "push activation depends on the simulation frame rate");
            }
        }

        MWPhysics::ActorPassThrough state;
        constexpr std::array<float, 5> durations{ 1.f / 144.f, 1.f / 30.f, 1.f / 60.f, 0.1f, 1.f / 240.f };
        double elapsed = 0.0;
        for (unsigned int frame = 0; state.readyActor() == 0 && frame < 100; ++frame)
        {
            const float seconds = durations[frame % durations.size()];
            elapsed += seconds;
            state.update(firstActor, seconds);
            check(state.readyActor() == (elapsed + 0.00001 >= MWPhysics::ActorPassThrough::PushDuration ? firstActor : 0),
                "variable frame times changed the sustained-push threshold");
        }
        check(state.readyActor() == firstActor, "variable frame times never completed a sustained push");
    }

    void interruptedContact()
    {
        MWPhysics::ActorPassThrough state;
        for (int touch = 0; touch < 10; ++touch)
        {
            state.update(firstActor, 0.75f);
            check(state.readyActor() == 0, "separate brief touches accumulated into a sustained push");
            state.update(0, 1.f / 60.f);
            checkReset(state);
        }

        state.update(firstActor, 1.4f);
        state.update(secondActor, 0.2f);
        check(state.blockingActor() == secondActor, "changed blocker retained the previous actor");
        check(std::abs(state.blockedSeconds() - 0.2f) < 0.00001f,
            "changed blocker inherited the previous actor's push time");
        check(state.readyActor() == 0, "changing the obstructing actor allowed immediate passage");
        state.update(firstActor, 0.2f);
        check(state.readyActor() == 0, "returning to the previous actor restored old push time");
        state.update(firstActor, 1.3f);
        check(state.readyActor() == firstActor, "fresh continuous contact failed after a blocker change");
        state.update(secondActor, 0.2f);
        check(state.readyActor() == 0, "ready actor's permission was transferred to a different blocker");
        state.update(secondActor, MWPhysics::ActorPassThrough::PushDuration);
        state.update(0, 1.f / 60.f);
        checkReset(state);
    }

    void invalidDurations()
    {
        for (const float seconds : { 0.f, -1.f, std::numeric_limits<float>::infinity(),
                 -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
        {
            MWPhysics::ActorPassThrough state;
            state.update(firstActor, 1.4f);
            state.update(firstActor, seconds);
            checkReset(state);
            state.update(firstActor, 0.2f);
            check(state.readyActor() == 0, "invalid duration preserved accumulated push time");
            state.update(firstActor, MWPhysics::ActorPassThrough::PushDuration);
            state.update(firstActor, seconds);
            checkReset(state);
        }
        MWPhysics::ActorPassThrough state;
        state.update(firstActor, std::numeric_limits<float>::max());
        check(state.readyActor() == firstActor, "large finite update failed to complete a push");
        check(std::isfinite(state.blockedSeconds())
                && state.blockedSeconds() == MWPhysics::ActorPassThrough::PushDuration,
            "large finite update overflowed elapsed push time");
    }

    void pairFiltering()
    {
        using MWPhysics::ignoresActorPair;
        check(!ignoresActorPair(player, 0, firstActor, 0), "ordinary actors stopped colliding");
        check(ignoresActorPair(player, 0, firstActor, player), "player remained blocked by the phased actor");
        check(ignoresActorPair(firstActor, player, player, 0), "phased actor remained blocked by the player");
        check(!ignoresActorPair(secondActor, 0, firstActor, player), "phased actor lost collision with another actor");
        check(!ignoresActorPair(firstActor, player, secondActor, 0), "another actor could enter the phased actor");
        check(!ignoresActorPair(player, 0, secondActor, 0), "player lost collision with an unrelated actor");
        check(!ignoresActorPair(firstActor, player, secondActor, player),
            "two actors phased for the player stopped colliding with each other");
        check(!ignoresActorPair(0, 0, firstActor, player), "missing first actor matched a phased pair");
        check(!ignoresActorPair(firstActor, player, 0, 0), "missing second actor matched a phased pair");
        check(!ignoresActorPair(0, 0, 0, 0), "empty actor identities disabled collision");
        check(!ignoresActorPair(player, player, player, player), "actor identity matched a pair with itself");
        check(!ignoresActorPair(player, 0, firstActor, 0), "clearing phase failed to restore pair collision");

        constexpr std::uint64_t replacementPlayer = (std::uint64_t{ 1 } << 40) + player;
        check(!ignoresActorPair(replacementPlayer, 0, firstActor, player),
            "new player identity inherited a previous player's phased pair");
        check(ignoresActorPair(replacementPlayer, 0, firstActor, replacementPlayer),
            "wide actor identity was truncated by pair filtering");
        check(ignoresActorPair(firstActor, replacementPlayer, replacementPlayer, 0),
            "wide actor identity was not symmetric in pair filtering");
    }

    void safeRestoration()
    {
        using MWPhysics::separatedActorBounds;
        constexpr std::array<float, 3> firstMin{ -16.f, -16.f, 0.f };
        constexpr std::array<float, 3> firstMax{ 16.f, 16.f, 128.f };
        check(!separatedActorBounds(firstMin, firstMax, firstMin, firstMax),
            "overlapping actors restored their collision pair");
        check(!separatedActorBounds(firstMin, firstMax, { -8.f, -8.f, 32.f }, { 8.f, 8.f, 96.f }),
            "actor contained inside another actor restored collision");

        // Clearance is tested in either direction on every axis, including
        // vertical separation for stairs/flying actors and unequal box sizes.
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            for (const float direction : { -1.f, 1.f })
            {
                for (const float gap : { -1.f, 0.f, 3.99f, 4.f, 4.01f, 32.f })
                {
                    auto secondMin = firstMin;
                    auto secondMax = firstMax;
                    if (direction > 0.f)
                    {
                        secondMin[axis] = firstMax[axis] + gap;
                        secondMax[axis] = secondMin[axis] + 8.f;
                    }
                    else
                    {
                        secondMax[axis] = firstMin[axis] - gap;
                        secondMin[axis] = secondMax[axis] - 8.f;
                    }
                    check(separatedActorBounds(firstMin, firstMax, secondMin, secondMax) == (gap > 4.f),
                        "collision restored before safe clearance, or remained disabled after clearance");
                    check(separatedActorBounds(secondMin, secondMax, firstMin, firstMax) == (gap > 4.f),
                        "safe collision restoration depended on actor ordering");
                }
            }
        }
    }

    void blockedMovement()
    {
        using MWPhysics::isBlockedActorMovement;
        for (const float requested : { 0.001f, 0.1f, 1.f, 10.f, 100.f })
        {
            check(isBlockedActorMovement(firstActor, requested, 0.f), "stationary obstruction was not detected");
            check(isBlockedActorMovement(firstActor, requested, requested * 0.25f),
                "substantial obstruction was not detected at the progress threshold");
            check(isBlockedActorMovement(firstActor, requested, -requested),
                "being pushed backwards lost obstruction tracking");
            check(!isBlockedActorMovement(firstActor, requested, requested * 0.26f),
                "successful movement around an actor counted as obstruction");
            check(!isBlockedActorMovement(firstActor, requested, requested),
                "unobstructed movement counted as pushing an actor");
            check(!isBlockedActorMovement(0, requested, 0.f), "wall contact counted as an actor obstruction");
        }
        for (const float requested : { -1.f, 0.f, 0.0001f })
            check(!isBlockedActorMovement(firstActor, requested, 0.f),
                "absent or negligible requested movement counted as pushing");
        for (const float invalid : { std::numeric_limits<float>::infinity(),
                 -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
        {
            check(!isBlockedActorMovement(firstActor, invalid, 0.f), "invalid requested distance counted as pushing");
            check(!isBlockedActorMovement(firstActor, 1.f, invalid), "invalid forward distance counted as pushing");
        }

        MWPhysics::ActorPassThrough state;
        state.update(firstActor, 1.4f);
        const auto blocker = isBlockedActorMovement(firstActor, 1.f, 0.5f) ? firstActor : 0;
        state.update(blocker, 0.1f);
        checkReset(state);
        state.update(firstActor, 0.2f);
        check(state.readyActor() == 0, "progress around the actor failed to interrupt the sustained-push timer");
    }
}

int main()
{
    try
    {
        sustainedContact();
        frameRates();
        interruptedContact();
        invalidDurations();
        pairFiltering();
        safeRestoration();
        blockedMovement();
        std::cout << "Actor pass-through regression tests passed: " << assertions << " assertions\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
