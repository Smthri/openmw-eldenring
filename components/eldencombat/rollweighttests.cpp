#include "combat.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    using namespace EldenCombat;

    void check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void close(float actual, float expected, const char* message)
    {
        check(std::isfinite(actual) && std::abs(actual - expected) < 0.001f, message);
    }

    float nativeDistance(const ActionDef& definition)
    {
        if (definition.mRootMotion.empty())
            return definition.mDistance;
        const auto first = Controller::rootMotionAt(definition, 0.f);
        const auto last = Controller::rootMotionAt(definition, definition.mDuration);
        return std::hypot(last[0] - first[0], last[2] - first[2]);
    }

    Input dodgeInput(Action action)
    {
        Input input;
        input.mDodge = { false, true, true };
        input.mCrouching = action == Action::CrouchRoll;
        if (action != Action::Backstep)
        {
            input.mMoveX = 0.6f;
            input.mMoveY = 0.8f;
        }
        return input;
    }

    std::array<float, 2> finishDodge(Controller& controller, std::array<float, 2> displacement = {})
    {
        for (int tick = 0; controller.action() != Action::Idle && tick < 1000; ++tick)
        {
            const Update update = controller.update({}, TickSeconds);
            displacement[0] += update.mDisplacementX;
            displacement[1] += update.mDisplacementY;
        }
        check(controller.action() == Action::Idle, "dodge did not finish");
        return displacement;
    }

    void loadThresholds()
    {
        check(Controller::loadFromRatio(0.f) == Load::Light, "empty equipment load must be light");
        check(Controller::loadFromRatio(std::nextafter(0.3f, 0.f)) == Load::Light,
            "load immediately below the light threshold must be light");
        check(Controller::loadFromRatio(0.3f) == Load::Medium, "light threshold equality must be medium");
        check(Controller::loadFromRatio(std::nextafter(0.7f, 0.f)) == Load::Medium,
            "load immediately below the heavy threshold must be medium");
        check(Controller::loadFromRatio(0.7f) == Load::Heavy, "heavy threshold equality must be heavy");
        check(Controller::loadFromRatio(1.5f) == Load::Heavy, "overloaded equipment must be heavy");
        check(Controller::loadFromRatio(-1.f) == Load::Light, "negative effective load must behave as zero");
        check(Controller::loadFromRatio(0.2f, 0.2f, 0.8f) == Load::Medium,
            "custom light threshold was ignored");
        check(Controller::loadFromRatio(0.8f, 0.2f, 0.8f) == Load::Heavy,
            "custom heavy threshold was ignored");
        check(Controller::loadFromRatio(0.5f, 0.8f, 0.2f) == Load::Medium,
            "reversed thresholds must be ordered");
        check(Controller::loadFromRatio(0.5f, -1.f, 2.f) == Load::Medium,
            "out-of-range thresholds must be clamped");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        check(Controller::loadFromRatio(0.5f, nan, nan) == Load::Medium,
            "invalid thresholds must use defaults");
        for (const float invalid : { nan, std::numeric_limits<float>::infinity() })
            check(Controller::loadFromRatio(invalid) == Load::Heavy, "invalid load must conservatively be heavy");
    }

    void rollDistances()
    {
        for (const bool custom : { false, true })
        {
            Config config;
            if (custom)
            {
                config.mLightRollDistance = 6.f;
                config.mMediumRollDistance = 4.f;
                config.mHeavyRollDistance = 2.f;
            }
            for (const Load load : { Load::Light, Load::Medium, Load::Heavy })
            {
                config.mLoad = load;
                const float expected = load == Load::Light ? config.mLightRollDistance
                    : (load == Load::Medium ? config.mMediumRollDistance : config.mHeavyRollDistance);
                close(Controller::rollDistance(config), expected, "configured tier distance was ignored");
                for (const Action action : { Action::Roll, Action::CrouchRoll })
                {
                    Config disabled = config;
                    disabled.mWeightBasedRollDistance = false;
                    const ActionDef original = Controller::actionDefinition(action, disabled);
                    Config nativeConfig;
                    nativeConfig.mLoad = load;
                    nativeConfig.mWeightBasedRollDistance = false;
                    const ActionDef native = Controller::actionDefinition(action, nativeConfig);
                    check(original.mDistance == native.mDistance && original.mRootMotion == native.mRootMotion,
                        "disabled distance tuning still changes native displacement");
                    const ActionDef scaled = Controller::actionDefinition(action, config);
                    close(scaled.mDistance, expected, "AI prediction does not match configured roll distance");
                    close(nativeDistance(scaled), expected, "roll root motion does not match its configured distance");
                    check(scaled.mDuration == original.mDuration && scaled.mAnimation == original.mAnimation
                            && scaled.mAnimationSpeed == original.mAnimationSpeed
                            && scaled.mInvincibleFrom == original.mInvincibleFrom
                            && scaled.mInvincibleTo == original.mInvincibleTo
                            && scaled.mCancelDodge == original.mCancelDodge
                            && scaled.mCancelMove == original.mCancelMove
                            && scaled.mStartCost == original.mStartCost,
                        "distance tuning changed roll timing, animation or stamina cost");
                    check(scaled.mRootMotion.size() == original.mRootMotion.size(), "roll samples were discarded");
                    for (std::size_t frame = 0; frame < scaled.mRootMotion.size(); ++frame)
                        close(scaled.mRootMotion[frame][1], original.mRootMotion[frame][1],
                            "distance tuning changed vertical root motion");

                    Controller controller;
                    controller.configure(config);
                    controller.update(dodgeInput(action), TickSeconds);
                    check(controller.action() == action, "requested roll did not start");
                    const auto displacement = finishDodge(controller);
                    close(std::hypot(displacement[0], displacement[1]), expected,
                        "executed diagonal roll does not travel the configured distance");

                    controller.configure(disabled);
                    controller.update(dodgeInput(action), TickSeconds);
                    const auto nativeDisplacement = finishDodge(controller);
                    close(std::hypot(nativeDisplacement[0], nativeDisplacement[1]), nativeDistance(original),
                        "disabled distance setting did not preserve native roll travel");
                }

                for (const Action action : { Action::Backstep, Action::Light1, Action::Jump })
                {
                    Config disabled = config;
                    disabled.mWeightBasedRollDistance = false;
                    const ActionDef original = Controller::actionDefinition(action, disabled);
                    const ActionDef selected = Controller::actionDefinition(action, config);
                    check(selected.mDistance == original.mDistance && selected.mRootMotion == original.mRootMotion,
                        "roll distance tuning changed another action's displacement");
                }
            }
        }
    }

    void rollSnapshot()
    {
        Config config;
        config.mLoad = Load::Light;
        config.mLightRollDistance = 6.f;
        config.mHeavyRollDistance = 2.f;
        Controller controller;
        controller.configure(config);
        controller.update(dodgeInput(Action::Roll), TickSeconds);
        std::array<float, 2> displacement{};
        for (int tick = 0; tick < 10; ++tick)
        {
            const Update update = controller.update({}, TickSeconds);
            displacement[0] += update.mDisplacementX;
            displacement[1] += update.mDisplacementY;
        }
        config.mLoad = Load::Heavy;
        controller.configure(config);
        displacement = finishDodge(controller, displacement);
        close(std::hypot(displacement[0], displacement[1]), 6.f, "equipment change altered a roll already underway");

        controller.update(dodgeInput(Action::Roll), TickSeconds);
        const auto heavy = finishDodge(controller);
        close(std::hypot(heavy[0], heavy[1]), 2.f, "next roll ignored the new equipment load");

        config.mLoad = Load::Light;
        controller.configure(config);
        Input attack;
        attack.mLight = { true, true, false };
        controller.update(attack, TickSeconds);
        for (int tick = 0; controller.frame() < controller.definition().mDodgeInputFrom && tick < 1000; ++tick)
            controller.update({}, TickSeconds);
        check(controller.action() == Action::Light1 && controller.frame() < controller.definition().mCancelDodge,
            "buffered roll fixture must precede the attack's dodge cancel window");
        Input dodge = dodgeInput(Action::Roll);
        controller.update(dodge, TickSeconds);
        check(controller.action() == Action::Light1, "roll must initially be buffered");
        config.mLoad = Load::Heavy;
        controller.configure(config);
        dodge.clearEdges();
        for (int tick = 0; controller.action() == Action::Light1 && tick < 1000; ++tick)
            controller.update(dodge, TickSeconds);
        check(controller.action() == Action::Roll, "buffered roll did not start");
        close(controller.definition().mDistance, 2.f, "buffered roll captured equipment load before it started");
        config.mLoad = Load::Light;
        config.mWeightBasedRollDistance = false;
        controller.configure(config);
        const auto buffered = finishDodge(controller);
        close(std::hypot(buffered[0], buffered[1]), 2.f,
            "changing load or disabling the option altered a buffered roll after it started");
    }

    void invalidDistances()
    {
        Config config;
        config.mLightRollDistance = -2.f;
        config.mMediumRollDistance = 100.f;
        config.mHeavyRollDistance = std::numeric_limits<float>::quiet_NaN();
        Controller controller;
        controller.configure(config);
        close(controller.config().mLightRollDistance, 0.1f, "negative roll distance was not clamped");
        close(controller.config().mMediumRollDistance, 20.f, "excessive roll distance was not clamped");
        close(controller.config().mHeavyRollDistance, 3.31f, "invalid roll distance did not use its default");
        for (const Load load : { Load::Light, Load::Medium, Load::Heavy })
        {
            config.mLoad = load;
            const float expected = load == Load::Light ? 0.1f : (load == Load::Medium ? 20.f : 3.31f);
            close(Controller::actionDefinition(Action::Roll, config).mDistance, expected,
                "direct AI roll prediction did not validate settings");
        }
    }
}

int main()
{
    try
    {
        loadThresholds();
        rollDistances();
        rollSnapshot();
        invalidDistances();
        std::cout << "EldenCombat weight-based roll tests passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "EldenCombat weight-based roll test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
