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

    void spearGuardSupport(const std::shared_ptr<const EldenCombat::AnimationBank>& bank)
    {
        float maximumError = 0.f;
        for (bool leftHanded : { false, true })
            for (float yaw : { -.7f, 0.f, .7f })
            {
                Rig rig(bank, false, true);
                auto* support = leftHanded ? rig.mHand.get() : rig.mNodes.at("bip01 l hand").get();
                auto* weapon = leftHanded ? rig.mShield.get() : rig.mWeapon.get();
                const std::string side = leftHanded ? "r" : "l";
                const osg::Vec3f palm = ((rig.modelMatrix(rig.mNodes.at("bip01 " + side + " finger1")).getTrans()
                    + rig.modelMatrix(rig.mNodes.at("bip01 " + side + " finger2")).getTrans()) * .5f)
                    * osg::Matrixf::inverse(rig.modelMatrix(support));
                weapon->addChild(spearGeometry());
                std::vector<std::pair<osg::MatrixTransform*, osg::Vec3f>> translations;
                for (const auto& [name, node] : rig.mNodes)
                    if (name.find("upperarm") != std::string::npos || name.find("forearm") != std::string::npos
                        || name.find(" hand") != std::string::npos)
                        translations.emplace_back(node, node->getMatrix().getTrans());
                for (int frame = 0; frame < 90; ++frame)
                {
                    check(rig.mAnimation->play(frame < 45 ? "a013_000000" : "a000_004200", float(frame),
                              frame < 45, 1.f / 30.f, yaw, "a013_000100", "a013_000100", "a013_000000",
                              true, false, false, false, 0.f, 0.f, leftHanded), "spear guard rejected");
                    rig.update();
                    // A settled guard must put both palms on the measured
                    // shaft cross-section, including the reflected left grip.
                    if ((frame > 12 && frame < 45) || frame > 57)
                    {
                        const auto p = (palm * rig.modelMatrix(support))
                            * osg::Matrixf::inverse(rig.modelMatrix(weapon));
                        const float error = std::hypot(p.x() - .25f, p.z() - .5f);
                        maximumError = std::max(maximumError, error);
                        if (error >= .001f)
                            std::cerr << "spear guard transverse error " << error << " left " << leftHanded << '\n';
                        check(error < .001f, "spear guard support palm missed its shaft");
                    }
                    for (const auto& [node, position] : translations)
                        check(close(node->getMatrix().getTrans(), position), "spear guard stretched a native arm bone");
                    const auto before = rig.modelMatrix(support);
                    rig.update();
                    check(close(before.getTrans(), rig.modelMatrix(support).getTrans()),
                        "repeated spear guard traversal accumulated its support correction");
                }
            }
        std::cout << "Spear guard maximum support error: " << maximumError << " native units\n";
    }


    void rangedNativeFootwork(const std::shared_ptr<const EldenCombat::AnimationBank>& bank, bool actual)
    {
        const std::vector<std::string_view> clips = actual
            ? std::vector<std::string_view>{ "a000_050170", "a417_045000", "a417_045011",
                "a436_045000", "a436_045011", "a506_045000", "a506_045010" }
            : std::vector<std::string_view>{ "a036_032500", "a036_032505" };
        Rig rig(bank, false, true);
        std::vector<std::pair<NifOsg::MatrixTransform*, osg::ref_ptr<NativePoseProbe>>> probes;
        for (const char* name : { "bip01 pelvis", "bip01 l thigh", "bip01 l calf", "bip01 l foot",
                 "bip01 r thigh", "bip01 r calf", "bip01 r foot" })
        {
            auto* node = static_cast<NifOsg::MatrixTransform*>(rig.mNodes.at(name).get());
            osg::ref_ptr<NativePoseProbe> probe = new NativePoseProbe;
            probe->mTranslation = node->getMatrix().getTrans();
            auto* before = node->getUpdateCallback();
            probe->setNestedCallback(before->getNestedCallback());
            before->setNestedCallback(probe);
            probes.emplace_back(node, probe);
        }
        for (const auto name : clips)
        {
            const auto* clip = bank->find(name);
            check(clip, "ranged footwork source clip missing");
            for (int frame = 0; frame < 60; ++frame)
            {
                // Stationary start, turn/step during windup, stationary finish.
                // The production selection test separately covers these AI
                // movement changes; this exercises its upper-body callbacks.
                for (const auto& [node, probe] : probes)
                    probe->mRotation = osg::Quat(frame < 10 || frame > 45 ? .2f : .2f + .01f * frame,
                        osg::Vec3f(0, 0, 1));
                check(rig.mAnimation->play(name, (clip->mFrames - 1) * frame / 59.f, false, 1.f / 60.f,
                          0.f, {}, {}, {}, false, false, true), "NPC ranged gesture rejected");
                rig.update();
                for (const auto& [node, probe] : probes)
                {
                    check(close(node->mRotationScale.toOsgMatrix().getRotate(), probe->mRotation),
                        "NPC cast/throw overwrote native hip, knee or foot rotation");
                    check(close(node->getMatrix().getTrans(), probe->mTranslation),
                        "NPC cast/throw overwrote native lower-body translation");
                }
            }
        }
        rig.mAnimation->clear();
        for (const auto& [node, probe] : probes)
            check(close(node->mRotationScale.toOsgMatrix().getRotate(), probe->mRotation),
                "NPC ranged recovery restored a stale lower-body pose");
    }

}
