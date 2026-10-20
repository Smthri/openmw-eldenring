// Exercise the production retarget callbacks on a native-style NIF hierarchy.
// No game assets are required. Optionally pass player_anims.bin to also check
// the extracted spear heavy attacks and sword roll/jump recovery blends.
#include <apps/openmw/mwrender/eldenanimation.hpp>
#include <apps/openmw/mwrender/eldenanatomy.hpp>
#include <components/eldencombat/animations.hpp>
#include <components/nifosg/matrixtransform.hpp>
#include <components/sceneutil/nodecallback.hpp>

#include <osg/NodeVisitor>
#include <osg/Geometry>
#include <osg/Geode>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace
{
    int checks = 0;
    void check(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
        ++checks;
    }

    bool close(const osg::Vec3f& a, const osg::Vec3f& b)
    {
        return (a - b).length() < 2e-4f;
    }

    bool close(const osg::Quat& a, const osg::Quat& b)
    {
        return close(a * osg::Vec3f(1, 0, 0), b * osg::Vec3f(1, 0, 0))
            && close(a * osg::Vec3f(0, 1, 0), b * osg::Vec3f(0, 1, 0));
    }

    void u32(std::string& out, std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            out.push_back(static_cast<char>(value >> shift));
    }

    void f32(std::string& out, float value)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        u32(out, bits);
    }

    void name(std::string& out, std::string_view value)
    {
        out.push_back(static_cast<char>(value.size()));
        out += value;
    }

    constexpr std::array<std::string_view, 38> joints = {
        "Pelvis", "Spine", "Spine1", "Spine2", "Neck", "Head",
        "L_Clavicle", "L_UpperArm", "L_Forearm", "L_Hand",
        "R_Clavicle", "R_UpperArm", "R_Forearm", "R_Hand",
        "L_Thigh", "L_Calf", "L_Foot", "L_Toe0", "R_Thigh", "R_Calf", "R_Foot", "R_Toe0",
        "R_Weapon", "L_Weapon",
        "L_Finger0", "L_Finger01", "L_Finger02", "L_Finger1", "L_Finger2", "L_Finger21", "L_Finger22",
        "R_Finger0", "R_Finger01", "R_Finger02", "R_Finger1", "R_Finger2", "R_Finger21", "R_Finger22",
    };

    std::shared_ptr<const EldenCombat::AnimationBank> fixtureBank(
        bool weaponAxes = true, bool animatedBody = false, bool windingTorso = false, bool windingArm = false)
    {
        // Express the fixture in OpenMW's coordinates, then encode ERAN's basis.
        std::array<osg::Vec3f, 38> p = {
            osg::Vec3f(0, 0, .9), {0, 0, 1.1}, {0, 0, 1.2}, {0, 0, 1.4}, {0, 0, 1.5}, {0, 0, 1.7},
            {-.08, 0, 1.5}, {-.15, 0, 1.45}, {-.3, 0, 1.3}, {-.45, 0, 1.2},
            {.08, 0, 1.5}, {.15, 0, 1.45}, {.3, 0, 1.3}, {.45, 0, 1.2},
            {-.1, 0, .85}, {-.1, 0, .45}, {-.1, 0, .05}, {-.1, .1, .05},
            {.1, 0, .85}, {.1, 0, .45}, {.1, 0, .05}, {.1, .1, .05},
            {.45, .08, 1.2}, {-.45, .08, 1.2},
        };
        for (int side = 0; side != 2; ++side)
            for (int finger = 0; finger != 7; ++finger)
                p[24 + side * 7 + finger] = p[side == 0 ? 9 : 13]
                    + osg::Vec3f(.02f * (finger % 3 + 1), finger < 3 ? -.02f : .02f * (finger - 3), 0);
        std::string bytes = "ERAN";
        u32(bytes, joints.size());
        u32(bytes, weaponAxes ? 3 : 1);
        u32(bytes, 17);
        for (const auto joint : joints)
            name(bytes, joint);
        name(bytes, "Head");
        if (weaponAxes)
        {
            name(bytes, "R_Weapon");
            name(bytes, "L_Weapon");
        }
        const auto vector = [&](const osg::Vec3f& v) {
            f32(bytes, -v.x());
            f32(bytes, v.z());
            f32(bytes, v.y());
        };
        for (std::string_view clip : {
                 "a000_000000", "a012_000000", "a013_000000", "a036_032505", "a036_032500",
                 "a000_000100", "a010_000100", "a012_000100", "a013_000100", "a015_000100",
                 "a000_004200", "a012_020100", "a012_020200",
                 "a014_000000", "a044_036010", "a044_036020", "a044_036000" })
        {
            auto positions = p;
            const bool wind = windingArm && (clip == "a036_032500" || clip == "a036_032505");
            if (wind)
            {
                const osg::Vec3f shift(0, .12f, 0);
                positions[13] += shift;
                positions[22] += shift;
                for (std::size_t j = 31; j < positions.size(); ++j)
                    positions[j] += shift;
            }
            if (clip.ends_with("_000100"))
            {
                // A visible raised arm distinguishes the guard layers from
                // the generic impact clip, whose arms are deliberately low.
                positions[8] = { -.25f, .1f, 1.2f };
                positions[9] = { -.15f, .35f, 1.5f };
                if (clip != "a000_000100")
                {
                    positions[12] = { .25f, .1f, 1.2f };
                    positions[13] = { .15f, .35f, 1.5f };
                }
                for (int side = 0; side != 2; ++side)
                {
                    const int hand = side == 0 ? 9 : 13;
                    for (int finger = 0; finger != 7; ++finger)
                        positions[24 + side * 7 + finger] += positions[hand] - p[hand];
                    positions[side == 0 ? 23 : 22] += positions[hand] - p[hand];
                }
            }
            if (clip == "a012_000000" || clip == "a012_020100" || clip == "a012_020200")
            {
                positions[8] = { -.20f, .16f, 1.2f };
                positions[9] = { -.08f, .22f, 1.22f };
                positions[12] = { .14f, .12f, 1.23f };
                positions[13] = { .08f, .16f, 1.27f };
                for (int side = 0; side != 2; ++side)
                {
                    const int first = 24 + side * 7;
                    const osg::Vec3f hand = positions[side == 0 ? 9 : 13];
                    const float direction = side == 0 ? -1.f : 1.f;
                    for (int digit = 0; digit != 7; ++digit)
                        positions[first + digit] = hand
                            + osg::Vec3f(digit < 3 ? -.03f : (digit == 3 ? -.02f : 0.f),
                                .08f + .02f * (digit < 3 ? digit : std::max(digit - 4, 0)), 0) * direction;
                }
                positions[22] = positions[13] + osg::Vec3f(0, .08f, 0);
                positions[23] = positions[22] - osg::Vec3f(0, .196f, 0);
            }
            name(bytes, clip);
            f32(bytes, 6.f);
            const int frames = wind ? 181 : 2;
            u32(bytes, frames);
            for (int frame = 0; frame != frames; ++frame)
            {
                osg::Vec3f armAxis = positions[12] - positions[11];
                armAxis.normalize();
                const osg::Quat armRotation(wind ? frame * 3.14159265358979323846f / 180.f : 0.f, armAxis);
                const osg::Quat bodyRotation(animatedBody && frame == 1 ? .8f : 0.f, osg::Vec3f(0, 0, 1));
                const osg::Quat torsoRotation(
                    windingTorso && clip == "a036_032500" ? .4f : 0.f, osg::Vec3f(0, 0, 1));
                for (std::size_t j = 0; j != p.size(); ++j)
                {
                    // Shift the weapon pivot, while the hand itself stays still.
                    osg::Vec3f position = positions[j]
                        + (j == 22 && frame == 1 ? osg::Vec3f(0, .32f, 0) : osg::Vec3f());
                    if (j == 13 || j == 22 || j >= 31)
                        position = positions[12] + armRotation * (position - positions[12]);
                    if ((j >= 6 && j <= 13) || j >= 22)
                        position = torsoRotation * position;
                    vector(bodyRotation * position);
                }
                vector({0, 0, 1}); vector({1, 0, 0}); vector({0, 1, 0});
                if (weaponAxes)
                    for (int side = 0; side != 2; ++side)
                    {
                        if (side == 1 && (clip == "a000_000100" || clip == "a015_000100"))
                        {
                            vector({0, 0, 1});
                            vector({1, 0, 0});
                            vector({0, -1, 0});
                            continue;
                        }
                        if (side == 1 && (clip.starts_with("a014_") || clip.starts_with("a044_")))
                        {
                            vector(bodyRotation * osg::Vec3f(0, 1, 0));
                            vector(bodyRotation * osg::Vec3f(0, 0, -1));
                            vector(bodyRotation * osg::Vec3f(-1, 0, 0));
                            continue;
                        }
                        const bool turn = clip == "a036_032500";
                        vector(turn ? osg::Vec3f(0, 1, 0) : osg::Vec3f(-1, 0, 0));
                        vector(turn ? osg::Vec3f(1, 0, 0) : osg::Vec3f(0, 1, 0));
                        vector({0, 0, 1}); // Mirrored third axis must be rebuilt.
                    }
            }
        }
        std::istringstream stream(bytes);
        return std::make_shared<EldenCombat::AnimationBank>(stream);
    }

    struct NativeWeapon : SceneUtil::NodeCallback<NativeWeapon, NifOsg::MatrixTransform*>
    {
        osg::Quat mRotation = osg::Quat(1.57079632679, osg::Vec3f(1, 0, 0));
        osg::Vec3f mTranslation = {6.2637f, .7815f, 2.3277f};
        float mScale = 1.25f;
        bool mWriteRotation = true;
        void operator()(NifOsg::MatrixTransform* node, osg::NodeVisitor* visitor)
        {
            if (mWriteRotation)
                node->setRotation(mRotation);
            node->setTranslation(mTranslation);
            node->setScale(mScale);
            traverse(node, visitor);
        }
    };

    struct Update : osg::NodeVisitor
    {
        Update() : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN) { setVisitorType(UPDATE_VISITOR); }
        void apply(osg::Node& node) override
        {
            if (auto* callback = node.getUpdateCallback())
                callback->run(&node, this);
            else
                traverse(node);
        }
        void apply(osg::Group& node) override { apply(static_cast<osg::Node&>(node)); }
        void apply(osg::Transform& node) override { apply(static_cast<osg::Node&>(node)); }
        void apply(osg::MatrixTransform& node) override { apply(static_cast<osg::Node&>(node)); }
    };

    struct FirstPersonNeck : SceneUtil::NodeCallback<FirstPersonNeck, osg::MatrixTransform*>
    {
        osg::Node* mRoot = nullptr;
        osg::Quat mPitch;

        void operator()(osg::MatrixTransform* node, osg::NodeVisitor* visitor)
        {
            // Match the native RotateController's post-pose model-space
            // rotation. It writes the matrix, not NIF's decomposed rotation.
            osg::Matrix matrix = node->getMatrix();
            const osg::Quat world = osg::computeLocalToWorld(node->getParentalNodePaths(mRoot).front()).getRotate();
            matrix.setRotate(world * mPitch * world.inverse() * matrix.getRotate());
            node->setMatrix(matrix);
            traverse(node, visitor);
        }
    };

    osg::ref_ptr<osg::Geometry> spearGeometry(bool embedded = false, float halfLength = 50.f)
    {
        osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
        const osg::Quat basis(-1.57079632679, osg::Vec3f(1, 0, 0));
        for (unsigned i = 0; i < 8; ++i)
        {
            osg::Vec3f point(i & 1 ? 1.25f : -.75f, i & 2 ? halfLength : -halfLength, i & 4 ? 1.5f : -.5f);
            vertices->push_back(embedded ? basis.inverse() * point : point);
        }
        osg::ref_ptr<osg::DrawElementsUInt> indices = new osg::DrawElementsUInt(GL_TRIANGLES);
        for (const auto& face : { std::array<unsigned, 4>{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1},
                 {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3} })
            for (unsigned index : { face[0], face[1], face[2], face[0], face[2], face[3] })
                indices->push_back(index);
        osg::ref_ptr<osg::Geometry> result = new osg::Geometry;
        result->setVertexArray(vertices);
        result->addPrimitiveSet(indices);
        return result;
    }

    struct Rig
    {
        osg::ref_ptr<osg::Group> mRoot = new osg::Group;
        MWRender::Animation::NodeMap mNodes;
        osg::ref_ptr<NifOsg::MatrixTransform> mHand, mWeapon, mShield;
        osg::ref_ptr<NativeWeapon> mNative = new NativeWeapon;
        osg::ref_ptr<NativeWeapon> mNativeShield = new NativeWeapon;
        std::unique_ptr<MWRender::EldenAnimation> mAnimation;

        NifOsg::MatrixTransform* bone(std::string key, osg::Group* parent, osg::Vec3f translation)
        {
            osg::ref_ptr<NifOsg::MatrixTransform> node = new NifOsg::MatrixTransform(Nif::NiTransform::getIdentity());
            node->setName(key);
            node->setTranslation(translation);
            parent->addChild(node);
            mNodes.emplace(std::move(key), node);
            return node.get();
        }

        explicit Rig(std::shared_ptr<const EldenCombat::AnimationBank> bank, bool armHelper = false,
            bool stockFingers = false, bool requireFullBody = false, bool embeddedSpear = false)
        {
            auto* pelvis = bone("bip01 pelvis", mRoot.get(), {0, 0, 60});
            auto* spine = bone("bip01 spine", pelvis, {0, 0, 10});
            auto* neck = bone("bip01 neck", spine, {0, 0, 20});
            bone("bip01 head", neck, {0, 0, 10});
            for (const char* side : { "l", "r" })
            {
                const std::string prefix = std::string("bip01 ") + side;
                auto* thigh = bone(prefix + " thigh", pelvis, {side[0] == 'l' ? -6.f : 6.f, 0, -3});
                auto* calf = bone(prefix + " calf", thigh, {0, 0, -24});
                auto* foot = bone(prefix + " foot", calf, {0, 0, -24});
                bone(prefix + " toe0", foot, {0, 6, 0});
            }
            auto* armParent = neck;
            if (armHelper)
            {
                armParent = bone("arm helper", neck, {0, 0, 0});
                armParent->setRotation(osg::Quat(.3f, osg::Vec3f(0, 1, 0)));
            }
            auto* upper = bone("bip01 r upperarm", armParent, {10, 0, 0});
            auto* forearm = bone("bip01 r forearm", upper, {15, 0, 0});
            forearm->setScale(1.1f);
            mHand = bone("bip01 r hand", forearm, {15, 0, 0});
            if (stockFingers)
            {
                auto* leftUpper = bone("bip01 l upperarm", neck, {-10, 0, 0});
                auto* leftForearm = bone("bip01 l forearm", leftUpper, {-15, 0, 0});
                auto* leftHand = bone("bip01 l hand", leftForearm, {-15, 0, 0});
                mNativeShield->mTranslation = {-5, .3f, -1.3f};
                mShield = bone("shield bone", leftHand, mNativeShield->mTranslation);
                mShield->setRotation(mNativeShield->mRotation);
                for (const char* side : { "l", "r" })
                {
                    const std::string prefix = std::string("bip01 ") + side + " finger";
                    auto* hand = side[0] == 'l' ? leftHand : mHand.get();
                    // Stock hands have two joints per digit, with a separate
                    // index and grouped remaining fingers. The terminal
                    // phalanges are already bent in the native bind pose.
                    auto* thumb = bone(prefix + "0", hand, {3, -2, 0});
                    thumb->setRotation(osg::Quat(.45, osg::Vec3f(0, 0, 1)));
                    bone(prefix + "01", thumb, {2.49f, 0, 0})
                        ->setRotation(osg::Quat(-.454, osg::Vec3f(0, 0, 1)));
                    auto* pointer = bone(prefix + "1", hand, {4, -1, 0});
                    pointer->setRotation(osg::Quat(.2, osg::Vec3f(0, 0, 1)));
                    bone(prefix + "11", pointer, {2.90f, 0, 0})
                        ->setRotation(osg::Quat(1.126, osg::Vec3f(0, 0, 1)));
                    auto* middle = bone(prefix + "2", hand, {4, 1, 0});
                    bone(prefix + "21", middle, {2.90f, 0, 0})
                        ->setRotation(osg::Quat(.829, osg::Vec3f(0, 0, 1)));
                }
            }
            else
            {
                bone("bip01 r finger1", mHand.get(), {4, -1, 0});
                bone("bip01 r finger2", mHand.get(), {4, 1, 0});
            }
            mWeapon = bone(embeddedSpear ? "hircine_spear" : "weapon bone",
                embeddedSpear ? mRoot.get() : mHand.get(), mNative->mTranslation);
            mWeapon->setRotation(mNative->mRotation);
            if (embeddedSpear)
                mWeapon->addChild(spearGeometry(true));
            mAnimation = std::make_unique<MWRender::EldenAnimation>(
                mNodes, *mRoot, std::move(bank), requireFullBody, embeddedSpear);
            check(mAnimation->valid(), "fixture rig was rejected");
            // Model base_anim's permanent controller: restore must precede it.
            mWeapon->addUpdateCallback(mNative.get());
            if (mShield)
                mShield->addUpdateCallback(mNativeShield.get());
            for (const auto& [node, callback] : mAnimation->beforeNativeControllers())
            {
                callback->setNestedCallback(node->getUpdateCallback());
                node->setUpdateCallback(callback.get());
            }
            for (const auto& [node, callback] : mAnimation->controllers())
                node->addUpdateCallback(callback.get());
        }

        void update() { Update visitor; mRoot->accept(visitor); }
        osg::Quat modelRotation(osg::Node* node) const
        {
            osg::Vec3f translation, scale;
            osg::Quat rotation, scaleOrientation;
            osg::computeLocalToWorld(node->getParentalNodePaths(mRoot.get()).front()).decompose(
                translation, rotation, scale, scaleOrientation);
            return rotation;
        }
        osg::Matrixf modelMatrix(osg::Node* node) const
        {
            return osg::computeLocalToWorld(node->getParentalNodePaths(mRoot.get()).front());
        }
        osg::Vec3f tip() const { return modelRotation(mWeapon.get()) * osg::Vec3f(0, 1, 0); }
        void play(std::string_view clip, float frame = 0, float duration = 1, bool spear = true, float yaw = 0,
            std::string_view grip = "a013_000000")
        {
            check(mAnimation->play(clip, frame, false, duration, yaw, {}, {}, grip, spear), "clip rejected");
            update();
        }
    };

    void bowAttachments(std::shared_ptr<const EldenCombat::AnimationBank> bank)
    {
        const auto* clip = bank->find("a044_036010");
        check(clip, "bow draw fixture missing");
        const auto axes = bank->axesOffset("L_Weapon");
        check(axes.has_value(), "bow axes missing");
        for (bool reverse : { false, true })
            for (bool upperBody : { false, true })
            {
                Rig rig(bank, false, true);
                auto* left = rig.mNodes.at("bip01 l hand").get();
                const auto leftBind = rig.modelMatrix(left);
                const auto rightBind = rig.modelMatrix(rig.mHand);
                osg::Vec3f palm = osg::Matrixf::transform3x3(rig.mNative->mTranslation, rightBind);
                palm.x() *= -1.f;
                palm = osg::Matrixf::transform3x3(palm, osg::Matrixf::inverse(leftBind));
                auto* upper = rig.mNodes.at("bip01 r upperarm").get();
                if (reverse)
                {
                    auto* parent = upper->getParent(0);
                    osg::ref_ptr<osg::Node> keep = upper;
                    parent->removeChild(upper);
                    parent->addChild(keep);
                }
                // This child stands in for the bow's native animated nock:
                // no controller or transform on the equipped mesh is replaced.
                auto* nock = rig.bone("ArrowBone", rig.mWeapon, {0, 0, 0});
                osg::ref_ptr<osg::Geometry> arrow = new osg::Geometry;
                osg::ref_ptr<osg::Vec3Array> arrowVertices = new osg::Vec3Array;
                for (const auto& vertex : {
                    osg::Vec3f(-.2f, -8, -.2f), osg::Vec3f(.2f, -8, -.2f), osg::Vec3f(0, 2, 0),
                    osg::Vec3f(-.2f, -8, .2f), osg::Vec3f(.2f, -8, .2f), osg::Vec3f(0, 2, 0),
                })
                    arrowVertices->push_back(vertex);
                arrow->setVertexArray(arrowVertices);
                arrow->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, arrowVertices->size()));
                nock->addChild(arrow);
                osg::ref_ptr<NativeWeapon> nativeNock = new NativeWeapon;
                nativeNock->mScale = .8f;
                nock->addUpdateCallback(nativeNock);
                std::vector<float> sample;
                for (int tick = 0; tick != 16; ++tick)
                {
                    const float frame = float(tick % 8) / 7.f * float(clip->mFrames - 1);
                    const float pitch = -.65f + .1f * tick;
                    const float yaw = -.4f + .05f * tick;
                    nativeNock->mTranslation = {float(tick), 2, 3};
                    check(rig.mAnimation->play("a044_036010", frame, false, 1.f, yaw, {}, {}, {},
                              false, false, upperBody, true, pitch), "bow pose rejected");
                    // One traversal only: a second update could hide stale
                    // left-arm reads when the right branch is visited first.
                    rig.update();
                    check(close(rig.modelMatrix(rig.mWeapon).getTrans(), palm * rig.modelMatrix(left)),
                        "bow attachment did not follow the current left palm");
                    bank->sample(*clip, frame, false, sample);
                    osg::Vec3f forward(-sample[*axes], sample[*axes + 2], sample[*axes + 1]);
                    forward.normalize();
                    const osg::Quat aim(pitch, osg::Vec3f(-1, 0, 0));
                    const osg::Quat facing(yaw, osg::Vec3f(0, 0, 1));
                    check(close(rig.modelRotation(rig.mWeapon) * osg::Vec3f(1, 0, 0), facing * (aim * forward)),
                        "native bow firing direction diverged from imported bow aim");
                    check(close(nock->getMatrix().getTrans(), nativeNock->mTranslation),
                        "bow pose replaced the native nock translation");
                    check(close(nock->mRotationScale.toOsgMatrix().getRotate(), nativeNock->mRotation),
                        "bow pose replaced the native nock rotation");
                    check(std::abs(rig.mWeapon->mScale - rig.mNative->mScale) < 1e-6f,
                        "bow pose changed native equipment scale");
                }
                const auto rightPalm = [&] {
                    return rig.mNative->mTranslation * rig.modelMatrix(rig.mHand);
                };
                // Use a reachable moving string target, updated by the native
                // bow callback in this traversal, then solve before consumers.
                const float lastFrame = float(clip->mFrames - 1);
                for (int tick = 0; tick != 5; ++tick)
                {
                    const osg::Vec3f target = rightPalm() + osg::Vec3f(.2f, -.3f, .1f);
                    nativeNock->mTranslation = target * osg::Matrixf::inverse(rig.modelMatrix(rig.mWeapon))
                        - nativeNock->mRotation * osg::Vec3f(0, -8, 0) * nativeNock->mScale;
                    check(rig.mAnimation->play("a044_036010", lastFrame, false, 1.f, .35f, {}, {}, {},
                              false, false, upperBody, true, .85f, 1.f), "bow nock pose rejected");
                    rig.update();
                    check(close(rightPalm(), osg::Vec3f(0, -8, 0) * rig.modelMatrix(nock)),
                        "bow draw hand missed the physical tail of the current native arrow");
                    check(close(osg::Vec3f(0, -8, 0) * rig.modelMatrix(nock), target),
                        "aligning the arrow moved its nock off the native string");
                    check(close(rig.modelRotation(nock) * osg::Vec3f(0, 1, 0),
                              rig.modelRotation(rig.mWeapon) * osg::Vec3f(1, 0, 0)),
                        "attached arrow points away from the intended firing direction");
                    check(close(rig.modelMatrix(rig.mWeapon).getTrans(), palm * rig.modelMatrix(left)),
                        "nock closure moved the bow away from its holding hand");
                }
                const osg::Vec3f held = rightPalm();
                nock->removeChildren(0, nock->getNumChildren());
                check(rig.mAnimation->play("a044_036000", 0, false, .001f, .35f, {}, {}, {},
                          false, false, upperBody, true, .85f, 0.f), "bow release rejected");
                rig.update();
                check((rightPalm() - held).length() < .2f,
                    "bow release snapped away from the displayed draw-hand correction");
                rig.mAnimation->clear();
                check(close(rig.mWeapon->getMatrix().getTrans(), rig.mNative->mTranslation),
                    "clearing bow pose left a relocated right attachment");
                check(close(rig.mWeapon->mRotationScale.toOsgMatrix().getRotate(), rig.mNative->mRotation),
                    "clearing bow pose left imported equipment rotation");
                rig.play("a013_000000");
                check(close(rig.mWeapon->getMatrix().getTrans(), rig.mNative->mTranslation),
                    "switching from bow to spear retained bow translation");
                check(!rig.mAnimation->play("missing bow", 0, false, 1, 0, {}, {}, {},
                          false, false, upperBody, true), "missing bow clip was accepted");
                check(close(rig.mWeapon->getMatrix().getTrans(), rig.mNative->mTranslation),
                    "missing bow clip did not restore native attachment");
            }
        Rig noLeft(bank);
        check(!noLeft.mAnimation->play("a044_036010", 0, false, 1, 0, {}, {}, {}, false, false, false, true),
            "bow accepted a rig without its holding hand");
    }

    void humanoidCreatureRigs()
    {
        auto bank = fixtureBank(true, true);
        Rig humanoid(bank, true, true, true);
        const osg::Quat native = humanoid.modelRotation(humanoid.mNodes.at("bip01 pelvis").get());
        humanoid.play("a000_000000", 1.f, 1.f, false, 0.f, {});
        check(!close(native, humanoid.modelRotation(humanoid.mNodes.at("bip01 pelvis").get())),
            "complete humanoid creature kept its native pose");
        humanoid.mAnimation->clear();

        // Creature meshes need not have the optional NPC fingers, toes, or
        // intermediate spine joints to share its humanoid motion.
        auto minimal = humanoid.mNodes;
        std::erase_if(minimal, [](const auto& entry) {
            return entry.first.find("finger") != std::string::npos
                || entry.first.find("toe") != std::string::npos || entry.first == "bip01 spine";
        });
        MWRender::EldenAnimation compatible(minimal, *humanoid.mRoot, bank, true);
        check(compatible.valid(), "humanoid creature without optional bones was rejected");
        check(compatible.play("a000_000000", 1.f, true, 1.f), "compatible creature could not play an imported clip");
        compatible.clear();

        for (const char* missing : { "bip01 head", "bip01 pelvis", "bip01 neck",
                 "bip01 l upperarm", "bip01 r forearm", "bip01 l hand",
                 "bip01 l thigh", "bip01 r calf", "bip01 l foot" })
        {
            auto incomplete = minimal;
            incomplete.erase(missing);
            MWRender::EldenAnimation unsupported(incomplete, *humanoid.mRoot, bank, true);
            check(!unsupported.valid(), "non-humanoid creature accepted a partial body");
            check(unsupported.controllers().empty(), "unsupported creature installed pose callbacks");
            check(!unsupported.play("a000_000000", 1.f, true, 1.f),
                "unsupported creature replaced its native animation");
        }

        // Entirely absent limbs remain supported by first-person NPC rigs,
        // but must not let a creature with only a few shared names opt in.
        Rig partial(bank);
        MWRender::EldenAnimation unsupported(partial.mNodes, *partial.mRoot, bank, true);
        check(!unsupported.valid(), "creature with an absent arm was accepted as a humanoid");
        check(partial.mAnimation->valid(), "first-person partial rig support regressed");

        auto* calf = static_cast<NifOsg::MatrixTransform*>(humanoid.mNodes.at("bip01 r calf").get());
        const osg::Vec3f calfTranslation = calf->getMatrix().getTrans();
        calf->setTranslation({});
        MWRender::EldenAnimation degenerate(humanoid.mNodes, *humanoid.mRoot, bank, true);
        check(!degenerate.valid(), "zero-length creature limb was accepted");
        calf->setTranslation(calfTranslation);

        // A quadruped can expose every same Bip01 label; anatomy must still
        // reject its horizontal torso instead of turning it into a human.
        auto* spine = static_cast<NifOsg::MatrixTransform*>(humanoid.mNodes.at("bip01 spine").get());
        const osg::Quat spineRotation = spine->getMatrix().getRotate();
        spine->setRotation(osg::Quat(1.57079632679, osg::Vec3f(1, 0, 0)));
        MWRender::EldenAnimation quadruped(humanoid.mNodes, *humanoid.mRoot, bank, true);
        check(!quadruped.valid() && quadruped.controllers().empty(),
            "horizontal creature torso with humanoid bone names was accepted");
        spine->setRotation(spineRotation);
    }

    void upperBodyOverNativeLocomotion()
    {
        auto bank = fixtureBank(true, true);
        Rig target(bank, true, true), layered(bank, true, true);
        target.play("a000_000000", 1.f, 1.f, false, 0.f, {});
        const auto nativeController = [&](const char* name, osg::Vec3f translation) {
            osg::ref_ptr<NativeWeapon> native = new NativeWeapon;
            native->mTranslation = translation;
            native->mRotation = osg::Quat(.6f, osg::Vec3f(0, 1, 0));
            native->mScale = 1.f;
            auto* node = layered.mNodes.at(name).get();
            native->setNestedCallback(node->getUpdateCallback());
            node->setUpdateCallback(native);
            return native;
        };
        auto pelvis = nativeController("bip01 pelvis", {3.f, 4.f, 50.f});
        auto calf = nativeController("bip01 r calf", {0.f, 2.f, -24.f});
        auto spine = nativeController("bip01 spine", {0.f, 0.f, 12.f});
        check(layered.mAnimation->play("a000_000000", 1.f, false, 1.f, 0.f, {}, {}, {}, false, false, true),
            "upper-body clip was rejected");
        for (int frame = 0; frame != 3; ++frame)
        {
            // The same pose must follow native ancestors updated after play,
            // rather than using the pelvis from the previous simulation tick.
            pelvis->mTranslation.x() += 2.f;
            pelvis->mRotation = osg::Quat(.2f * frame, osg::Vec3f(0, 0, 1));
            layered.update();
            for (const auto& [name, native] : {
                     std::pair{ "bip01 pelvis", pelvis }, std::pair{ "bip01 r calf", calf } })
            {
                auto* node = static_cast<NifOsg::MatrixTransform*>(layered.mNodes.at(name).get());
                check(close(node->getMatrix().getTrans(), native->mTranslation),
                    "upper-body pose overwrote native lower-body translation");
                check(close(node->mRotationScale.toOsgMatrix().getRotate(), native->mRotation),
                    "upper-body pose overwrote native lower-body rotation");
            }
            check(close(layered.mNodes.at("bip01 spine")->getMatrix().getTrans(), spine->mTranslation),
                "upper-body pose overwrote native torso translation");
            for (const char* name : { "bip01 spine", "bip01 neck", "bip01 r hand", "bip01 l hand" })
                check(close(layered.modelRotation(layered.mNodes.at(name).get()),
                          target.modelRotation(target.mNodes.at(name).get())),
                    "native pelvis rotation changed imported upper-body orientation");
        }
        const osg::Matrixf before = layered.mNodes.at("bip01 pelvis")->getMatrix();
        layered.mAnimation->clear();
        check(layered.mNodes.at("bip01 pelvis")->getMatrix() == before,
            "clearing an upper-body pose reset native locomotion");
        check(close(layered.mNodes.at("bip01 spine")->getMatrix().getTrans(), spine->mTranslation),
            "clearing an upper-body pose reset native torso translation");
        layered.play("a000_000000", 1.f, 1.f, false, 0.f, {});
        check(close(layered.modelRotation(layered.mHand.get()), target.modelRotation(target.mHand.get())),
            "returning to full-body animation retained an upper-body mask");
    }

    void poseBlending()
    {
        auto bank = fixtureBank();
        Rig rig(bank, true), target(bank, true);
        target.play("a000_000000", 0, 1, false, 0, "a000_000000");
        const osg::Quat finalHand = target.modelRotation(target.mHand.get());
        // Two large native local turns can almost cancel in model space.
        // Independently interpolating each local turn makes the hand take
        // the long way around while entering the imported stance.
        const osg::Quat turn(170.f * 3.14159265359f / 180.f, osg::Vec3f(0, 0, 1));
        for (const char* name : { "bip01 r upperarm", "bip01 r forearm" })
            static_cast<NifOsg::MatrixTransform*>(rig.mNodes.at(name).get())->setRotation(turn);
        const osg::Quat nativeHand = rig.modelRotation(rig.mHand.get());
        rig.play("a000_000000", 0, .1f, false, 0, "a000_000000");
        osg::Quat expected;
        expected.slerp(.5f, nativeHand, finalHand);
        check(close(rig.modelRotation(rig.mHand.get()), expected),
            "native parent/child turns compounded into an imported wrist twist");
        check(close(rig.mWeapon->mRotationScale.toOsgMatrix().getRotate(), rig.mNative->mRotation),
            "sword pose blend changed the native attachment rotation");

        // An interrupted transition must start from the displayed orientation,
        // including an unmapped ancestor between retargeted bones.
        const osg::Quat interrupted = rig.modelRotation(rig.mHand.get());
        target.play("a036_032500", 0, 1, false, .7f, "a000_000000");
        rig.play("a036_032500", 0, .1f, false, .7f, "a000_000000");
        expected.slerp(.5f, interrupted,
            target.modelRotation(target.mHand.get()) * osg::Quat(-.7f, osg::Vec3f(0, 0, 1)));
        expected = expected * osg::Quat(.35f, osg::Vec3f(0, 0, 1));
        check(close(rig.modelRotation(rig.mHand.get()), expected),
            "interrupted transition lost the displayed wrist orientation");

        rig.mAnimation->clear();
        const osg::Quat restored = rig.modelRotation(rig.mHand.get());
        rig.play("a000_000000", 0, .1f, false, 0, "a000_000000");
        expected.slerp(.5f, restored, finalHand);
        check(close(rig.modelRotation(rig.mHand.get()), expected),
            "reactivating imported animation reused a stale model-space blend origin");
    }

    constexpr std::array<const char*, 10> directionNodes = { "bip01 pelvis", "bip01 spine", "bip01 neck",
        "bip01 r upperarm", "bip01 r hand", "bip01 l thigh", "bip01 l calf", "bip01 r thigh",
        "bip01 r calf", "weapon bone" };
    using Rotations = std::array<osg::Quat, directionNodes.size()>;

    Rotations rotations(const Rig& rig)
    {
        Rotations result;
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] = rig.modelRotation(rig.mNodes.at(directionNodes[i]).get());
        return result;
    }

    void checkDirectionBlend(const Rig& rig, const Rotations& from, const Rotations& target,
        float sourceYaw, float shownYaw, float amount, const char* message)
    {
        const osg::Quat removeSourceYaw(-sourceYaw, osg::Vec3f(0, 0, 1));
        const osg::Quat heading(shownYaw, osg::Vec3f(0, 0, 1));
        const Rotations shown = rotations(rig);
        for (std::size_t i = 0; i < shown.size(); ++i)
        {
            osg::Quat expected;
            expected.slerp(amount, from[i] * removeSourceYaw, target[i]);
            check(close(shown[i], expected * heading), message);
        }
    }

    void directionReversals()
    {
        constexpr float pi = 3.14159265359f;
        constexpr float degrees = pi / 180.f;
        const auto bank = fixtureBank(true, false, true);
        Rig forward(bank, true), wound(bank, true);
        forward.play("a000_000000");
        wound.play("a036_032500");
        const Rotations forwardPose = rotations(forward), woundPose = rotations(wound);

        // A small change in torso winding straddles the 180-degree boundary:
        // independent world-space bone blends turn the chest against the legs.
        for (float direction : { 1.f, -1.f })
        {
            const bool positive = direction > 0.f;
            const char* fromClip = positive ? "a000_000000" : "a036_032500";
            const char* targetClip = positive ? "a036_032500" : "a000_000000";
            const Rotations& from = positive ? forwardPose : woundPose;
            const Rotations& target = positive ? woundPose : forwardPose;
            Rig rig(bank, true);
            rig.play(fromClip);
            const float turn = direction * (pi - .05f);
            rig.play(targetClip, 0, .1f, true, turn);
            checkDirectionBlend(rig, from, target, 0, turn * .5f, .5f,
                "opposite movement turned upper and lower body in different directions");
        }

        // Crossing the yaw representation seam must take the short 20-degree turn.
        Rig seam(bank, true);
        seam.play("a000_000000", 0, 1, true, 170.f * degrees);
        const Rotations beforeSeam = rotations(seam);
        seam.play("a036_032500", 0, .1f, true, -170.f * degrees);
        checkDirectionBlend(seam, beforeSeam, woundPose, 170.f * degrees, pi, .5f,
            "yaw seam made the character spin or split during a direction change");

        // A moving yaw target must keep its turn direction across the antipode
        // and retain the original fade, even when the clip remains unchanged.
        Rig moving(bank, true);
        moving.play("a000_000000");
        moving.play("a036_032500", 0, .05f, true, 170.f * degrees);
        moving.play("a036_032500", 0, .1f, true, -170.f * degrees);
        checkDirectionBlend(moving, forwardPose, woundPose, 0, 190.f * degrees * .84375f, .84375f,
            "same-clip yaw update reversed or restarted the active body turn");
        moving.play("a036_032500", 0, .05f, true, -170.f * degrees);
        checkDirectionBlend(moving, forwardPose, woundPose, 0, 190.f * degrees, 1.f,
            "direction change did not finish at the current heading");

        // Interrupt at the displayed pose, then blend from its shared heading.
        Rig interrupted(bank, true);
        interrupted.play("a000_000000");
        interrupted.play("a036_032500", 0, .1f, true, 170.f * degrees);
        const Rotations shown = rotations(interrupted);
        interrupted.play("a000_000000", 0, 0, true, -170.f * degrees);
        checkDirectionBlend(interrupted, shown, forwardPose, 85.f * degrees, 85.f * degrees, 0.f,
            "interrupted reversal jumped away from the displayed pose");
        interrupted.play("a000_000000", 0, .1f, true, -170.f * degrees);
        checkDirectionBlend(interrupted, shown, forwardPose, 85.f * degrees, 137.5f * degrees, .5f,
            "interrupted reversal lost the displayed heading or pose");

        // At exactly opposite headings either shared direction is valid. Every
        // bone must nevertheless preserve the same heading-relative pose.
        for (float turn : { pi, -pi })
        {
            Rig opposite(bank, true);
            opposite.play("a000_000000");
            opposite.play("a036_032500", 0, .1f, true, turn);
            const Rotations actual = rotations(opposite);
            osg::Quat pelvis;
            pelvis.slerp(.5f, forwardPose[0], woundPose[0]);
            for (std::size_t i = 1; i < actual.size(); ++i)
            {
                osg::Quat expected;
                expected.slerp(.5f, forwardPose[i], woundPose[i]);
                check(close(actual[i] * actual[0].inverse(), expected * pelvis.inverse()),
                    "exactly opposite headings selected conflicting body turn directions");
            }
        }
    }

    void spearGripAttachments(std::shared_ptr<const EldenCombat::AnimationBank> bank)
    {
        {
            Rig incomplete(bank);
            MWRender::EldenAnimation rejected(incomplete.mNodes, *incomplete.mRoot, bank, true, true);
            check(!rejected.valid(), "incomplete Guile replacement rig received humanoid poses");
        }
        {
            Rig missingSpear(bank, false, true);
            MWRender::EldenAnimation rejected(missingSpear.mNodes, *missingSpear.mRoot, bank, true, true);
            check(!rejected.valid(), "Guile replacement without its embedded spear lost native fallback");
        }
        {
            Rig missingGeometry(bank, false, true, true, true);
            missingGeometry.mAnimation->clear();
            missingGeometry.mWeapon->removeChildren(0, missingGeometry.mWeapon->getNumChildren());
            MWRender::EldenAnimation rejected(missingGeometry.mNodes, *missingGeometry.mRoot, bank, true, true);
            check(!rejected.valid(), "unusable embedded spear geometry lost native fallback");
        }
        for (const auto [embedded, twoHanded] : {
                 std::pair{ false, false }, std::pair{ false, true },
                 std::pair{ true, false }, std::pair{ true, true } })
            for (bool reversed : { false, true })
            {
                Rig rig(bank, false, true, true, embedded);
                check(rig.mAnimation->hasEmbeddedSpear() == embedded, "embedded spear capability mismatch");
                const auto rightBind = rig.modelMatrix(rig.mHand);
                const osg::Vec3f palm = ((rig.modelMatrix(rig.mNodes.at("bip01 r finger1")).getTrans()
                    + rig.modelMatrix(rig.mNodes.at("bip01 r finger2")).getTrans()) * .5f)
                    * osg::Matrixf::inverse(rightBind);
                const auto leftHand = rig.mNodes.at("bip01 l hand");
                const osg::Vec3f leftPalm = ((rig.modelMatrix(rig.mNodes.at("bip01 l finger1")).getTrans()
                    + rig.modelMatrix(rig.mNodes.at("bip01 l finger2")).getTrans()) * .5f)
                    * osg::Matrixf::inverse(rig.modelMatrix(leftHand));
                if (reversed && embedded)
                {
                    rig.mRoot->removeChild(rig.mWeapon);
                    rig.mRoot->insertChild(0, rig.mWeapon);
                }
                else if (reversed)
                {
                    auto* leftUpper = rig.mNodes.at("bip01 l upperarm").get();
                    auto* parent = leftUpper->getParent(0);
                    osg::ref_ptr<osg::Node> keep = leftUpper;
                    parent->removeChild(leftUpper);
                    parent->insertChild(0, leftUpper);
                }
                osg::ref_ptr<FirstPersonNeck> neck = new FirstPersonNeck;
                neck->mRoot = rig.mRoot;
                rig.mNodes.at("bip01 neck")->addUpdateCallback(neck);
                for (float length : { 40.f, 90.f })
                {
                    rig.mWeapon->removeChildren(0, rig.mWeapon->getNumChildren());
                    rig.mWeapon->addChild(spearGeometry(embedded, length));
                    const osg::Quat basis = embedded
                        ? osg::Quat(-1.57079632679, osg::Vec3f(1, 0, 0)) : osg::Quat();
                    const osg::Vec3f contact = basis.inverse() * osg::Vec3f(.25f, -.2f * length, .5f);
                    for (float scale : { .5f, 1.f, 2.f })
                        for (float pitch : { -.7f, 0.f, .7f })
                            for (int frame = 0; frame != 8; ++frame)
                            {
                                rig.mNative->mScale = scale;
                                neck->mPitch = osg::Quat(pitch, osg::Vec3f(-1, 0, 0));
                                rig.play(frame % 2 ? "a036_032500" : "a036_032505", 0.f, 1.f / 60.f,
                                    !embedded, .4f, twoHanded ? "a013_000000" : "a000_000000");
                                const osg::Vec3f expected = (embedded ? palm : rig.mNative->mTranslation)
                                    * rig.modelMatrix(rig.mHand);
                                if (twoHanded)
                                {
                                    const auto inverseWeapon = osg::Matrixf::inverse(rig.modelMatrix(rig.mWeapon));
                                    const osg::Vec3f right = basis * (expected * inverseWeapon);
                                    const osg::Vec3f left
                                        = basis * ((leftPalm * rig.modelMatrix(leftHand)) * inverseWeapon);
                                    const float front = std::max(right.y(), left.y());
                                    const float rear = std::min(right.y(), left.y());
                                    check(rear >= -.92f * length - 2e-4f,
                                        "lower spear grip left the rear hand past the butt");
                                    if (front - rear <= .72f * length)
                                        check(std::abs(front + .2f * length) < 2e-4f,
                                            "front hand is not below the spear midpoint");
                                    else
                                        check(std::abs(rear + .92f * length) < 2e-4f,
                                            "short spear did not preserve its rear palm margin");
                                    check(std::abs(right.x() - .25f) < 2e-4f && std::abs(right.z() - .5f) < 2e-4f,
                                        "moving the front grip below midpoint displaced the shaft sideways");
                                }
                                else
                                    check(close(contact * rig.modelMatrix(rig.mWeapon), expected),
                                        "spear grip missed the palm during scaling, aiming or blending");
                                const osg::Matrixf before = rig.modelMatrix(rig.mWeapon);
                                rig.update();
                                check(close(before.getTrans(), rig.modelMatrix(rig.mWeapon).getTrans()),
                                    "paused spear translation accumulated");
                            }
                }
                rig.mAnimation->clear();
                check(close(rig.mWeapon->getMatrix().getTrans(), rig.mNative->mTranslation),
                    "leaving imported poses did not restore native spear translation");
                check(close(rig.mWeapon->mRotationScale.toOsgMatrix().getRotate(), rig.mNative->mRotation),
                    "leaving imported poses did not restore native spear rotation");
                if (!embedded)
                {
                    rig.play("a036_032505", 0, 1, false);
                    check(close(rig.mWeapon->getMatrix().getTrans(), rig.mNative->mTranslation),
                        "non-spear inherited the grip correction");
                    rig.mWeapon->removeChildren(0, rig.mWeapon->getNumChildren());
                    rig.play("a036_032505");
                    check(close(rig.mWeapon->getMatrix().getTrans(), rig.mNative->mTranslation),
                        "missing geometry retained the old spear grip");
                }
            }
    }

    void regression()
    {
        Rig rig(fixtureBank());
        rig.play("a036_032505");
        check(close(rig.tip(), {0, 1, 0}), "forward thrust spear points vertically instead of forward");
        check(close(rig.mWeapon->getMatrix().getTrans(), rig.mNative->mTranslation), "attachment offset changed");
        check(std::abs(rig.mWeapon->mScale - rig.mNative->mScale) < 1e-5, "attachment scale changed");
        const auto firstHand = rig.modelRotation(rig.mHand.get());
        rig.play("a036_032505", 1);
        check(close(rig.tip(), {0, 1, 0}), "sliding grip changed spear direction");
        check(close(rig.modelRotation(rig.mHand.get()), firstHand), "sliding shaft dragged fingers away from wrist");

        const osg::Quat previous = rig.modelRotation(rig.mWeapon.get());
        rig.play("a036_032500", 0, .1f);
        osg::Quat expected;
        const osg::Quat target = osg::Matrixf(0, 1, 0, 0, 1, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1).getRotate();
        expected.slerp(.5, previous, target);
        check(close(rig.modelRotation(rig.mWeapon.get()), expected), "weapon blend did not cancel displayed parent pose");
        rig.play("a036_032500", 1);
        check(close(rig.tip(), {1, 0, 0}), "weapon axes were replaced with a constant correction");
        rig.play("a036_032505", 0, 1, true, 1.57079632679f);
        check(close(rig.tip(), {-1, 0, 0}), "actor-local animation yaw was omitted");

        rig.mNative->mRotation = osg::Quat(.42, osg::Vec3f(0, 0, 1));
        rig.play("a036_032505");
        rig.mAnimation->clear();
        check(close(rig.mWeapon->mRotationScale.toOsgMatrix().getRotate(), rig.mNative->mRotation),
            "clear failed to restore embedded native rotation");
        rig.play("a036_032505");
        rig.mNative->mWriteRotation = false;
        rig.play("a036_032505", 1);
        rig.mAnimation->clear();
        check(close(rig.mWeapon->mRotationScale.toOsgMatrix().getRotate(), rig.mNative->mRotation),
            "translation-only native track inherited previous spear rotation");
        rig.mNative->mWriteRotation = true;
        rig.play("a036_032505", 0, 1, false);
        check(close(rig.mWeapon->mRotationScale.toOsgMatrix().getRotate(), rig.mNative->mRotation),
            "non-spear attachment was overridden");
        rig.play("a036_032505");
        check(!rig.mAnimation->play("missing", 0, false, 1, 0, {}, {}, {}, true), "missing clip accepted");
        check(close(rig.mWeapon->mRotationScale.toOsgMatrix().getRotate(), rig.mNative->mRotation),
            "missing clip left spear override active");

        Rig noAxes(fixtureBank(false));
        noAxes.play("a036_032505");
        check(close(noAxes.mWeapon->mRotationScale.toOsgMatrix().getRotate(), noAxes.mNative->mRotation),
            "bank without weapon axes lost native fallback");
    }

    void firstPersonSpear(std::shared_ptr<const EldenCombat::AnimationBank> bank, bool actual = false)
    {
        const std::array<std::string_view, 4> clips{
            "a036_030500", "a036_030505", "a036_032500", "a036_032505" };
        for (bool helper : { false, true })
        {
            Rig reference(bank, helper);
            Rig aimed(bank, helper);
            osg::ref_ptr<FirstPersonNeck> neck = new FirstPersonNeck;
            neck->mRoot = aimed.mRoot;
            aimed.mNodes.at("bip01 neck")->addUpdateCallback(neck);
            neck->mPitch = osg::Quat(.6f, osg::Vec3f(-1, 0, 0));
            reference.play("a036_032505", 0.f, .01f);
            aimed.play("a036_032505", 0.f, .01f);
            check(close(aimed.tip(), neck->mPitch * reference.tip()),
                "entering first-person spear posing duplicated native neck pitch");
            for (std::string_view clip : clips)
            {
                const auto* animation = bank->find(clip);
                if (!animation && !actual)
                    continue;
                check(animation != nullptr, "first-person spear clip missing");
                for (float frame : { 0.f, (animation->mFrames - 1) * .5f, float(animation->mFrames - 1) })
                    for (float yaw : { 0.f, .7f })
                        for (float pitch : { -.9f, -.3f, 0.f, .3f, .9f })
                            for (float factor : { .75f, 1.f })
                            {
                                neck->mPitch = osg::Quat(pitch * factor, osg::Vec3f(-1, 0, 0));
                                reference.play(clip, frame, 1.f, true, yaw);
                                aimed.play(clip, frame, 1.f, true, yaw);
                                check(close(aimed.tip(), neck->mPitch * reference.tip()),
                                    "first-person spear cancels the neck's up/down aim");
                                check(close(aimed.modelRotation(aimed.mHand),
                                          reference.modelRotation(reference.mHand) * neck->mPitch),
                                    "first-person fixture did not pitch the weapon hand");
                                check(close(aimed.mWeapon->getMatrix().getTrans(), aimed.mNative->mTranslation),
                                    "first-person aim moved the native spear attachment");
                                check(std::abs(aimed.mWeapon->mScale - aimed.mNative->mScale) < 1e-5,
                                    "first-person aim changed spear scale");
                            }
            }

            // Move the camera while an attack transition is in progress. Its
            // pitch must not become part of the saved animation blend pose.
            for (int frame = 0; frame < 30; ++frame)
            {
                neck->mPitch = osg::Quat(-.8f + .05f * frame, osg::Vec3f(-1, 0, 0));
                const std::string_view clip = frame % 2 ? "a036_032505" : "a036_032500";
                reference.play(clip, 0.f, 1.f / 60.f, true, .4f);
                aimed.play(clip, 0.f, 1.f / 60.f, true, .4f);
                check(close(aimed.tip(), neck->mPitch * reference.tip()),
                    "first-person spear accumulated camera pitch during an attack blend");
            }
            aimed.mAnimation->clear();
            check(close(aimed.mWeapon->mRotationScale.toOsgMatrix().getRotate(), aimed.mNative->mRotation),
                "first-person spear failed to restore the native attachment");
        }
    }

    void repeatedActions()
    {
        const auto bank = fixtureBank(true, true);
        Rig reference(bank);
        const auto play = [](Rig& rig, float frame, bool loop, float duration) {
            check(rig.mAnimation->play("a036_032505", frame, loop, duration), "transition clip rejected");
            rig.update();
            return rig.modelRotation(rig.mNodes.at("bip01 pelvis").get());
        };
        const auto target = [&](float frame) { return play(reference, frame, false, 1.f); };
        const osg::Quat start = target(0.f);
        const osg::Quat end = target(1.f);
        check(!close(start, end), "transition fixture has no animated body rotation");

        for (const float step : { 1.f / 30.f, 1.f / 60.f, 1.f / 144.f })
        {
            Rig rig(bank);
            play(rig, 1.f, false, 1.f);
            const auto actual = play(rig, 0.f, false, step);
            const float amount = step / .2f;
            osg::Quat expected;
            expected.slerp(amount * amount * (3.f - 2.f * amount), end, start);
            check(close(actual, expected), "repeated action snapped to the first frame of the same clip");
        }

        Rig rig(bank);
        play(rig, 1.f, false, 1.f);
        play(rig, 0.f, false, .1f);
        const auto advancing = play(rig, .5f, false, .05f);
        osg::Quat expected;
        expected.slerp(.84375, end, target(.5f));
        check(close(advancing, expected), "forward playback restarted the action transition blend");
        check(close(play(rig, .5f, false, 0.f), advancing), "hit-stop changed the displayed transition pose");
        check(close(play(rig, .75f, false, .05f), target(.75f)), "resumed action did not finish its blend");

        play(rig, .75f, true, 1.f);
        check(close(play(rig, 0.f, true, 1.f / 60.f), start), "ordinary loop wrap restarted the action blend");
        check(close(play(rig, .5f, true, 1.f / 60.f), target(.5f)), "loop playback retained a spurious blend");
    }

    void shieldRelease(const std::shared_ptr<const EldenCombat::AnimationBank>& bank)
    {
        for (bool twoHanded : { false, true })
        {
            Rig rig(bank, true, true);
            const auto play = [&](bool guard, float duration, float yaw = 0.f) {
                const std::string_view layer = twoHanded ? "a015_000100" : "a000_000100";
                check(rig.mAnimation->play("a000_000000", 0.f, true, duration, yaw,
                          guard ? layer : "", guard && twoHanded ? layer : ""),
                    "shield release clip rejected");
                rig.update();
            };
            const auto nativeRotation = [&] {
                return rig.mNativeShield->mRotation * rig.modelRotation(rig.mNodes.at("bip01 l hand").get());
            };
            play(true, 1.f);
            const osg::Quat guarded = rig.modelRotation(rig.mShield.get());
            check(!close(guarded, nativeRotation()), "shield release fixture lacks an attachment correction");
            play(false, 0.f);
            check(close(rig.modelRotation(rig.mShield.get()), guarded),
                "releasing block instantly flipped the shield while the guard arm was still raised");

            // Native controllers can animate their attachment during release.
            // Blend toward that current pose, including translation-only frames.
            rig.mNativeShield->mRotation = osg::Quat(.42f, osg::Vec3f(0, 0, 1));
            play(false, .03f);
            osg::Quat expected;
            expected.slerp(.15625f, guarded, nativeRotation());
            check(close(rig.modelRotation(rig.mShield.get()), expected),
                "shield release did not crossfade toward the current native attachment");
            rig.mNativeShield->mWriteRotation = false;
            play(false, .03f);
            expected.slerp(.5f, guarded, nativeRotation());
            check(close(rig.modelRotation(rig.mShield.get()), expected),
                "shield release lost its native rotation on a translation-only controller");
            check(close(rig.mShield->getMatrix().getTrans(), rig.mNativeShield->mTranslation),
                "shield release changed the attachment offset");
            check(std::abs(rig.mShield->mScale - rig.mNativeShield->mScale) < 1e-5f,
                "shield release changed the attachment scale");

            const osg::Quat releasing = rig.modelRotation(rig.mShield.get());
            play(true, 0.f);
            check(close(rig.modelRotation(rig.mShield.get()), releasing),
                "raising block during release snapped the shield back to guard");
            play(true, 1.f, .65f);
            const osg::Quat turningGuard = rig.modelRotation(rig.mShield.get());
            play(false, 0.f, -.4f);
            check(close(rig.modelRotation(rig.mShield.get()), turningGuard),
                "turning on guard release discarded the displayed shield orientation");
            play(false, 1.f, -.4f);
            check(close(rig.modelRotation(rig.mShield.get()), nativeRotation()),
                "shield release did not finish on the native attachment pose");
            rig.mNativeShield->mRotation = osg::Quat(-.3f, osg::Vec3f(0, 1, 0));
            rig.mNativeShield->mWriteRotation = true;
            play(false, .016f, -.4f);
            check(close(rig.modelRotation(rig.mShield.get()), nativeRotation()),
                "completed shield release retained control of the native attachment");
        }
    }

    void shieldGuard(const std::shared_ptr<const EldenCombat::AnimationBank>& bank, bool actualBank = false)
    {
        Rig rig(bank, true, true);
        const auto play = [&](float duration, float yaw = 0.f, std::string_view rightArm = {}) {
            check(rig.mAnimation->play("a000_000000", 0.f, true, duration, yaw,
                      "a000_000100", rightArm),
                "shield guard clip rejected");
            rig.update();
        };
        play(1.f);
        const osg::Quat guarded = rig.modelRotation(rig.mShield.get());
        const osg::Vec3f up = guarded * osg::Vec3f(0, 1, 0);
        const osg::Vec3f front = guarded * osg::Vec3f(0, 0, 1);
        check(up.z() > .7f, "shield top points down during imported guard");
        check(front.y() > .9f, "shield face fails to cover the actor during imported guard");
        check(close(rig.mShield->getMatrix().getTrans(), rig.mNativeShield->mTranslation),
            "shield correction changed its native wrist offset");
        check(std::abs(rig.mShield->mScale - rig.mNativeShield->mScale) < 1e-5f,
            "shield correction changed native attachment scale");
        check(close(rig.mWeapon->mRotationScale.toOsgMatrix().getRotate(), rig.mNative->mRotation),
            "shield guard changed the right-hand weapon attachment");
        if (actualBank)
        {
            std::vector<float> sample;
            bank->sample(*bank->find("a000_000100"), 30.f, true, sample);
            const std::size_t axes = *bank->axesOffset("L_Weapon");
            const auto source = [&](std::size_t at) {
                return osg::Vec3f(-sample[at], sample[at + 2], sample[at + 1]);
            };
            check(close(up, source(axes)), "shield top does not follow the authored guard equipment frame");
            check(close(front, -source(axes + 6)), "shield front does not follow the authored guard equipment frame");
        }
        else
        {
            check(close(up, {0, 0, 1}), "shield guard discarded source up axis");
            check(close(front, {0, 1, 0}), "shield guard discarded source face axis");
            const osg::Quat shown = rig.modelRotation(rig.mShield.get());
            play(0.f, 1.57079632679f, "a013_000000");
            check(close(rig.modelRotation(rig.mShield.get()), shown),
                "changing the moving guard layer snapped the shield orientation");
            play(1.f, 1.57079632679f, "a013_000000");
            check(close(rig.modelRotation(rig.mShield.get()) * osg::Vec3f(0, 0, 1), {-1, 0, 0}),
                "walking shield guard lost the authored pose or actor-local yaw");
        }
        rig.mNativeShield->mRotation = osg::Quat(.42f, osg::Vec3f(0, 0, 1));
        play(1.f);
        rig.mNativeShield->mWriteRotation = false;
        play(1.f);
        rig.mAnimation->clear();
        check(close(rig.mShield->mRotationScale.toOsgMatrix().getRotate(), rig.mNativeShield->mRotation),
            "clearing shield guard retained its override on a translation-only native controller");
        play(1.f);
        rig.play("a000_000000", 0, 1, false, 0, {});
        check(close(rig.mShield->mRotationScale.toOsgMatrix().getRotate(), rig.mNativeShield->mRotation),
            "leaving guard failed to restore the native shield attachment");
        play(1.f);
        check(!rig.mAnimation->play("missing", 0.f, false, 1.f), "missing shield clip accepted");
        check(close(rig.mShield->mRotationScale.toOsgMatrix().getRotate(), rig.mNativeShield->mRotation),
            "missing clip left the shield attachment overridden");
        shieldRelease(bank);
    }

    void shieldFallbacks()
    {
        for (bool axes : { false, true })
        {
            Rig rig(fixtureBank(axes), false, true);
            check(rig.mAnimation->play("a000_000000", 0.f, true, 1.f, 0.f,
                      "a000_000100", axes ? "a000_000100" : ""),
                "native shield fallback clip rejected");
            rig.update();
            check(close(rig.mShield->mRotationScale.toOsgMatrix().getRotate(), rig.mNativeShield->mRotation),
                "missing equipment axes or a two-handed weapon guard changed the shield attachment");
        }
        Rig shield(fixtureBank(), false, true);
        check(shield.mAnimation->play("a000_000000", 0.f, true, 1.f, 0.f,
                  "a015_000100", "a015_000100"),
            "two-handed shield guard clip rejected");
        shield.update();
        check(close(shield.modelRotation(shield.mShield.get()) * osg::Vec3f(0, 0, 1), {0, 1, 0}),
            "two-handed shield guard did not align its shield attachment");
    }

    void guardRecoilLayers(const std::shared_ptr<const EldenCombat::AnimationBank>& bank)
    {
        const auto* recoil = bank->find("a000_004200");
        check(recoil != nullptr, "guard recoil clip missing");
        for (std::string_view guard : { "a000_000100", "a010_000100", "a012_000100",
                 "a013_000100", "a015_000100" })
        {
            const bool bothArms = guard != "a000_000100";
            check(bank->find(guard), "equipment guard layer missing");
            for (std::size_t frame = 0; frame < recoil->mFrames; ++frame)
            {
                Rig held(bank, false, true), impact(bank, false, true), unguarded(bank, false, true);
                const auto play = [&](Rig& rig, std::string_view clip, bool layers) {
                    check(rig.mAnimation->play(clip, static_cast<float>(frame), false, 1.f, 0.f,
                              layers ? guard : "", layers && bothArms ? guard : ""),
                        "guard impact fixture pose rejected");
                    rig.update();
                };
                play(held, "a000_000000", true);
                play(impact, "a000_004200", true);
                play(unguarded, "a000_004200", false);
                bool raised = false;
                for (const char* side : { "l", "r" })
                {
                    if (side[0] == 'r' && !bothArms)
                        continue;
                    for (const char* joint : { "upperarm", "forearm", "hand" })
                    {
                        const std::string name = std::string("bip01 ") + side + " " + joint;
                        const auto rotation = impact.modelRotation(impact.mNodes.at(name).get());
                        const auto expected = held.modelRotation(held.mNodes.at(name).get());
                        // Elbow-plane stabilization can adjust twist as the
                        // torso recoils. The limb's direction must still come
                        // from the equipment's raised guard, never the low arm
                        // in the generic reaction track.
                        const osg::Vec3f along(side[0] == 'l' ? -1.f : 1.f, 0, 0);
                        check((rotation * along - expected * along).length() < .005f,
                            "blocked-hit recoil lost its authored guard arm direction");
                        raised |= !close(rotation, unguarded.modelRotation(unguarded.mNodes.at(name).get()));
                    }
                }
                check(raised, "guard fixture cannot detect the previous dropped-arm recoil");
                for (const char* name : { "bip01 pelvis", "bip01 l thigh", "bip01 r thigh" })
                    check(close(impact.modelMatrix(impact.mNodes.at(name).get()).getTrans(),
                              unguarded.modelMatrix(unguarded.mNodes.at(name).get()).getTrans())
                            && close(impact.modelRotation(impact.mNodes.at(name).get()),
                                unguarded.modelRotation(unguarded.mNodes.at(name).get())),
                        "guard layer erased the source impact's lower-body recoil");
                if (guard == "a000_000100" || guard == "a015_000100")
                    check(close(impact.modelRotation(impact.mShield), held.modelRotation(held.mShield)),
                        "blocked-hit recoil lost its shield attachment alignment");
            }
        }
    }

    void deflectionNativeFallback(const std::shared_ptr<const EldenCombat::AnimationBank>& bank)
    {
        Rig rig(bank, false, true);
        osg::ref_ptr<NativeWeapon> recoil = new NativeWeapon;
        recoil->mRotation = osg::Quat(.9f, osg::Vec3f(0, 0, 1));
        recoil->mTranslation = rig.mHand->getMatrix().getTrans();
        recoil->mScale = 1.f;
        // Native reaction controllers run before imported pose callbacks, just
        // like the engine. A stale attack override would erase this recoil.
        recoil->setNestedCallback(rig.mHand->getUpdateCallback());
        rig.mHand->setUpdateCallback(recoil.get());
        rig.play("a036_032500", 10.f, 1.f, false, 0.f, {});
        check(!close(rig.mHand->mRotationScale.toOsgMatrix().getRotate(), recoil->mRotation),
            "deflection fixture did not start with an imported attack override");
        rig.mAnimation->clear();
        rig.update();
        check(close(rig.mHand->mRotationScale.toOsgMatrix().getRotate(), recoil->mRotation),
            "clearing the blocked attack failed to expose native recoil immediately");
        // The subsequent pose update has no imported repelled clip. It must
        // keep native recoil, including a frozen zero-duration hit-stop update.
        check(!rig.mAnimation->play({}, 0.f, false, 0.f),
            "empty deflection animation unexpectedly retained an imported pose");
        rig.update();
        check(close(rig.mHand->mRotationScale.toOsgMatrix().getRotate(), recoil->mRotation),
            "hit-stop restored the interrupted attack over native deflection");
    }

    void fingerCurl(const std::shared_ptr<const EldenCombat::AnimationBank>& bank, std::string_view stance)
    {
        Rig rig(bank, false, true);
        const auto* clip = bank->find(stance);
        check(clip != nullptr, "finger stance missing");
        std::vector<float> sample;
        bank->sample(*clip, 0, false, sample);
        const auto position = [&](const std::string& name) {
            const auto offset = bank->jointOffset(name);
            check(offset.has_value(), "finger source joint missing");
            return osg::Vec3f(-sample[*offset], sample[*offset + 2], sample[*offset + 1]);
        };
        std::array<osg::Vec3f, 12> translations;
        const std::array<std::string_view, 6> nativeDigits = { "0", "01", "1", "11", "2", "21" };
        const std::array<std::string_view, 6> sourceBegin = { "0", "01", "2", "21", "2", "21" };
        const std::array<std::string_view, 6> sourceEnd = { "01", "02", "21", "22", "21", "22" };
        for (int side = 0; side != 2; ++side)
            for (std::size_t digit = 0; digit != nativeDigits.size(); ++digit)
            {
                const std::string key = std::string(side == 0 ? "bip01 l finger" : "bip01 r finger")
                    + std::string(nativeDigits[digit]);
                translations[side * 6 + digit] = rig.mNodes.at(key)->getMatrix().getTrans();
            }
        for (int entry = 0; entry != 2; ++entry)
        {
            rig.play(stance, 0, 1, false, 0, stance);
            for (int side = 0; side != 2; ++side)
                for (std::size_t digit = 0; digit != nativeDigits.size(); ++digit)
                {
                    const std::string key = std::string(side == 0 ? "bip01 l finger" : "bip01 r finger")
                        + std::string(nativeDigits[digit]);
                    const std::string source = side == 0 ? "L_Finger" : "R_Finger";
                    osg::Vec3f expected = position(source + std::string(sourceEnd[digit]))
                        - position(source + std::string(sourceBegin[digit]));
                    expected.normalize();
                    auto* node = rig.mNodes.at(key).get();
                    check(close(rig.modelRotation(node) * osg::Vec3f(1, 0, 0), expected),
                        "native finger turned away from the imported curl");
                    check(close(node->getMatrix().getTrans(), translations[side * 6 + digit]),
                        "finger retarget changed native phalanx length");
                }
            rig.mAnimation->clear();
        }
    }

    void swordSupportGrip(const std::shared_ptr<const EldenCombat::AnimationBank>& bank = fixtureBank(true, true))
    {
        Rig rig(bank, false, true), reference(bank, false, true);
        const auto attachHandle = [](Rig& target, bool useGeode = true) {
            osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
            for (float y : { -18.f, 2.f })
                for (const osg::Vec3f corner : { osg::Vec3f(-.5f, 0, -.5f), osg::Vec3f(.5f, 0, -.5f),
                         osg::Vec3f(.5f, 0, .5f), osg::Vec3f(-.5f, 0, .5f) })
                    vertices->push_back(corner + osg::Vec3f(0, y, 0));
            osg::ref_ptr<osg::DrawElementsUInt> indices = new osg::DrawElementsUInt(GL_TRIANGLES);
            for (unsigned side = 0; side != 4; ++side)
            {
                const unsigned next = (side + 1) % 4;
                for (unsigned index : { side, next, next + 4, side, next + 4, side + 4 })
                    indices->push_back(index);
            }
            osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
            geometry->setVertexArray(vertices.get());
            geometry->addPrimitiveSet(indices.get());
            osg::ref_ptr<osg::MatrixTransform> meshOffset = new osg::MatrixTransform;
            meshOffset->setMatrix(osg::Matrixf::translate(.25f, 0, .4f));
            if (useGeode)
            {
                osg::ref_ptr<osg::Geode> geode = new osg::Geode;
                geode->addDrawable(geometry.get());
                meshOffset->addChild(geode.get());
            }
            else
                meshOffset->addChild(geometry.get()); // Native NIFs attach drawables directly.
            target.mWeapon->addChild(meshOffset.get());
            target.update(); // Finish the native attachment's scale/offset first.
        };
        attachHandle(rig);
        attachHandle(reference);
        auto* leftHand = rig.mNodes.at("bip01 l hand").get();
        const osg::Matrixf leftBind = rig.modelMatrix(leftHand);
        const osg::Matrixf rightBind = rig.modelMatrix(rig.mHand.get());
        osg::Vec3f nativePalm = osg::Matrixf::transform3x3(rig.mNative->mTranslation, rightBind);
        nativePalm.x() = -nativePalm.x();
        nativePalm = osg::Matrixf::transform3x3(nativePalm, osg::Matrixf::inverse(leftBind));
        const float nativePalmLength = (rig.modelMatrix(rig.mNodes.at("bip01 r finger2").get()).getTrans()
            - rightBind.getTrans()).length();
        std::vector<float> readySample;
        bank->sample(*bank->find("a012_000000"), 0.f, false, readySample);
        const auto position = [&](std::string_view joint) {
            const auto offset = *bank->jointOffset(joint);
            return osg::Vec3f(-readySample[offset], readySample[offset + 2], readySample[offset + 1]);
        };
        const auto axes = *bank->axesOffset("R_Weapon");
        osg::Vec3f shaft(-readySample[axes + 3], readySample[axes + 5], readySample[axes + 4]);
        shaft.normalize();
        const float gripDistance = ((position("R_Weapon") - position("L_Weapon")) * shaft)
            * nativePalmLength / (position("R_Finger2") - position("R_Hand")).length();
        std::vector<std::pair<osg::MatrixTransform*, osg::Vec3f>> translations;
        for (const auto& [name, node] : rig.mNodes)
            if (name.find("upperarm") != std::string::npos || name.find("forearm") != std::string::npos
                || name.find("hand") != std::string::npos || name.find("finger") != std::string::npos)
                translations.emplace_back(node.get(), node->getMatrix().getTrans());
        std::string_view sampledClip;
        float sampledFrame = 0.f;
        const auto play = [&](Rig& target, bool correction, std::string_view clip = "a012_000000",
                              bool loop = true, float duration = 1.f, std::string_view left = {},
                              bool spear = false, float yaw = 0.f, float frame = 0.f) {
            sampledClip = clip;
            sampledFrame = frame;
            check(target.mAnimation->play(clip, frame, loop, duration, yaw, left, {}, "a012_000000", spear, correction),
                "support grip fixture clip rejected");
            target.update();
        };
        const auto palm = [&] { return nativePalm * rig.modelMatrix(leftHand); };
        const auto contact = [&] {
            const osg::Matrixf weapon = rig.modelMatrix(rig.mWeapon.get());
            const float scale = osg::Matrixf::transform3x3(osg::Vec3f(0, 1, 0), weapon).length();
            return osg::Vec3f(.25f, -gripDistance / scale, .4f) * weapon;
        };
        const auto checkContact = [&] {
            const osg::Vec3f shoulder = rig.modelMatrix(rig.mNodes.at("bip01 l upperarm").get()).getTrans();
            const osg::Vec3f elbow = rig.modelMatrix(rig.mNodes.at("bip01 l forearm").get()).getTrans();
            const osg::Vec3f wrist = rig.modelMatrix(leftHand).getTrans();
            const osg::Vec3f desiredWrist = contact()
                - osg::Matrixf::transform3x3(nativePalm, rig.modelMatrix(leftHand));
            const float upper = (elbow - shoulder).length();
            const float lower = (wrist - elbow).length();
            const float reach = (desiredWrist - shoulder).length();
            // The deliberately short, asymmetric fixture arms cannot reach
            // every real pose. Reachable handles must close exactly; others
            // must stop at the closest reach boundary without stretching.
            const float unavoidable = std::max({ 0.f, reach - upper - lower, std::abs(upper - lower) - reach });
            if (std::abs((palm() - contact()).length() - unavoidable) >= 2e-4f)
                std::cerr << "grip " << sampledClip << " frame " << sampledFrame << ": error "
                          << (palm() - contact()).length() << " unavoidable " << unavoidable
                          << " requested " << reach << " arm " << upper << '+' << lower << '\n';
            check(std::abs((palm() - contact()).length() - unavoidable) < 2e-4f,
                "supporting palm missed the reachable handle or nearest native arm reach");
        };
        for (float yaw : { 0.f, 1.1f })
        {
            play(reference, false, "a012_000000", true, 1.f, {}, false, yaw);
            play(rig, false, "a012_000000", true, 1.f, {}, false, yaw);
            check((palm() - contact()).length() > .1f, "support grip fixture was already closed");
            play(rig, true, "a012_000000", true, 1.f, {}, false, yaw);
            checkContact();
            check(close(rig.modelRotation(leftHand), reference.modelRotation(reference.mNodes.at("bip01 l hand").get())),
                "closing the supporting arm rotated its hand/finger curl");
            check(close(rig.modelMatrix(rig.mWeapon.get()).getTrans(), reference.modelMatrix(reference.mWeapon.get()).getTrans())
                    && close(rig.modelRotation(rig.mWeapon.get()), reference.modelRotation(reference.mWeapon.get())),
                "supporting arm correction moved the sword or leading hand");
            for (const auto& [node, translation] : translations)
                check(close(node->getMatrix().getTrans(), translation), "support grip changed a native bone length");
            const osg::Vec3f held = palm();
            play(rig, true, "a012_000000", true, 0.f, {}, false, yaw);
            check(close(palm(), held), "frozen support grip accumulated another correction");
            rig.update();
            check(close(palm(), held), "repeated scene traversal accumulated a support correction");
        }

        for (std::string_view clip : { "a012_020100", "a012_020200" })
        {
            const auto* gait = bank->find(clip);
            check(gait != nullptr, "sword carry gait missing from bank");
            for (float yaw : { 0.f, 1.1f })
            {
                for (float frame = 0; frame < gait->mFrames - 1; frame += .25f)
                {
                    play(reference, false, clip, true, 1.f, {}, false, yaw, frame);
                    play(rig, true, clip, true, 1.f, {}, false, yaw, frame);
                    checkContact();
                    check(close(rig.modelRotation(leftHand),
                              reference.modelRotation(reference.mNodes.at("bip01 l hand").get())),
                        "running grip correction changed the supporting hand's orientation");
                    check(close(rig.modelMatrix(rig.mWeapon.get()).getTrans(),
                              reference.modelMatrix(reference.mWeapon.get()).getTrans())
                            && close(rig.modelRotation(rig.mWeapon.get()), reference.modelRotation(reference.mWeapon.get())),
                        "running grip correction changed the leading hand or sword");
                    for (const auto& [node, translation] : translations)
                        check(close(node->getMatrix().getTrans(), translation), "running grip changed native bone lengths");
                    const osg::Vec3f held = palm();
                    play(rig, true, clip, true, 0.f, {}, false, yaw, frame);
                    check(close(palm(), held), "paused running grip accumulated another correction");
                }
            }
        }

        // Native stance entry and action recovery must begin at the displayed
        // pose, including the corrected elbow, before advancing their fade.
        rig.mAnimation->clear();
        play(rig, false, "a000_000000");
        const osg::Vec3f entry = palm();
        play(rig, true, "a012_000000", true, 0.f);
        check(close(palm(), entry), "entering sword ready snapped its supporting hand");
        const float halfReadyFade = bank->find("a012_000000")->mBlendFrames / 60.f;
        play(rig, true, "a012_000000", true, halfReadyFade);
        const osg::Vec3f halfway = palm();
        play(rig, true, "a012_000000", true, 0.f);
        check(close(palm(), halfway), "paused ready fade changed its support solve");
        play(rig, true, "a012_000000", true, halfReadyFade);
        checkContact();
        const osg::Vec3f ready = palm();
        play(rig, true, "a036_032505", false, 0.f);
        check(close(palm(), ready), "leaving sword ready discarded its displayed support pose");

        // The flag alone must never override attacks, overlays or spear poses.
        for (const auto& [clip, loop, layer, spear] : {
                 std::tuple{ "a012_000000", false, "", false },
                 std::tuple{ "a036_032505", false, "", false },
                 std::tuple{ "a000_000000", true, "", false },
                 std::tuple{ "a012_000000", true, "a013_000000", false },
                 std::tuple{ "a013_000000", true, "", true },
             })
        {
            // Separate fixtures keep the idle-layer clocks identical even
            // when the real bank's carry clip has a breathing loop.
            Rig scoped(bank, false, true), native(bank, false, true);
            attachHandle(scoped);
            attachHandle(native);
            play(scoped, true, clip, loop, 1.f, layer, spear);
            play(native, false, clip, loop, 1.f, layer, spear);
            for (const char* name : { "bip01 l upperarm", "bip01 l forearm", "bip01 l hand" })
                check(close(scoped.modelRotation(scoped.mNodes.at(name).get()), native.modelRotation(native.mNodes.at(name).get()))
                        && close(scoped.modelMatrix(scoped.mNodes.at(name).get()).getTrans(),
                            native.modelMatrix(native.mNodes.at(name).get()).getTrans()),
                    "sword support correction escaped its carry-pose scope");
        }
        rig.mWeapon->removeChildren(0, rig.mWeapon->getNumChildren());
        attachHandle(rig, false);
        play(rig, true);
        checkContact();
        rig.mWeapon->removeChildren(0, rig.mWeapon->getNumChildren());
        play(rig, true);
        play(reference, false);
        check(close(rig.modelMatrix(leftHand).getTrans(),
                  reference.modelMatrix(reference.mNodes.at("bip01 l hand").get()).getTrans()),
            "removing the equipped mesh retained an obsolete support contact");
    }

    void comboPlaneContinuity()
    {
        Rig rig(fixtureBank(true, false, false, true), false, true);
        rig.play("a036_032500", 0.f, 1.f, false, 0.f, "");
        for (int frame = 1; frame <= 180; ++frame)
            rig.play("a036_032500", float(frame), 1.f / 60.f, false, 0.f, "");
        std::array<osg::Quat, 3> before;
        constexpr std::array names{ "bip01 r upperarm", "bip01 r forearm", "bip01 r hand" };
        for (std::size_t i = 0; i < names.size(); ++i)
            before[i] = rig.mNodes.at(names[i])->getMatrix().getRotate();
        // The next combo starts at the exact displayed pose. Its different
        // clip name must not choose a fresh elbow-plane hemisphere and turn
        // the upper arm and local wrist through 180 degrees during the fade.
        rig.play("a036_032505", 180.f, 1.f, false, 0.f, "");
        for (std::size_t i = 0; i < names.size(); ++i)
            check(close(before[i], rig.mNodes.at(names[i])->getMatrix().getRotate()),
                "combo transition flipped the shoulder or local wrist at an identical source pose");
    }

    float wristTwist(const osg::Quat& rotation, const osg::Quat& bind, const osg::Vec3f& axis)
    {
        const osg::Quat delta = bind.inverse() * rotation;
        const float axial = osg::Vec3f(delta.x(), delta.y(), delta.z()) * axis;
        return 2.f * std::abs(std::remainder(std::atan2(axial, float(delta.w())), 3.14159265358979323846f));
    }

    float comboArmSequence(Rig& rig, Rig& authored,
        const std::shared_ptr<const EldenCombat::AnimationBank>& bank, float fps, int sequence)
    {
        float maximumWrist = 0.f;
        const bool spear = sequence != 0;
        const std::string_view grip = !spear ? "a000_000000"
            : sequence == 1 ? "a010_000000" : "a013_000000";
        // Imported cancel windows: buffered longsword light
        // attacks chain at 17/18 frames; a roll attacks at 18.
        const std::vector<std::pair<std::string_view, float>> clips = !spear
            ? std::vector<std::pair<std::string_view, float>>{
                { "a023_030000", 17.f }, { "a023_030010", 18.f }, { "a023_030020", 65.f } }
            : std::vector<std::pair<std::string_view, float>>{
                { "a000_027100", 18.f }, { sequence == 1 ? "a036_030300" : "a036_032300", 55.f } };
        const osg::Quat handBind = rig.mHand->getMatrix().getRotate();
        osg::Vec3f axis = rig.mHand->getMatrix().getTrans();
        axis.normalize();
        auto* upper = rig.mNodes.at("bip01 r upperarm").get();
        auto* forearm = rig.mNodes.at("bip01 r forearm").get();
        const auto segment = [&](osg::Node* a, osg::Node* b) {
            return (rig.modelMatrix(a).getTrans()
                - rig.modelMatrix(b).getTrans()).length();
        };
        const float upperLength = segment(upper, forearm);
        const float lowerLength = segment(forearm, rig.mHand);
        const auto position = [&](osg::Node* node) { return rig.modelMatrix(node).getTrans(); };
        const osg::Vec3f bindUpper = position(forearm) - position(upper);
        const osg::Vec3f bindLower = position(rig.mHand) - position(forearm);
        const osg::Vec3f bindUp = MWRender::EldenPose::unit(
            position(rig.mNodes.at("bip01 neck")) - position(rig.mNodes.at("bip01 pelvis")), { 0, 0, 1 });
        const osg::Vec3f bindRight = MWRender::EldenPose::unit(
            position(upper) - position(rig.mNodes.at("bip01 l upperarm")), { 1, 0, 0 });
        const osg::Vec3f bindChest = MWRender::EldenPose::unit(bindUp ^ bindRight, { 0, 1, 0 });
        const osg::Vec3f bindNormal = MWRender::EldenPose::unit(bindUpper
            ^ MWRender::EldenPose::bendSecondary(bindUpper, bindLower, bindUpper, bindUpper ^ bindChest), bindRight);
        const osg::Vec3f localNormal = rig.modelRotation(upper).inverse() * bindNormal;
        std::vector<float> sample;
        const auto sourcePosition = [&](std::string_view joint) {
            const auto offset = bank->jointOffset(joint);
            check(offset.has_value(), "combo fixture lacks an arm joint");
            return osg::Vec3f(-sample[*offset], sample[*offset + 2], sample[*offset + 1]);
        };
        rig.play(grip, 0.f, 1.f, spear, 0.f, grip);
        for (const auto& [name, end] : clips)
        {
            const auto* clip = bank->find(name);
            check(clip, "reported combo regression clip missing");
            for (float frame = 0.f; frame < end; frame += 30.f / fps)
            {
                rig.play(name, frame, 1.f / fps, spear, 0.f, grip);
                const float twist = wristTwist(rig.mHand->getMatrix().getRotate(), handBind, axis);
                maximumWrist = std::max(maximumWrist, twist);
                check(twist < .524f,
                    "longsword chain or spear roll concentrated forearm pronation into the wrist");
                check(std::abs(segment(upper, forearm) - upperLength) < .0001f
                        && std::abs(segment(forearm, rig.mHand) - lowerLength) < .0001f,
                    "combo wrist correction changed a native arm segment length");
                if (name == "a036_032300" && frame >= 20.f && frame <= 24.f)
                {
                    bank->sample(*clip, frame, false, sample);
                    const osg::Vec3f sourceUpper = sourcePosition("R_Forearm") - sourcePosition("R_UpperArm");
                    const osg::Vec3f sourceLower = sourcePosition("R_Hand") - sourcePosition("R_Forearm");
                    const osg::Vec3f expected = MWRender::EldenPose::unit(sourceUpper ^ sourceLower, { 0, 0, 1 });
                    check((rig.modelRotation(upper) * localNormal) * expected > .99f,
                        "spear roll follow-up retained an inverted shoulder on a deeply bent elbow");
                }
                if (frame >= clip->mBlendFrames)
                {
                    authored.play(name, frame, 1.f, spear, 0.f, grip);
                    check(close(rig.modelRotation(rig.mHand), authored.modelRotation(authored.mHand)),
                        "combo wrist correction changed the authored palm orientation");
                }
            }
        }
        return maximumWrist;
    }

    void comboArmPoses(const std::shared_ptr<const EldenCombat::AnimationBank>& bank)
    {
        float maximumWrist = 0.f;
        for (float fps : { 30.f, 60.f, 120.f })
            for (bool helper : { false, true })
                for (int sequence = 0; sequence < 3; ++sequence)
                {
                    Rig rig(bank, helper, true), authored(bank, helper, true);
                    maximumWrist = std::max(maximumWrist, comboArmSequence(rig, authored, bank, fps, sequence));
                }
        std::cout << "Longsword and spear-roll maximum wrist twist: "
                  << maximumWrist * 180.f / 3.14159265358979323846f << " degrees\n";
    }

    void realClips(const char* path)
    {
        std::ifstream file(path, std::ios::binary);
        auto bank = std::make_shared<EldenCombat::AnimationBank>(file);
        spearGripAttachments(bank);
        firstPersonSpear(bank, true);
        shieldGuard(bank, true);
        guardRecoilLayers(bank);
        deflectionNativeFallback(bank);
        comboArmPoses(bank);
        swordSupportGrip(bank);
        for (const std::string_view stance : { "a000_000000", "a010_000000", "a012_000000", "a013_000000" })
            fingerCurl(bank, stance);
        Rig rig(bank);
        const auto axes = bank->axesOffset("R_Weapon");
        check(axes.has_value(), "real bank lacks weapon axes");
        std::vector<float> sample;
        for (const auto name : { "a036_030500", "a036_030505", "a036_032500", "a036_032505" })
        {
            const auto* clip = bank->find(name);
            check(clip != nullptr, "real spear heavy clip missing");
            for (std::size_t frame = 0; frame < clip->mFrames; ++frame)
            {
                rig.play(name, static_cast<float>(frame));
                bank->sample(*clip, static_cast<float>(frame), false, sample);
                osg::Vec3f expected(-sample[*axes + 3], sample[*axes + 5], sample[*axes + 4]);
                expected.normalize();
                check(close(rig.tip(), expected), "real spear tip diverged from imported weapon axis");
            }
        }

        Rig stance(bank);
        stance.play("a000_000000", 0, 1, false, 0, "a000_000000");
        const osg::Quat targetHand = stance.modelRotation(stance.mHand.get());
        const auto* idle = bank->find("a000_000000");
        check(idle && idle->mBlendFrames > 0.f, "real stance lacks its recovery blend");
        for (const auto& [name, frame] : { std::pair{ "a000_027100", 10.f },
                 std::pair{ "a000_027100", 15.f }, std::pair{ "a000_027110", 22.f },
                 std::pair{ "a000_202020", 24.f }, std::pair{ "a000_202040", 15.f } })
            for (float yaw : { 0.f, 1.57079632679f })
            {
                rig.play(name, frame, 1, false, yaw, "a000_000000");
                const osg::Quat from = rig.modelRotation(rig.mHand.get());
                rig.play("a000_000000", 0, idle->mBlendFrames / 60.f, false, 0, "a000_000000");
                osg::Quat expected;
                expected.slerp(.5f, from * osg::Quat(-yaw, osg::Vec3f(0, 0, 1)), targetHand);
                expected = expected * osg::Quat(yaw * .5f, osg::Vec3f(0, 0, 1));
                check(close(rig.modelRotation(rig.mHand.get()), expected),
                    "real roll/jump recovery compounded body rotations into a sword wrist twist");
            }
    }
}

#include "eldenring-test-left-grip.hpp"

int main(int argc, char** argv)
{
    try
    {
        regression();
        leftGripTests(fixtureBank());
        spearGripAttachments(fixtureBank());
        firstPersonSpear(fixtureBank());
        shieldGuard(fixtureBank());
        guardRecoilLayers(fixtureBank());
        deflectionNativeFallback(fixtureBank());
        bowAttachments(fixtureBank(true, true));
        shieldFallbacks();
        humanoidCreatureRigs();
        upperBodyOverNativeLocomotion();
        poseBlending();
        directionReversals();
        repeatedActions();
        comboPlaneContinuity();
        fingerCurl(fixtureBank(), "a013_000000");
        swordSupportGrip();
        if (argc > 1)
        {
            realClips(argv[1]);
            std::ifstream input(argv[1], std::ios::binary);
            const auto actualBank = std::make_shared<EldenCombat::AnimationBank>(input);
            bowAttachments(actualBank);
            leftGripTests(actualBank);
        }
        std::cout << "Weapon attachment retarget validation passed: " << checks << " assertions\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
