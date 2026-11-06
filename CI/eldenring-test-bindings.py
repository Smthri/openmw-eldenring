#!/usr/bin/env python3
"""Exercise production combat bindings with real ICS events and XML persistence.

Requires C++20, SDL2 headers/library, and the vendored ICS/TinyXML sources.
Engine services are stubbed; this is not a rendered Controls-menu test.
"""
import argparse
import ctypes.util
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-root', type=Path, default=ROOT,
                    help='source tree to validate (including the mwinput headers)')
args = parser.parse_args()
source = (args.source_root / 'apps/openmw/mwinput/bindingsmanager.cpp').read_text()


def function(signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


fixture = r'''
#include <array>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <oics/ICSInputControlSystem.h>
#include <oics/ICSChannelListener.h>
#include "apps/openmw/mwinput/eldeninput.hpp"
namespace Settings {
struct Game { bool mEldenRingCombat = true; };
Game& game() { static Game value; return value; }
}
namespace Files {
std::string pathToUnicodeString(const std::filesystem::path& value) { return value.string(); }
}
namespace MWInput {
static const int sFakeDeviceId = 1;
class BindingsManager;
class BindingsListener : public ICS::ChannelListener, public ICS::DetectingBindingListener {
public:
    BindingsListener(ICS::InputControlSystem*, BindingsManager*) {}
    void channelChanged(ICS::Channel*, float, float) override {}
};
class InputControlSystem : public ICS::InputControlSystem {
public:
    InputControlSystem(const std::filesystem::path& path)
        : ICS::InputControlSystem(path.string(), true, nullptr, nullptr, A_Last) {}
};
class BindingsManager {
public:
    BindingsManager(const std::filesystem::path&, bool);
    void loadKeyDefaults(bool force = false);
    void loadControllerDefaults(bool force = false);
    void keyPressed(const SDL_KeyboardEvent&);
    void keyReleased(const SDL_KeyboardEvent&);
    void mousePressed(const SDL_MouseButtonEvent&, Uint8);
    void mouseReleased(const SDL_MouseButtonEvent&, Uint8);
    void mouseWheelMoved(const SDL_MouseWheelEvent&);
    void controllerButtonPressed(int, const SDL_ControllerButtonEvent&);
    void controllerButtonReleased(int, const SDL_ControllerButtonEvent&);
    void controllerAxisMoved(int, const SDL_ControllerAxisEvent&);
    SDL_Scancode getKeyBinding(int);
    bool isDetectingBindingState() const;
    bool eldenInputEnabled() const { return Settings::game().mEldenRingCombat; }
    bool captureEldenAction(int action, bool down, EldenInput::Source device) {
        mEldenInput.set(action, down, true, device);
        return true;
    }
    std::filesystem::path mUserFile;
    bool mDragDrop;
    std::unique_ptr<InputControlSystem> mInputBinder;
    std::unique_ptr<BindingsListener> mListener;
    std::array<bool, A_Last> mLoadedControls{};
    std::array<bool, A_Last> mCapturedActions{};
    EldenInput mEldenInput;
};
'''
for signature in [
        'void clearAllKeyBindings(', 'void clearAllControllerBindings(',
        'BindingsManager::BindingsManager(',
        'void BindingsManager::loadKeyDefaults(', 'void BindingsManager::loadControllerDefaults(',
        'void BindingsManager::keyPressed(', 'void BindingsManager::keyReleased(',
        'void BindingsManager::mousePressed(', 'void BindingsManager::mouseReleased(',
        'void BindingsManager::mouseWheelMoved(',
        'void BindingsManager::controllerButtonPressed(', 'void BindingsManager::controllerButtonReleased(',
        'void BindingsManager::controllerAxisMoved(',
        'SDL_Scancode BindingsManager::getKeyBinding(', 'bool BindingsManager::isDetectingBindingState() const',
]:
    fixture += '\n' + function(signature) + '\n'
fixture += r'''
}
using namespace MWInput;
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
ICS::Control* control(BindingsManager& manager, int action) {
    return manager.mInputBinder->getChannel(action)->getAttachedControls().front().control;
}
void key(BindingsManager& manager, SDL_Scancode code, bool down) {
    SDL_KeyboardEvent event{};
    event.keysym.scancode = code;
    if (down) manager.keyPressed(event); else manager.keyReleased(event);
}
void bindKey(BindingsManager& manager, int action, SDL_Scancode code) {
    clearAllKeyBindings(manager.mInputBinder.get(), control(manager, action));
    manager.mInputBinder->addKeyBinding(control(manager, action), code, ICS::Control::INCREASE);
}
int main(int argc, char** argv) { try {
    if (argc != 2) throw std::runtime_error("expected temporary XML path");
    const std::filesystem::path file(argv[1]);
    {
        BindingsManager fresh(file, false);
        check(fresh.getKeyBinding(A_Dodge) == SDL_SCANCODE_SPACE, "fresh dodge default missing");
        check(fresh.getKeyBinding(A_HeavyModifier) == SDL_SCANCODE_LSHIFT, "fresh heavy modifier missing");
        check(fresh.mInputBinder->getJoystickAxisBinding(control(fresh, A_HeavyAttack), 1,
                  ICS::Control::INCREASE) == SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
              "fresh controller defaults missing after keyboard defaults created controls");
        // Right Shift must be independent of the configured left Shift. A hidden
        // alias would turn this custom dodge + light chord into a heavy attack.
        bindKey(fresh, A_Dodge, SDL_SCANCODE_RSHIFT);
        key(fresh, SDL_SCANCODE_RSHIFT, true);
        fresh.mousePressed({}, SDL_BUTTON_LEFT);
        check(fresh.mEldenInput.consume(A_Dodge).mPressed, "custom right Shift dodge ignored");
        check(fresh.mEldenInput.consume(A_Use).mPressed, "right Shift still secretly modifies attacks");
        check(!fresh.mEldenInput.consume(A_HeavyAttack).mPressed, "right Shift also started heavy attack");
        fresh.mouseReleased({}, SDL_BUTTON_LEFT);
        key(fresh, SDL_SCANCODE_RSHIFT, false);
        fresh.mEldenInput.clear();
        bindKey(fresh, A_HeavyModifier, SDL_SCANCODE_P);
        key(fresh, SDL_SCANCODE_P, true);
        fresh.mousePressed({}, SDL_BUTTON_LEFT);
        check(fresh.mEldenInput.consume(A_HeavyAttack).mPressed, "custom heavy modifier ignored");
        key(fresh, SDL_SCANCODE_P, false);
        check(fresh.mEldenInput.consume(A_HeavyAttack).mHeld, "custom modifier release cancelled charge");
        fresh.mouseReleased({}, SDL_BUTTON_LEFT);
        check(fresh.mEldenInput.consume(A_HeavyAttack).mReleased, "custom chord did not release charge");
        fresh.mEldenInput.clear();
        key(fresh, SDL_SCANCODE_LSHIFT, true);
        fresh.mousePressed({}, SDL_BUTTON_LEFT);
        check(fresh.mEldenInput.consume(A_Use).mPressed, "old modifier survived rebinding");
        fresh.mouseReleased({}, SDL_BUTTON_LEFT);
        key(fresh, SDL_SCANCODE_LSHIFT, false);
        fresh.mEldenInput.clear();
        // Bind the heavy modifier to an ordinary controller button and heavy
        // attack to the other trigger. Check both paths route configured actions.
        clearAllControllerBindings(fresh.mInputBinder.get(), control(fresh, A_HeavyModifier));
        fresh.mInputBinder->addJoystickButtonBinding(control(fresh, A_HeavyModifier), 1,
            SDL_CONTROLLER_BUTTON_LEFTSTICK, ICS::Control::INCREASE);
        SDL_ControllerButtonEvent button{};
        button.button = SDL_CONTROLLER_BUTTON_LEFTSTICK;
        fresh.controllerButtonPressed(1, button);
        button.button = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
        fresh.controllerButtonPressed(1, button);
        check(fresh.mEldenInput.consume(A_HeavyAttack).mPressed, "controller modifier ignored");
        fresh.controllerButtonReleased(1, button);
        button.button = SDL_CONTROLLER_BUTTON_LEFTSTICK;
        fresh.controllerButtonReleased(1, button);
        fresh.mEldenInput.clear();
        clearAllControllerBindings(fresh.mInputBinder.get(), control(fresh, A_HeavyAttack));
        fresh.mInputBinder->addJoystickAxisBinding(control(fresh, A_HeavyAttack), 1,
            SDL_CONTROLLER_AXIS_TRIGGERLEFT, ICS::Control::INCREASE);
        SDL_ControllerAxisEvent axis{};
        axis.axis = SDL_CONTROLLER_AXIS_TRIGGERLEFT;
        axis.value = 32767;
        fresh.controllerAxisMoved(1, axis);
        check(fresh.mEldenInput.consume(A_HeavyAttack).mPressed, "remapped trigger ignored");
        axis.value = 0;
        fresh.controllerAxisMoved(1, axis);
        check(fresh.mEldenInput.consume(A_HeavyAttack).mReleased, "remapped trigger release ignored");
        // An intentional unbound action must survive a restart, independently
        // for keyboard/mouse and controller devices.
        clearAllKeyBindings(fresh.mInputBinder.get(), control(fresh, A_Block));
        clearAllControllerBindings(fresh.mInputBinder.get(), control(fresh, A_Dodge));
        check(fresh.mInputBinder->save(file.string()), "real ICS XML save failed");
    }
    {
        BindingsManager restored(file, true);
        check(restored.getKeyBinding(A_Dodge) == SDL_SCANCODE_RSHIFT, "custom keyboard binding lost on reload");
        check(restored.getKeyBinding(A_HeavyModifier) == SDL_SCANCODE_P, "custom modifier lost on reload");
        check(restored.mInputBinder->getMouseButtonBinding(control(restored, A_Block),
                  ICS::Control::INCREASE) == ICS_MAX_DEVICE_BUTTONS, "unbound mouse restored to default");
        check(restored.mInputBinder->getJoystickButtonBinding(control(restored, A_Dodge), 1,
                  ICS::Control::INCREASE) == ICS_MAX_DEVICE_BUTTONS, "unbound controller restored to default");
        check(restored.mInputBinder->getJoystickAxisBinding(control(restored, A_HeavyAttack), 1,
                  ICS::Control::INCREASE) == SDL_CONTROLLER_AXIS_TRIGGERLEFT, "custom trigger lost on reload");
        check(restored.mInputBinder->getJoystickButtonBinding(control(restored, A_HeavyModifier), 1,
                  ICS::Control::INCREASE) == SDL_CONTROLLER_BUTTON_LEFTSTICK, "controller modifier lost on reload");
        restored.loadKeyDefaults(true);
        check(restored.getKeyBinding(A_Dodge) == SDL_SCANCODE_SPACE, "explicit keyboard reset did not restore default");
        check(restored.mInputBinder->getJoystickAxisBinding(control(restored, A_HeavyAttack), 1,
                  ICS::Control::INCREASE) == SDL_CONTROLLER_AXIS_TRIGGERLEFT, "keyboard reset changed controller");
        restored.loadControllerDefaults(true);
        check(restored.mInputBinder->getJoystickAxisBinding(control(restored, A_HeavyAttack), 1,
                  ICS::Control::INCREASE) == SDL_CONTROLLER_AXIS_TRIGGERRIGHT, "explicit controller reset failed");
    }
    // Old profiles have no control record for actions introduced later. Keep
    // old custom controls and populate new defaults for both device families.
    {
        TiXmlDocument xml(file.string().c_str());
        check(xml.LoadFile(), "could not load XML migration fixture");
        auto* root = xml.RootElement();
        for (auto* node = root->FirstChildElement("Control"); node;) {
            auto* next = node->NextSiblingElement("Control");
            if (std::stoi(node->Attribute("name")) >= A_HeavyAttack) root->RemoveChild(node);
            node = next;
        }
        check(xml.SaveFile(), "could not save XML migration fixture");
        BindingsManager old(file, true);
        check(old.getKeyBinding(A_HeavyModifier) == SDL_SCANCODE_LSHIFT, "missing new action did not acquire keyboard default");
        check(old.mInputBinder->getJoystickAxisBinding(control(old, A_HeavyAttack), 1,
                  ICS::Control::INCREASE) == SDL_CONTROLLER_AXIS_TRIGGERRIGHT, "missing new action did not acquire controller default");
        Settings::game().mEldenRingCombat = false;
        BindingsManager native({}, false);
        check(native.getKeyBinding(A_Activate) == SDL_SCANCODE_SPACE, "native activate default changed");
        check(native.getKeyBinding(A_Jump) == SDL_SCANCODE_E, "native jump default changed");
        check(native.mInputBinder->getJoystickAxisBinding(control(native, A_Use), 1,
                  ICS::Control::INCREASE) == SDL_CONTROLLER_AXIS_TRIGGERRIGHT, "native controller default changed");
    }
    std::cout << "Production bindings passed: " << checks
              << " checks; real ICS events, XML save/reload, migration and resets\n";
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
'''
fixture = fixture.replace('"apps/openmw/mwinput/eldeninput.hpp"',
                          '"' + str(args.source_root.resolve() / 'apps/openmw/mwinput/eldeninput.hpp') + '"')

if shutil.which('pkg-config') and subprocess.run(
        ['pkg-config', '--exists', 'sdl2'], check=False).returncode == 0:
    sdl_flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'sdl2'], text=True))
