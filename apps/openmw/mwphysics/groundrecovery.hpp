#ifndef OPENMW_MWPHYSICS_GROUNDRECOVERY_H
#define OPENMW_MWPHYSICS_GROUNDRECOVERY_H

#include <algorithm>
#include <cmath>
#include <optional>

#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletCollision/CollisionShapes/btConvexShape.h>

#include "collisiontype.hpp"
#include "constants.hpp"
#include "contacttestwrapper.h"

namespace MWPhysics
{
    // Call only while physics workers are stopped. Terrain height is sampled at
    // the current feet position, after applying any scripted position change.
    // A loaded heightfield must confirm it; missing terrain is never a floor.
    inline std::optional<btVector3> findGroundRecoveryPosition(btCollisionWorld& world, btConvexShape& shape,
        const btTransform& actorTransform, const btVector3& feet, const btVector3& collisionOffset,
        btScalar halfHeight, btScalar terrainHeight)
    {
        const btScalar threshold = std::max(btScalar(64), halfHeight);
        if (!std::isfinite(feet.x()) || !std::isfinite(feet.y()) || !std::isfinite(feet.z())
            || !std::isfinite(terrainHeight) || !std::isfinite(halfHeight) || halfHeight <= 0
            || terrainHeight - feet.z() <= threshold)
            return {};

        const auto ray = [&](btScalar fromZ, btScalar toZ, int mask) {
            const btVector3 from(feet.x(), feet.y(), fromZ);
            const btVector3 to(feet.x(), feet.y(), toZ);
            btCollisionWorld::ClosestRayResultCallback hit(from, to);
            hit.m_collisionFilterGroup = CollisionType_Actor;
            hit.m_collisionFilterMask = mask;
            world.rayTest(from, to, hit);
            return hit;
        };

        const auto terrain = ray(terrainHeight + 64, terrainHeight - 64, CollisionType_HeightMap);
        if (!terrain.hasHit() || terrain.m_hitPointWorld.z() - feet.z() <= threshold)
            return {};

        // Preserve intentionally usable space below exterior terrain, e.g. a
        // mod's sunken room. Bridges/roofs are never used as the terrain test.
        // Do not use swimming state here: an NPC below the land may also be
        // below sea level, without actually being in accessible water.
        const auto floor = ray(feet.z() + sGroundOffset, feet.z() - std::max(btScalar(128), 2 * halfHeight),
            CollisionType_World | CollisionType_Door);
        const btScalar head = feet.z() + 2 * halfHeight + sGroundOffset;
        const bool hasCeiling = head < terrain.m_hitPointWorld.z()
            && ray(head, terrain.m_hitPointWorld.z(), CollisionType_World | CollisionType_Door).hasHit();

        struct Clearance : btCollisionWorld::ContactResultCallback
        {
            bool mBlocked = false;
            btScalar addSingleResult(btManifoldPoint& contact, const btCollisionObjectWrapper*, int, int,
                const btCollisionObjectWrapper*, int, int) override
            {
                if (contact.getDistance() < -btScalar(sCollisionMargin))
                    mBlocked = true;
                return 0;
            }
        };
        const auto clear = [&](const btVector3& position,
                               int mask = CollisionType_World | CollisionType_HeightMap | CollisionType_Door) {
            btCollisionObject probe;
            probe.setCollisionShape(&shape);
            btTransform transform(actorTransform);
            transform.setOrigin(position + collisionOffset);
            probe.setWorldTransform(transform);
            Clearance result;
            result.m_collisionFilterGroup = CollisionType_Actor;
            result.m_collisionFilterMask = mask;
            ContactTestWrapper::contactTest(&world, &probe, result);
            return !result.mBlocked;
        };

        if (((floor.hasHit() && floor.m_hitNormalWorld.z() > btScalar(0.5)) || hasCeiling)
            && clear(feet, CollisionType_World | CollisionType_Door))
            return {}; // usable underground space, not an NPC embedded in a solid

        btVector3 target(feet.x(), feet.y(), terrain.m_hitPointWorld.z() + sGroundOffset);
        if (clear(target))
            return target;

        // On slopes and small rocks the centre ray can leave the capsule partly
        // embedded. Find a supporting surface using the actual actor shape,
        // then check the entire destination (including headroom) again.
        btTransform from(actorTransform), to(actorTransform);
        const btScalar lift = std::max(btScalar(128), 2 * halfHeight);
        const btVector3 start = target + btVector3(0, 0, lift);
        if (!clear(start))
            return {};
        from.setOrigin(start + collisionOffset);
        to.setOrigin(target + collisionOffset - btVector3(0, 0, 2 * sGroundOffset));
        btCollisionWorld::ClosestConvexResultCallback hit(from.getOrigin(), to.getOrigin());
        hit.m_collisionFilterGroup = CollisionType_Actor;
        hit.m_collisionFilterMask = CollisionType_World | CollisionType_HeightMap | CollisionType_Door;
        world.convexSweepTest(&shape, from, to, hit);
        if (!hit.hasHit() || hit.m_hitNormalWorld.z() <= btScalar(0.5))
            return {};
        const btVector3 destination = from.getOrigin().lerp(to.getOrigin(), hit.m_closestHitFraction)
            - collisionOffset + btVector3(0, 0, sGroundOffset);
        // Do not relocate a buried NPC onto a building's roof.
        if (destination.z() - target.z() > sStepSizeDown || !clear(destination))
            return {};
        return destination;
    }
}

#endif
