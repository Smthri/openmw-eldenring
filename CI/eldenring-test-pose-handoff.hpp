// Included by the production OSG callback harness.
namespace
{
    struct NativePoseProbe : SceneUtil::NodeCallback<NativePoseProbe, NifOsg::MatrixTransform*>
    {
        osg::Quat mRotation{ .37f, osg::Vec3f(0, 0, 1) };
        osg::Quat mObserved;
        osg::Vec3f mTranslation;
        float mScale = .93f;
        bool mWriteRotation = true;

        void operator()(NifOsg::MatrixTransform* node, osg::NodeVisitor* visitor)
        {
            // Native smooth transitions read the existing local rotation when
            // a group changes; translation-only tracks leave it untouched.
            mObserved = node->mRotationScale.toOsgMatrix().getRotate();
            if (mWriteRotation)
                node->setRotation(mRotation);
            node->setTranslation(mTranslation);
            node->setScale(mScale);
            traverse(node, visitor);
        }
    };

    void nativePoseHandoff(const std::shared_ptr<const EldenCombat::AnimationBank>& bank)
    {
        Rig rig(bank, false, true);
        std::vector<std::pair<NifOsg::MatrixTransform*, osg::ref_ptr<NativePoseProbe>>> probes;
        for (const char* name : { "bip01 pelvis", "bip01 l upperarm", "bip01 l forearm", "bip01 l hand",
                 "bip01 r upperarm", "bip01 r forearm", "bip01 r hand" })
        {
            auto* node = static_cast<NifOsg::MatrixTransform*>(rig.mNodes.at(name).get());
            osg::ref_ptr<NativePoseProbe> probe = new NativePoseProbe;
            probe->mTranslation = osg::Vec3f(node->getMatrix().getTrans()) + osg::Vec3f(.3f, -.7f, .1f);
            auto* before = node->getUpdateCallback();
            probe->setNestedCallback(before->getNestedCallback());
            before->setNestedCallback(probe);
            probes.emplace_back(node, probe);
        }
        rig.update();
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            for (const auto& [node, probe] : probes)
                probe->mWriteRotation = false;
            for (int frame = 0; frame < 5; ++frame)
            {
                check(rig.mAnimation->play("a000_000000", float(frame), true, .1f, .8f,
                          {}, {}, "a000_000000", false, false, false, false, 0.f, 0.f, false, true),
                    "dual-wield native handoff fixture rejected");
                rig.update();
                for (const auto& [node, probe] : probes)
                    check(close(probe->mObserved, probe->mRotation),
                        "native blend origin or partial track inherited an imported bone rotation");
            }
            rig.mAnimation->clear();
            for (const auto& [node, probe] : probes)
            {
                check(close(node->mRotationScale.toOsgMatrix().getRotate(), probe->mRotation),
                    "draw/sheath handoff reset the native animation rotation to bind pose");
                check(close(node->getMatrix().getTrans(), probe->mTranslation),
                    "draw/sheath handoff reset native animation translation");
                check(std::abs(node->mScale - probe->mScale) < 1e-5f,
                    "draw/sheath handoff reset native animation scale");
                probe->mRotation = osg::Quat(.2f * (cycle + 1), osg::Vec3f(1, 0, 0));
                probe->mWriteRotation = true;
            }
            rig.update();
        }
    }

    void swordAxisAlignment(const std::shared_ptr<const EldenCombat::AnimationBank>& bank, bool actual)
    {
        const std::vector<std::string_view> clips = actual
            ? std::vector<std::string_view>{ "a023_030000", "a023_030010", "a023_030020",
                "a023_030300", "a023_030500", "a023_032500", "a012_000000" }
            : std::vector<std::string_view>{ "a000_000000", "a036_032505" };
        const auto axes = *bank->axesOffset("R_Weapon");
        std::vector<float> sample;
        for (bool helper : { false, true })
        {
            Rig rig(bank, helper, true);
            osg::ref_ptr<FirstPersonNeck> neck = new FirstPersonNeck;
            neck->mRoot = rig.mRoot;
            rig.mNodes.at("bip01 neck")->addUpdateCallback(neck);
            for (const auto clipName : clips)
            {
                const auto* clip = bank->find(clipName);
                check(clip != nullptr, "sword axis regression clip missing");
                for (float frame = 0.f; frame < clip->mFrames; frame += 1.f)
                    for (float yaw : { 0.f, .7f })
                        for (float pitch : { -.6f, 0.f, .6f })
                        {
                            neck->mPitch = osg::Quat(pitch, osg::Vec3f(-1, 0, 0));
                            check(rig.mAnimation->play(clipName, frame, false, 1.f, yaw,
                                      {}, {}, "a000_000000", false, false, false, false, 0.f, 0.f,
                                      false, false, false, true), "sword source alignment rejected");
                            rig.update();
                            bank->sample(*clip, frame, false, sample);
                            const osg::Vec3f source(-sample[axes + 3], sample[axes + 5], sample[axes + 4]);
                            const osg::Quat facing(yaw, osg::Vec3f(0, 0, 1));
                            check(close(rig.tip(), neck->mPitch * (facing * source)),
                                "sword thrust retained a sideways native attachment offset");
                            check(close(rig.mWeapon->getMatrix().getTrans(), rig.mNative->mTranslation),
                                "sword axis alignment moved its native palm attachment");
                            check(std::abs(rig.mWeapon->mScale - rig.mNative->mScale) < 1e-5f,
                                "sword axis alignment changed its model scale");
                        }
            }
            rig.mAnimation->clear();
            check(close(rig.mWeapon->mRotationScale.toOsgMatrix().getRotate(), rig.mNative->mRotation),
                "sword alignment failed to restore its native attachment");
        }
    }

    void timedGripLayers(const std::shared_ptr<const EldenCombat::AnimationBank>& bank)
    {
        for (bool leftHanded : { false, true })
            for (float yaw : { -.8f, 0.f, .8f })
                for (const std::string_view grip : { "a000_029010", "a000_029020", "a000_029040",
                         "a000_029050", "a000_029060", "a000_029080" })
                {
                    Rig layered(bank, false, true), base(bank, false, true), direct(bank, false, true);
                    const auto* clip = bank->find(grip);
                    check(clip, "grip-change source clip missing");
                    for (float frame : { 0.f, 2.f, float(clip->mFrames - 1) })
                    {
                        const auto play = [&](Rig& rig, std::string_view main, std::string_view overlay,
                                              float duration, bool reflected) {
                            check(rig.mAnimation->play(main, main == grip ? frame : 7.f, main != grip, duration, yaw,
                                      {}, {}, main == grip ? "" : "a000_000000", false, false, false, false, 0.f, 0.f,
                                      reflected, false, false, false, overlay, frame), "timed grip layer rejected");
                            rig.update();
                        };
                        play(base, "a000_020100", {}, 1.f, leftHanded);
                        play(layered, "a000_020100", grip, 1.f, leftHanded);
                        play(direct, grip, {}, 1.f, false);
                        for (const char* name : { "bip01 pelvis", "bip01 l thigh", "bip01 l calf",
                                 "bip01 l foot", "bip01 r thigh", "bip01 r calf", "bip01 r foot" })
                        {
                            check(close(layered.modelRotation(layered.mNodes.at(name)),
                                      base.modelRotation(base.mNodes.at(name))),
                                "grip change replaced movement or remembered facing");
                            check(close(layered.modelMatrix(layered.mNodes.at(name)).getTrans(),
                                      base.modelMatrix(base.mNodes.at(name)).getTrans()),
                                "grip change moved the locomotion skeleton");
                        }
                        // Palm orientation follows the hand-specific clip at the
                        // supplied frame, without a second left-side reflection.
                        for (const char* name : { "bip01 l hand", "bip01 r hand" })
                            check(close(layered.modelRotation(layered.mNodes.at(name)),
                                      direct.modelRotation(direct.mNodes.at(name))),
                                "grip overlay lost its exact clock or reflected an authored hand");
                    }
                }
    }
}
