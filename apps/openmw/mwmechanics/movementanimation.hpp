#ifndef OPENMW_MECHANICS_MOVEMENTANIMATION_H
#define OPENMW_MECHANICS_MOVEMENTANIMATION_H

#include "../mwworld/movementdirection.hpp"

#include <array>
#include <cstddef>

namespace MWMechanics
{
    // Native creature rigs often only have a forward gait. Keep the requested
    // travel direction, but use that gait instead of stopping on a missing
    // sideways/backward clip. CharacterController rotates its root movement
    // toward the requested velocity after playback.
    template <class State>
    State nativeMovementAnimation(State requested, const std::array<State, 4>& states,
        MWWorld::MovementDirectionFlags supported)
    {
        constexpr std::array directions{ MWWorld::MovementDirectionFlag_Forward,
            MWWorld::MovementDirectionFlag_Back, MWWorld::MovementDirectionFlag_Left,
            MWWorld::MovementDirectionFlag_Right };
        for (std::size_t i = 0; i < states.size(); ++i)
        {
            if (requested == states[i] && !(supported & directions[i])
                && (supported & MWWorld::MovementDirectionFlag_Forward))
                return states[0];
        }
        return requested;
    }
}

#endif
