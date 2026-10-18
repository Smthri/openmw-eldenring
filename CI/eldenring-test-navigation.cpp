#include <apps/openmw/mwmechanics/movementanimation.hpp>
#include <apps/openmw/mwmechanics/obstacledirection.hpp>
#include <components/eldencombat/locomotion.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    int checks = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
        ++checks;
    }

    void repeatedObstruction()
    {
        MWMechanics::ObstacleDirection direction;
        constexpr std::array<std::array<float, 2>, 7> expected{ {
            { 1.f, 1.f }, { 1.f, 0.f }, { 1.f, -1.f }, { 0.f, -1.f },
            { -1.f, -1.f }, { -1.f, 0.f }, { -1.f, 1.f },
        } };
        for (int retry = 0; retry < 21; ++retry)
        {
            check(direction.advance(15), "mobile actor has no evasive route");
            const auto& actual = direction.get();
            const auto& wanted = expected[retry % expected.size()];
            check(actual.mX == wanted[0] && actual.mY == wanted[1],
                "repeated obstruction did not advance to the next route");
        }
    }

    void forwardOnlyCreatures()
    {
        MWMechanics::ObstacleDirection direction;
        // Rats and other creatures need not have sideways/backward clips. They
        // must still try both forward diagonals to escape terrain on either side.
        for (int retry = 0; retry < 20; ++retry)
        {
            check(direction.advance(MWWorld::MovementDirectionFlag_Forward), "forward-only creature cannot evade");
            check(direction.get().mX == (retry % 2 == 0 ? 1.f : -1.f) && direction.get().mY == 1.f,
                "forward-only creature keeps pushing into the same side of an obstacle");
        }
    }

    void supportedDirections()
    {
        for (MWWorld::MovementDirectionFlags mask = 0; mask < 16; ++mask)
        {
            MWMechanics::ObstacleDirection direction;
            for (int retry = 0; retry < 21; ++retry)
            {
                check(direction.advance(mask) == (mask != 0), "no supported movement did not stop evasion");
                if (mask != 0)
                    check((direction.get().mRequiredAnimation & mask) != 0,
                        "obstacle evasion selected an unsupported creature animation");
            }
        }
        MWMechanics::ObstacleDirection direction;
        check(direction.advance(MWWorld::MovementDirectionFlag_Right), "right-only route unavailable");
        check(!direction.advance(0), "immobile actor has an evasive route");
        check(direction.advance(MWWorld::MovementDirectionFlag_Left), "changed animation capabilities ignored");
        check(direction.get().mX == -1.f && direction.get().mY == 0.f, "updated movement mask was ignored");
    }

    enum class Gait
    {
        Idle,
        Forward,
        Back,
        Left,
        Right,
        Turn,
    };

    constexpr std::array gaits{ Gait::Forward, Gait::Back, Gait::Left, Gait::Right };

    Gait requestedGait(EldenCombat::PlanarMovement movement)
    {
        // CharacterController classifies the smoothed velocity, not the AI's
        // input. Pure diagonal movement uses the forward/backward animation.
        if (std::abs(movement.mX) > 2.f * std::abs(movement.mY))
            return movement.mX < 0.f ? Gait::Left : Gait::Right;
        return movement.mY < 0.f ? Gait::Back : Gait::Forward;
    }

    void nativeGaitSupport()
    {
        constexpr std::array directions{ MWWorld::MovementDirectionFlag_Forward,
            MWWorld::MovementDirectionFlag_Back, MWWorld::MovementDirectionFlag_Left,
            MWWorld::MovementDirectionFlag_Right };
        for (MWWorld::MovementDirectionFlags mask = 0; mask < 16; ++mask)
        {
            for (std::size_t i = 0; i < gaits.size(); ++i)
            {
                const auto actual = MWMechanics::nativeMovementAnimation(gaits[i], gaits, mask);
                if (mask & directions[i])
                    check(actual == gaits[i], "available native directional gait was replaced");
                else if (mask & MWWorld::MovementDirectionFlag_Forward)
                    check(actual == Gait::Forward, "missing directional gait did not fall back to native forward");
                else
                    check(actual == gaits[i], "gait fallback invented a missing forward animation");
            }
            for (const auto other : { Gait::Idle, Gait::Turn })
                check(MWMechanics::nativeMovementAnimation(other, gaits, mask) == other,
                    "native gait fallback replaced an idle or turning animation");
        }
    }

    void forwardOnlySmoothTurns()
    {
        constexpr float pi = 3.14159265358979323846f;
        int missingRequests = 0;
        // A nearby path corner can directly request a sideways gait. Even a
        // forward-only input clamp would miss the sideways/backwards velocity
        // retained by world-space smoothing during a sharp path change.
        for (const int fps : { 30, 60, 144 })
        {
            for (const float speed : { .4f, 1.f })
            {
                for (const float start : { -pi, -.5f * pi, 0.f, .5f * pi })
                {
                    for (const float target : { -80.f, -45.f, 0.f, 45.f, 80.f })
                    {
                        EldenCombat::PlanarMovement current{ speed * std::sin(start), speed * std::cos(start) };
                        const EldenCombat::PlanarMovement desired{
                            speed * std::sin(target * pi / 180.f), speed * std::cos(target * pi / 180.f) };
                        for (int frame = 0; frame < fps / 2; ++frame)
                        {
                            current = EldenCombat::smoothMovement(current, desired, 1.f / fps, true, false, 1.f / 6.f);
                            const auto requested = requestedGait(current);
                            missingRequests += requested != Gait::Forward;
                            check(MWMechanics::nativeMovementAnimation(
                                      requested, gaits, MWWorld::MovementDirectionFlag_Forward) == Gait::Forward,
                                "forward-only creature stopped on a smoothed path corner");
                            check(MWMechanics::nativeMovementAnimation(requested, gaits, 15) == requested,
                                "fully supported humanoid gait changed during path steering");
                        }
                    }
                }
            }
        }
        check(missingRequests > 100, "path-corner regression did not exercise missing sideways/backward gaits");
    }
}

int main()
{
    try
    {
        repeatedObstruction();
        forwardOnlyCreatures();
        supportedDirections();
        nativeGaitSupport();
        forwardOnlySmoothTurns();
        std::cout << "Navigation regression tests passed: " << checks << " assertions\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
