#!/usr/bin/env python3
"""Exercise production teleport and pose-selection blocks with native service stubs.

The combat, grip timing, movement and facing policies are the real components.
Only the actor/world and animation output are stubbed; no rendered claim is made.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def block(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


STUBS = r'''
#include <components/eldencombat/animations.hpp>
#include <components/eldencombat/bow.hpp>
#include <components/eldencombat/combat.hpp>
#include <components/eldencombat/grip.hpp>
#include <components/eldencombat/guard.hpp>
#include <components/eldencombat/jumpmovement.hpp>
#include <components/eldencombat/locomotion.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace osg
{
    struct Vec2f {};
    struct Vec3f
    {
        float values[3]{};
        float x() const { return values[0]; }
        float y() const { return values[1]; }
    };
}
namespace ESM { struct WeaponType { static constexpr int MarksmanBow = 1; }; }
namespace Settings
{
    struct Game { bool mTurnToMovementDirection = true; };
    Game& game() { static Game settings; return settings; }
}
namespace MWBase
{
    struct World
    {
        bool firstPerson = false;
        bool isFirstPerson() const { return firstPerson; }
    };
    struct Environment
    {
        World world;
        static Environment& get() { static Environment env; return env; }
        World* getWorld() { return &world; }
    };
}
enum class DrawState { Nothing, Weapon };
struct CreatureStats
{
    static constexpr int Stance_Run = 1;
    struct BossArt { bool cancelled = false; void cancel() { cancelled = true; } };
    EldenCombat::Controller combat;
    EldenCombat::Input input;
    EldenCombat::GripTransition transition;
    BossArt bossArt;
    bool teleported = false;
    bool running = true;
    DrawState draw = DrawState::Weapon;
    auto& getEldenCombat() { return combat; }
    auto& getEldenCombatInput() { return input; }
    auto& getEldenGripTransition() { return transition; }
    auto& getEldenBossArt() { return bossArt; }
    const auto& getEldenCombat() const { return combat; }
    const auto& getEldenCombatInput() const { return input; }
    const auto& getEldenGripTransition() const { return transition; }
    bool wasTeleported() const { return teleported; }
    void setTeleported(bool value) { teleported = value; }
    bool getStance(int) const { return running; }
    DrawState getDrawState() const { return draw; }
};
struct Position { float rot[3]{}; };
struct RefData
{
    Position position;
    const Position& getPosition() const { return position; }
};
struct Ptr
{
    RefData data;
    const RefData& getRefData() const { return data; }
    bool operator==(const Ptr&) const { return true; }
};
Ptr getPlayer() { return {}; }
struct Movement { float mPosition[3]{}; };
struct Class
{
    CreatureStats& stats;
    Movement& movement;
    bool isActor() const { return true; }
    CreatureStats& getCreatureStats(const Ptr&) const { return stats; }
    Movement& getMovementSettings(const Ptr&) const { return movement; }
};
struct Animation
{
    std::string clip;
    std::string gripChange;
    float frame = 0.f;
    float yaw = 0.f;
    float gripFrame = 0.f;
    bool upperBodyOnly = false;
    bool missingGrip = false;
    bool hasEldenEmbeddedSpear() const { return false; }
    float getEldenAnimationLength(std::string_view name) const
    {
        return missingGrip && name.starts_with("a000_029") ? 0.f : 60.f;
    }
    void playEldenAnimation(std::string_view name, float at, bool, float, float facing = 0.f,
        std::string_view = {}, std::string_view = {}, std::string_view = {},
        bool upper = false, bool = false, float = 0.f,
        std::string_view change = {}, float changeFrame = 0.f)
    {
        clip = name;
        frame = at;
        yaw = facing;
        upperBodyOnly = upper;
        gripChange = change;
        gripFrame = changeFrame;
    }
};
struct CharacterController
{
    Ptr mPtr;
    CreatureStats stats;
    Movement movement;
    Animation animation;
    Animation* mAnimation = &animation;
    osg::Vec2f mSmoothedSpeed;
    EldenCombat::LocomotionAnimationState mEldenLocomotion;
    EldenCombat::GroundContactGrace mEldenGroundContact;
    EldenCombat::IdleFacing mEldenIdleFacing;
    EldenCombat::JumpMovement mEldenJumpMovement;
    EldenCombat::Update mEldenUpdate;
    float mEldenPoseTime = 0.f;
    float mEldenStridePhase = 0.f;
    bool mEldenMeleeActive = true;
    bool visuallyGrounded = true;
    bool bow = false;

    void teleport();
    void pose(float duration, const osg::Vec3f& movement);
};
'''


CHECKS = r'''
int checks = 0;
void check(bool condition, const char* message)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
void close(float actual, float expected, const char* message)
{
    check(std::isfinite(actual) && std::abs(actual - expected) < 1e-4f, message);
}
void setGrip(EldenCombat::Input& input, EldenCombat::Grip grip)
{
    input.mTwoHanded = grip != EldenCombat::Grip::OneHand;
    input.mTwoHandedLeft = grip == EldenCombat::Grip::TwoHandLeft;
}
void cellTransitions()
{
    using EldenCombat::Grip;
    for (const Grip from : { Grip::TwoHandRight, Grip::TwoHandLeft, Grip::OneHand })
        for (const Grip to : { Grip::OneHand, Grip::TwoHandRight, Grip::TwoHandLeft })
            for (const float frame : { 0.f, 2.f, 4.f, 8.f, 23.f })
            {
                CharacterController actor;
                setGrip(actor.stats.input, from);
                actor.stats.transition.start(from, to);
                if (actor.stats.transition.advance(frame / EldenCombat::AnimationFps))
                    setGrip(actor.stats.input, to);
                const bool twoHanded = actor.stats.input.mTwoHanded;
                const bool left = actor.stats.input.mTwoHandedLeft;
                actor.stats.input.mLight = { true, true, true };
                actor.stats.input.mGuard = { true, true, true };
                actor.stats.input.mMoveX = 1.f;
                actor.stats.input.mLockedOn = true;
                actor.stats.input.mHandsBusy = true;
                actor.stats.teleported = true;
                actor.teleport();
                check(actor.stats.input.mTwoHanded == twoHanded && actor.stats.input.mTwoHandedLeft == left,
                    "entering a cell changed the selected armament grip");
                check(!actor.stats.transition.active() && !actor.stats.transition.advance(1.f),
                    "a cancelled cell-transition swap changed grip later");
                check(!actor.stats.input.mLight.mHeld && !actor.stats.input.mLight.mPressed
                        && !actor.stats.input.mGuard.mHeld && !actor.stats.input.mHandsBusy
                        && !actor.stats.input.mLockedOn && actor.stats.input.mMoveX == 0.f,
                    "cell transition retained action buttons, movement or target context");
                check(!actor.stats.teleported && actor.stats.bossArt.cancelled,
                    "cell transition lost native teleport or boss-art cleanup");
                actor.teleport();
                check(actor.stats.input.mTwoHanded == twoHanded && actor.stats.input.mTwoHandedLeft == left,
                    "the update after entering a cell changed grip");
            }
}
void gripFacing()
{
    using EldenCombat::Grip;
    for (const Grip from : { Grip::OneHand, Grip::TwoHandRight, Grip::TwoHandLeft })
        for (const Grip to : { Grip::OneHand, Grip::TwoHandRight, Grip::TwoHandLeft })
            for (const bool moving : { false, true })
                for (const bool turnToMovement : { true, false })
                    for (const bool missingClip : { false, true })
                    {
                        if (from == to)
                            continue;
                        CharacterController actor;
                        Settings::game().mTurnToMovementDirection = turnToMovement;
                        setGrip(actor.stats.input, from);
                        actor.animation.missingGrip = missingClip;
                        // Start while walking sideways, then orbit the camera.
                        actor.pose(1.f / 60.f, { { 225.f, 0.f, 0.f } });
                        const float worldYaw = actor.animation.yaw;
                        actor.stats.transition.start(from, to);
                        float previousStride = actor.mEldenStridePhase;
                        for (int tick = 0; tick <= 50; ++tick)
                        {
                            if (actor.stats.transition.advance(1.f / 60.f))
                                setGrip(actor.stats.input, to);
                            if (!moving)
                                actor.mPtr.data.position.rot[2] = .02f * tick;
                            const auto gripPose = actor.stats.transition.pose();
                            actor.pose(1.f / 60.f, moving
                                ? osg::Vec3f{ { 225.f, 0.f, 0.f } } : osg::Vec3f{});
                            close(std::remainder(actor.animation.yaw - actor.mPtr.data.position.rot[2] - worldYaw,
                                      6.283185307f), 0.f,
                                "grip change rotated the character toward the camera");
                            check(!actor.animation.upperBodyOnly && !actor.animation.clip.starts_with("a000_029"),
                                "grip change replaced native-facing legs or discarded the selected gait");
                            if (moving)
                                check(actor.mEldenStridePhase != previousStride,
                                    "grip change stopped the locomotion clock");
                            previousStride = actor.mEldenStridePhase;
                            check(actor.animation.gripChange == (missingClip ? "" : gripPose.mAnimation),
                                "grip overlay selected the wrong hand or ignored missing assets");
                            if (!actor.animation.gripChange.empty())
                                close(actor.animation.gripFrame, gripPose.mFrame,
                                    "grip overlay lost the authored preparation/completion frame");
                        }
                    }
    for (const bool firstPerson : { false, true })
        for (const bool lockedOn : { false, true })
        {
            CharacterController actor;
            MWBase::Environment::get().world.firstPerson = firstPerson;
            actor.pose(1.f / 60.f, { { 225.f, 0.f, 0.f } });
            actor.stats.input.mLockedOn = lockedOn;
            actor.stats.transition.start(Grip::OneHand, Grip::TwoHandLeft);
            actor.mPtr.data.position.rot[2] = 1.f;
            actor.pose(1.f / 60.f, {});
            if (firstPerson || lockedOn)
                close(actor.animation.yaw, 0.f, "grip change overrode first-person or lock-on aiming");
        }
    MWBase::Environment::get().world.firstPerson = false;
}
int main(int argc, char** argv)
{
    try
    {
        const std::string test = argc > 1 ? argv[1] : "all";
        if (test == "all" || test == "teleport") cellTransitions();
        if (test == "all" || test == "pose") gripFacing();
        std::cout << "Passed " << checks << " production grip-state assertions (" << test << ")\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "Grip-state regression after " << checks << " assertions: " << error.what() << '\n';
        return 1;
    }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "apps/openmw/mwmechanics/character.cpp")
    parser.add_argument("--compiler", default=os.environ.get("CXX") or shutil.which("c++"))
    parser.add_argument("--case", choices=("all", "teleport", "pose"), default="all")
    args = parser.parse_args()
    if not args.compiler:
        parser.error("a C++20 compiler is required")
    source = args.source.read_text(encoding="utf-8")
    teleport = block(source, "if (cls.isActor() && cls.getCreatureStats(mPtr).wasTeleported())")
    pose = block(source, "void CharacterController::updateEldenPose(")
    pose = pose[pose.index("        mEldenPoseTime += duration;\n        const bool embeddedSpear"):-1]
    methods = (
        "\nvoid CharacterController::teleport()\n{\n const Class cls{ stats, movement };\n"
        + teleport + "\n}\nvoid CharacterController::pose(float duration, const osg::Vec3f& movement)\n{\n"
        " const auto& input = stats.getEldenCombatInput();\n"
        " const auto& combat = stats.getEldenCombat();\n const auto action = combat.action();\n"
        + pose + "\n}\n"
    )
    with tempfile.TemporaryDirectory(prefix="elden-grip-state-") as directory:
        fixture = Path(directory) / "grip-state.cpp"
        binary = Path(directory) / "grip-state"
        fixture.write_text(STUBS + methods + CHECKS, encoding="utf-8")
        subprocess.run(
            [args.compiler, "-std=c++20", "-O0", "-Wall", "-Wextra", "-I", str(ROOT),
             str(fixture), str(ROOT / "components/eldencombat/combat.cpp"),
             str(ROOT / "components/eldencombat/animations.cpp"), "-o", str(binary)],
            check=True,
        )
        subprocess.run([str(binary), args.case], check=True)


if __name__ == "__main__":
    main()
