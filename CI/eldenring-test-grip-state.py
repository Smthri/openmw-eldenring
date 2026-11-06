#!/usr/bin/env python3
"""Exercise production grip suspension, teleport and posing with native service stubs.

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
namespace ESM { struct WeaponType { static constexpr int MarksmanBow = 1; static constexpr int Spell = 2; }; }
namespace Settings
{
    struct Game
    {
        bool mTurnToMovementDirection = true;
        bool mPlayerMovementIgnoresAnimation = false;
        bool mSmoothMovement = false;
        float mSmoothMovementPlayerTurningDelay = .333f;
    };
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
    bool dead = false;
    bool isDead() const { return dead; }
    bool isDeathAnimationFinished() const { return false; }
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
    void setKnockedDown(bool) {}
    void setHitRecovery(bool) {}
    void setBlock(bool) {}
};
struct Position { float rot[3]{}; };
struct RefData
{
    Position position;
    const Position& getPosition() const { return position; }
};
struct Class;
struct Ptr
{
    Class* cls = nullptr;
    bool player = true;
    const Class& getClass() const { return *cls; }
    RefData data;
    const RefData& getRefData() const { return data; }
    bool operator==(const Ptr& other) const { return player == other.player; }
};
Ptr getPlayer() { return {}; }
void updateEldenBossArt(const Ptr&, float) {}
bool eldenReactionAction(EldenCombat::Action) { return false; }
std::string getWeaponAnimation(int) { return "weapon"; }
enum class UpperBodyState { None, WeaponEquipped, AttackWindUp, Equipping };
struct Movement { float mPosition[3]{}; float mSpeedFactor = 0.f; };
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
    std::optional<float> nativeYaw;
    float legsYaw = 0.f;
    void setEldenNativeYaw(std::optional<float> value) { nativeYaw = value; }
    float getLegsYawRadians() const { return legsYaw; }
    bool hasBank = true;
    bool hasEldenAnimation() const { return hasBank; }
    bool missingGrip = false;
    bool missingAction = false;
    bool deathRetimed = false;
    void clearEldenAnimation() { clip.clear(); gripChange.clear(); yaw = 0.f; upperBodyOnly = false; }
    bool getInfo(std::string_view, float* completion) const { *completion = 0.f; return true; }
    float getTextKeyTime(std::string_view name) const { return name.ends_with("stop") ? 3.f : 0.f; }
    void adjustSpeedMult(std::string_view, float) { deathRetimed = true; }
    bool embeddedSpear = false;
    bool hasEldenEmbeddedSpear() const { return embeddedSpear; }
    bool getWeaponsShown() const { return true; }
    void showWeapons(bool) {}
    void showCarriedLeft(bool) {}
    void disable(std::string_view) {}
    float getEldenAnimationLength(std::string_view name) const
    {
        return missingGrip && name.starts_with("a000_029") ? 0.f : 60.f;
    }
    bool playEldenAnimation(std::string_view name, float at, bool loop, float, float facing = 0.f,
        std::string_view = {}, std::string_view = {}, std::string_view = {},
        bool upper = false, bool = false, float = 0.f,
        std::string_view change = {}, float changeFrame = 0.f)
    {
        if (missingAction && !loop)
        {
            clearEldenAnimation();
            return false;
        }
        clip = name;
        frame = at;
        yaw = facing;
        upperBodyOnly = upper;
        gripChange = change;
        gripFrame = changeFrame;
        return true;
    }
};
constexpr int CharState_None = 0;
constexpr int CharState_TurnLeft = 1, CharState_TurnRight = 2;
constexpr int CharState_SwimTurnLeft = 3, CharState_SwimTurnRight = 4;
struct CharacterController
{
    Ptr mPtr;
    CreatureStats stats;
    Movement movement;
    Class cls{ stats, movement };
    CharacterController() { mPtr.cls = &cls; }
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
    bool mEldenCombatActive = true;
    bool mEldenGuardAnimation = false;
    unsigned mEldenAnimationSequence = 0;
    bool mEldenCriticalPending = false;
    EldenCombat::Action mEldenAnimationAction = EldenCombat::Action::Idle;
    std::string mEldenDodgeAnimation;
    std::string mCurrentWeapon;
    int mWeaponTypeId = 0;
    UpperBodyState mUpperBodyState = UpperBodyState::WeaponEquipped;
    bool visuallyGrounded = true;
    int mHitState = CharState_None, mDeathState = CharState_None;
    int mIdleState = 1, mMovementState = CharState_None;
    bool mMovementAnimationHasMovement = true, mInJump = false;
    bool isMovementAnimationControlled() const;
    bool useEldenNativeFacing() const;
    int cameraTurn(float effectiveRotation, bool retainPrevious);
    bool isTurning() const { return true; }
    bool scripted = false;
    bool isScriptedAnimPlaying() const { return scripted; }
    bool bow = false;
    bool mEldenPoseDead = false;
    std::string mCurrentDeath = "death1";

    void death(float duration);
    void teleport();
    void suspend(float duration);
    bool updateCarriedLeftVisible(int) { return true; }
    void clearStateAnimation(std::string&) {}
    void resetCurrentHitState() {}
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
                            if (moving)
                            {
                                close(std::remainder(actor.animation.yaw - worldYaw, 6.283185307f), 0.f,
                                    "grip change rotated the running gait");
                                check(!actor.animation.upperBodyOnly && !actor.animation.clip.starts_with("a000_029"),
                                    "grip change discarded the imported running gait");
                                check(actor.mEldenStridePhase != previousStride,
                                    "grip change stopped the running clock");
                            }
                            else
                            {
                                close(std::remainder(actor.animation.yaw - actor.mPtr.data.position.rot[2] - worldYaw,
                                          6.283185307f), 0.f, "grip change rotated the armed idle");
                                check(!actor.animation.clip.empty() && !actor.animation.upperBodyOnly,
                                    "drawn weapon lost its imported ready stance during a grip change");
                            }
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
void nativePoses()
{
    for (const bool equipped : { false, true })
        for (const bool bow : { false, true })
            for (const bool running : { false, true })
                for (const float speed : { 0.f, 50.f })
                {
                    CharacterController actor;
                    actor.stats.running = running;
                    actor.stats.draw = equipped ? DrawState::Weapon : DrawState::Nothing;
                    actor.bow = bow;
                    actor.animation.clip = "previous combat pose";
                    actor.pose(1.f / 60.f, { { 0.f, speed, 0.f } });
                    if (!equipped && (speed == 0.f || !running))
                        check(actor.animation.clip.empty(), "sheathed idle/walk was covered by an imported pose");
                    else
                        check(!actor.animation.clip.empty() && !actor.animation.upperBodyOnly,
                            "drawn weapon or running lost its imported full-body pose");
                }
    for (const bool moving : { false, true })
        for (const bool twoHanded : { false, true })
        {
            CharacterController actor;
            actor.stats.running = false;
            actor.stats.input.mTwoHanded = twoHanded;
            auto config = actor.stats.combat.config();
            config.mHasShield = !twoHanded;
            actor.stats.combat.configure(config);
            actor.stats.input.mGuard.mHeld = true;
            actor.stats.combat.update(actor.stats.input, .5f);
            check(actor.stats.combat.guarding(), "fixture failed to raise guard");
            actor.pose(1.f / 60.f, { { 0.f, moving ? 50.f : 0.f, 0.f } });
            check(!actor.animation.upperBodyOnly && !actor.animation.clip.empty(),
                "guard lost imported armed idle or walking legs");
        }
    CharacterController actor;
    actor.stats.running = false;
    actor.pose(1.f / 60.f, {});
    check(!actor.animation.clip.empty(), "drawn weapon did not enter imported idle");
    actor.pose(1.f / 60.f, { { 0.f, 50.f, 0.f } });
    check(!actor.animation.clip.empty(), "drawn weapon lost imported walking");
    actor.pose(1.f / 60.f, {});
    check(!actor.animation.clip.empty(), "stopping a walk did not restore imported armed idle");
    actor.stats.draw = DrawState::Nothing;
    actor.pose(1.f / 60.f, { { 0.f, 50.f, 0.f } });
    check(actor.animation.clip.empty(), "sheathing retained imported walking");
    actor.pose(1.f / 60.f, {});
    check(actor.animation.clip.empty(), "sheathing retained imported idle");
    actor.animation.embeddedSpear = true;
    actor.pose(1.f / 60.f, {});
    check(!actor.animation.clip.empty(), "embedded spear lost imported armed idle");
    actor.animation.embeddedSpear = false;
    actor.stats.dead = true;
    actor.animation.clip = "active attack";
    actor.death(1.f / 60.f);
    check(actor.animation.clip.empty(), "death retained imported combat pose");
    check(!actor.animation.deathRetimed, "death duration changed from native text keys");
}
void nativeMovement()
{
    CharacterController actor;
    actor.stats.draw = DrawState::Nothing;
    check(actor.isMovementAnimationControlled(), "native idle lost root movement");
    actor.stats.draw = DrawState::Weapon;
    check(!actor.isMovementAnimationControlled(), "armed idle gained hidden native root movement");
    actor.stats.draw = DrawState::Nothing;
    actor.animation.embeddedSpear = true;
    check(!actor.isMovementAnimationControlled(), "embedded spear idle gained native root movement");
    actor.animation.embeddedSpear = false;
    actor.stats.running = false;
    actor.movement.mSpeedFactor = 1.f;
    actor.mMovementState = 1;
    check(actor.isMovementAnimationControlled(), "native walk lost root movement");
    actor.stats.draw = DrawState::Weapon;
    check(!actor.isMovementAnimationControlled(), "drawn walking gained native root movement");
    actor.stats.draw = DrawState::Nothing;
    check(actor.isMovementAnimationControlled(), "sheathing failed to restore native walking root");
    actor.stats.running = true;
    check(!actor.isMovementAnimationControlled(), "native root replaced imported run velocity");
    // Deceleration still has smoothed travel after requested movement stops.
    actor.movement.mSpeedFactor = .1f;
    check(!actor.isMovementAnimationControlled(), "NPC deceleration reverted imported running velocity");
    actor.movement.mSpeedFactor = 0.f;
    check(actor.isMovementAnimationControlled(), "stopped runner lost native idle root");
    actor.stats.input.mCrouching = true;
    check(!actor.isMovementAnimationControlled(), "crouching lost imported movement");
    actor.stats.input.mCrouching = false;
    actor.stats.input.mGrounded = false;
    check(!actor.isMovementAnimationControlled(), "falling gained native root movement");
    actor.stats.input.mFlying = true;
    check(actor.isMovementAnimationControlled(), "native levitation idle lost its movement policy");
    actor.stats.input.mGrounded = true;
    actor.stats.input.mFlying = false;
    actor.stats.input.mLight = { true, true, false };
    actor.stats.combat.update(actor.stats.input, 1.f / 60.f);
    check(actor.stats.combat.action() != EldenCombat::Action::Idle, "attack fixture remained idle");
    check(!actor.isMovementAnimationControlled(), "attack gained native root displacement");
    actor.mDeathState = 1;
    check(actor.isMovementAnimationControlled(), "death lost native root displacement");
    actor.mDeathState = CharState_None;
    actor.animation.hasBank = false;
    check(actor.isMovementAnimationControlled(), "missing assets lost native movement");
    actor.animation.hasBank = true;
    actor.mEldenCombatActive = false;
    check(actor.isMovementAnimationControlled(), "disabled combat lost native movement");
    Settings::game().mPlayerMovementIgnoresAnimation = true;
    check(!actor.isMovementAnimationControlled(), "native player movement preference was ignored");
    Settings::game().mPlayerMovementIgnoresAnimation = false;
}
void nativeFacing()
{
    CharacterController actor;
    actor.stats.draw = DrawState::Nothing;
    actor.mUpperBodyState = UpperBodyState::None;
    actor.stats.running = false;
    // Native directional gait defines initial body heading. Camera rotation
    // must not change that world heading once movement stops, through wraps.
    check(actor.cameraTurn(.1f, false) == CharState_None, "camera orbit started native right turn");
    check(actor.cameraTurn(-.1f, false) == CharState_None, "camera orbit started native left turn");
    check(actor.cameraTurn(0.f, true) == CharState_None, "camera orbit retained prior native turn");
    for (const float legYaw : { -1.5f, 0.f, 1.5f })
    {
        actor.animation.legsYaw = legYaw;
        actor.mPtr.data.position.rot[2] = .7f;
        actor.pose(1.f / 60.f, { { 0.f, 50.f, 0.f } });
        check(actor.animation.nativeYaw.has_value(), "native walking lacks coherent body facing");
        close(*actor.animation.nativeYaw, legYaw, "native gait direction changed");
        const float worldYaw = legYaw - .7f;
        for (int frame = -360; frame <= 360; ++frame)
        {
            actor.mPtr.data.position.rot[2] = frame * .03f;
            actor.animation.legsYaw = 0.f;
            actor.pose(1.f / 60.f, {});
            check(actor.animation.nativeYaw.has_value(), "sheathed idle lost orbit facing");
            close(std::remainder(*actor.animation.nativeYaw - actor.mPtr.data.position.rot[2] - worldYaw,
                      6.283185307f), 0.f, "camera rotation changed native idle world heading");
            check(actor.animation.clip.empty(), "native orbit replaced Morrowind idle");
        }
    }
    // The visual override must not leak into the existing imported mode or
    // native first-person/lock-on/scripted/fallback paths.
    actor.stats.draw = DrawState::Weapon;
    check(actor.cameraTurn(.1f, false) == CharState_TurnRight, "imported-mode turn bookkeeping changed");
    actor.pose(1.f / 60.f, {});
    check(!actor.animation.nativeYaw, "drawn weapon inherited native yaw override");
    check(!actor.animation.clip.empty(), "drawn weapon lost imported pose");
    actor.stats.draw = DrawState::Nothing;
    MWBase::Environment::get().world.firstPerson = true;
    actor.pose(1.f / 60.f, {});
    check(!actor.animation.nativeYaw, "first person inherited native orbit yaw");
    MWBase::Environment::get().world.firstPerson = false;
    actor.stats.input.mLockedOn = true;
    actor.pose(1.f / 60.f, {});
    check(!actor.animation.nativeYaw, "lock-on inherited native orbit yaw");
    actor.stats.input.mLockedOn = false;
    actor.scripted = true;
    check(!actor.useEldenNativeFacing(), "scripted actor received orbit facing");
    actor.scripted = false;
    actor.animation.hasBank = false;
    check(!actor.useEldenNativeFacing(), "missing bank changed native facing");
    actor.animation.hasBank = true;
    actor.mEldenCombatActive = false;
    check(!actor.useEldenNativeFacing(), "disabled combat changed native facing");
    check(actor.cameraTurn(-.1f, false) == CharState_TurnLeft, "disabled combat lost native turn");
    actor.mEldenCombatActive = true;
    actor.mPtr.player = false;
    check(!actor.useEldenNativeFacing(), "NPC inherited player camera orbit");
    actor.mPtr.player = true;
    actor.mUpperBodyState = UpperBodyState::Equipping;
    check(!actor.useEldenNativeFacing(), "equip transition inherited orbit yaw");
    actor.mUpperBodyState = UpperBodyState::None;
    actor.pose(1.f / 60.f, {});
    actor.stats.dead = true;
    actor.death(1.f / 60.f);
    check(!actor.animation.nativeYaw, "death retained player orbit yaw");
}
void suspendedGrip()
{
    using EldenCombat::Grip;
    for (const Grip from : { Grip::OneHand, Grip::TwoHandRight, Grip::TwoHandLeft })
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
                actor.stats.input.mMoveY = 1.f;
                actor.stats.input.mHandsBusy = true;
                actor.suspend(1.f / 60.f);
                check(actor.stats.input.mTwoHanded == twoHanded && actor.stats.input.mTwoHandedLeft == left,
                    "swimming or save-screen suspension changed the applied grip");
                check(!actor.stats.input.mLight.mHeld && !actor.stats.input.mGuard.mHeld
                        && !actor.stats.input.mHandsBusy && actor.stats.input.mMoveY == 0.f,
                    "suspension retained queued buttons or movement");
                check(!actor.stats.transition.active(), "suspension retained a pending grip change");
                actor.suspend(1.f / 60.f);
                check(actor.stats.input.mTwoHanded == twoHanded && actor.stats.input.mTwoHandedLeft == left,
                    "continued suspension changed the applied grip");
            }
}
void dodgeFacing()
{
    // Drive the real controller through directional rolls and neutral
    // backsteps. The animation service reports a successfully sampled pose;
    // --imported also uses the retained locally extracted action definitions.
    for (const float rate : { 30.f, 60.f, 144.f })
        for (const bool longFinalUpdate : { false, true })
            for (const bool missingAction : { false, true })
                for (const auto direction : { std::array{ 1.f, 0.f }, std::array{ -1.f, 1.f },
                         std::array{ 0.f, -1.f }, std::array{ 0.f, 0.f } })
                {
                    CharacterController actor;
                    actor.stats.draw = DrawState::Nothing;
                    actor.stats.running = false;
                    actor.mUpperBodyState = UpperBodyState::None;
                    actor.mEldenMeleeActive = false;
                    actor.animation.missingAction = missingAction;
                    const float dt = 1.f / rate;
                    const bool backstep = direction[0] == 0.f && direction[1] == 0.f;
                    const float actionYaw = backstep ? 0.f : std::atan2(-direction[0], direction[1]);
                    actor.animation.legsYaw = backstep ? 3.14159265359f : actionYaw + .6f;
                    actor.mPtr.data.position.rot[2] = .7f;
                    // Seed an unrelated prior idle heading. A completed action
                    // must replace it with its visible facing, not its travel.
                    actor.pose(dt, {});
                    actor.stats.input.mMoveX = direction[0];
                    actor.stats.input.mMoveY = direction[1];
                    actor.stats.input.mDodge = { false, true, true };
                    actor.mEldenUpdate = actor.stats.combat.update(actor.stats.input, dt);
                    check(actor.stats.combat.action() == (backstep ? EldenCombat::Action::Backstep
                                                                  : EldenCombat::Action::Roll),
                        "dodge-facing fixture did not start the requested action");
                    actor.pose(dt, {});
                    if (!missingAction)
                        close(actor.animation.yaw, actionYaw, "action pose faced displacement instead of authored direction");
                    if (backstep)
                        check(actor.mEldenUpdate.mMoveY < 0.f, "backstep fixture did not travel backwards");
                    actor.stats.input.mDodge = {};
                    actor.stats.input.mMoveX = actor.stats.input.mMoveY = 0.f;
                    float shownWorldYaw = actionYaw - actor.mPtr.data.position.rot[2];
                    float elapsed = 0.f;
                    bool finished = false;
                    for (int frame = 0; frame < 1000; ++frame)
                    {
                        const float step = longFinalUpdate && elapsed >= .3f ? 1.f : dt;
                        actor.mPtr.data.position.rot[2] += .013f;
                        actor.mEldenUpdate = actor.stats.combat.update(actor.stats.input, step);
                        // The engine adds mDisplacement to physics velocity
                        // AFTER updateEldenPose. No user movement enters posing.
                        actor.pose(step, {});
                        if (actor.stats.combat.action() == EldenCombat::Action::Idle)
                        {
                            finished = true;
                            if (longFinalUpdate)
                                check(std::hypot(actor.mEldenUpdate.mDisplacementX,
                                          actor.mEldenUpdate.mDisplacementY) > 0.f,
                                    "long update did not retain final action displacement");
                            check(actor.animation.nativeYaw.has_value(), "finished dodge failed to resume native idle");
                            const float expected = missingAction
                                ? actor.animation.legsYaw - actor.mPtr.data.position.rot[2] : shownWorldYaw;
                            close(std::remainder(*actor.animation.nativeYaw - actor.mPtr.data.position.rot[2] - expected,
                                      6.283185307f), 0.f,
                                "finished sheathed dodge snapped to stale native legs instead of action facing");
                            shownWorldYaw = expected;
                            break;
                        }
                        if (!missingAction)
                            close(actor.animation.yaw, actionYaw, "active dodge lost its captured facing");
                        shownWorldYaw = actionYaw - actor.mPtr.data.position.rot[2];
                        elapsed += step;
                    }
                    check(finished, "dodge-facing controller never returned to idle");
                    for (int orbit = -24; orbit <= 24; ++orbit)
                    {
                        actor.mPtr.data.position.rot[2] = orbit * .4f;
                        actor.mEldenUpdate = actor.stats.combat.update(actor.stats.input, dt);
                        actor.pose(dt, {});
                        close(std::remainder(*actor.animation.nativeYaw - actor.mPtr.data.position.rot[2] - shownWorldYaw,
                                  6.283185307f), 0.f, "camera orbit changed completed action heading");
                    }
                    // With smoothing disabled, new walking takes ownership immediately.
                    actor.animation.legsYaw = -.6f;
                    actor.pose(dt, { { 30.f, 40.f, 0.f } });
                    close(*actor.animation.nativeYaw, -.6f, "walking retained the completed dodge's heading");
                }
}
void smoothFacing()
{
    Settings::game().mSmoothMovement = true;
    Settings::game().mTurnToMovementDirection = true;
    constexpr float pi = 3.14159265359f;
    for (const float rate : { 30.f, 60.f, 144.f })
    {
        const float dt = 1.f / rate;
        const float limit = pi * dt / Settings::game().mSmoothMovementPlayerTurningDelay + .0001f;
        CharacterController actor;
        actor.stats.draw = DrawState::Nothing;
        actor.mUpperBodyState = UpperBodyState::None;
        actor.stats.running = false;
        actor.pose(dt, {});
        actor.mPtr.data.position.rot[2] = pi;
        actor.pose(dt, {});
        float worldYaw = *actor.animation.nativeYaw - actor.mPtr.data.position.rot[2];
        // Starting imported running from a front-facing native idle must turn
        // gradually, rather than discarding the heading at the clip boundary.
        actor.stats.running = true;
        for (int frame = 0; frame < int(rate); ++frame)
        {
            actor.pose(dt, { { 0.f, 100.f, 0.f } });
            const float next = actor.animation.yaw - actor.mPtr.data.position.rot[2];
            check(std::abs(std::remainder(next - worldYaw, 2.f * pi)) <= limit,
                "sheathed run snapped when leaving native idle");
            worldYaw = next;
        }
        close(std::remainder(worldYaw + pi, 2.f * pi), 0.f, "sheathed run never reached travel facing");
        // Both camera-relative direction changes and native/imported switches
        // use one world-space turning limit.
        for (int frame = 0; frame < int(rate); ++frame)
        {
            actor.mPtr.data.position.rot[2] += .8f * dt;
            actor.stats.running = frame % 2 == 0;
            actor.animation.legsYaw = .7f;
            actor.pose(dt, { { -70.f, 50.f, 0.f } });
            const float poseYaw = actor.animation.nativeYaw.value_or(actor.animation.yaw);
            const float next = poseYaw - actor.mPtr.data.position.rot[2];
            check(std::abs(std::remainder(next - worldYaw, 2.f * pi)) <= limit,
                "native walk/imported run transition snapped its facing");
            worldYaw = next;
        }
        actor.stats.running = false;
        actor.mPtr.data.position.rot[2] += 1.f;
        actor.pose(dt, {});
        close(std::remainder(*actor.animation.nativeYaw - actor.mPtr.data.position.rot[2] - worldYaw, 2.f * pi),
            0.f, "stopping completed a hidden turn instead of holding displayed heading");

        Settings::game().mSmoothMovement = false;
        actor.pose(dt, { { 0.f, 50.f, 0.f } });
        close(*actor.animation.nativeYaw, .7f, "disabled smoothing lost immediate native facing");
        Settings::game().mSmoothMovement = true;
    }
    // Drawn weapons retain the original immediate heading selection.
    CharacterController armed;
    armed.pose(1.f / 60.f, {});
    armed.mPtr.data.position.rot[2] = pi;
    armed.pose(1.f / 60.f, { { 0.f, 100.f, 0.f } });
    close(armed.animation.yaw, 0.f, "sheathed smoothing changed drawn-weapon facing");
    Settings::game().mSmoothMovement = false;
}
int main(int argc, char** argv)
{
    try
    {
        const std::string test = argc > 1 ? argv[1] : "all";
        if (test == "all" || test == "teleport") cellTransitions();
        if (test == "all" || test == "pose") { gripFacing(); nativePoses(); nativeMovement(); nativeFacing(); }
        if (test == "all" || test == "suspend") suspendedGrip();
        if (test == "all" || test == "dodge-facing") dodgeFacing();
        if (test == "all" || test == "smooth-facing") smoothFacing();
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
    parser.add_argument("--case", choices=("all", "teleport", "pose", "suspend", "dodge-facing", "smooth-facing"), default="all")
    parser.add_argument("--imported", action="store_true", help="use the retained local imported timing header")
    args = parser.parse_args()
    if not args.compiler:
        parser.error("a C++20 compiler is required")
    source = args.source.read_text(encoding="utf-8")
    teleport = block(source, "if (cls.isActor() && cls.getCreatureStats(mPtr).wasTeleported())")
    update = block(source, "void CharacterController::updateEldenCombat(")
    cancel_grip = (
        update[update.index("        if (!mEldenMeleeActive || combat.action()"):
               update.index("        if (wasEldenMelee")]
    )
    suspend = block(update, "if (!enabled)")
    pose = block(source, "void CharacterController::updateEldenPose(")
    reset_yaw = "mAnimation->setEldenNativeYaw(std::nullopt);"
    reset_yaw = reset_yaw if reset_yaw in pose else ""
    death = block(pose, "if (stats.isDead())")
    pose = pose[pose.index("        mEldenPoseTime += duration;\n        const bool embeddedSpear"):-1]
    movement = block(source, "bool CharacterController::isMovementAnimationControlled() const")
    facing = (block(source, "bool CharacterController::useEldenNativeFacing() const")
              if "bool CharacterController::useEldenNativeFacing() const" in source else
              "bool CharacterController::useEldenNativeFacing() const { return false; }")
    turn = block(source, "if (!sneak && !isFirstPersonPlayer && isBiped")
    retained_turn = block(source, "else if (movestate == CharState_None && isTurning()")
    retained_turn = retained_turn.replace("else if", "if", 1)
    methods = (
        "\nvoid CharacterController::teleport()\n{\n const Class cls{ stats, movement };\n"
        + teleport + "\n}\nvoid CharacterController::pose(float duration, const osg::Vec3f& movement)\n{\n"
        " const auto& input = stats.getEldenCombatInput();\n"
        " const auto& combat = stats.getEldenCombat();\n const auto action = combat.action();\n"
        + reset_yaw + pose + "\n}\n"
        + "\nvoid CharacterController::suspend(float duration)\n{\n"
        " const bool enabled = false;\n mEldenMeleeActive = false;\n"
        " auto& input = stats.input;\n auto& combat = stats.combat;\n auto& gripTransition = stats.transition;\n"
        + cancel_grip + suspend + "\n}\n"
        + "\nvoid CharacterController::death(float duration)\n{\n (void)duration;\n" + reset_yaw + death + "\n}\n"
        + movement + facing + "\n"
        + "int CharacterController::cameraTurn(float effectiveRotation, bool retainPrevious) {\n"
        " bool sneak = false, isBiped = true, inwater = false;\n"
        " bool isFirstPersonPlayer = MWBase::Environment::get().world.firstPerson;\n"
        " int movestate = CharState_None;\n"
        " if (!retainPrevious) {" + turn + "} else {\n"
        " float mTurnAnimationThreshold = 1.f; int mMovementState = CharState_TurnRight;\n"
        + retained_turn + "}\n return movestate; }\n"
    )
    with tempfile.TemporaryDirectory(prefix="elden-grip-state-") as directory:
        fixture = Path(directory) / "grip-state.cpp"
        binary = Path(directory) / "grip-state"
        fixture.write_text(STUBS + methods + CHECKS, encoding="utf-8")
        subprocess.run(
            [args.compiler, "-std=c++20", "-O0", "-Wall", "-Wextra", "-I", str(ROOT),
             *(["-DOPENMW_ELDENCOMBAT_IMPORTED_TIMINGS"] if args.imported else []),
             str(fixture), str(ROOT / "components/eldencombat/combat.cpp"),
             str(ROOT / "components/eldencombat/animations.cpp"), "-o", str(binary)],
            check=True,
        )
        subprocess.run([str(binary), args.case], check=True)


if __name__ == "__main__":
    main()
