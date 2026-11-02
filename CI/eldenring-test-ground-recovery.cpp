#include <apps/openmw/mwphysics/groundrecovery.hpp>

#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletCollision/CollisionDispatch/btCollisionDispatcher.h>
#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletCollision/CollisionShapes/btHeightfieldTerrainShape.h>
#include <BulletCollision/CollisionShapes/btStaticPlaneShape.h>

#include <array>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    unsigned int assertions = 0;

    void check(bool condition, const std::string& message)
    {
        ++assertions;
        if (!condition)
            throw std::runtime_error(message);
    }

    bool near(btScalar actual, btScalar expected, btScalar tolerance = btScalar(0.05))
    {
        return btFabs(actual - expected) <= tolerance;
    }

    // These are synthetic collision fixtures. All decisions about whether and
    // where to recover are made by the production helper using real Bullet rays,
    // contact manifolds and convex sweeps.
    struct Fixture
    {
        btDefaultCollisionConfiguration configuration;
        btCollisionDispatcher dispatcher{ &configuration };
        btDbvtBroadphase broadphase;
        btCollisionWorld world{ &dispatcher, &broadphase, &configuration };
#if BT_BULLET_VERSION < 310
        // The legacy constructor interprets PHY_FLOAT as btScalar.
        std::array<btScalar, 25> heights{};
#else
        std::array<float, 25> heights{};
#endif
        std::vector<std::unique_ptr<btCollisionShape>> shapes;
        std::vector<std::unique_ptr<btCollisionObject>> objects;

        ~Fixture()
        {
            for (const auto& object : objects)
                world.removeCollisionObject(object.get());
        }

        void add(std::unique_ptr<btCollisionShape> shape, const btVector3& position, int group)
        {
            auto object = std::make_unique<btCollisionObject>();
            object->setCollisionShape(shape.get());
            object->setWorldTransform(btTransform(btQuaternion::getIdentity(), position));
            world.addCollisionObject(object.get(), group, MWPhysics::CollisionType_AnyPhysical);
            shapes.push_back(std::move(shape));
            objects.push_back(std::move(object));
        }

        void terrain(btScalar elevation = 0, btScalar slope = 0, btScalar originX = 0)
        {
            check(shapes.empty(), "heightfield fixture must be added before other shapes");
            for (int y = 0; y < 5; ++y)
                for (int x = 0; x < 5; ++x)
                    heights[y * 5 + x] = static_cast<float>((x - 2) * 256 * slope);
#if BT_BULLET_VERSION < 310
            auto shape = std::make_unique<btHeightfieldTerrainShape>(
                5, 5, heights.data(), btScalar(1), btScalar(-1024), btScalar(1024), 2, PHY_FLOAT, false);
#else
            auto shape = std::make_unique<btHeightfieldTerrainShape>(
                5, 5, heights.data(), btScalar(-1024), btScalar(1024), 2, false);
#endif
            shape->setLocalScaling(btVector3(256, 256, 1));
            add(std::move(shape), btVector3(originX, 0, elevation), MWPhysics::CollisionType_HeightMap);
        }

        void box(const btVector3& center, const btVector3& halfExtents,
            int group = MWPhysics::CollisionType_World)
        {
            auto shape = std::make_unique<btBoxShape>(halfExtents);
            shape->setMargin(0);
            add(std::move(shape), center, group);
        }

        void water(btScalar elevation)
        {
            add(std::make_unique<btStaticPlaneShape>(btVector3(0, 0, 1), 0), btVector3(0, 0, elevation),
                MWPhysics::CollisionType_Water);
        }
    };

    enum class ActorShape
    {
        Box,
        Capsule,
        Cylinder
    };

    constexpr std::array actorShapes{ ActorShape::Box, ActorShape::Capsule, ActorShape::Cylinder };

    struct Actor
    {
        btScalar halfHeight;
        std::unique_ptr<btConvexShape> shape;
        btVector3 offset;
        btTransform transform{ btQuaternion::getIdentity() };

        explicit Actor(btScalar height = 48, ActorShape kind = ActorShape::Box)
            : halfHeight(height)
            , offset(0, 0, height)
        {
            if (kind == ActorShape::Capsule)
                shape = std::make_unique<btCapsuleShapeZ>(16, 2 * (height - 16));
            else if (kind == ActorShape::Cylinder)
                shape = std::make_unique<btCylinderShapeZ>(btVector3(16, 16, height));
            else
                shape = std::make_unique<btBoxShape>(btVector3(16, 12, height));
        }

        std::optional<btVector3> recover(Fixture& fixture, const btVector3& feet, btScalar terrainHeight = 0)
        {
            transform.setOrigin(feet + offset);
            return MWPhysics::findGroundRecoveryPosition(
                fixture.world, *shape, transform, feet, offset, halfHeight, terrainHeight);
        }

        btVector3 verifyRecovery(Fixture& fixture, const btVector3& feet, btScalar terrainHeight = 0)
        {
            const auto result = recover(fixture, feet, terrainHeight);
            check(result.has_value(), "buried actor did not recover to accessible terrain");
            check(std::isfinite(result->x()) && std::isfinite(result->y()) && std::isfinite(result->z()),
                "recovery produced a non-finite position");
            check(near(result->x(), feet.x()) && near(result->y(), feet.y()),
                "recovery changed the actor's horizontal position");
            check(result->z() > feet.z(), "recovery moved a buried actor farther down");

            struct Penetration : btCollisionWorld::ContactResultCallback
            {
                btScalar deepest = 0;
                btScalar addSingleResult(btManifoldPoint& contact, const btCollisionObjectWrapper*, int, int,
                    const btCollisionObjectWrapper*, int, int) override
                {
                    deepest = btMin(deepest, contact.getDistance());
                    return 0;
                }
            };
            btCollisionObject probe;
            probe.setCollisionShape(shape.get());
            transform.setOrigin(*result + offset);
            probe.setWorldTransform(transform);
            Penetration contact;
            contact.m_collisionFilterGroup = MWPhysics::CollisionType_Actor;
            contact.m_collisionFilterMask = MWPhysics::CollisionType_World | MWPhysics::CollisionType_Door
                | MWPhysics::CollisionType_HeightMap;
            fixture.world.contactTest(&probe, contact);
            check(contact.deepest >= -btScalar(MWPhysics::sCollisionMargin) - btScalar(0.01),
                "recovered actor still penetrates terrain or solid geometry");
            check(!recover(fixture, *result, terrainHeight), "recovery is not idempotent at the destination");
            return *result;
        }
    };

    void flatTerrainAndThresholds()
    {
        for (const ActorShape kind : actorShapes)
        {
            Fixture fixture;
            fixture.terrain();
            for (const btScalar halfHeight : { btScalar(32), btScalar(48), btScalar(96) })
            {
                Actor actor(halfHeight, kind);
                const btScalar threshold = btMax(btScalar(64), halfHeight);
                for (const btScalar z : { btScalar(500), btScalar(1), btScalar(0), btScalar(-1),
                         -threshold + btScalar(0.5), -threshold })
                    check(!actor.recover(fixture, btVector3(10, -20, z)),
                        "grounded, airborne or shallowly buried actor was teleported");
                for (const btScalar depth : { threshold + btScalar(0.5), btScalar(300), btScalar(10000) })
                {
                    const auto result = actor.verifyRecovery(fixture, btVector3(10, -20, -depth));
                    check(near(result.z(), MWPhysics::sGroundOffset), "flat-terrain recovery used the wrong height");
                }
            }
        }

        Fixture fixture;
        fixture.terrain(256);
        Actor actor;
        actor.offset = btVector3(5, -3, actor.halfHeight);
        actor.transform.setRotation(btQuaternion(btVector3(0, 0, 1), btScalar(0.7)));
        const auto result = actor.verifyRecovery(fixture, btVector3(123, -71, -100), 256);
        check(near(result.z(), 257), "actor collision offset or yaw changed the recovered root height");
    }

    void terrainMustBeLoadedAndConfirmed()
    {
        Actor actor;
        Fixture empty;
        check(!actor.recover(empty, btVector3(0, 0, -300)), "missing terrain was treated as a floor");
        empty.box(btVector3(0, 0, -5), btVector3(128, 128, 5));
        check(!actor.recover(empty, btVector3(0, 0, -300)), "static geometry was mistaken for heightfield terrain");

        Fixture fixture;
        fixture.terrain();
        check(!actor.recover(fixture, btVector3(2000, 0, -300)), "unloaded neighboring terrain allowed recovery");
        check(!actor.recover(fixture, btVector3(0, 0, -300), 1000),
            "inconsistent terrain metadata was accepted without a matching heightfield");
        check(!actor.recover(fixture, btVector3(0, 0, -50), 30),
            "estimated height bypassed the actual terrain burial threshold");
        const auto result = actor.verifyRecovery(fixture, btVector3(0, 0, -100), 30);
        check(near(result.z(), 1), "reported terrain height replaced the actual collision surface");

        Fixture shallow;
        shallow.terrain(30);
        check(!actor.recover(shallow, btVector3(0, 0, -50), 0),
            "shallow reported burial bypassed the conservative initial threshold");
    }

    void preserveUndergroundRooms()
    {
        Actor actor;
        for (const int group : { MWPhysics::CollisionType_World, MWPhysics::CollisionType_Door })
        {
            Fixture floor;
            floor.terrain();
            floor.box(btVector3(0, 0, -305), btVector3(100, 100, 5), group);
            check(!actor.recover(floor, btVector3(0, 0, -299)), "actor on an intentional underground floor moved");
            check(!actor.recover(floor, btVector3(0, 0, -240)), "actor above an underground floor moved");

            Fixture ceiling;
            ceiling.terrain();
            ceiling.box(btVector3(0, 0, -50), btVector3(100, 100, 5), group);
            check(!actor.recover(ceiling, btVector3(0, 0, -400)),
                "airborne actor in a deep exterior cave was pulled through its ceiling");
        }

        Fixture bridge;
        bridge.terrain();
        bridge.box(btVector3(0, 0, 200), btVector3(100, 100, 5));
        const auto result = actor.verifyRecovery(bridge, btVector3(0, 0, -300));
        check(near(result.z(), 1), "recovery put an actor on a bridge above the accessible ground");
    }

    void blockedDestinationAndRoofs()
    {
        Actor actor;
        for (const int group : { MWPhysics::CollisionType_World, MWPhysics::CollisionType_Door })
        {
            Fixture building;
            building.terrain();
            building.box(btVector3(0, 0, 60), btVector3(100, 100, 58), group);
            check(!actor.recover(building, btVector3(0, 0, -300)),
                "blocked recovery moved an actor into a building or onto its roof");

            Fixture ceiling;
            ceiling.terrain();
            ceiling.box(btVector3(0, 0, 80), btVector3(100, 100, 5), group);
            check(!actor.recover(ceiling, btVector3(0, 0, -300)),
                "recovery ignored blocked headroom or moved an actor above a low roof");
        }

        Fixture blockedSweep;
        blockedSweep.terrain();
        blockedSweep.box(btVector3(0, 0, 120), btVector3(100, 100, 118));
        check(!actor.recover(blockedSweep, btVector3(0, 0, -300)),
            "convex recovery sweep started inside solid geometry");
    }

    void embeddedUndergroundActors()
    {
        for (const ActorShape kind : actorShapes)
        {
            for (const bool ceiling : { false, true })
            {
                Fixture fixture;
                fixture.terrain();
                if (ceiling)
                    fixture.box(btVector3(0, 0, -50), btVector3(100, 100, 5));
                else
                    fixture.box(btVector3(0, 0, -310), btVector3(100, 100, 5));
                Actor actor(48, kind);
                const btVector3 feet(0, 0, -300);
                check(!actor.recover(fixture, feet), "usable underground room did not preserve its actor");
                // The side of this pillar overlaps the actor but misses the
                // centre ray. A nearby floor/ceiling alone is not evidence that
                // the NPC still occupies usable underground space.
                fixture.box(btVector3(10, 0, -250), btVector3(8, 8, 50));
                const auto result = actor.verifyRecovery(fixture, feet);
                check(near(result.z(), 1), "NPC embedded in underground solid geometry was left trapped");
            }
        }
    }

    void waterAndOtherActors()
    {
        Actor actor;
        for (const btScalar waterHeight : { btScalar(-350), btScalar(-200), btScalar(10) })
        {
            Fixture fixture;
            fixture.terrain(-100);
            fixture.water(waterHeight);
            fixture.box(btVector3(0, 0, -50), btVector3(16, 16, 50), MWPhysics::CollisionType_Actor);
            fixture.box(btVector3(0, 0, -120), btVector3(30, 30, 30), MWPhysics::CollisionType_Projectile);
            const auto result = actor.verifyRecovery(fixture, btVector3(0, 0, -300), -100);
            check(near(result.z(), -99), "water or an unrelated actor prevented terrain recovery");
            check(!actor.recover(fixture, btVector3(0, 0, -90), -100),
                "actor swimming above seabed was relocated");
        }
    }

    void slopedTerrainAndSmallRocks()
    {
        for (const ActorShape kind : actorShapes)
        {
            for (const btScalar slope : { btScalar(-0.75), btScalar(-0.25), btScalar(0.25), btScalar(0.75) })
            {
                Fixture fixture;
                fixture.terrain(0, slope);
                Actor actor(48, kind);
                const btScalar x = 80;
                const btScalar ground = x * slope;
                const auto result = actor.verifyRecovery(fixture, btVector3(x, 17, ground - 300), ground);
                check(result.z() >= ground + btScalar(0.9), "slope recovery left the root below the centre surface");
                check(result.z() <= ground + 20,
                    "slope recovery lifted an actor too far above the surface (shape "
                        + std::to_string(static_cast<int>(kind)) + ", slope " + std::to_string(slope) + ", ground "
                        + std::to_string(ground) + ", result " + std::to_string(result.z()) + ")");
                if (kind != ActorShape::Capsule)
                    check(result.z() > ground + 2, "box actor on a slope never used its full collision footprint");
            }

            Fixture rock;
            rock.terrain();
            rock.box(btVector3(0, 0, 12), btVector3(12, 12, 10));
            Actor actor(48, kind);
            const auto result = actor.verifyRecovery(rock, btVector3(0, 0, -300));
            check(near(result.z(), 23, btScalar(0.15)), "small rock did not support the recovered actor's full shape");
        }
    }

    void invalidAndDistantCoordinates()
    {
        Fixture fixture;
        fixture.terrain();
        Actor actor;
        for (const btScalar invalid : { std::numeric_limits<btScalar>::infinity(),
                 -std::numeric_limits<btScalar>::infinity(), std::numeric_limits<btScalar>::quiet_NaN() })
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                btVector3 feet(0, 0, -300);
                feet[axis] = invalid;
                check(!actor.recover(fixture, feet), "non-finite actor position reached ground recovery");
            }
            check(!actor.recover(fixture, btVector3(0, 0, -300), invalid),
                "non-finite reported terrain height reached ground recovery");
            actor.halfHeight = invalid;
            check(!actor.recover(fixture, btVector3(0, 0, -300)), "non-finite actor height reached ground recovery");
        }
        for (const btScalar height : { btScalar(0), btScalar(-1) })
        {
            actor.halfHeight = height;
            check(!actor.recover(fixture, btVector3(0, 0, -300)), "nonpositive actor height reached ground recovery");
        }
        actor.halfHeight = 48;
        for (const btScalar x : { btScalar(-10000000), btScalar(10000000) })
            check(!actor.recover(fixture, btVector3(x, 0, -300)), "distant unloaded terrain allowed recovery");

        Fixture distant;
        distant.terrain(0, 0, 1000000);
        const auto result = actor.verifyRecovery(distant, btVector3(1000000, 0, -300));
        check(near(result.z(), 1), "large world coordinates broke loaded-terrain recovery");
    }
}

int main()
{
    try
    {
        flatTerrainAndThresholds();
        terrainMustBeLoadedAndConfirmed();
        preserveUndergroundRooms();
        blockedDestinationAndRoofs();
        embeddedUndergroundActors();
        waterAndOtherActors();
        slopedTerrainAndSmallRocks();
        invalidAndDistantCoordinates();
        std::cout << "Ground recovery: " << assertions << " assertions passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Ground recovery failed after " << assertions << " assertions: " << error.what() << '\n';
        return 1;
    }
}
