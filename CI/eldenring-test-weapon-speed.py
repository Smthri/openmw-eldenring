#!/usr/bin/env python3
"""Exercise the production equipment speed adapter with real combat timing data.

Always checks missing and partial runtime data; also checks the local compiled
table when present and an optional runtime file. Does not render animations.
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


PREAMBLE = r'''
#include <components/eldencombat/combat.hpp>
#include <components/eldencombat/timingdata.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
'''

TESTS = r'''
using namespace EldenCombat;
int checks = 0;
void check(bool value, const char* message)
{
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
Config equipment(WeaponClass right = WeaponClass::Dagger, WeaponClass left = WeaponClass::StraightSword)
{
    Config config;
    config.mWeaponClass = right;
    config.mLeftWeaponClass = left;
    config.mHasLeftWeapon = true;
    config.mSpeedMultiplier = 2.5f;
    config.mLeftSpeedMultiplier = 1.5f;
    return config;
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
void sparseRuntime()
{
    // Authored fixture: only the one-handed dagger has local timing data.
    std::string bytes = "ERAT";
    integer(bytes, 1);
    integer(bytes, 1);
    for (const auto value : { static_cast<unsigned int>(Action::Light1),
             static_cast<unsigned int>(WeaponClass::Dagger), 0u, 0xffffffffu, 0u, 0u })
        integer(bytes, value);
    bytes += "a000_000001";
    for (const float value : { 60.f, 10.f, 11.f, 20.f, 21.f, 22.f, 23.f, 24.f, 25.f,
             9999.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f })
        number(bytes, value);
    integer(bytes, 0); // No contacts or root samples are needed for this fixture.
    integer(bytes, 0);
    bytes.push_back(0);
    std::istringstream stream(bytes);
    setRuntimeTimings(std::make_shared<TimingData>(stream));
    auto config = equipment();
    configureWeaponSpeed(config, false);
    check(config.mSpeedMultiplier == 1.f, "imported dagger retained native acceleration");
    check(config.mLeftSpeedMultiplier == 1.5f, "missing off-hand data lost native speed");
    config = equipment();
    configureWeaponSpeed(config, true);
    check(config.mSpeedMultiplier == 2.5f, "missing two-handed moveset lost native speed");
    config = equipment(WeaponClass::StraightSword, WeaponClass::Dagger);
    configureWeaponSpeed(config, false);
    check(config.mSpeedMultiplier == 2.5f, "missing primary moveset lost native speed");
    check(config.mLeftSpeedMultiplier == 1.f, "imported off-hand dagger retained native acceleration");
    setRuntimeTimings({});
}
void sameClock(const ActionDef& actual, const ActionDef& expected)
{
    check(actual.mAnimation == expected.mAnimation, "changed selected attack clip");
    check(actual.mAnimationSpeed == expected.mAnimationSpeed, "changed authored pose rate");
    check(actual.mRootMotionSpeed == expected.mRootMotionSpeed, "changed authored root clock");
    for (const auto field : TimingData::scalarFields())
        check(actual.*field == expected.*field, "changed authored action timing or cost");
    check(actual.mHitCount == expected.mHitCount, "changed contact count");
    for (std::size_t i = 0; i < actual.mHitCount; ++i)
        check(actual.mHits[i].mFrom == expected.mHits[i].mFrom
                && actual.mHits[i].mTo == expected.mHits[i].mTo,
            "contact clock no longer matches source poses");
    check(actual.mRootMotion == expected.mRootMotion, "changed source displacement");
    if (actual.mLanding)
        sameClock(*actual.mLanding, *expected.mLanding);
    if (actual.mLandingShort)
        sameClock(*actual.mLandingShort, *expected.mLandingShort);
}
void sourceSpeeds()
{
    // Short blades, ordinary swords and slower heavy weapons must all use their
    // own source timing. Native per-item speed is for the native animation set.
    for (const auto weapon : { WeaponClass::Dagger, WeaponClass::StraightSword, WeaponClass::Greatsword })
        for (const bool twoHanded : { false, true })
            for (const float nativeSpeed : { 0.75f, 1.f, 1.5f, 2.5f })
            {
                check(hasImportedWeaponMoveset(weapon, twoHanded), "missing actual-bank test moveset");
                auto config = equipment(weapon, WeaponClass::Dagger);
                config.mSpeedMultiplier = nativeSpeed;
                configureWeaponSpeed(config, twoHanded);
                check(config.mSpeedMultiplier == 1.f, "native item speed accelerated imported attacks");
                check(config.mLeftSpeedMultiplier == 1.f, "native item speed accelerated off-hand attacks");
                Config expected = config;
                expected.mSpeedMultiplier = expected.mLeftSpeedMultiplier = 1.f;
                for (const auto action : { Action::Light1, Action::Heavy1, Action::Heavy1Charge,
                         Action::JumpLight, Action::JumpHeavy, Action::LeftLight1, Action::PairedLight1 })
                    sameClock(Controller::actionDefinition(action, config, twoHanded),
                        Controller::actionDefinition(action, expected, twoHanded));
                // Selected left-hand two-handed armaments are configured as
                // the primary class, so they obey the same source-rate policy.
                config = equipment(WeaponClass::Dagger, WeaponClass::Dagger);
                config.mSpeedMultiplier = config.mLeftSpeedMultiplier;
                configureWeaponSpeed(config, true);
                check(config.mSpeedMultiplier == 1.f, "left two-handed grip retained native acceleration");
            }
}
int main(int argc, char** argv)
{
    try
    {
#ifndef OPENMW_ELDENCOMBAT_IMPORTED_TIMINGS
        auto config = equipment();
        configureWeaponSpeed(config, false);
        check(config.mSpeedMultiplier == 2.5f && config.mLeftSpeedMultiplier == 1.5f,
            "missing timing data changed native fallback speed");
#else
        sourceSpeeds();
#endif
        sparseRuntime();
        if (argc > 1)
        {
            std::ifstream stream(argv[1], std::ios::binary);
            setRuntimeTimings(std::make_shared<TimingData>(stream));
            sourceSpeeds();
        }
        std::cout << checks << " weapon-speed checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, help="existing local action_timings.bin")
    args = parser.parse_args()
    compiler = os.environ.get("CXX") or shutil.which("c++")
    if not compiler:
        raise RuntimeError("No C++ compiler; source CI/eldenring-build-env.sh first")
    source = (ROOT / "apps/openmw/mwmechanics/eldencombat.cpp").read_text()
    adapter = block(source, "void configureWeaponSpeed(")
    config = block(source, "EldenCombat::Config eldenCombatConfig(")
    assert "configureWeaponSpeed(config," in config, "production config omitted source-rate policy"
    with tempfile.TemporaryDirectory(prefix="elden-weapon-speed-") as directory:
        path = Path(directory)
        harness = path / "speed.cpp"
        harness.write_text(PREAMBLE + adapter + TESTS)
        for imported in (False, True):
            if imported and not (ROOT / "components/eldencombat/imported_timings.hpp").is_file():
                continue
            binary = path / ("speed-imported" if imported else "speed-fallback")
            command = [compiler, "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT)]
            if imported:
                command.append("-DOPENMW_ELDENCOMBAT_IMPORTED_TIMINGS")
            command += [str(harness), str(ROOT / "components/eldencombat/combat.cpp"), "-o", str(binary)]
            subprocess.run(command, check=True)
            subprocess.run([str(binary)] + ([str(args.runtime.resolve())] if args.runtime else []), check=True)


if __name__ == "__main__":
    main()
