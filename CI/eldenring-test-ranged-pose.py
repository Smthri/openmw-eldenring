#!/usr/bin/env python3
"""Compile the production cast/throw pose selection with native input stubs."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'apps/openmw/mwmechanics/character.cpp').read_text()
functions = []
for name, marker in [('throwing', 'const bool throwing ='),
                     ('casting', 'const auto& spell = mEldenSpellAnimation;')]:
    start = source.index('const bool upperBodyOnly =', source.index(marker))
    end = source.index(';', start) + 1
    functions.append(f'bool {name}(int mPtr, Input input, Movement movement) {{'
                     + source[start:end] + ' return upperBodyOnly; }')
fixture = r'''
#include <cmath>
#include <iostream>
#include <stdexcept>
struct Input { bool mFlying = false, mCrouching = false, mGrounded = true; };
struct Movement { float speed = 0; float x() const { return speed; } float y() const { return 0; } };
int getPlayer() { return 1; }
namespace MWBase {
struct World { bool firstPerson = false; bool isFirstPerson() const { return firstPerson; } };
struct Environment { World world; static Environment& get() { static Environment e; return e; }
World* getWorld() { return &world; } };
}
'''
fixture += '\n'.join(functions)
fixture += r'''
void check(bool value, const char* error) { if (!value) throw std::runtime_error(error); }
int main() { try {
    for (auto select : {throwing, casting}) {
        // AI footwork pauses at windup and resumes during aim/release. Even
        // sub-threshold movement must not change lower-body ownership.
        for (float speed : {0.f, .0001f, .002f, 30.f, .0001f, 0.f})
            check(select(2, {}, {speed}), "NPC gesture switched away from native lower-body pose");
        check(!select(1, {}, {}), "stationary third-person player lost full-body gesture");
        check(select(1, {}, {1.f}), "moving player lost native footwork");
        check(select(1, {true, false, false}, {}), "flight lost upper-body gesture");
        check(select(1, {false, true, true}, {}), "crouch lost native lower body");
        check(select(1, {false, false, false}, {}), "airborne cast replaced legs");
        MWBase::Environment::get().world.firstPerson = true;
        check(select(1, {}, {}), "first-person gesture replaced lower body");
        MWBase::Environment::get().world.firstPerson = false;
    }
    std::cout << "Production ranged pose selection passed: NPC stops/starts, player full-body and native leg fallbacks\n";
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
'''
with tempfile.TemporaryDirectory(prefix='elden-ranged-pose-') as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(fixture)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', str(path / 'test.cpp'),
                    '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
