#include "combat.hpp"
#include "timingdata.hpp"

#ifdef OPENMW_ELDENCOMBAT_IMPORTED_TIMINGS
#include "imported_timings.hpp"
#endif

#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

namespace
{
    using namespace EldenCombat;
    std::size_t checks = 0;

    void check(bool valid, const char* message)
    {
        ++checks;
        if (!valid)
            throw std::runtime_error(message);
    }

    void integer(std::string& bytes, std::uint32_t value)
    {
        for (unsigned int shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<char>((value >> shift) & 0xff));
    }

    void number(std::string& bytes, float value)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        integer(bytes, bits);
    }

    void definition(std::string& bytes, bool landing = false)
    {
        bytes += landing ? "a002_000002" : "a002_000001";
        for (const float value : { 60.f, 10.f, 11.f, 20.f, 21.f, 22.f, 23.f, 24.f, 25.f,
                 9999.f, 1.f, 3.f, 12.f, 18.f, 7.f, 2.f, 0.75f })
            number(bytes, value);
        integer(bytes, 2);
        for (unsigned int i = 0; i < 2; ++i)
        {
            for (const float value : { 11.f + i, 15.f + i, 1.2f, 12.f, 7.5f, 1.5f, 0.04f })
                number(bytes, value);
            bytes += "\1\1"; // Two successive contacts from the same hand.
        }
        integer(bytes, 2);
        for (const float value : { 0.f, 0.f, 0.f, -0.5f, 0.1f, 2.f })
            number(bytes, value);
        bytes.push_back(landing ? 0 : 3);
        if (!landing)
        {
            definition(bytes, true);
            definition(bytes, true);
        }
    }

    std::string fixture()
    {
        std::string bytes = "ERAT";
        integer(bytes, 1);
        integer(bytes, 1);
        integer(bytes, static_cast<unsigned int>(Action::Light1));
        integer(bytes, static_cast<unsigned int>(WeaponClass::StraightSword));
        integer(bytes, 0);
        integer(bytes, 0xffffffffu);
        integer(bytes, 10);
        integer(bytes, 11);
        definition(bytes);
        return bytes;
    }

    std::shared_ptr<const TimingData> read(const std::string& bytes)
    {
        std::istringstream stream(bytes);
        return std::make_shared<TimingData>(stream);
    }

    void reject(const std::string& bytes)
    {
        try
        {
            read(bytes);
        }
        catch (const std::runtime_error&)
        {
            ++checks;
            return;
        }
        check(false, "accepted malformed timing data");
    }

    void equal(const ActionDef& left, const ActionDef& right)
    {
        check(left.mAnimation == right.mAnimation && left.mNativeAnimation == right.mNativeAnimation,
            "animation metadata differs");
        check(left.mAnimationSpeed == right.mAnimationSpeed && left.mRootMotionSpeed == right.mRootMotionSpeed,
            "clock speed differs");
        for (const auto field : TimingData::scalarFields())
            check(left.*field == right.*field, "action scalar differs");
        check(left.mHitCount == right.mHitCount, "hit count differs");
        for (std::size_t i = 0; i < left.mHitCount; ++i)
        {
            const auto& a = left.mHits[i];
            const auto& b = right.mHits[i];
            check(a.mFrom == b.mFrom && a.mTo == b.mTo && a.mDamageMultiplier == b.mDamageMultiplier
                    && a.mStaminaCost == b.mStaminaCost && a.mStanceDamage == b.mStanceDamage
                    && a.mGuardDamage == b.mGuardDamage && a.mHitStop == b.mHitStop
                    && a.mLeftHand == b.mLeftHand && a.mHasHandAssignment == b.mHasHandAssignment,
                "hit metadata differs");
        }
        check(left.mRootMotion == right.mRootMotion, "root samples differ");
        check(bool(left.mLanding) == bool(right.mLanding)
                && bool(left.mLandingShort) == bool(right.mLandingShort),
            "landing phases differ");
        if (left.mLanding)
            equal(*left.mLanding, *right.mLanding);
        if (left.mLandingShort)
            equal(*left.mLandingShort, *right.mLandingShort);
    }

    void fixtures()
    {
        const auto bytes = fixture();
        const auto table = read(bytes);
        Config config;
        config.mSpeedMultiplier = 2.f;
        const auto attack = table->actionDefinition(Action::Light1, config, false);
        check(attack && attack->mDuration == 30.f && attack->mHits[0].mFrom == 5.5f, "weapon speed not applied");
        check(attack->mLanding && attack->mLandingShort && attack->mLanding->mDuration == 30.f
                && attack->mLandingShort->mHits[1].mTo == 8.f, "landing speed not applied");
        check(attack->mHits[0].mStaminaCost == 12.f && attack->mHits[1].mLeftHand
                && attack->mHits[1].mHasHandAssignment, "cost or authored hand changed");
        check(table->stanceCategory(WeaponClass::StraightSword, true) == 11
                && !table->hasWeaponMoveset(WeaponClass::StraightSword, true), "stance metadata changed");
        check(!table->actionDefinition(Action::Light1, config, true), "missing grip did not fall back");
        const auto original = Controller::actionDefinition(Action::Light1, config, false);
        setRuntimeTimings(table);
        equal(Controller::actionDefinition(Action::Light1, config, false), *attack);
        Controller controller;
        controller.configure(config);
        Input input;
        input.mLight.mPressed = true;
        controller.update(input, 1.f / 60.f);
        check(controller.action() == Action::Light1 && controller.definition().mDuration == 30.f,
            "controller did not consume runtime timings");
        check(stanceAnimation(WeaponClass::StraightSword, false, 0) == "a010_000000",
            "runtime stance not selected");
        for (std::size_t size = 0; size < bytes.size(); ++size)
            reject(bytes.substr(0, size));
        reject(bytes + "trailing");
        for (const std::size_t offset : { 8, 28, 32, 47, 119 })
        {
            auto broken = bytes;
            broken.replace(offset, 4, 4, static_cast<char>(0xff));
            reject(broken);
        }
        for (const std::size_t offset : { 0, 4, 12, 16, 20, 36, 115, 147, 148, 177, 178, 179, 207 })
        {
            auto broken = bytes;
            broken[offset] = static_cast<char>(0xff);
            reject(broken);
        }
        // Rejecting a replacement cannot mutate an already-installed table.
        check(runtimeTimings() == table, "failed load replaced the current table");
        setRuntimeTimings({});
        equal(Controller::actionDefinition(Action::Light1, config, false), original);
    }

    void actual(const char* path)
    {
        std::ifstream stream(path, std::ios::binary);
        const auto table = std::make_shared<TimingData>(stream);
        std::size_t variants = 0;
        for (int weapon = 0; weapon <= static_cast<int>(WeaponClass::Shield); ++weapon)
            for (const bool grip : { false, true })
                for (int load = 0; load <= static_cast<int>(Load::Heavy); ++load)
                    for (const float speed : { 0.f, 0.7f, 1.f, 1.4f, 8.f, std::numeric_limits<float>::quiet_NaN() })
                    {
                        Config config;
                        config.mWeaponClass = config.mLeftWeaponClass = static_cast<WeaponClass>(weapon);
                        config.mLoad = static_cast<Load>(load);
                        config.mSpeedMultiplier = config.mLeftSpeedMultiplier = speed;
#ifdef OPENMW_ELDENCOMBAT_IMPORTED_TIMINGS
                        check(table->stanceCategory(config.mWeaponClass, grip)
                                == importedStanceCategory(config.mWeaponClass, grip), "compiled stance differs");
                        check(table->hasWeaponMoveset(config.mWeaponClass, grip)
                                == importedHasWeaponMoveset(config.mWeaponClass, grip), "compiled moveset differs");
#endif
                        for (int index = 0; index <= static_cast<int>(Action::Dead); ++index)
                        {
                            const auto action = static_cast<Action>(index);
                            const auto runtime = table->actionDefinition(action, config, grip);
#ifdef OPENMW_ELDENCOMBAT_IMPORTED_TIMINGS
                            const auto compiled = importedActionDefinition(action, config, grip);
                            check(bool(runtime) == bool(compiled), "compiled action selection differs");
                            if (compiled)
                            {
                                equal(*runtime, *compiled);
                                setRuntimeTimings({});
                                const auto before = Controller::actionDefinition(action, config, grip);
                                setRuntimeTimings(table);
                                equal(before, Controller::actionDefinition(action, config, grip));
                            }
#else
                            setRuntimeTimings(table);
                            if (runtime && action != Action::Roll && action != Action::CrouchRoll)
                                equal(*runtime, Controller::actionDefinition(action, config, grip));
#endif
                            variants += runtime.has_value();
                        }
                    }
        setRuntimeTimings({});
        std::cout << "Runtime data: " << table->size() << " actions, " << variants << " variants\n";
    }
}

int main(int argc, char** argv)
{
    try
    {
        fixtures();
        if (argc > 1)
            actual(argv[1]);
        std::cout << "Timing data passed: " << checks << " assertions\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Timing data failed: " << error.what() << '\n';
        return 1;
    }
}