else:
    headers = ROOT / 'build-tools-local/engine-headers/sdl'
    library = ctypes.util.find_library('SDL2-2.0')
    if not headers.is_dir() or not library:
        raise SystemExit('SDL2 development files required (pkg-config sdl2 or local headers + system library)')
    # The local compiler uses its own sysroot, so resolve the host's runtime
    # library explicitly instead of assuming its default linker search paths.
    matches = [path for base in (Path('/usr/lib'), Path('/lib'))
               for pattern in (library, '*/' + library) for path in base.glob(pattern)]
    if not matches:
        raise SystemExit('Could not resolve the host SDL2 runtime library')
    sdl_flags = ['-I', str(headers), str(matches[0])]

with tempfile.TemporaryDirectory(prefix='elden-bindings-') as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(fixture)
    sources = sorted((ROOT / 'extern/oics').glob('*.cpp'))
    sources += sorted((ROOT / 'extern/oics/tinyxml').glob('*.cpp'))
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O0', '-g0', '-I', str(ROOT)]
    for include in ('extern/oics/include', 'extern/oics/include/oics', 'extern/oics/tinyxml'):
        command += ['-I', str(ROOT / include)]
    command += [str(path / 'test.cpp'), *map(str, sources), *sdl_flags, '-o', str(path / 'test')]
    subprocess.run(command, check=True)
    subprocess.run([str(path / 'test'), str(path / 'input_v3.xml')], check=True)
