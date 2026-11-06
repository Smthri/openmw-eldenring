#!/usr/bin/env python3
"""Check native orbit facing through production yaw selection and OSG callbacks.

Uses the pinned local OSG build used by run-renderer-validation.py. No graphics
context or game data is needed; this verifies CPU skeleton transforms only.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--animation-source', type=Path,
                    default=ROOT / 'apps/openmw/mwrender/animation.cpp')
args = parser.parse_args()
source = args.animation_source.read_text()
start = source.index('        const float epsilon = 0.001f;', source.index('osg::Vec3f Animation::runAnimation('))
end = source.index('        return movement;', start) + len('        return movement;')
selection = source[start:end]
fixture = r'''
#include "apps/openmw/mwrender/rotatecontroller.hpp"
#include <osg/Group>
#include <osg/MatrixTransform>
#include <osg/NodeVisitor>
#include <array>
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>

int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
bool close(const osg::Vec3f& a, const osg::Vec3f& b) { return (a-b).length() < .0001f; }
bool close(const osg::Matrix& a, const osg::Matrix& b) {
    for (int row=0; row<4; ++row)
        for (int col=0; col<4; ++col)
            if (std::abs(a(row,col)-b(row,col)) > .0001) return false;
    return true;
}
class NoTraversal : public osg::NodeVisitor {
public:
    void apply(osg::Node&) override {}
};
struct Rig {
    osg::ref_ptr<osg::Group> object = new osg::Group;
    std::array<osg::ref_ptr<osg::MatrixTransform>, 3> nodes{
        new osg::MatrixTransform, new osg::MatrixTransform, new osg::MatrixTransform};
    std::array<osg::Matrix, 3> authored;
    osg::ref_ptr<MWRender::RotateController> mRootController = new MWRender::RotateController(object);
    osg::ref_ptr<MWRender::RotateController> mSpineController = new MWRender::RotateController(object);
    osg::ref_ptr<MWRender::RotateController> mHeadController = new MWRender::RotateController(object);
    std::optional<float> mEldenNativeYaw;
    float mLegsYawRadians = .7f;
    float mUpperBodyYawRadians = .35f;
    float mHeadYawRadians = -.2f;
    float mBodyPitchRadians = 0.f;
    float mHeadPitchRadians = 0.f;
    Rig() {
        object->addChild(nodes[0]);
        nodes[0]->addChild(nodes[1]);
        nodes[1]->addChild(nodes[2]);
        for (int i=0; i<3; ++i) {
            authored[i] = osg::Matrix::rotate(osg::Quat(.13f*(i+1), osg::Vec3f(1,0,0))
                * osg::Quat(-.08f*(i+1), osg::Vec3f(0,1,0)));
            authored[i].setTrans(i == 0 ? osg::Vec3f(0,0,2) : osg::Vec3f(.1f*i,.2f*i,1.f));
        }
    }
    osg::Vec3f configure(osg::Vec3f movement) {
SELECTION
    }
    osg::Vec3f update(osg::Vec3f movement) {
        // A native keyframe track restores its authored locals each update.
        for (int i=0; i<3; ++i) nodes[i]->setMatrix(authored[i]);
        movement = configure(movement);
        NoTraversal visitor;
        if (mRootController) (*mRootController)(nodes[0], &visitor);
        if (mSpineController) (*mSpineController)(nodes[1], &visitor);
        if (mHeadController) (*mHeadController)(nodes[2], &visitor);
        return movement;
    }
    std::array<osg::Matrix,3> worlds() const {
        return {nodes[0]->getMatrix(), nodes[1]->getMatrix()*nodes[0]->getMatrix(),
            nodes[2]->getMatrix()*nodes[1]->getMatrix()*nodes[0]->getMatrix()};
    }
};
int main() { try {
    constexpr float pi = 3.14159265358979323846f;
    const osg::Vec3f velocity(.4f,-.7f,.2f);
    for (float legs : {0.f, .7f, -1.5f}) {
        Rig rig;
        rig.mLegsYawRadians = legs;
        rig.mUpperBodyYawRadians = legs*.5f;
        const auto originalMovement = rig.update(velocity);
        const auto originalWorlds = rig.worlds();
        const auto storedHeadYaw = rig.mHeadYawRadians;
        const auto storedUpperYaw = rig.mUpperBodyYawRadians;
        std::array<osg::Matrix,3> nativeWorlds{rig.authored[0], rig.authored[1]*rig.authored[0],
            rig.authored[2]*rig.authored[1]*rig.authored[0]};
        for (int cycle=0; cycle<3; ++cycle) {
            for (float yaw : {0.f, pi/2, -pi/2, pi-.001f, -pi+.001f, 3*pi, -3*pi}) {
                rig.mEldenNativeYaw = yaw;
                const auto movement = rig.update(velocity);
                check(close(movement, originalMovement), "visual orbit yaw changed native displacement");
                check(close(movement, osg::Quat(legs, osg::Vec3f(0,0,1))*velocity),
                    "zero visible yaw lost the native leg-frame displacement");
                const auto worlds = rig.worlds();
                for (int joint=0; joint<3; ++joint)
                    check(close(worlds[joint], nativeWorlds[joint]*osg::Matrix::rotate(yaw, osg::Vec3f(0,0,1))),
                        "orbit yaw twisted native root/spine/head instead of rotating the body together");
                check(close(rig.nodes[1]->getMatrix(), rig.authored[1]), "orbit changed authored spine articulation");
                check(close(rig.nodes[2]->getMatrix(), rig.authored[2]), "orbit changed authored head articulation");
                check(rig.mLegsYawRadians == legs && rig.mUpperBodyYawRadians == storedUpperYaw
                        && rig.mHeadYawRadians == storedHeadYaw,
                    "visual override mutated movement/head tracking state");
                // Action, draw/sheath, first-person, lock-on or scripted/native
                // fallbacks clear the override. Existing native posing resumes.
                rig.mEldenNativeYaw.reset();
                check(close(rig.update(velocity), originalMovement), "clearing override changed displacement");
                const auto resumed = rig.worlds();
                for (int joint=0; joint<3; ++joint)
                    check(close(resumed[joint], originalWorlds[joint]), "clearing orbit left a stale native rotation");
            }
        }
    }
    for (bool missingSpine : {false, true}) {
        Rig rig;
        rig.mRootController = nullptr;
        if (missingSpine) rig.mSpineController = nullptr;
        const auto movement = rig.update(velocity);
        const auto original = rig.worlds();
        for (float yaw : {0.f, pi/2, -pi}) {
            rig.mEldenNativeYaw = yaw;
            check(close(rig.update(velocity), movement), "incomplete rig changed native displacement");
            const auto posed = rig.worlds();
            for (int joint=0; joint<3; ++joint)
                check(close(posed[joint], original[joint]), "incomplete rig twisted torso without root support");
        }
    }
    std::cout << "Native facing production OSG callbacks passed: " << checks
        << " checks; whole-body orbit, authored articulation, displacement, reset and incomplete rigs\n";
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
'''.replace('SELECTION', selection)

tools = ROOT / 'build-tools-local'
osg_source = tools / 'osg-9b384d5439977784e5cd250ff8ff7966928ddd27'
osg_build = tools / 'osg-renderer-validation-build'
if not (osg_build / 'lib/libosg.a').exists():
    raise SystemExit('Pinned local OSG build required; see build-tools-local/run-renderer-validation.py')
with tempfile.TemporaryDirectory(prefix='elden-native-facing-') as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(fixture)
    command = [str(tools / 'bin/c++'), '-std=c++20', '-O1', '-g0', '-ffunction-sections', '-fdata-sections',
               '-DOSG_LIBRARY_STATIC', '-DOPENTHREADS_LIBRARY_STATIC', '-DOSGANIMATION_LIBRARY_STATIC',
               '-I', str(ROOT)]
    for include in (osg_build / 'include', osg_source / 'include', tools / 'osg-renderer-validation-gl'):
        command += ['-isystem', str(include)]
    command += [str(path / 'test.cpp'), str(ROOT / 'apps/openmw/mwrender/rotatecontroller.cpp'),
                str(osg_source / 'src/osgAnimation/Bone.cpp'), str(osg_build / 'lib/libosg.a'),
                str(osg_build / 'lib/libOpenThreads.a'), '/usr/lib/x86_64-linux-gnu/libGL.so.1',
                '-ldl', '-lpthread', '-Wl,--allow-shlib-undefined', '-Wl,--gc-sections', '-o', str(path / 'test')]
    subprocess.run(command, check=True)
    subprocess.run([str(path / 'test')], check=True)
