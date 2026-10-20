#include "eldenanimation.hpp"
#include "eldenanatomy.hpp"
#include "eldengrip.hpp"

#include <components/eldencombat/animations.hpp>
#include <components/nifosg/matrixtransform.hpp>
#include <components/sceneutil/nodecallback.hpp>

#include <osg/Matrix>
#include <osg/NodeVisitor>
#include <osg/Drawable>
#include <osg/Geometry>
#include <osg/TriangleFunctor>
#include <osg/observer_ptr>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace MWRender
{
    namespace
    {
        enum Joint : std::size_t
        {
            Pelvis, Spine, Spine1, Spine2, Neck, Head,
            LClavicle, LUpperArm, LForearm, LHand,
            RClavicle, RUpperArm, RForearm, RHand,
            LThigh, LCalf, LFoot, LToe,
            RThigh, RCalf, RFoot, RToe,
            RWeapon, LWeapon,
            LFinger0, LFinger01, LFinger02, LFinger1, LFinger2, LFinger21, LFinger22,
            RFinger0, RFinger01, RFinger02, RFinger1, RFinger2, RFinger21, RFinger22,
            JointCount
        };

        constexpr std::array<std::string_view, JointCount> sJointNames = {
            "Pelvis", "Spine", "Spine1", "Spine2", "Neck", "Head",
            "L_Clavicle", "L_UpperArm", "L_Forearm", "L_Hand",
            "R_Clavicle", "R_UpperArm", "R_Forearm", "R_Hand",
            "L_Thigh", "L_Calf", "L_Foot", "L_Toe0",
            "R_Thigh", "R_Calf", "R_Foot", "R_Toe0", "R_Weapon", "L_Weapon",
            "L_Finger0", "L_Finger01", "L_Finger02", "L_Finger1", "L_Finger2", "L_Finger21", "L_Finger22",
            "R_Finger0", "R_Finger01", "R_Finger02", "R_Finger1", "R_Finger2", "R_Finger21", "R_Finger22",
        };

        constexpr std::size_t sNoJoint = JointCount;
        constexpr std::size_t sNoParent = std::numeric_limits<std::size_t>::max();
        using JointPositions = std::array<osg::Vec3f, JointCount>;

        bool upperBodyJoint(std::size_t joint)
        {
            return (joint >= Spine && joint <= RHand) || (joint >= LFinger0 && joint < JointCount);
        }

        osg::Quat nodeRotation(const osg::MatrixTransform& node)
        {
            if (const auto* nif = dynamic_cast<const NifOsg::MatrixTransform*>(&node))
                return nif->mRotationScale.toOsgMatrix().getRotate();
            osg::Vec3f translation, scale;
            osg::Quat rotation, scaleOrientation;
            node.getMatrix().decompose(translation, rotation, scale, scaleOrientation);
            return rotation;
        }

        osg::Vec3f unit(osg::Vec3f value, const osg::Vec3f& fallback)
        {
            if (value.length2() < 1e-10f)
                return fallback;
            value.normalize();
            return value;
        }

        osg::Quat frameRotation(osg::Vec3f primary, osg::Vec3f secondary)
        {
            primary = unit(primary, osg::Vec3f(0, 0, 1));
            secondary -= primary * (secondary * primary);
            if (secondary.length2() < 1e-10f)
            {
                secondary = std::abs(primary.z()) < 0.8f ? osg::Vec3f(0, 0, 1) : osg::Vec3f(0, 1, 0);
                secondary -= primary * (secondary * primary);
            }
            secondary.normalize();
            const osg::Vec3f third = primary ^ secondary;
            // OSG matrices use row vectors. Rebuild the third axis: the ERAN
            // bake reflects the original game basis, which is not a rotation.
            return osg::Matrixf(primary.x(), primary.y(), primary.z(), 0,
                secondary.x(), secondary.y(), secondary.z(), 0,
                third.x(), third.y(), third.z(), 0, 0, 0, 0, 1).getRotate();
        }

        osg::Vec3f fromSource(const std::vector<float>& values, std::size_t offset)
        {
            // Recreation: +X left, +Y up, +Z forward. OpenMW: +X right,
            // +Y forward, +Z up. Positions and axes must use the same transform.
            return osg::Vec3f(-values[offset], values[offset + 2], values[offset + 1]);
        }

        struct AnatomicalFrame
        {
            osg::Vec3f mUp;
            osg::Vec3f mRight;
            osg::Vec3f mForward;
            osg::Vec3f mShoulderRight;
            osg::Vec3f mChestForward;
        };

        AnatomicalFrame anatomy(const JointPositions& joints)
        {
            const osg::Vec3f up = unit(joints[Neck] - joints[Pelvis], osg::Vec3f(0, 0, 1));
            const osg::Vec3f right = unit(joints[RThigh] - joints[LThigh], osg::Vec3f(1, 0, 0));
            const osg::Vec3f shoulders = unit(joints[RUpperArm] - joints[LUpperArm], right);
            return { up, right, unit(up ^ right, osg::Vec3f(0, 1, 0)), shoulders,
                unit(up ^ shoulders, osg::Vec3f(0, 1, 0)) };
        }

        osg::Quat jointFrame(std::size_t joint, const JointPositions& p, const AnatomicalFrame& body,
            const osg::Vec3f& headUp, const osg::Vec3f& headForward,
            const std::array<osg::Vec3f, 10>* limbPlanes = nullptr)
        {
            osg::Vec3f primary = body.mUp;
            osg::Vec3f secondary = body.mRight;
            if (joint <= Neck)
            {
                primary = p[joint + 1] - p[joint];
                // Positions do not encode twist along the central spine.
                // The hip and shoulder lines do: distribute their relative
                // turn over the torso instead of locking every spine bone
                // to the pelvis during a winding sword strike.
                const float chest = std::min(float(joint) / float(Spine2), 1.f);
                secondary = body.mRight * (1.f - chest) + body.mShoulderRight * chest;
            }
            else if (joint == Head)
            {
                primary = headUp;
                secondary = headForward;
            }
            else if (joint >= LClavicle && joint <= RHand)
            {
                const std::size_t arm = joint < RClavicle ? LClavicle : RClavicle;
                const std::size_t finger = arm == LClavicle ? LFinger0 : RFinger0;
                if (joint == arm)
                {
                    primary = p[arm + 1] - p[arm];
                    secondary = body.mUp;
                }
                else if (joint == arm + 3)
                {
                    primary = p[finger + 4] - p[joint];
                    secondary = p[finger + 3] - p[finger + 4];
                }
                else if (joint == arm + 2)
                {
                    primary = p[arm + 3] - p[joint];
                    // Elbow positions describe flexion, not forearm
                    // pronation. Giving the forearm only the elbow plane
                    // concentrates the palm's full axial turn at the wrist.
                    // Carry the palm frame back along the wrist bend instead;
                    // this preserves the authored hand and weapon orientation
                    // while placing pronation along the forearm.
                    osg::Quat wristBend;
                    wristBend.makeRotate(unit(p[finger + 4] - p[arm + 3], unit(primary, body.mUp)),
                        unit(primary, body.mUp));
                    secondary = wristBend * (p[finger + 3] - p[finger + 4]);
                }
                else
                {
                    primary = p[joint + 1] - p[joint];
                    secondary = limbPlanes ? (*limbPlanes)[(arm == LClavicle ? 0 : 2) + joint - arm - 1]
                        : EldenPose::bendSecondary(p[arm + 2] - p[arm + 1], p[arm + 3] - p[arm + 2],
                            primary, primary ^ body.mChestForward);
                }
            }
            else if (joint >= LThigh && joint <= RToe)
            {
                const std::size_t leg = joint < RThigh ? LThigh : RThigh;
                if (joint >= leg + 2)
                {
                    primary = p[leg + 3] - p[leg + 2];
                    secondary = limbPlanes ? (*limbPlanes)[leg == LThigh ? 8 : 9]
                        : EldenPose::footSecondary(primary, body.mRight);
                }
                else
                {
                    primary = p[joint + 1] - p[joint];
                    secondary = limbPlanes ? (*limbPlanes)[(leg == LThigh ? 4 : 6) + joint - leg]
                        : EldenPose::kneeSecondary(p[leg + 1] - p[leg], p[leg + 2] - p[leg + 1],
                            primary, body.mRight);
                }
            }
            else if (joint >= LFinger0)
            {
                const std::size_t first = joint < RFinger0 ? LFinger0 : RFinger0;
                const std::size_t digit = joint - first;
                if (digit == 2 || digit == 6)
                    primary = p[joint] - p[joint - 1];
                else if (digit == 3)
                    primary = p[first + 5] - p[first + 4];
                else
                    primary = p[joint + 1] - p[joint];
                secondary = p[first + 3] - p[first + 4];
            }
            return frameRotation(primary, secondary);
        }

        struct LocalPose
        {
            osg::Quat mRotation;
            osg::Vec3f mTranslation;
        };

        void applyPose(osg::MatrixTransform& node, const LocalPose& pose, float scale)
        {
            if (auto* nif = dynamic_cast<NifOsg::MatrixTransform*>(&node))
            {
                nif->setScale(scale);
                nif->setRotation(pose.mRotation);
                nif->setTranslation(pose.mTranslation);
            }
            else
            {
                osg::Matrixf matrix = osg::Matrixf::scale(scale, scale, scale) * osg::Matrixf::rotate(pose.mRotation);
                matrix.setTrans(pose.mTranslation);
                node.setMatrix(matrix);
            }
        }

        struct GripTriangles
        {
            osg::Matrixf mTransform;
            std::vector<EldenPose::GripTriangle>* mTriangles = nullptr;

            void operator()(const osg::Vec3f& a, const osg::Vec3f& b, const osg::Vec3f& c)
            {
                mTriangles->push_back({ a * mTransform, b * mTransform, c * mTransform });
            }
        };

        class GripGeometryVisitor : public osg::NodeVisitor
        {
        public:
            std::vector<EldenPose::GripTriangle> mTriangles;

            GripGeometryVisitor() : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN) {}

            void apply(osg::Drawable& node) override
            {
                osg::TriangleFunctor<GripTriangles> triangles;
                triangles.mTransform = osg::computeLocalToWorld(getNodePath());
                triangles.mTriangles = &mTriangles;
                node.accept(triangles);
            }

            void apply(osg::Geometry& node) override { apply(static_cast<osg::Drawable&>(node)); }
        };
    }

    struct EldenAnimation::Impl
    {
        struct Bone
        {
            osg::ref_ptr<osg::MatrixTransform> mNode;
            std::size_t mParent = sNoParent;
            std::size_t mJoint = sNoJoint;
            LocalPose mRest;
            float mScale = 1.f;
            osg::Matrixf mRestMatrix;
            osg::Matrixf mRestModel;
            osg::Quat mRestModelRotation;
            osg::Quat mCorrection;
            LocalPose mShown;
            osg::Quat mShownModelRotation;
            LocalPose mFrom;
            osg::Quat mFromModelRotation;
            LocalPose mNative;
            osg::Matrixf mNativeMatrix;
            float mNativeScale = 1.f;
            bool mOverridden = false;

            void restoreNative()
            {
                if (!mOverridden)
                    return;
                if (dynamic_cast<NifOsg::MatrixTransform*>(mNode.get()))
                    applyPose(*mNode, mNative, mNativeScale);
                else
                    mNode->setMatrix(mNativeMatrix);
                mOverridden = false;
            }
        };

        struct Callback : SceneUtil::NodeCallback<Callback, osg::MatrixTransform*>
        {
            std::weak_ptr<Impl> mState;
            std::size_t mIndex = 0;
            bool mBeforeNative = false;

            Callback() = default;
            Callback(const Callback& copy, const osg::CopyOp& op)
                : SceneUtil::NodeCallback<Callback, osg::MatrixTransform*>(copy, op)
                , mState(copy.mState)
                , mIndex(copy.mIndex)
                , mBeforeNative(copy.mBeforeNative)
            {
            }
            META_Object(MWRender, Callback)

            void operator()(osg::MatrixTransform* node, osg::NodeVisitor* visitor)
            {
                if (const auto state = mState.lock())
                {
                    Bone& bone = state->mBones[mIndex];
                    if (mBeforeNative)
                        bone.restoreNative();
                    else if (state->mEnabled)
                    {
                        // Keep imported locals out of native blend origins and
                        // tracks that animate only translation or scale. The
                        // next native update resumes its own last pose.
                        bone.mNative = { nodeRotation(*node), node->getMatrix().getTrans() };
                        bone.mNativeMatrix = node->getMatrix();
                        if (const auto* nif = dynamic_cast<const NifOsg::MatrixTransform*>(node))
                            bone.mNativeScale = nif->mScale;
                        if (state->mUpperBodyOnly)
                        {
                            if (upperBodyJoint(bone.mJoint))
                            {
                                // Native locomotion has just updated the pelvis
                                // and other ancestors. Resolve the imported upper
                                // body's model-space rotation against that current
                                // pose, preserving native translations and scale.
                                osg::Quat parent;
                                for (std::size_t i = bone.mParent; i != sNoParent; i = state->mBones[i].mParent)
                                    parent *= nodeRotation(*state->mBones[i].mNode);
                                bone.mShown.mRotation = bone.mShownModelRotation * parent.inverse();
                                bone.mShown.mTranslation = node->getMatrix().getTrans();
                                static_cast<NifOsg::MatrixTransform*>(node)->setRotation(bone.mShown.mRotation);
                                bone.mOverridden = true;
                            }
                        }
                        else
                        {
                            if (bone.mJoint == sNoJoint && !dynamic_cast<NifOsg::MatrixTransform*>(node))
                                node->setMatrix(bone.mRestMatrix);
                            else
                                applyPose(*node, bone.mShown, bone.mScale);
                            bone.mOverridden = true;
                        }
                    }
                }
                traverse(node, visitor);
            }
        };

        struct WeaponCallback : SceneUtil::NodeCallback<WeaponCallback, NifOsg::MatrixTransform*>
        {
            std::weak_ptr<Impl> mState;
            bool mBeforeNative = false;

            WeaponCallback() = default;
            WeaponCallback(const WeaponCallback& copy, const osg::CopyOp& op)
                : SceneUtil::NodeCallback<WeaponCallback, NifOsg::MatrixTransform*>(copy, op)
                , mState(copy.mState)
                , mBeforeNative(copy.mBeforeNative)
            {
            }
            META_Object(MWRender, WeaponCallback)

            void operator()(NifOsg::MatrixTransform* node, osg::NodeVisitor* visitor)
            {
                if (const auto state = mState.lock())
                {
                    if (mBeforeNative)
                        state->restoreWeapon();
                    else if (state->mEnabled && (state->mAlignSpear || state->mAlignWeapon) && !state->mTwoHandedSpear
                        && !state->mLeftHanded)
                        state->applyWeapon();
                }
                traverse(node, visitor);
            }
        };

        // Embedded weapons and the other hand may follow the weapon during
        // traversal. Wait for both displayed palms before placing a two-hand grip.
        struct SpearCallback : SceneUtil::NodeCallback<SpearCallback, osg::Group*>
        {
            std::weak_ptr<Impl> mState;
            SpearCallback() = default;
            SpearCallback(const SpearCallback& copy, const osg::CopyOp& op)
                : SceneUtil::NodeCallback<SpearCallback, osg::Group*>(copy, op)
                , mState(copy.mState)
            {
            }
            META_Object(MWRender, SpearCallback)
            void operator()(osg::Group* node, osg::NodeVisitor* visitor)
            {
                traverse(node, visitor);
                if (const auto state = mState.lock(); state && state->mEnabled
                    && ((state->mAlignSpear && (state->mEmbeddedSpear || state->mTwoHandedSpear))
                        || state->mAlignLeftWeapon))
                    state->applyWeapon();
            }
        };

        struct ShieldCallback : SceneUtil::NodeCallback<ShieldCallback, NifOsg::MatrixTransform*>
        {
            std::weak_ptr<Impl> mState;
            bool mBeforeNative = false;

            ShieldCallback() = default;
            ShieldCallback(const ShieldCallback& copy, const osg::CopyOp& op)
                : SceneUtil::NodeCallback<ShieldCallback, NifOsg::MatrixTransform*>(copy, op)
                , mState(copy.mState)
                , mBeforeNative(copy.mBeforeNative)
            {
            }
            META_Object(MWRender, ShieldCallback)

            void operator()(NifOsg::MatrixTransform* node, osg::NodeVisitor* visitor)
            {
                if (const auto state = mState.lock())
                {
                    if (mBeforeNative)
                    {
                        state->restoreShield();
                        if (state->mOverriddenWeaponNode.get() == node)
                            state->restoreWeapon();
                    }
                    else if (state->mEnabled && state->mAlignOffhandWeapon)
                        state->applyOffhandWeapon();
                    else if (state->mEnabled && !state->mAlignLeftWeapon
                        && (state->mAlignShield || state->mHaveShownShield))
                    {
                        osg::Vec3f translation, scale;
                        osg::Quat parent, scaleOrientation;
                        osg::computeLocalToWorld(state->mShieldParents).decompose(
                            translation, parent, scale, scaleOrientation);
                        const osg::Quat facing(state->mFacing.shown(), osg::Vec3f(0, 0, 1));
                        state->mNativeShieldRotation = node->mRotationScale.toOsgMatrix().getRotate();
                        state->mNativeShieldTranslation = node->getMatrix().getTrans();
                        if (!state->mHaveShieldBlendFrom)
                        {
                            state->mShieldBlendFrom = state->mNativeShieldRotation * parent * facing.inverse();
                            state->mHaveShieldBlendFrom = true;
                        }
                        if (!state->mAlignShield && state->mShieldBlend >= 1.f)
                        {
                            // The arm has finished returning to its new pose.
                            // The before-native callback has already removed
                            // our previous override; leave the attachment alone.
                            state->mHaveShownShield = false;
                            state->mHaveShieldBlendFrom = false;
                        }
                        else
                        {
                            // Release the guard correction on the same timeline
                            // as the arm. Restoring the native local attachment
                            // immediately would flip it on the still-raised wrist.
                            const osg::Quat target = state->mAlignShield ? state->mShieldTargetRotation
                                : state->mNativeShieldRotation * parent * facing.inverse();
                            state->mShownShieldRotation.slerp(
                                state->mShieldBlend, state->mShieldBlendFrom, target);
                            state->mShownShieldRotation *= facing;
                            // Shield Bone retains its native wrist offset and
                            // scale, but the shield face follows the authored
                            // equipment frame instead of inferred finger twist.
                            node->setRotation(state->mShownShieldRotation * parent.inverse());
                            state->mShieldOverridden = true;
                            state->mHaveShownShield = true;
                        }
                    }
                }
                traverse(node, visitor);
            }
        };

        struct BowCallback : SceneUtil::NodeCallback<BowCallback, NifOsg::MatrixTransform*>
        {
            std::weak_ptr<Impl> mState;
            bool mBeforeNative = false;

            BowCallback() = default;
            BowCallback(const BowCallback& copy, const osg::CopyOp& op)
                : SceneUtil::NodeCallback<BowCallback, NifOsg::MatrixTransform*>(copy, op)
                , mState(copy.mState)
                , mBeforeNative(copy.mBeforeNative)
            {
            }
            META_Object(MWRender, BowCallback)

            void operator()(NifOsg::MatrixTransform* node, osg::NodeVisitor* visitor)
            {
                if (const auto state = mState.lock())
                {
                    if (mBeforeNative)
                        state->restoreBow();
                    else if (state->mEnabled && state->mBow)
                    {
                        state->mNativeBowRotation = nodeRotation(*node);
                        state->mNativeBowTranslation = node->getMatrix().getTrans();
                        state->applyBow();
                    }
                }
                traverse(node, visitor);
            }
        };

        std::shared_ptr<const EldenCombat::AnimationBank> mBank;
        std::vector<Bone> mBones;
        std::array<std::size_t, JointCount> mOffsets{};
        std::optional<std::size_t> mHeadAxes;
        JointPositions mBindJoints{};
        std::array<bool, JointCount> mHaveBindJoint{};
        std::vector<float> mSample;
        std::vector<float> mLayer;
        std::vector<float> mMirrorSource;
        std::vector<std::pair<std::size_t, std::size_t>> mMirrorOffsets;
        std::vector<osg::Quat> mModelRotations;
        std::vector<osg::Matrixf> mModelMatrices;
        std::string mClip;
        std::string mLeftArmClip;
        std::string mRightArmClip;
        std::string mGripClip;
        std::string mGripChangeClip;
        float mGripChangeFrame = 0.f;
        float mFade = 0.f;
        float mFadeLength = 0.f;
        EldenPose::HeadingBlend mFacing;
        std::array<EldenPose::ArmBendPlane, 2> mArmPlanes;
        std::array<float, 2> mArmPlaneSigns{ 1.f, 1.f };
        std::array<EldenPose::LimbBendPlane, 6> mLimbPlanes;
        float mFrame = 0.f;
        bool mLoop = false;
        float mLayerFrame = 0.f;
        float mSourcePelvisHeight = 0.f;
        float mScale = 1.f;
        bool mValid = false;
        bool mEnabled = false;
        bool mUpperBodyOnly = false;
        bool mLeftHanded = false;
        bool mAlignLeftWeapon = false;

        osg::ref_ptr<NifOsg::MatrixTransform> mWeaponNode;
        osg::NodePath mWeaponParents;
        osg::NodePath mWeaponPoseParents;
        std::size_t mWeaponPoseParent = sNoParent;
        std::optional<std::size_t> mWeaponAxes;
        osg::Quat mNativeWeaponRotation;
        osg::Vec3f mNativeWeaponTranslation;
        osg::Quat mWeaponBasis;
        bool mEmbeddedSpear = false;
        bool mTwoHandedSpear = false;
        osg::Vec3f mEmbeddedSpearPalm;
        osg::Vec3f mSpearLeftPalm;
        osg::NodePath mSpearLeftParents;
        osg::Vec3f mSpearRightPalm;
        osg::NodePath mSpearRightParents;
        std::vector<osg::observer_ptr<osg::Node>> mSpearGeometry;
        std::optional<osg::Vec3f> mSpearContact;
        float mSpearRearTravel = 0.f;
        osg::Quat mWeaponTargetRotation;
        osg::Quat mShownWeaponRotation;
        osg::Quat mWeaponBlendFrom;
        float mWeaponBlend = 1.f;
        bool mAlignSpear = false;
        bool mAlignWeapon = false;
        bool mWeaponOverridden = false;
        bool mHaveShownWeapon = false;
        bool mHaveWeaponBlendFrom = false;
        osg::ref_ptr<NifOsg::MatrixTransform> mOverriddenWeaponNode;

        osg::ref_ptr<NifOsg::MatrixTransform> mShieldNode;
        osg::NodePath mShieldParents;
        osg::NodePath mShieldPoseParents;
        std::size_t mShieldPoseParent = sNoParent;
        std::optional<std::size_t> mShieldAxes;
        osg::Quat mNativeShieldRotation;
        osg::Vec3f mNativeShieldTranslation;
        osg::Quat mShieldTargetRotation;
        osg::Quat mShownShieldRotation;
        osg::Quat mShieldBlendFrom;
        float mShieldBlend = 1.f;
        bool mAlignShield = false;
        bool mAlignOffhandWeapon = false;
        bool mAlignOffhandSpear = false;
        std::vector<osg::observer_ptr<osg::Node>> mOffhandGeometry;
        std::optional<osg::Vec3f> mOffhandSpearContact;
        bool mShieldOverridden = false;
        bool mHaveShownShield = false;
        bool mHaveShieldBlendFrom = false;

        struct BowArrowRestore : SceneUtil::NodeCallback<BowArrowRestore, osg::MatrixTransform*>
        {
            std::weak_ptr<Impl> mState;
            BowArrowRestore() = default;
            BowArrowRestore(const BowArrowRestore& copy, const osg::CopyOp& op)
                : SceneUtil::NodeCallback<BowArrowRestore, osg::MatrixTransform*>(copy, op)
                , mState(copy.mState)
            {
            }
            META_Object(MWRender, BowArrowRestore)
            void operator()(osg::MatrixTransform* node, osg::NodeVisitor* visitor)
            {
                if (const auto state = mState.lock())
                    state->restoreBowArrow();
                traverse(node, visitor);
            }
        };

        struct BowFinishCallback : SceneUtil::NodeCallback<BowFinishCallback, osg::Group*>
        {
            std::weak_ptr<Impl> mState;

            BowFinishCallback() = default;
            BowFinishCallback(const BowFinishCallback& copy, const osg::CopyOp& op)
                : SceneUtil::NodeCallback<BowFinishCallback, osg::Group*>(copy, op)
                , mState(copy.mState)
            {
            }
            META_Object(MWRender, BowFinishCallback)

            void operator()(osg::Group* node, osg::NodeVisitor* visitor)
            {
                traverse(node, visitor);
                if (const auto state = mState.lock(); state && state->mEnabled && state->mBow)
                    state->closeBowGrip(*node);
            }
        };

        struct BowHandCallback : SceneUtil::NodeCallback<BowHandCallback, osg::MatrixTransform*>
        {
            std::weak_ptr<Impl> mState;

            BowHandCallback() = default;
            BowHandCallback(const BowHandCallback& copy, const osg::CopyOp& op)
                : SceneUtil::NodeCallback<BowHandCallback, osg::MatrixTransform*>(copy, op)
                , mState(copy.mState)
            {
            }
            META_Object(MWRender, BowHandCallback)

            void operator()(osg::MatrixTransform* node, osg::NodeVisitor* visitor)
            {
                // A replacement rig may visit either arm first. If the bow's
                // right-hand attachment already ran, update it again after the
                // left palm receives this frame's pose. No bone/skin changes.
                if (const auto state = mState.lock(); state && state->mEnabled && state->mBowOverridden)
                    state->applyBow();
                traverse(node, visitor);
            }
        };

        osg::ref_ptr<NifOsg::MatrixTransform> mBowNode;
        osg::NodePath mBowParents;
        osg::NodePath mBowHandPath;
        osg::Vec3f mBowPalm;
        osg::Quat mNativeBowRotation;
        osg::Vec3f mNativeBowTranslation;
        osg::Quat mBowTargetRotation;
        osg::Quat mShownBowRotation;
        osg::Vec3f mShownBowPosition;
        osg::Quat mBowFromRotation;
        osg::Vec3f mBowFromPosition;
        float mBowBlend = 1.f;
        bool mBow = false;
        float mBowDraw = 0.f;
        std::array<std::size_t, 3> mBowArm{ sNoParent, sNoParent, sNoParent };
        osg::Vec3f mBowDrawPalm;
        osg::ref_ptr<osg::MatrixTransform> mSkeletonArrow;
        osg::ref_ptr<osg::MatrixTransform> mBowArrow;
        osg::ref_ptr<BowArrowRestore> mBowArrowRestore;
        osg::Quat mNativeBowArrowRotation;
        osg::Vec3f mNativeBowArrowTranslation;
        std::vector<osg::observer_ptr<osg::Node>> mArrowGeometry;
        std::optional<osg::Vec3f> mArrowTail;

        std::optional<osg::Vec3f> arrowTail(osg::MatrixTransform& node)
        {
            bool changed = mArrowGeometry.size() != node.getNumChildren();
            for (unsigned int i = 0; !changed && i < node.getNumChildren(); ++i)
                changed = mArrowGeometry[i].get() != node.getChild(i);
            if (!changed)
                return mArrowTail;
            GripGeometryVisitor geometry;
            mArrowGeometry.clear();
            for (unsigned int i = 0; i < node.getNumChildren(); ++i)
            {
                mArrowGeometry.emplace_back(node.getChild(i));
                node.getChild(i)->accept(geometry);
            }
            mArrowTail.reset();
            if (geometry.mTriangles.empty())
                return mArrowTail;
            // Native projectile meshes point along +Y; their origin is often
            // near the tip. Find the rear shaft in the actual equipped mesh.
            float rear = std::numeric_limits<float>::max();
            for (const auto& triangle : geometry.mTriangles)
                for (const auto& vertex : triangle)
                    rear = std::min(rear, vertex.y());
            osg::Vec3f low(std::numeric_limits<float>::max(), rear, std::numeric_limits<float>::max());
            osg::Vec3f high(-low.x(), rear, -low.z());
            for (const auto& triangle : geometry.mTriangles)
                for (const auto& vertex : triangle)
                    if (vertex.y() <= rear + 1e-3f)
                        for (int axis : { 0, 2 })
                        {
                            low[axis] = std::min(low[axis], vertex[axis]);
                            high[axis] = std::max(high[axis], vertex[axis]);
                        }
            mArrowTail = (low + high) * .5f;
            return mArrowTail;
        }

        bool mBowArrowOverridden = false;
        std::weak_ptr<Impl> mSelf;

        void restoreBowArrow()
        {
            if (mBowArrowOverridden)
            {
                if (auto* nif = dynamic_cast<NifOsg::MatrixTransform*>(mBowArrow.get()))
                {
                    nif->setRotation(mNativeBowArrowRotation);
                    nif->setTranslation(mNativeBowArrowTranslation);
                }
                mBowArrowOverridden = false;
            }
        }

        void detachBowArrow()
        {
            restoreBowArrow();
            if (mBowArrow && mBowArrowRestore)
                mBowArrow->removeUpdateCallback(mBowArrowRestore);
            mBowArrow = nullptr;
            mBowArrowRestore = nullptr;
        }

        void alignBowArrow(osg::MatrixTransform& nock, const osg::Vec3f& tail)
        {
            auto* nif = dynamic_cast<NifOsg::MatrixTransform*>(&nock);
            if (!nif || mBowParents.empty())
                return;
            if (mBowArrow != &nock)
            {
                detachBowArrow();
                mBowArrow = &nock;
                mBowArrowRestore = new BowArrowRestore;
                mBowArrowRestore->mState = mSelf;
                mBowArrowRestore->setNestedCallback(nock.getUpdateCallback());
                nock.setUpdateCallback(mBowArrowRestore);
            }
            const auto paths = nock.getParentalNodePaths(mBowParents.front());
            if (paths.empty())
                return;
            auto parents = paths.front();
            parents.pop_back();
            osg::Vec3f translation, scale;
            osg::Quat parentRotation, scaleOrientation;
            osg::computeLocalToWorld(parents).decompose(translation, parentRotation, scale, scaleOrientation);
            mNativeBowArrowRotation = nodeRotation(nock);
            mNativeBowArrowTranslation = nock.getMatrix().getTrans();
            const osg::Vec3f nativeTail = tail * nock.getMatrix();
            const osg::Quat modelRotation = mNativeBowArrowRotation * parentRotation;
            osg::Quat correction;
            correction.makeRotate(modelRotation * osg::Vec3f(0, 1, 0),
                mShownBowRotation * osg::Vec3f(1, 0, 0));
            osg::Quat blended;
            blended.slerp(mBowDraw * mBowBlend, osg::Quat(), correction);
            // Rotate around the physical nock, not the model origin near the
            // arrow tip. This keeps the tail on the native animated string.
            nif->setRotation(modelRotation * blended * parentRotation.inverse());
            nif->setTranslation(nativeTail - osg::Matrixf::transform3x3(tail, nif->getMatrix()));
            mBowArrowOverridden = true;
        }

        bool mBowOverridden = false;
        bool mHaveBowFrom = false;

        void closeBowGrip(osg::Group& commonRoot)
        {
            if (mBowDraw <= 0.f
                || std::find(mBowArm.begin(), mBowArm.end(), sNoParent) != mBowArm.end())
                return;
            const auto [upper, forearm, hand] = mBowArm;
            if (mBones[forearm].mParent != upper || mBones[hand].mParent != forearm)
                return;
            class FindNock : public osg::NodeVisitor
            {
            public:
                osg::MatrixTransform* mNock = nullptr;
                bool mSkin = false;
                FindNock() : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN) {}
                void apply(osg::Node& node) override
                {
                    // NPC body parts are attached beside the skeleton root.
                    // A custom rig with skin inside the arm subtree may have
                    // cached its bones already: keep its authored hand pose.
                    mSkin |= std::string_view(node.className()) == "RigGeometry";
                    if (node.getName() == "ArrowBone" || node.getName() == "arrowbone")
                        mNock = dynamic_cast<osg::MatrixTransform*>(&node);
                    traverse(node);
                }
            } visitor;
            commonRoot.accept(visitor);
            osg::MatrixTransform* nock = mSkeletonArrow ? mSkeletonArrow.get() : visitor.mNock;
            if (!nock || visitor.mSkin || nock->getNumChildren() == 0)
                return;
            const auto nockPaths = nock->getParentalNodePaths(&commonRoot);
            if (nockPaths.empty())
                return;
            const auto model = [&](std::size_t index) {
                const auto path = mBones[index].mNode->getParentalNodePaths(&commonRoot).front();
                return osg::Matrixf(osg::computeLocalToWorld(path));
            };
            // Solve entirely in the common ancestor's frame. Both arms and the
            // native bow nock have just updated, independent of sibling order.
            const osg::Matrixf upperModel = model(upper);
            const osg::Matrixf forearmModel = model(forearm);
            const osg::Matrixf handModel = model(hand);
            const auto tail = arrowTail(*nock);
            if (!tail)
                return;
            const osg::Vec3f target = *tail * osg::computeLocalToWorld(nockPaths.front())
                - osg::Matrixf::transform3x3(mBowDrawPalm, handModel);
            const osg::Vec3f root = upperModel.getTrans();
            const osg::Vec3f elbow = forearmModel.getTrans();
            const osg::Vec3f wrist = handModel.getTrans();
            const auto solved = EldenPose::solveTwoBone(
                root, elbow, wrist, wrist + (target - wrist) * mBowDraw * mBowBlend);
            osg::Quat upperTurn, forearmTurn;
            upperTurn.makeRotate(elbow - root, solved.mJoint - root);
            forearmTurn.makeRotate(wrist - elbow, solved.mEnd - solved.mJoint);
            const auto rotation = [](const osg::Matrixf& value) {
                osg::Vec3f translation, scale;
                osg::Quat result, scaleOrientation;
                value.decompose(translation, result, scale, scaleOrientation);
                return result;
            };
            const osg::Quat upperRotation = rotation(upperModel) * upperTurn;
            const osg::Quat forearmRotation = rotation(forearmModel) * forearmTurn;
            const osg::Quat parentRotation = mBones[upper].mParent == sNoParent
                ? osg::Quat() : rotation(model(mBones[upper].mParent));
            static_cast<NifOsg::MatrixTransform*>(mBones[upper].mNode.get())
                ->setRotation(upperRotation * parentRotation.inverse());
            static_cast<NifOsg::MatrixTransform*>(mBones[forearm].mNode.get())
                ->setRotation(forearmRotation * upperRotation.inverse());
            static_cast<NifOsg::MatrixTransform*>(mBones[hand].mNode.get())
                ->setRotation(rotation(handModel) * forearmRotation.inverse());
            for (std::size_t index : mBowArm)
            {
                auto& bone = mBones[index];
                bone.mShown.mRotation = nodeRotation(*bone.mNode);
                osg::Quat parent;
                for (std::size_t i = bone.mParent; i != sNoParent; i = mBones[i].mParent)
                    parent *= nodeRotation(*mBones[i].mNode);
                bone.mShownModelRotation = bone.mShown.mRotation * parent;
            }
            // The stock bow's parent is that same right hand. Restore its
            // left-palm position after the right arm moves towards the string.
            applyBow();
            alignBowArrow(*nock, *tail);
        }

        void applyBow()
        {
            // Stock skeletons attach bows to the right Weapon Bone.
            // Move that attachment onto the displayed left palm;
            // do not reparent the mesh or replace its string tracks.
            const osg::Matrixf parent = osg::computeLocalToWorld(mBowParents);
            const osg::Matrixf hand = osg::computeLocalToWorld(mBowHandPath);
            osg::Vec3f translation, scale;
            osg::Quat parentRotation, scaleOrientation;
            parent.decompose(translation, parentRotation, scale, scaleOrientation);
            const osg::Quat facing(mFacing.shown(), osg::Vec3f(0, 0, 1));
            if (!mHaveBowFrom)
            {
                mBowFromRotation = mNativeBowRotation * parentRotation * facing.inverse();
                mBowFromPosition = (mNativeBowTranslation * parent) - hand.getTrans();
                mHaveBowFrom = true;
            }
            mShownBowRotation.slerp(
                mBowBlend, mBowFromRotation, mBowTargetRotation);
            mShownBowRotation *= facing;
            const osg::Vec3f palm = mBowPalm * hand;
            const osg::Vec3f position = hand.getTrans()
                + mBowFromPosition * (1.f - mBowBlend)
                + (palm - hand.getTrans()) * mBowBlend;
            mBowNode->setRotation(mShownBowRotation * parentRotation.inverse());
            mBowNode->setTranslation(position * osg::Matrixf::inverse(parent));
            mShownBowPosition = position - hand.getTrans();
            mBowOverridden = true;
        }

        void restoreBow()
        {
            if (mBowOverridden)
            {
                mBowNode->setRotation(mNativeBowRotation);
                mBowNode->setTranslation(mNativeBowTranslation);
                mBowOverridden = false;
            }
        }

        std::array<std::size_t, 4> mSwordArm{ sNoParent, sNoParent, sNoParent, sNoParent };
        std::array<std::size_t, 4> mLeftSwordArm{ sNoParent, sNoParent, sNoParent, sNoParent };
        osg::Vec3f mBindRight;
        float mSwordGripDistance = 0.f;
        float mSwordPalmHalfWidth = 0.f;
        float mSwordGeometryScale = 0.f;
        std::vector<osg::observer_ptr<osg::Node>> mSwordGeometry;
        std::optional<osg::Vec3f> mSwordContact;

        void closeSwordGrip(float blend)
        {
            auto* node = heldWeaponNode();
            const auto& arm = mLeftHanded ? mLeftSwordArm : mSwordArm;
            const auto& parents = heldWeaponParents();
            if (!node || mSwordGripDistance <= 0.f || mSwordPalmHalfWidth <= 0.f
                || std::find(arm.begin(), arm.end(), sNoParent) != arm.end())
                return;
            const auto [upper, forearm, hand, rightHand] = arm;
            if (mBones[forearm].mParent != upper || mBones[hand].mParent != forearm
                || parents.empty() || parents.back() != mBones[rightHand].mNode.get())
                return;

            // Native ready and locomotion attachments are constant. Read the last completed
            // native update, then solve before this frame's bone callbacks and
            // skinning cache run. The normal pose fade covers entry transitions.
            osg::Matrixf attachment = node->getMatrix();
            if (mLeftHanded || mAlignWeapon)
            {
                const osg::Quat facing(mFacing.shown(), osg::Vec3f(0, 0, 1));
                osg::Quat rotation;
                rotation.slerp(mWeaponBlend, mHaveWeaponBlendFrom ? mWeaponBlendFrom
                    : node->mRotationScale.toOsgMatrix().getRotate() * mModelRotations[rightHand] * facing.inverse(),
                    mWeaponTargetRotation);
                attachment = osg::Matrixf::scale(node->mScale, node->mScale, node->mScale)
                    * osg::Matrixf::rotate(rotation * facing * mModelRotations[rightHand].inverse());
                attachment.setTrans(mLeftHanded ? mSpearLeftPalm : osg::Vec3f(node->getMatrix().getTrans()));
            }
            const osg::Matrixf weaponModel = attachment * mModelMatrices[rightHand];
            const float scale = osg::Matrixf::transform3x3(osg::Vec3f(0, 1, 0), weaponModel).length();
            if (!std::isfinite(scale) || scale < 1e-5f)
                return;
            bool changed = std::abs(scale - mSwordGeometryScale) > 1e-4f * std::max(scale, mSwordGeometryScale)
                || mSwordGeometry.size() != node->getNumChildren();
            for (unsigned int i = 0; !changed && i < node->getNumChildren(); ++i)
                changed = mSwordGeometry[i].get() != node->getChild(i);
            if (changed)
            {
                // Inspect the equipped model in attachment space, once per
                // model/scale. This accommodates short and offset sword hilts.
                GripGeometryVisitor geometry;
                mSwordGeometry.clear();
                for (unsigned int i = 0; i < node->getNumChildren(); ++i)
                {
                    auto* child = node->getChild(i);
                    mSwordGeometry.emplace_back(child);
                    child->accept(geometry);
                }
                mSwordGeometryScale = scale;
                mSwordContact = EldenPose::findSwordSupportGrip(
                    geometry.mTriangles, -mSwordGripDistance / scale, mSwordPalmHalfWidth / scale);
            }
            if (!mSwordContact)
                return;

            // Mirror the native right-hand attachment into the left hand's
            // own bind frame. This locates the palm, rather than the wrist,
            // on the handle without assuming a particular race's bone axes.
            osg::Vec3f grip = osg::Matrixf::transform3x3(attachment.getTrans(), mBones[rightHand].mRestModel);
            grip -= mBindRight * (2.f * (grip * mBindRight));
            grip = osg::Matrixf::transform3x3(grip, osg::Matrixf::inverse(mBones[hand].mRestModel));
            if (mLeftHanded)
                grip = mSpearRightPalm;
            const osg::Vec3f root = mModelMatrices[upper].getTrans();
            const osg::Vec3f elbow = mModelMatrices[forearm].getTrans();
            const osg::Vec3f wrist = mModelMatrices[hand].getTrans();
            const osg::Vec3f desired = *mSwordContact * weaponModel
                - osg::Matrixf::transform3x3(grip, mModelMatrices[hand]);
            const auto solved = EldenPose::solveTwoBone(root, elbow, wrist, wrist + (desired - wrist) * blend);
            osg::Quat upperTurn, forearmTurn;
            upperTurn.makeRotate(elbow - root, solved.mJoint - root);
            forearmTurn.makeRotate(wrist - elbow, solved.mEnd - solved.mJoint);
            const osg::Quat upperRotation = mModelRotations[upper] * upperTurn;
            const osg::Quat forearmRotation = mModelRotations[forearm] * forearmTurn;
            const osg::Quat parentRotation = mBones[upper].mParent == sNoParent
                ? osg::Quat() : mModelRotations[mBones[upper].mParent];
            mBones[upper].mShown.mRotation = upperRotation * parentRotation.inverse();
            mBones[forearm].mShown.mRotation = forearmRotation * upperRotation.inverse();
            mBones[hand].mShown.mRotation = mModelRotations[hand] * forearmRotation.inverse();
            // Translations, segment lengths and the hand's model orientation
            // stay intact. Finger locals therefore retain their imported curl.
        }

        NifOsg::MatrixTransform* heldWeaponNode() const
        {
            return mLeftHanded ? mShieldNode.get() : mWeaponNode.get();
        }

        const osg::NodePath& heldWeaponParents() const
        {
            return mLeftHanded ? mShieldParents : mWeaponParents;
        }

        std::optional<osg::Vec3f> spearContact()
        {
            auto* node = heldWeaponNode();
            bool changed = mSpearGeometry.size() != node->getNumChildren();
            for (unsigned int i = 0; !changed && i < node->getNumChildren(); ++i)
                changed = mSpearGeometry[i].get() != node->getChild(i);
            if (changed)
            {
                GripGeometryVisitor geometry;
                mSpearGeometry.clear();
                for (unsigned int i = 0; i < node->getNumChildren(); ++i)
                {
                    auto* child = node->getChild(i);
                    mSpearGeometry.emplace_back(child);
                    child->accept(geometry);
                }
                for (auto& triangle : geometry.mTriangles)
                    for (auto& vertex : triangle)
                        vertex = mWeaponBasis * vertex;
                mSpearContact = EldenPose::findSpearGrip(geometry.mTriangles);
                if (mSpearContact)
                {
                    float low = mSpearContact->y(), high = low;
                    for (const auto& triangle : geometry.mTriangles)
                        for (const auto& vertex : triangle)
                        {
                            low = std::min(low, vertex.y());
                            high = std::max(high, vertex.y());
                        }
                    // Leave a small palm margin above the butt when the pose's
                    // hand spacing is wider than the lower part of the shaft.
                    mSpearRearTravel = std::max(0.f, mSpearContact->y() - low - .04f * (high - low));
                    mSpearContact = mWeaponBasis.inverse() * *mSpearContact;
                }
            }
            return mSpearContact;
        }

        void applyWeapon()
        {
            auto* node = heldWeaponNode();
            const osg::Matrixf parentMatrix = osg::computeLocalToWorld(heldWeaponParents());
            osg::Vec3f translation, scale;
            osg::Quat parent, scaleOrientation;
            parentMatrix.decompose(translation, parent, scale, scaleOrientation);
            osg::Quat postPose;
            const osg::Matrixf handMatrix = osg::computeLocalToWorld(
                mLeftHanded ? mShieldPoseParents : mWeaponPoseParents);
            const std::size_t poseParent = mLeftHanded ? mShieldPoseParent : mWeaponPoseParent;
            if (poseParent != sNoParent)
            {
                osg::Quat displayed;
                handMatrix.decompose(translation, displayed, scale, scaleOrientation);
                // Preserve first-person neck aiming without storing it in the
                // animation blend, where repeated changes would accumulate it.
                postPose = mModelRotations[poseParent].inverse() * displayed;
            }
            const osg::Quat facing(mFacing.shown(), osg::Vec3f(0, 0, 1));
            mNativeWeaponRotation = node->mRotationScale.toOsgMatrix().getRotate();
            mNativeWeaponTranslation = node->getMatrix().getTrans();
            mOverriddenWeaponNode = node;
            if (!mHaveWeaponBlendFrom)
            {
                mWeaponBlendFrom = mNativeWeaponRotation * parent * postPose.inverse() * facing.inverse();
                mHaveWeaponBlendFrom = true;
            }
            mShownWeaponRotation.slerp(mWeaponBlend, mWeaponBlendFrom, mWeaponTargetRotation);
            mShownWeaponRotation *= facing;
            node->setRotation(mShownWeaponRotation * postPose * parent.inverse());
            const osg::Vec3f palm = mLeftHanded
                ? (mSpearLeftPalm * osg::computeLocalToWorld(mSpearLeftParents))
                    * osg::Matrixf::inverse(parentMatrix)
                : (mEmbeddedSpear
                    ? (mEmbeddedSpearPalm * handMatrix) * osg::Matrixf::inverse(parentMatrix)
                    : mNativeWeaponTranslation);
            if (mAlignLeftWeapon)
                node->setTranslation(palm);
            if (const auto contact = mAlignSpear ? spearContact() : std::nullopt)
            {
                osg::Vec3f placement = palm - osg::Matrixf::transform3x3(*contact, node->getMatrix());
                if (mTwoHandedSpear)
                {
                    // A two-handed spear's right hand is normally the rear
                    // hand. Placing it at 40% left the front hand above
                    // the midpoint. Keep the foremost palm at the measured
                    // section, preserving the pose's changing hand spacing.
                    // Very short shafts and wide sliding thrusts must first
                    // keep the rear palm on the weapon, not beyond its butt.
                    const osg::Vec3f supportPalm = ((mLeftHanded ? mSpearRightPalm : mSpearLeftPalm)
                        * osg::computeLocalToWorld(mLeftHanded ? mSpearRightParents : mSpearLeftParents))
                        * osg::Matrixf::inverse(parentMatrix);
                    osg::Vec3f shaft = osg::Matrixf::transform3x3(
                        mWeaponBasis.inverse() * osg::Vec3f(0, 1, 0), node->getMatrix());
                    const float weaponScale = shaft.normalize();
                    const float spacing = (supportPalm - palm) * shaft;
                    const float shift = std::min(std::max(0.f, spacing),
                        mSpearRearTravel * weaponScale + std::min(0.f, spacing));
                    placement += shaft * shift;
                }
                node->setTranslation(placement);
            }
            mWeaponOverridden = true;
            mHaveShownWeapon = true;
        }

        void restoreWeapon()
        {
            if (mWeaponOverridden)
            {
                mOverriddenWeaponNode->setRotation(mNativeWeaponRotation);
                mOverriddenWeaponNode->setTranslation(mNativeWeaponTranslation);
                mWeaponOverridden = false;
            }
        }

        void restoreShield()
        {
            if (mShieldOverridden)
            {
                mShieldNode->setRotation(mNativeShieldRotation);
                mShieldNode->setTranslation(mNativeShieldTranslation);
                mShieldOverridden = false;
            }
        }

        void applyOffhandWeapon()
        {
            const osg::Matrixf parentMatrix = osg::computeLocalToWorld(mShieldParents);
            osg::Vec3f translation, scale;
            osg::Quat parent, scaleOrientation;
            parentMatrix.decompose(translation, parent, scale, scaleOrientation);
            osg::Quat postPose;
            if (mShieldPoseParent != sNoParent)
            {
                osg::Quat displayed;
                osg::computeLocalToWorld(mShieldPoseParents).decompose(translation, displayed, scale, scaleOrientation);
                postPose = mModelRotations[mShieldPoseParent].inverse() * displayed;
            }
            const osg::Quat facing(mFacing.shown(), osg::Vec3f(0, 0, 1));
            mNativeShieldRotation = mShieldNode->mRotationScale.toOsgMatrix().getRotate();
            mNativeShieldTranslation = mShieldNode->getMatrix().getTrans();
            if (!mHaveShieldBlendFrom)
            {
                mShieldBlendFrom = mNativeShieldRotation * parent * postPose.inverse() * facing.inverse();
                mHaveShieldBlendFrom = true;
            }
            mShownShieldRotation.slerp(mShieldBlend, mShieldBlendFrom, mShieldTargetRotation);
            mShownShieldRotation *= facing;
            mShieldNode->setRotation(mShownShieldRotation * postPose * parent.inverse());
            const osg::Vec3f palm = (mSpearLeftPalm * osg::computeLocalToWorld(mSpearLeftParents))
                * osg::Matrixf::inverse(parentMatrix);
            osg::Vec3f placement = palm;
            if (mAlignOffhandSpear)
            {
                bool changed = mOffhandGeometry.size() != mShieldNode->getNumChildren();
                for (unsigned int i = 0; !changed && i < mShieldNode->getNumChildren(); ++i)
                    changed = mOffhandGeometry[i].get() != mShieldNode->getChild(i);
                if (changed)
                {
                    GripGeometryVisitor geometry;
                    mOffhandGeometry.clear();
                    for (unsigned int i = 0; i < mShieldNode->getNumChildren(); ++i)
                    {
                        auto* child = mShieldNode->getChild(i);
                        mOffhandGeometry.emplace_back(child);
                        child->accept(geometry);
                    }
                    mOffhandSpearContact = EldenPose::findSpearGrip(geometry.mTriangles);
                }
                if (mOffhandSpearContact)
                    placement -= osg::Matrixf::transform3x3(*mOffhandSpearContact, mShieldNode->getMatrix());
            }
            mShieldNode->setTranslation(placement);
            mShieldOverridden = true;
            mHaveShownShield = true;
        }

        void grip(std::string_view name, bool slidingGrip, bool preserveLeftGrip = false,
            bool leftSlidingGrip = false)
        {
            const auto* clip = mBank->find(name);
            if (!clip)
                return;
            mBank->sample(*clip, 0.f, false, mLayer);
            for (bool left : { true, false })
            {
                if (left && preserveLeftGrip)
                    continue;
                const std::size_t finger = left ? LFinger0 : RFinger0;
                const std::size_t weapon = left ? LWeapon : RWeapon;
                const auto axes = mBank->axesOffset(sJointNames[weapon]);
                if (!axes)
                    continue;
                // Spear attacks slide a hand along the shaft. The weapon pivot
                // can move over 30 cm from the wrist; moving knuckles around
                // that pivot stretches the hand and rotates its inferred frame.
                // Carry the stance's curl from the animated wrist instead.
                const bool sliding = slidingGrip || (left && leftSlidingGrip);
                const std::size_t origin = mOffsets[sliding ? (left ? LHand : RHand) : weapon];
                const auto vectorAt = [](const std::vector<float>& pose, std::size_t at) {
                    return osg::Vec3f(pose[at], pose[at + 1], pose[at + 2]);
                };
                const osg::Vec3f before = vectorAt(mLayer, origin);
                const osg::Vec3f after = vectorAt(mSample, origin);
                for (std::size_t i = 0; i != 7; ++i)
                {
                    const std::size_t offset = mOffsets[finger + i];
                    const osg::Vec3f delta = vectorAt(mLayer, offset) - before;
                    osg::Vec3f position = after;
                    for (std::size_t axis = 0; axis != 3; ++axis)
                    {
                        const std::size_t at = *axes + axis * 3;
                        position += vectorAt(mSample, at) * (delta * vectorAt(mLayer, at));
                    }
                    std::copy_n(position.ptr(), 3, mSample.begin() + offset);
                }
            }
        }

        void overlay(std::string_view name, bool left, float frame, bool loop = true)
        {
            if (name.empty())
                return;
            const auto* clip = mBank->find(name);
            if (!clip)
                return;
            mBank->sample(*clip, frame, loop, mLayer);
            const std::size_t chest = mOffsets[Spine2];
            const std::size_t firstArm = left ? LClavicle : RClavicle;
            const std::size_t firstFinger = left ? LFinger0 : RFinger0;
            const std::size_t weapon = left ? LWeapon : RWeapon;
            const auto copyJoint = [&](std::size_t joint) {
                const std::size_t offset = mOffsets[joint];
                for (std::size_t axis = 0; axis != 3; ++axis)
                    mSample[offset + axis]
                        = mLayer[offset + axis] + mSample[chest + axis] - mLayer[chest + axis];
            };
            for (std::size_t i = 0; i != 4; ++i)
                copyJoint(firstArm + i);
            for (std::size_t i = 0; i != 7; ++i)
                copyJoint(firstFinger + i);
            copyJoint(weapon);
            if (const auto offset = mBank->axesOffset(sJointNames[weapon]))
                std::copy_n(mLayer.begin() + *offset, 9, mSample.begin() + *offset);
        }

        void initializeMirror()
        {
            const auto opposite = [](const std::string& name) {
                std::string result = name;
                if (name.starts_with("L_"))
                    result[0] = 'R';
                else if (name.starts_with("R_"))
                    result[0] = 'L';
                return result;
            };
            for (const auto& name : mBank->jointNames())
                if (const auto from = mBank->jointOffset(opposite(name)))
                    mMirrorOffsets.emplace_back(*mBank->jointOffset(name), *from);
            for (const auto& name : mBank->orientedNames())
                if (const auto from = mBank->axesOffset(opposite(name)))
                    for (std::size_t axis = 0; axis < 9; axis += 3)
                        mMirrorOffsets.emplace_back(*mBank->axesOffset(name) + axis, *from + axis);
        }

        void mirrorPose()
        {
            mMirrorSource = mSample;
            for (const auto [target, from] : mMirrorOffsets)
            {
                mSample[target] = -mMirrorSource[from];
                mSample[target + 1] = mMirrorSource[from + 1];
                mSample[target + 2] = mMirrorSource[from + 2];
            }
        }

        void restore()
        {
            restoreWeapon();
            restoreShield();
            restoreBow();
            detachBowArrow();
            mBow = false;
            mHaveBowFrom = false;
            mAlignSpear = false;
            mAlignWeapon = false;
            mAlignLeftWeapon = false;
            mHaveShownWeapon = false;
            mHaveWeaponBlendFrom = false;
            mAlignShield = false;
            mAlignOffhandWeapon = false;
            mAlignOffhandSpear = false;
            mHaveShownShield = false;
            mHaveShieldBlendFrom = false;
            mFacing.reset();
            if (!mEnabled)
                return;
            mEnabled = false;
            for (Bone& bone : mBones)
                bone.restoreNative();
            mClip.clear();
            mLeftArmClip.clear();
            mRightArmClip.clear();
            mGripClip.clear();
            mGripChangeClip.clear();
        }
    };

    EldenAnimation::EldenAnimation(const Animation::NodeMap& nodes, osg::Group& objectRoot,
        std::shared_ptr<const EldenCombat::AnimationBank> bank, bool requireFullBody, bool embeddedSpear)
        : mImpl(std::make_shared<Impl>())
    {
        auto& state = *mImpl;
        state.mSelf = mImpl;
        state.mBank = std::move(bank);
        if (!state.mBank)
            return;
        state.initializeMirror();
        for (std::size_t joint = 0; joint != JointCount; ++joint)
        {
            const auto offset = state.mBank->jointOffset(sJointNames[joint]);
            if (!offset)
                return;
            state.mOffsets[joint] = *offset;
        }
        state.mHeadAxes = state.mBank->axesOffset("Head");

        std::vector<std::pair<osg::MatrixTransform*, std::size_t>> mapped;
        const auto map = [&](std::size_t joint, std::initializer_list<std::string> names) {
            for (const std::string& name : names)
            {
                const auto found = nodes.find(name);
                if (found != nodes.end())
                {
                    mapped.emplace_back(found->second.get(), joint);
                    return;
                }
            }
        };
        map(Pelvis, { "bip01 pelvis" });
        map(Spine, { "bip01 spine" });
        map(Spine1, { "bip01 spine1" });
        map(Spine2, { "bip01 spine2" });
        map(Neck, { "bip01 neck" });
        map(Head, { "bip01 head" });
        for (bool left : { true, false })
        {
            const std::string prefix = left ? "bip01 l " : "bip01 r ";
            const std::size_t arm = left ? LClavicle : RClavicle;
            const std::size_t leg = left ? LThigh : RThigh;
            const std::size_t finger = left ? LFinger0 : RFinger0;
            map(arm, { prefix + "clavicle" });
            map(arm + 1, { prefix + "upperarm" });
            map(arm + 2, { prefix + "forearm" });
            map(arm + 3, { prefix + "hand" });
            map(leg, { prefix + "thigh" });
            map(leg + 1, { prefix + "calf" });
            map(leg + 2, { prefix + "foot" });
            map(leg + 3, { prefix + "toe0", prefix + "toe" });
            map(finger, { prefix + "finger0", prefix + "thumb1" });
            map(finger + 1, { prefix + "finger01", prefix + "thumb2" });
            map(finger + 2, { prefix + "finger02", prefix + "thumb3" });
            map(finger + 3, { prefix + "finger1", prefix + "pointer1" });
            map(finger + 4, { prefix + "finger2", prefix + "middle1" });
            map(finger + 5, { prefix + "finger21", prefix + "middle2" });
            map(finger + 6, { prefix + "finger22", prefix + "middle3" });
            // Stock Morrowind hands have a separate index-finger chain,
            // Finger1 -> Finger11, alongside the grouped Finger2 -> Finger21.
            // The bake has only the index knuckle; share the middle curl for
            // its remaining joints just as for replacement pointer bones.
            map(finger + 5, { prefix + "finger11" });
            map(finger + 6, { prefix + "finger12" });
            // The bake contains the middle finger's complete curl. Use it for
            // the remaining native fingers, whose individual joints were not
            // exported, while preserving each finger's own bind geometry.
            for (std::string_view digit : { "pointer", "ring", "pinky" })
            {
                if (digit != "pointer")
                    map(finger + 4, { prefix + std::string(digit) + "1" });
                map(finger + 5, { prefix + std::string(digit) + "2" });
                map(finger + 6, { prefix + std::string(digit) + "3" });
            }
        }
        if (mapped.size() < 8)
            return;

        std::unordered_map<osg::MatrixTransform*, std::size_t> index;
        for (const auto& [node, joint] : mapped)
        {
            // osgAnimation bones have a separate skeleton-space cache and
            // traversal protocol. Only NIF bones support this adapter.
            if (!dynamic_cast<NifOsg::MatrixTransform*>(node))
                return;
            const auto paths = node->getParentalNodePaths(&objectRoot);
            if (paths.empty())
                return;
            std::size_t parent = sNoParent;
            for (osg::Node* pathNode : paths.front())
            {
                auto* transform = dynamic_cast<osg::MatrixTransform*>(pathNode);
                if (!transform || pathNode == &objectRoot)
                    continue;
                auto found = index.find(transform);
                if (found == index.end())
                {
                    Impl::Bone bone;
                    bone.mNode = transform;
                    bone.mParent = parent;
                    bone.mRestMatrix = transform->getMatrix();
                    bone.mRest.mTranslation = bone.mRestMatrix.getTrans();
                    if (const auto* nif = dynamic_cast<NifOsg::MatrixTransform*>(transform))
                    {
                        bone.mScale = nif->mScale;
                        bone.mRest.mRotation = nif->mRotationScale.toOsgMatrix().getRotate();
                    }
                    else
                    {
                        osg::Vec3f scale;
                        osg::Quat scaleOrientation;
                        bone.mRestMatrix.decompose(
                            bone.mRest.mTranslation, bone.mRest.mRotation, scale, scaleOrientation);
                        bone.mScale = scale.x();
                    }
                    if (!std::isfinite(bone.mScale) || std::abs(bone.mScale) < 1e-6f)
                        return;
                    bone.mRestModel = bone.mRestMatrix;
                    bone.mRestModelRotation = bone.mRest.mRotation;
                    if (parent != sNoParent)
                    {
                        bone.mRestModel *= state.mBones[parent].mRestModel;
                        bone.mRestModelRotation *= state.mBones[parent].mRestModelRotation;
                    }
                    bone.mShown = bone.mFrom = bone.mRest;
                    found = index.emplace(transform, state.mBones.size()).first;
                    state.mBones.push_back(std::move(bone));
                }
                parent = found->second;
            }
            const auto found = index.find(node);
            if (found == index.end())
                return;
            Impl::Bone& bone = state.mBones[found->second];
            bone.mJoint = joint;
            if (!state.mHaveBindJoint[joint])
            {
                state.mBindJoints[joint] = bone.mRestModel.getTrans();
                state.mHaveBindJoint[joint] = true;
            }
        }
        if (!state.mHaveBindJoint[Pelvis] || !state.mHaveBindJoint[Neck]
            || (requireFullBody && !state.mHaveBindJoint[Head]))
            return;

        const auto nativeFingerDirection = [&](std::size_t index) -> osg::Vec3f {
            const Impl::Bone& bone = state.mBones[index];
            for (const Impl::Bone& child : state.mBones)
                if (child.mParent == index && child.mJoint >= LFinger0 && child.mJoint < JointCount)
                    return child.mRestModel.getTrans() - bone.mRestModel.getTrans();
            // A terminal native phalanx still has its own rest rotation.
            // Continue its chain's local bone axis through that rotation;
            // extrapolating the previous model-space segment discards its
            // existing knuckle bend, while a point near the wrist reverses it.
            return bone.mRestModelRotation * unit(bone.mRest.mTranslation, osg::Vec3f(1, 0, 0));
        };

        // A fully absent limb is normal on first-person rigs, but creatures
        // must have both human arms and legs. A partially present chain does
        // not define the bind segment directions needed for retargeting;
        // retain that rig's native animation instead of calibrating its
        // remaining bones against the origin.
        for (const std::array<std::size_t, 3>& chain : {
                 std::array<std::size_t, 3>{ LUpperArm, LForearm, LHand },
                 std::array<std::size_t, 3>{ RUpperArm, RForearm, RHand },
                 std::array<std::size_t, 3>{ LThigh, LCalf, LFoot },
                 std::array<std::size_t, 3>{ RThigh, RCalf, RFoot },
             })
        {
            const int present = int(state.mHaveBindJoint[chain[0]]) + int(state.mHaveBindJoint[chain[1]])
                + int(state.mHaveBindJoint[chain[2]]);
            if ((requireFullBody && present != 3) || (present != 0 && present != 3))
                return;
            if (requireFullBody)
                for (std::size_t j = 1; j != chain.size(); ++j)
                {
                    const float length = (state.mBindJoints[chain[j]] - state.mBindJoints[chain[j - 1]]).length();
                    if (!std::isfinite(length) || length < 1e-5f)
                        return;
                }
        }

        // Some first-person and replacement NIFs omit intermediate torso,
        // finger or leg bones. These virtual joints only define orientations;
        // no absent bone or mesh is created or translated.
        const osg::Vec3f torso = state.mBindJoints[Neck] - state.mBindJoints[Pelvis];
        if (requireFullBody && (!std::isfinite(torso.length()) || torso.length() < 1e-5f
            || torso.z() < .6f * torso.length()
            || state.mBindJoints[LFoot].z() >= state.mBindJoints[Pelvis].z()
            || state.mBindJoints[RFoot].z() >= state.mBindJoints[Pelvis].z()))
        {
            // Matching Bip01 labels are insufficient: rats and other native
            // quadrupeds share them. Player clips require an upright torso and
            // two legs below the hips, including replacement creature meshes.
            return;
        }
        for (std::size_t joint = Spine; joint <= Spine2; ++joint)
            if (!state.mHaveBindJoint[joint])
                state.mBindJoints[joint] = state.mBindJoints[Pelvis] + torso * (float(joint) / 4.f);
        if (!state.mHaveBindJoint[Head])
            state.mBindJoints[Head] = state.mBindJoints[Neck] + unit(torso, osg::Vec3f(0, 0, 1));
        for (bool left : { true, false })
        {
            const std::size_t arm = left ? LClavicle : RClavicle;
            const std::size_t leg = left ? LThigh : RThigh;
            const std::size_t finger = left ? LFinger0 : RFinger0;
            const float side = left ? -1.f : 1.f;
            if (!state.mHaveBindJoint[leg])
                state.mBindJoints[leg] = state.mBindJoints[Pelvis] + osg::Vec3f(side, 0, 0);
            if (!state.mHaveBindJoint[arm])
                state.mBindJoints[arm] = state.mBindJoints[Spine2];
            const osg::Vec3f hand = state.mBindJoints[arm + 3];
            const osg::Vec3f along = unit(hand - state.mBindJoints[arm + 2], osg::Vec3f(side, 0, 0));
            for (std::size_t j = 0; j != 7; ++j)
                if (!state.mHaveBindJoint[finger + j])
                    state.mBindJoints[finger + j]
                        = hand + along * (1.f + float(j % 3)) + osg::Vec3f(0, j < 3 ? -0.5f : 0.5f * float(j - 3), 0);
            for (std::size_t tip : { finger + 2, finger + 6 })
                if (!state.mHaveBindJoint[tip] && state.mHaveBindJoint[tip - 1])
                    for (std::size_t i = 0; i != state.mBones.size(); ++i)
                        if (state.mBones[i].mJoint == tip - 1)
                        {
                            state.mBindJoints[tip] = state.mBindJoints[tip - 1] + nativeFingerDirection(i);
                            break;
                        }
            if (!state.mHaveBindJoint[leg + 3])
                state.mBindJoints[leg + 3] = state.mBindJoints[leg + 2] + osg::Vec3f(0, 1, 0);
        }
        const AnatomicalFrame bindBody = anatomy(state.mBindJoints);
        state.mBindRight = bindBody.mRight;
        for (std::size_t i = 0; i != state.mBones.size(); ++i)
        {
            Impl::Bone& bone = state.mBones[i];
            if (bone.mJoint != sNoJoint)
            {
                if (bone.mJoint >= LFinger0)
                {
                    const std::size_t first = bone.mJoint < RFinger0 ? LFinger0 : RFinger0;
                    // Several native digits share the baked middle-finger
                    // curl. Calibrate each one's actual bind direction so its
                    // own rest bend/spread does not turn that curl outwards.
                    bone.mCorrection = bone.mRestModelRotation
                        * frameRotation(nativeFingerDirection(i),
                            state.mBindJoints[first + 3] - state.mBindJoints[first + 4]).inverse();
                    continue;
                }
                bone.mCorrection = bone.mRestModelRotation
                    * jointFrame(bone.mJoint, state.mBindJoints, bindBody, bindBody.mUp, bindBody.mChestForward).inverse();
            }
        }

        // Establish vertical scale from leg lengths, never from an animated
        // arm or from the absolute location of the model's origin.
        const auto* standing = state.mBank->find("a000_000000");
        if (!standing)
            standing = state.mBank->find("a000_020000");
        if (!standing)
            return;
        state.mBank->sample(*standing, 0.f, false, state.mSample);
        JointPositions reference;
        for (std::size_t j = 0; j != JointCount; ++j)
            reference[j] = fromSource(state.mSample, state.mOffsets[j]);
        const AnatomicalFrame referenceBody = anatomy(reference);
        for (std::size_t side = 0; side < state.mArmPlanes.size(); ++side)
        {
            const std::size_t arm = side == 0 ? LClavicle : RClavicle;
            const osg::Vec3f upper = reference[arm + 2] - reference[arm + 1];
            const osg::Vec3f lower = reference[arm + 3] - reference[arm + 2];
            // Source elbow flexion has a fixed sign. Calibrate it once in
            // neutral; choosing a new hemisphere during a swing can turn the
            // shoulder inside out while its deeply bent elbow stays still.
            state.mArmPlaneSigns[side] = (upper ^ lower) * (upper ^ referenceBody.mChestForward) < 0.f
                ? -1.f : 1.f;
        }
        for (Impl::Bone& bone : state.mBones)
            if (bone.mJoint == LFoot || bone.mJoint == LToe || bone.mJoint == RFoot || bone.mJoint == RToe)
            {
                const std::size_t foot = bone.mJoint == LFoot || bone.mJoint == LToe ? LFoot : RFoot;
                bone.mCorrection = EldenPose::footCorrection(bone.mRestModelRotation,
                    state.mBindJoints[foot + 1] - state.mBindJoints[foot],
                    jointFrame(bone.mJoint, reference, referenceBody, referenceBody.mUp, referenceBody.mChestForward));
            }
        const float sourceLeg = (reference[LThigh] - reference[LCalf]).length()
            + (reference[LCalf] - reference[LFoot]).length();
        const float targetLeg = (state.mBindJoints[LThigh] - state.mBindJoints[LCalf]).length()
            + (state.mBindJoints[LCalf] - state.mBindJoints[LFoot]).length();
        if (state.mHaveBindJoint[LThigh] && state.mHaveBindJoint[LCalf] && state.mHaveBindJoint[LFoot]
            && sourceLeg > 1e-5f)
            state.mScale = targetLeg / sourceLeg;
        else
            state.mScale = torso.length() / std::max((reference[Neck] - reference[Pelvis]).length(), 1e-5f);
        state.mSourcePelvisHeight = reference[Pelvis].z();
        for (std::size_t i = 0; i < state.mBones.size(); ++i)
        {
            const std::array<std::size_t, 4> arm{ LUpperArm, LForearm, LHand, RHand };
            constexpr std::array<std::size_t, 4> leftArm{ RUpperArm, RForearm, RHand, LHand };
            for (std::size_t j = 0; j < arm.size(); ++j)
            {
                if (state.mBones[i].mJoint == arm[j])
                    state.mSwordArm[j] = i;
                if (state.mBones[i].mJoint == leftArm[j])
                    state.mLeftSwordArm[j] = i;
            }
            constexpr std::array bowArm{ RUpperArm, RForearm, RHand };
            for (std::size_t j = 0; j < bowArm.size(); ++j)
                if (state.mBones[i].mJoint == bowArm[j])
                    state.mBowArm[j] = i;
        }
        if (const auto* ready = state.mBank->find("a012_000000"); ready
            && state.mHaveBindJoint[RFinger1] && state.mHaveBindJoint[RFinger2]
            && state.mHaveBindJoint[LFinger1] && state.mHaveBindJoint[LFinger2])
        {
            state.mBank->sample(*ready, 0.f, false, state.mSample);
            const float sourcePalm = (fromSource(state.mSample, state.mOffsets[RFinger2])
                - fromSource(state.mSample, state.mOffsets[RHand])).length();
            if (const auto axes = state.mBank->axesOffset("R_Weapon"); axes && sourcePalm > 1e-5f)
            {
                const float palmScale = (state.mBindJoints[RFinger2] - state.mBindJoints[RHand]).length() / sourcePalm;
                const osg::Vec3f separation = fromSource(state.mSample, state.mOffsets[RWeapon])
                    - fromSource(state.mSample, state.mOffsets[LWeapon]);
                state.mSwordGripDistance = (separation * unit(fromSource(state.mSample, *axes + 3), osg::Vec3f(0, 1, 0)))
                    * palmScale;
                state.mSwordPalmHalfWidth = (state.mBindJoints[RFinger1] - state.mBindJoints[RFinger2]).length();
            }
        }
        state.mModelRotations.resize(state.mBones.size());
        state.mModelMatrices.resize(state.mBones.size());
        state.mValid = true;
        for (std::size_t i = 0; i != state.mBones.size(); ++i)
        {
            for (bool before : { true, false })
            {
                osg::ref_ptr<Impl::Callback> callback = new Impl::Callback;
                callback->mState = mImpl;
                callback->mIndex = i;
                callback->mBeforeNative = before;
                auto& controllers = before ? mBeforeNativeControllers : mControllers;
                controllers.emplace_back(state.mBones[i].mNode, callback);
            }
        }

        state.mWeaponAxes = state.mBank->axesOffset("R_Weapon");
        if (const auto found = nodes.find(embeddedSpear ? "hircine_spear" : "weapon bone");
            found != nodes.end() && state.mWeaponAxes)
        {
            state.mWeaponNode = dynamic_cast<NifOsg::MatrixTransform*>(found->second.get());
            if (state.mWeaponNode)
            {
                const auto paths = state.mWeaponNode->getParentalNodePaths(&objectRoot);
                if (!paths.empty())
                {
                    state.mWeaponParents = paths.front();
                    state.mWeaponParents.pop_back();
                    std::erase(state.mWeaponParents, &objectRoot);
                    // A replacement rig may put extra attachment helpers below
                    // the hand. Compare the nearest retargeted ancestor with
                    // its displayed pose, keeping native helper transforms out
                    // of the view correction.
                    for (osg::Node* parent : state.mWeaponParents)
                    {
                        const auto poseParent = index.find(dynamic_cast<osg::MatrixTransform*>(parent));
                        if (poseParent != index.end())
                            state.mWeaponPoseParent = poseParent->second;
                    }
                    if (state.mWeaponPoseParent != sNoParent)
                    {
                        for (osg::Node* parent : state.mWeaponParents)
                        {
                            state.mWeaponPoseParents.push_back(parent);
                            if (parent == state.mBones[state.mWeaponPoseParent].mNode.get())
                                break;
                        }
                    }
                    if (embeddedSpear)
                    {
                        const auto hand = nodes.find("bip01 r hand");
                        if (hand == nodes.end() || !index.contains(hand->second.get()))
                        {
                            state.mValid = false;
                            return;
                        }
                        const auto handPaths = hand->second->getParentalNodePaths(&objectRoot);
                        if (handPaths.empty())
                        {
                            state.mValid = false;
                            return;
                        }
                        state.mWeaponPoseParent = index.at(hand->second.get());
                        state.mWeaponPoseParents = handPaths.front();
                        std::erase(state.mWeaponPoseParents, &objectRoot);
                        // Hircine's built-in spear points along +Z; ordinary
                        // equipped Morrowind spears point along +Y.
                        state.mWeaponBasis.makeRotate(osg::Vec3f(0, 0, 1), osg::Vec3f(0, 1, 0));
                        state.mEmbeddedSpearPalm = ((state.mBindJoints[RFinger1] + state.mBindJoints[RFinger2]) * .5f)
                            * osg::Matrixf::inverse(state.mBones[state.mWeaponPoseParent].mRestModel);
                        if (!state.spearContact())
                        {
                            state.mValid = false;
                            return;
                        }
                        state.mEmbeddedSpear = true;
                    }
                    if (const auto leftHand = nodes.find("bip01 l hand");
                        leftHand != nodes.end() && index.contains(leftHand->second.get())
                        && state.mHaveBindJoint[LFinger1] && state.mHaveBindJoint[LFinger2])
                    {
                        const auto leftPaths = leftHand->second->getParentalNodePaths(&objectRoot);
                        if (!leftPaths.empty())
                        {
                            state.mSpearLeftParents = leftPaths.front();
                            std::erase(state.mSpearLeftParents, &objectRoot);
                            state.mSpearLeftPalm
                                = ((state.mBindJoints[LFinger1] + state.mBindJoints[LFinger2]) * .5f)
                                * osg::Matrixf::inverse(state.mBones[index.at(leftHand->second.get())].mRestModel);
                        }
                    }
                    if (embeddedSpear && state.mSpearLeftParents.empty())
                    {
                        state.mValid = false;
                        return;
                    }
                    for (bool before : { true, false })
                    {
                        if (embeddedSpear && !before)
                            continue;
                        osg::ref_ptr<Impl::WeaponCallback> callback = new Impl::WeaponCallback;
                        callback->mState = mImpl;
                        callback->mBeforeNative = before;
                        auto& controllers = before ? mBeforeNativeControllers : mControllers;
                        controllers.emplace_back(state.mWeaponNode, callback);
                    }
                }
                else
                    state.mWeaponNode = nullptr;
            }
        }

        if (embeddedSpear && !state.mEmbeddedSpear)
        {
            state.mValid = false;
            return;
        }
        state.mShieldAxes = state.mBank->axesOffset("L_Weapon");
        if (const auto found = nodes.find("shield bone"); found != nodes.end() && state.mShieldAxes)
        {
            state.mShieldNode = dynamic_cast<NifOsg::MatrixTransform*>(found->second.get());
            if (state.mShieldNode)
            {
                const auto paths = state.mShieldNode->getParentalNodePaths(&objectRoot);
                if (!paths.empty())
                {
                    state.mShieldParents = paths.front();
                    state.mShieldParents.pop_back();
                    std::erase(state.mShieldParents, &objectRoot);
                    for (osg::Node* parent : state.mShieldParents)
                        if (const auto poseParent = index.find(dynamic_cast<osg::MatrixTransform*>(parent));
                            poseParent != index.end())
                            state.mShieldPoseParent = poseParent->second;
                    if (state.mShieldPoseParent != sNoParent)
                        for (osg::Node* parent : state.mShieldParents)
                        {
                            state.mShieldPoseParents.push_back(parent);
                            if (parent == state.mBones[state.mShieldPoseParent].mNode.get())
                                break;
                        }
                    for (bool before : { true, false })
                    {
                        osg::ref_ptr<Impl::ShieldCallback> callback = new Impl::ShieldCallback;
                        callback->mState = mImpl;
                        callback->mBeforeNative = before;
                        auto& controllers = before ? mBeforeNativeControllers : mControllers;
                        controllers.emplace_back(state.mShieldNode, callback);
                    }
                }
                else
                    state.mShieldNode = nullptr;
            }
        }
        if (const auto rightHand = nodes.find("bip01 r hand"); rightHand != nodes.end()
            && index.contains(rightHand->second.get()) && state.mHaveBindJoint[RFinger1]
            && state.mHaveBindJoint[RFinger2])
        {
            const auto paths = rightHand->second->getParentalNodePaths(&objectRoot);
            if (!paths.empty())
            {
                state.mSpearRightParents = paths.front();
                std::erase(state.mSpearRightParents, &objectRoot);
                state.mSpearRightPalm = ((state.mBindJoints[RFinger1] + state.mBindJoints[RFinger2]) * .5f)
                    * osg::Matrixf::inverse(state.mBones[index.at(rightHand->second.get())].mRestModel);
            }
        }
        if (state.mWeaponNode || state.mShieldNode)
        {
            osg::ref_ptr<Impl::SpearCallback> finish = new Impl::SpearCallback;
            finish->mState = mImpl;
            mControllers.emplace_back(&objectRoot, finish);
        }
        // Prefer the optional left attachment, matching native weapon lookup.
        // Original Morrowind skeletons use the right attachment for bows too.
        const auto left = nodes.find("bip01 l hand");
        const auto attachment = nodes.contains("weapon bone left")
            ? nodes.find("weapon bone left") : nodes.find("weapon bone");
        if (left != nodes.end() && attachment != nodes.end() && state.mShieldAxes)
        {
            state.mBowNode = dynamic_cast<NifOsg::MatrixTransform*>(attachment->second.get());
            const auto handPaths = left->second->getParentalNodePaths(&objectRoot);
            const auto bowPaths = attachment->second->getParentalNodePaths(&objectRoot);
            if (state.mBowNode && !handPaths.empty() && !bowPaths.empty())
            {
                state.mBowHandPath = handPaths.front();
                state.mBowParents = bowPaths.front();
                state.mBowParents.pop_back();
                std::erase(state.mBowHandPath, &objectRoot);
                std::erase(state.mBowParents, &objectRoot);
                if (attachment->first == "weapon bone left")
                    state.mBowPalm = state.mBowNode->getMatrix().getTrans();
                else if (state.mSwordArm[2] != sNoParent && state.mSwordArm[3] != sNoParent)
                {
                    const auto& leftHand = state.mBones[state.mSwordArm[2]];
                    const auto& rightHand = state.mBones[state.mSwordArm[3]];
                    osg::Vec3f grip = osg::Matrixf::transform3x3(
                        state.mBowNode->getMatrix().getTrans(), rightHand.mRestModel);
                    grip -= state.mBindRight * (2.f * (grip * state.mBindRight));
                    state.mBowPalm = osg::Matrixf::transform3x3(grip, osg::Matrixf::inverse(leftHand.mRestModel));
                }
                if (state.mSwordArm[3] != sNoParent)
                {
                    state.mBowDrawPalm = state.mWeaponNode
                        ? osg::Vec3f(state.mWeaponNode->getMatrix().getTrans()) : osg::Vec3f();
                    const auto rightPaths = state.mBones[state.mSwordArm[3]].mNode->getParentalNodePaths(&objectRoot);
                    if (!rightPaths.empty())
                    {
                        const auto& rightPath = rightPaths.front();
                        osg::Node* common = nullptr;
                        for (std::size_t i = 0; i < std::min(rightPath.size(), handPaths.front().size()); ++i)
                        {
                            if (rightPath[i] != handPaths.front()[i])
                                break;
                            if (rightPath[i] != &objectRoot)
                                common = rightPath[i];
                        }
                        if (common && common->asGroup())
                        {
                            osg::ref_ptr<Impl::BowFinishCallback> callback = new Impl::BowFinishCallback;
                            callback->mState = mImpl;
                            mControllers.emplace_back(common, callback);
                        }
                    }
                    if (const auto arrow = nodes.find("bip01 arrow"); arrow != nodes.end())
                        state.mSkeletonArrow = arrow->second;
                }
                osg::ref_ptr<Impl::BowHandCallback> handCallback = new Impl::BowHandCallback;
                handCallback->mState = mImpl;
                mControllers.emplace_back(left->second, handCallback);
                for (bool before : { true, false })
                {
                    osg::ref_ptr<Impl::BowCallback> callback = new Impl::BowCallback;
                    callback->mState = mImpl;
                    callback->mBeforeNative = before;
                    auto& controllers = before ? mBeforeNativeControllers : mControllers;
                    controllers.emplace_back(state.mBowNode, callback);
                }
            }
            else
                state.mBowNode = nullptr;
        }
    }

    EldenAnimation::~EldenAnimation()
    {
        clear();
    }

    bool EldenAnimation::hasEmbeddedSpear() const
    {
        return mImpl->mValid && mImpl->mEmbeddedSpear;
    }

    bool EldenAnimation::valid() const
    {
        return mImpl->mValid;
    }

    bool EldenAnimation::play(std::string_view clipName, float frame, bool loop, float duration, float yaw,
        std::string_view leftArmClip, std::string_view rightArmClip, std::string_view gripClip, bool alignSpear,
        bool twoHandedSword, bool upperBodyOnly, bool bow, float pitch, float bowDraw, bool leftHanded,
        bool offhandWeapon, bool offhandSpear, bool alignWeapon,
        std::string_view gripChange, float gripChangeFrame)
    {
        auto& state = *mImpl;
        if (!state.mValid || !std::isfinite(frame) || !std::isfinite(yaw) || !std::isfinite(pitch)
            || (bow && !state.mBowNode))
        {
            clear();
            return false;
        }
        const auto* clip = state.mBank->find(clipName);
        if (!clip)
        {
            clear();
            return false;
        }
        duration = std::isfinite(duration) ? std::max(duration, 0.f) : 0.f;
        const bool handChanged = state.mLeftHanded != leftHanded;
        if (handChanged)
        {
            state.restoreWeapon();
            state.mHaveShownWeapon = false;
            state.mHaveWeaponBlendFrom = false;
            state.mSpearGeometry.clear();
            state.mSpearContact.reset();
            state.mSwordGeometry.clear();
            state.mSwordContact.reset();
        }
        state.mLeftHanded = leftHanded;
        state.mBank->sample(*clip, frame, loop, state.mSample);
        state.mLayerFrame += duration * 30.f;
        state.overlay(leftArmClip, true, state.mLayerFrame);
        state.overlay(rightArmClip, false, state.mLayerFrame);
        alignSpear = alignSpear || state.mEmbeddedSpear;
        const bool shieldLayer = (leftArmClip == "a000_000100" && rightArmClip != leftArmClip)
            || leftArmClip == "a015_000100";
        // Left and paired attacks author the offhand weapon's own grip. The
        // right weapon's carry curl can rotate that palm away from its shaft.
        const bool offhandAttack = offhandWeapon && !leftHanded && clipName.size() >= 8
            && (clipName.substr(5, 3) == "034" || clipName.substr(5, 3) == "035");
        state.grip(gripClip, alignSpear, shieldLayer || offhandAttack, offhandSpear);
        const bool shieldStance = clipName.starts_with("a015_") || gripClip.starts_with("a015_");
        // Weapon action tables share right-led clips. Reflection swaps both
        // anatomical sides and oriented equipment axes. Shield and grip-swap
        // clips already author the left arm and must retain their source pose.
        if (leftHanded && !shieldStance && !clipName.starts_with("a000_029"))
            state.mirrorPose();
        // Grip changes have their own hand-specific source clips. Layer their
        // exact action clock over the facing and legs of the current gait.
        if (!gripChange.empty() && std::isfinite(gripChangeFrame))
        {
            state.overlay(gripChange, true, gripChangeFrame, false);
            state.overlay(gripChange, false, gripChangeFrame, false);
        }
        const auto weaponAxes = leftHanded ? state.mShieldAxes : state.mWeaponAxes;
        const bool spear = alignSpear && state.heldWeaponNode() && weaponAxes;
        const bool weapon = alignWeapon && !leftHanded && state.mWeaponNode && state.mWeaponAxes;
        const bool leftWeapon = leftHanded && !shieldStance && state.mShieldNode
            && state.mShieldAxes && !state.mSpearLeftParents.empty();
        const bool offhand = offhandWeapon && !leftHanded && state.mShieldNode
            && state.mShieldAxes && !state.mSpearLeftParents.empty();
        // Casts and death poses leave the other hand free, even when Guile's
        // spear is embedded. Only the two-handed stance owns both palms.
        state.mTwoHandedSpear = spear
            && !(leftHanded ? state.mSpearRightParents : state.mSpearLeftParents).empty()
            && gripClip == "a013_000000";
        // Shield guards supply these arm layers. Other two-handed weapons
        // have their own guard layers; their attachments stay native.
        const bool shield = ((shieldLayer && state.mBank->find(leftArmClip)) || (leftHanded && shieldStance))
            && state.mShieldNode && state.mShieldAxes && !offhand;
        // Buffered rolls and other repeated actions can restart the same clip.
        // Blend from the displayed pose when its action clock resets, while
        // ordinary gait loop seams and a frozen action retain their blend.
        const bool restarted = !loop && !state.mLoop && frame < state.mFrame;
        const bool clipChanged = state.mClip != clipName || restarted;
        const bool changed = clipChanged || state.mLeftArmClip != leftArmClip
            || state.mRightArmClip != rightArmClip || state.mGripClip != gripClip || state.mAlignSpear != spear
            || state.mAlignWeapon != weapon
            || state.mAlignShield != shield || state.mUpperBodyOnly != upperBodyOnly || state.mBow != bow
            || handChanged || state.mAlignLeftWeapon != leftWeapon || state.mAlignOffhandWeapon != offhand
            || state.mAlignOffhandSpear != (offhand && offhandSpear)
            || state.mGripChangeClip != gripChange
            || (!gripChange.empty() && gripChangeFrame < state.mGripChangeFrame);
        state.mFrame = frame;
        state.mGripChangeFrame = gripChangeFrame;
        state.mLoop = loop;
        if (!spear && !leftWeapon && !weapon)
        {
            state.restoreWeapon();
            state.mHaveShownWeapon = false;
            state.mHaveWeaponBlendFrom = false;
        }
        if (!bow)
        {
            state.restoreBow();
            state.detachBowArrow();
            state.mHaveBowFrom = false;
        }
        const bool bowReleased = state.mBow && state.mBowDraw > 0.f && bowDraw <= 0.f;
        state.mBow = bow;
        state.mBowDraw = std::isfinite(bowDraw) ? std::clamp(bowDraw, 0.f, 1.f) : 0.f;
        state.mAlignSpear = spear;
        state.mAlignWeapon = weapon;
        state.mAlignShield = shield;
        state.mAlignOffhandWeapon = offhand;
        state.mAlignOffhandSpear = offhand && offhandSpear;
        state.mAlignLeftWeapon = leftWeapon;
        if (leftWeapon)
        {
            state.restoreShield();
            state.mHaveShownShield = false;
            state.mHaveShieldBlendFrom = false;
        }
        if (changed)
        {
            for (auto& plane : state.mLimbPlanes)
                plane.reset();
            // A new clip immediately supplies its calibrated signed elbow
            // plane; the displayed pose crossfade handles the transition.
            // Never select a fresh torso-facing hemisphere for that elbow.
            for (auto& plane : state.mArmPlanes)
                if (!state.mEnabled)
                    plane.reset();
                else
                    plane.restart();
            state.mFacing.start(yaw);
            const osg::Quat inverseFacing = osg::Quat(state.mFacing.from(), osg::Vec3f(0, 0, 1)).inverse();
            if (state.mHaveBowFrom)
            {
                state.mBowFromRotation = state.mShownBowRotation * inverseFacing;
                state.mBowFromPosition = state.mShownBowPosition;
            }
            state.mHaveWeaponBlendFrom = state.mHaveShownWeapon;
            if (state.mHaveShownWeapon)
                state.mWeaponBlendFrom = state.mShownWeaponRotation * inverseFacing;
            state.mHaveShieldBlendFrom = state.mHaveShownShield;
            if (state.mHaveShownShield)
                state.mShieldBlendFrom = state.mShownShieldRotation * inverseFacing;
            state.mClip = clipName;
            state.mLeftArmClip = leftArmClip;
            state.mRightArmClip = rightArmClip;
            state.mGripClip = gripClip;
            state.mGripChangeClip = gripChange;
            state.mFade = 0.f;
            state.mFadeLength = clipChanged ? clip->mBlendFrames / 30.f : 0.12f;
            if (bowReleased)
                state.mFadeLength = std::max(state.mFadeLength, .1f);
            for (Impl::Bone& bone : state.mBones)
            {
                if (state.mEnabled && (!state.mUpperBodyOnly || upperBodyJoint(bone.mJoint)))
                    bone.mFrom = bone.mShown;
                else
                {
                    bone.mFrom.mTranslation = bone.mNode->getMatrix().getTrans();
                    if (const auto* nif = dynamic_cast<const NifOsg::MatrixTransform*>(bone.mNode.get()))
                        bone.mFrom.mRotation = nif->mRotationScale.toOsgMatrix().getRotate();
                    else
                        bone.mFrom.mRotation = bone.mRest.mRotation;
                }
                const osg::Quat parentRotation
                    = bone.mParent == sNoParent ? inverseFacing : state.mBones[bone.mParent].mFromModelRotation;
                bone.mFromModelRotation = bone.mFrom.mRotation * parentRotation;
            }
        }
        state.mFade += duration;
        float blend = state.mFadeLength > 0.f ? std::clamp(state.mFade / state.mFadeLength, 0.f, 1.f) : 1.f;
        blend = blend * blend * (3.f - 2.f * blend);
        const osg::Quat facing(state.mFacing.update(yaw, blend), osg::Vec3f(0, 0, 1));
        if (spear || leftWeapon || weapon)
        {
            state.mWeaponBlend = blend;
            state.mWeaponTargetRotation = state.mWeaponBasis * frameRotation(fromSource(state.mSample, *weaponAxes),
                fromSource(state.mSample, *weaponAxes + 3));
        }
        if (shield || offhand || state.mHaveShownShield)
            state.mShieldBlend = blend;
        if (offhand)
            // The authored left socket points its +Y toward the little finger;
            // native weapon meshes point +Y from the grip toward the blade.
            // Turn the model about local X, keeping its edge toward the knuckles.
            state.mShieldTargetRotation = frameRotation(fromSource(state.mSample, *state.mShieldAxes),
                -fromSource(state.mSample, *state.mShieldAxes + 3));
        else if (shield)
        {
            // Native shields lie in local XY, with +Y towards the top and
            // +Z out of the face. The source shield uses +X up and -Z out;
            // rebuild the third axis to account for ERAN's reflected basis.
            state.mShieldTargetRotation = frameRotation(-fromSource(state.mSample, *state.mShieldAxes + 3),
                fromSource(state.mSample, *state.mShieldAxes));
        }
        const osg::Quat aim(bow ? pitch : 0.f, osg::Vec3f(-1, 0, 0));
        if (bow)
        {
            state.mBowBlend = blend;
            // Native bows extend along +Y, with arrows flying towards +X.
            // The imported left-weapon frame uses +X forward and -Y up.
            state.mBowTargetRotation = frameRotation(fromSource(state.mSample, *state.mShieldAxes),
                -fromSource(state.mSample, *state.mShieldAxes + 3)) * aim;
        }
        JointPositions positions;
        for (std::size_t j = 0; j != JointCount; ++j)
            positions[j] = fromSource(state.mSample, state.mOffsets[j]);
        const AnatomicalFrame body = anatomy(positions);
        std::array<osg::Vec3f, 10> limbPlanes;
        for (std::size_t i = 0; i != state.mArmPlanes.size(); ++i)
        {
            const std::size_t arm = i == 0 ? LClavicle : RClavicle;
            const osg::Vec3f upper = positions[arm + 2] - positions[arm + 1];
            const osg::Vec3f lower = positions[arm + 3] - positions[arm + 2];
            limbPlanes[i * 2] = state.mArmPlanes[i].secondary(upper, lower, upper,
                upper ^ body.mChestForward, duration, state.mArmPlaneSigns[i]);
            limbPlanes[i * 2 + 1] = unit(upper ^ limbPlanes[i * 2], body.mRight) ^ lower;
        }
        for (std::size_t i = 0; i != 4; ++i)
        {
            const std::size_t leg = i < 2 ? LThigh : RThigh;
            const osg::Vec3f upper = positions[leg + 1] - positions[leg];
            const osg::Vec3f lower = positions[leg + 2] - positions[leg + 1];
            const osg::Vec3f primary = i % 2 == 0 ? upper : lower;
            limbPlanes[i + 4] = state.mLimbPlanes[i].secondary(upper, lower, primary, -body.mRight, duration);
        }
        for (std::size_t i = 0; i != 2; ++i)
        {
            const std::size_t foot = i == 0 ? LFoot : RFoot;
            const osg::Vec3f primary = positions[foot + 1] - positions[foot];
            limbPlanes[i + 8]
                = state.mLimbPlanes[i + 4].secondary(primary, body.mRight ^ primary, primary, body.mRight, duration);
        }
        const osg::Vec3f headUp = state.mHeadAxes ? fromSource(state.mSample, *state.mHeadAxes) : body.mUp;
        const osg::Vec3f headForward
            = state.mHeadAxes ? fromSource(state.mSample, *state.mHeadAxes + 6) : body.mChestForward;
        for (std::size_t i = 0; i != state.mBones.size(); ++i)
        {
            Impl::Bone& bone = state.mBones[i];
            LocalPose pose = bone.mRest;
            const osg::Quat parentRotation = bone.mParent == sNoParent ? osg::Quat() : state.mModelRotations[bone.mParent];
            const osg::Matrixf parentMatrix
                = bone.mParent == sNoParent ? osg::Matrixf() : state.mModelMatrices[bone.mParent];
            if (bone.mJoint != sNoJoint)
            {
                const osg::Quat modelRotation
                    = bone.mCorrection * jointFrame(bone.mJoint, positions, body, headUp, headForward, &limbPlanes)
                    * (upperBodyJoint(bone.mJoint) ? aim : osg::Quat());
                osg::Quat shownModelRotation;
                shownModelRotation.slerp(blend, bone.mFromModelRotation, modelRotation);
                // A reversal has two equally short heading arcs. Choose that
                // turn once for the whole body, independently of each bone's
                // pose, so the chest and pelvis cannot take opposite paths.
                shownModelRotation *= facing;
                bone.mShownModelRotation = shownModelRotation;
                // Blend the displayed anatomical orientation once. Blending
                // every local joint independently compounds parent and child
                // turns, making the wrist/weapon spin during roll recovery.
                pose.mRotation = shownModelRotation * parentRotation.inverse();
                if (bone.mJoint == Pelvis)
                {
                    osg::Vec3f translation = bone.mRestModel.getTrans();
                    translation.z() += (positions[Pelvis].z() - state.mSourcePelvisHeight) * state.mScale;
                    pose.mTranslation = translation * osg::Matrixf::inverse(parentMatrix);
                }
                pose.mTranslation = bone.mFrom.mTranslation * (1.f - blend) + pose.mTranslation * blend;
                bone.mShown = pose;
            }
            state.mModelRotations[i] = pose.mRotation * parentRotation;
            osg::Matrixf local = bone.mRestMatrix;
            if (bone.mJoint != sNoJoint)
            {
                local = osg::Matrixf::scale(bone.mScale, bone.mScale, bone.mScale)
                    * osg::Matrixf::rotate(pose.mRotation);
                local.setTrans(pose.mTranslation);
            }
            state.mModelMatrices[i] = local * parentMatrix;
        }
        // The support hand must stay on the native handle throughout the
        // two-handed carry, including the sprint's lowered weapon. Attacks,
        // crouching, guard layers and other moves retain their authored poses.
        constexpr std::array swordCarryClips{
            "a012_000000", "a012_020000", "a012_020001", "a012_020002", "a012_020003",
            "a012_020100", "a012_020101", "a012_020102", "a012_020103", "a012_020200",
            "a012_022100", "a012_022101", "a012_022102", "a012_022103",
        };
        if (twoHandedSword && loop && gripClip == "a012_000000" && !alignSpear && !upperBodyOnly
            && leftArmClip.empty() && rightArmClip.empty() && gripChange.empty()
            && std::find(swordCarryClips.begin(), swordCarryClips.end(), clipName) != swordCarryClips.end())
            state.closeSwordGrip(blend);
        state.mEnabled = true;
        state.mUpperBodyOnly = upperBodyOnly;
        return true;
    }

    void EldenAnimation::clear()
    {
        mImpl->restore();
    }
}
