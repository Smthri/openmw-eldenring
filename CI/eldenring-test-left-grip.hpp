// Included by the native retarget callback harness after its shared rig fixtures.
namespace
{
    void offhandHandednessTests(const std::shared_ptr<const EldenCombat::AnimationBank>& bank)
    {
        if (!bank->find("a023_034000"))
            return;
        for (bool selectedLeft : { false, true })
            for (float pitch : { 0.f, .55f })
            {
                Rig equipped(bank, true, true), reference(bank, true, true);
                osg::ref_ptr<FirstPersonNeck> neck = new FirstPersonNeck;
                neck->mRoot = equipped.mRoot;
                neck->mPitch = osg::Quat(pitch, osg::Vec3f(-1, 0, 0));
                equipped.mNodes.at("bip01 neck")->addUpdateCallback(neck);
                osg::ref_ptr<FirstPersonNeck> referenceNeck = new FirstPersonNeck;
                referenceNeck->mRoot = reference.mRoot;
                referenceNeck->mPitch = neck->mPitch;
                reference.mNodes.at("bip01 neck")->addUpdateCallback(referenceNeck);
                const std::vector<std::string_view> clips = selectedLeft
                    ? std::vector<std::string_view>{ "a010_000000", "a023_032000", "a023_032010", "a023_032020" }
                    : std::vector<std::string_view>{ "a000_000000", "a023_034000", "a023_034010", "a023_034020",
                        "a023_034030", "a023_034200", "a023_034300", "a023_034400", "a023_034530",
                        "a023_034570", "a023_035000", "a023_035010" };
                for (const auto clip : clips)
                    for (float frame : { 0.f, 8.f, 20.f })
                        for (float yaw : { 0.f, .4f })
                        {
                            check(equipped.mAnimation->play(clip, frame, false, 1.f, yaw, {}, {}, {},
                                      false, false, false, false, 0.f, 0.f, selectedLeft, !selectedLeft),
                                "offhand handedness pose rejected");
                            check(reference.mAnimation->play(clip, frame, false, 1.f, yaw, {}, {}, {},
                                      false, false, false, false, 0.f, 0.f, selectedLeft),
                                "offhand handedness reference rejected");
                            equipped.update();
                            reference.update();
                            std::vector<float> sample;
                            bank->sample(*bank->find(clip), frame, false, sample);
                            const auto joint = [&](std::string_view suffix) {
                                const auto offset = *bank->jointOffset(std::string(selectedLeft ? "R_" : "L_")
                                    + std::string(suffix));
                                return osg::Vec3f((selectedLeft ? 1.f : -1.f) * sample[offset],
                                    sample[offset + 2], sample[offset + 1]);
                            };
                            const auto direction = [&](osg::Vec3f value) {
                                value.normalize();
                                return neck->mPitch * (osg::Quat(yaw, osg::Vec3f(0, 0, 1)) * value);
                            };
                            const osg::Vec3f forward = direction(joint("Finger2") - joint("Hand"));
                            const osg::Vec3f thumbward = direction(joint("Finger1") - joint("Finger2"));
                            osg::Vec3f palmNormal = forward ^ thumbward;
                            palmNormal.normalize();
                            const auto weapon = equipped.modelRotation(equipped.mShield);
                            // Native sword geometry extends from the handle along
                            // +Y, with its edge across X and its flat across Z.
                            // Use hand landmarks, independently of weapon axes:
                            // the blade exits the index/thumb side of the fist,
                            // and its edge stays toward the curled fingers.
                            check((weapon * osg::Vec3f(0, 1, 0)) * thumbward > .5f,
                                "offhand blade exits the little-finger side of its holding hand");
                            check((weapon * osg::Vec3f(1, 0, 0)) * forward > .5f,
                                "offhand blade edge points away from the knuckles");
                            check((weapon * osg::Vec3f(0, 0, 1)) * palmNormal > .6f,
                                "offhand blade flat has the opposite handedness to its palm");
                            check(close(equipped.modelRotation(equipped.mWeapon),
                                      reference.modelRotation(reference.mWeapon)),
                                "offhand handedness correction changed the right weapon");
                            if (selectedLeft)
                                check(close(weapon, reference.modelRotation(reference.mShield)),
                                    "offhand handedness correction changed a mirrored two-handed weapon");
                        }
            }
    }

    void offhandCurlTests(const std::shared_ptr<const EldenCombat::AnimationBank>& bank)
    {
        if (!bank->find("a020_035000"))
            return;
        for (const auto grip : { "a000_000000", "a002_000000", "a003_000000" })
            for (const auto clip : { "a020_034000", "a020_034010", "a020_035000", "a020_035010",
                     "a036_035000", "a024_035000", "a000_000000", "a003_000000", "a036_030000" })
            {
                Rig equipped(bank, false, true), reference(bank, false, true);
                const bool action = std::string_view(clip).substr(5, 3) == "034"
                    || std::string_view(clip).substr(5, 3) == "035";
                const bool spear = std::string_view(grip) == "a003_000000";
                for (float frame : { 0.f, 5.f, 8.f, 21.f, 24.f, 34.f })
                {
                    check(equipped.mAnimation->play(clip, frame, false, 1.f, 0.f, {}, {}, grip,
                              spear, false, false, false, 0.f, 0.f, false, true),
                        "offhand grip pose rejected");
                    check(reference.mAnimation->play(clip, frame, false, 1.f, 0.f, {}, {}, action ? "" : grip,
                              spear), "offhand grip reference pose rejected");
                    equipped.update();
                    reference.update();
                    for (const auto name : { "bip01 l hand", "bip01 l finger0", "bip01 l finger01",
                             "bip01 l finger1", "bip01 l finger11", "bip01 l finger2", "bip01 l finger21" })
                        check(close(equipped.modelRotation(equipped.mNodes.at(name)),
                                  reference.modelRotation(reference.mNodes.at(name))),
                            action ? "right stance replaced the authored paired/left attack curl"
                                   : "equipping an offhand weapon changed its existing carry curl");
                }
            }
    }

    void ordinaryOffhandTests(const std::shared_ptr<const EldenCombat::AnimationBank>& bank)
    {
        const std::array<std::string_view, 2> clips = bank->find("a020_034000")
            ? std::array<std::string_view, 2>{ "a020_034000", "a020_035000" }
            : std::array<std::string_view, 2>{ "a036_032500", "a036_032505" };
        for (bool rightSpear : { false, true })
            for (bool leftSpear : { false, true })
                for (float pitch : { 0.f, .55f })
                {
                    Rig rig(bank, true, true);
                    auto* left = rig.mNodes.at("bip01 l hand").get();
                    const osg::Vec3f leftPalm = ((rig.modelMatrix(rig.mNodes.at("bip01 l finger1")).getTrans()
                        + rig.modelMatrix(rig.mNodes.at("bip01 l finger2")).getTrans()) * .5f)
                        * osg::Matrixf::inverse(rig.modelMatrix(left));
                    osg::ref_ptr<FirstPersonNeck> neck = new FirstPersonNeck;
                    neck->mRoot = rig.mRoot;
                    neck->mPitch = osg::Quat(pitch, osg::Vec3f(-1, 0, 0));
                    rig.mNodes.at("bip01 neck")->addUpdateCallback(neck);
                    rig.mWeapon->addChild(spearGeometry(false, 50.f));
                    const std::string_view grip = rightSpear && bank->find("a003_000000")
                        ? "a003_000000" : "a000_000000";
                    for (float leftLength : { 40.f, 90.f })
                    {
                        rig.mShield->removeChildren(0, rig.mShield->getNumChildren());
                        rig.mShield->addChild(spearGeometry(false, leftLength));
                        for (const auto clip : clips)
                            for (float yaw : { 0.f, .4f })
                                for (float frame : { 0.f, 5.f, 13.f, 25.f })
                                {
                                    check(rig.mAnimation->play(clip, frame, false, 1.f, yaw, {}, {}, grip,
                                              rightSpear, false, false, false, 0.f, 0.f, false, true, leftSpear),
                                        "ordinary dual/offhand pose rejected");
                                    rig.update();
                                    std::vector<float> sample;
                                    bank->sample(*bank->find(clip), frame, false, sample);
                                    const auto axis = [&](std::string_view name) {
                                        const auto offset = *bank->axesOffset(name) + 3;
                                        const osg::Vec3f source(-sample[offset], sample[offset + 2], sample[offset + 1]);
                                        return neck->mPitch * (osg::Quat(yaw, osg::Vec3f(0, 0, 1)) * source);
                                    };
                                    check(close(rig.modelRotation(rig.mShield) * osg::Vec3f(0, 1, 0), -axis("L_Weapon")),
                                        "ordinary offhand weapon retained shield orientation instead of its source frame");
                                    const auto contact = leftSpear ? osg::Vec3f(.25f, -.2f * leftLength, .5f) : osg::Vec3f();
                                    check(close(contact * rig.modelMatrix(rig.mShield), leftPalm * rig.modelMatrix(left)),
                                        "ordinary offhand weapon missed its palm after swapping meshes or aiming");
                                    if (rightSpear)
                                    {
                                        check(close(rig.modelRotation(rig.mWeapon) * osg::Vec3f(0, 1, 0), axis("R_Weapon")),
                                            "offhand correction changed the right spear's source shaft");
                                        check(close(osg::Vec3f(.25f, -10.f, .5f) * rig.modelMatrix(rig.mWeapon),
                                                  rig.mNative->mTranslation * rig.modelMatrix(rig.mHand)),
                                            "left and right spear attachments shared their geometry/contact state");
                                    }
                                    const auto before = rig.modelMatrix(rig.mShield);
                                    rig.update();
                                    check(close(before.getTrans(), rig.modelMatrix(rig.mShield).getTrans()),
                                        "ordinary offhand placement accumulated during a repeated traversal");
                                }
                    }
                    rig.mAnimation->clear();
                    check(close(rig.mShield->getMatrix().getTrans(), rig.mNativeShield->mTranslation)
                            && close(rig.mShield->mRotationScale.toOsgMatrix().getRotate(), rig.mNativeShield->mRotation),
                        "ordinary offhand weapon did not restore native shield attachment state");
                }
    }

    void leftSwordGripTests(const std::shared_ptr<const EldenCombat::AnimationBank>& bank)
    {
        Rig corrected(bank, false, true), reference(bank, false, true);
        for (Rig* rig : { &corrected, &reference })
        {
            rig->mShield->addChild(spearGeometry(false, 25.f));
            rig->update();
        }
        const osg::Vec3f palm = ((corrected.modelMatrix(corrected.mNodes.at("bip01 r finger1")).getTrans()
            + corrected.modelMatrix(corrected.mNodes.at("bip01 r finger2")).getTrans()) * .5f)
            * osg::Matrixf::inverse(corrected.modelMatrix(corrected.mHand));
        const float nativePalmLength = (corrected.modelMatrix(corrected.mNodes.at("bip01 r finger2")).getTrans()
            - corrected.modelMatrix(corrected.mHand).getTrans()).length();
        std::vector<float> sample;
        bank->sample(*bank->find("a012_000000"), 0.f, false, sample);
        const auto source = [&](std::string_view name) {
            const auto offset = *bank->jointOffset(name);
            return osg::Vec3f(-sample[offset], sample[offset + 2], sample[offset + 1]);
        };
        const auto axes = *bank->axesOffset("R_Weapon") + 3;
        osg::Vec3f shaft(-sample[axes], sample[axes + 2], sample[axes + 1]);
        shaft.normalize();
        const float separation = ((source("R_Weapon") - source("L_Weapon")) * shaft)
            * nativePalmLength / (source("R_Finger2") - source("R_Hand")).length();
        for (const auto clip : { "a012_000000", "a012_020100", "a012_020200" })
            for (float yaw : { 0.f, 1.1f })
                for (float frame : { 0.f, 6.f, 15.f })
                {
                    for (Rig* rig : { &corrected, &reference })
                    {
                        check(rig->mAnimation->play(clip, frame, true, 1.f, yaw, {}, {}, "a012_000000", false,
                                  rig == &corrected, false, false, 0.f, 0.f, true), "left sword carry rejected");
                        rig->update();
                    }
                    const auto weapon = corrected.modelMatrix(corrected.mShield);
                    const float scale = osg::Matrixf::transform3x3(osg::Vec3f(0, 1, 0), weapon).length();
                    const auto contact = osg::Vec3f(.25f, -separation / scale, .5f) * weapon;
                    const auto wrist = corrected.modelMatrix(corrected.mHand);
                    const auto shoulder = corrected.modelMatrix(corrected.mNodes.at("bip01 r upperarm")).getTrans();
                    const auto elbow = corrected.modelMatrix(corrected.mNodes.at("bip01 r forearm")).getTrans();
                    const float upper = (elbow - shoulder).length();
                    const float lower = (wrist.getTrans() - elbow).length();
                    const float reach = (contact - osg::Matrixf::transform3x3(palm, wrist) - shoulder).length();
                    const float unavoidable = std::max({ 0.f, reach - upper - lower, std::abs(upper - lower) - reach });
                    check(std::abs((palm * wrist - contact).length() - unavoidable) < 4e-4f,
                        "right supporting palm missed the left sword handle or nearest native reach");
                    check(close(corrected.modelRotation(corrected.mHand), reference.modelRotation(reference.mHand)),
                        "left sword support correction rotated the right hand's authored curl");
                    check(close(weapon.getTrans(), reference.modelMatrix(reference.mShield).getTrans())
                            && close(corrected.modelRotation(corrected.mShield), reference.modelRotation(reference.mShield)),
                        "left sword support correction moved its leading hand or attachment");
                }
    }

    void leftGripTests(const std::shared_ptr<const EldenCombat::AnimationBank>& bank)
    {
        offhandHandednessTests(bank);
        offhandCurlTests(bank);
        ordinaryOffhandTests(bank);
        leftSwordGripTests(bank);
        for (bool reverse : { false, true })
            for (float pitch : { 0.f, .65f })
            {
                Rig rig(bank, true, true);
                auto* left = rig.mNodes.at("bip01 l hand").get();
                const auto palm = [&](const char* side, osg::Node* hand) {
                    const std::string prefix = std::string("bip01 ") + side;
                    return ((rig.modelMatrix(rig.mNodes.at(prefix + " finger1")).getTrans()
                        + rig.modelMatrix(rig.mNodes.at(prefix + " finger2")).getTrans()) * .5f)
                        * osg::Matrixf::inverse(rig.modelMatrix(hand));
                };
                const osg::Vec3f leftPalm = palm("l", left);
                const osg::Vec3f rightPalm = palm("r", rig.mHand);
                if (reverse)
                {
                    auto* upper = rig.mNodes.at("bip01 l upperarm").get();
                    auto* parent = upper->getParent(0);
                    osg::ref_ptr<osg::Node> keep = upper;
                    parent->removeChild(upper);
                    parent->insertChild(0, upper);
                }
                osg::ref_ptr<FirstPersonNeck> neck = new FirstPersonNeck;
                neck->mRoot = rig.mRoot;
                neck->mPitch = osg::Quat(pitch, osg::Vec3f(-1, 0, 0));
                rig.mNodes.at("bip01 neck")->addUpdateCallback(neck);
                rig.mShield->addChild(spearGeometry(false, 90.f));
                std::vector<float> sample;
                for (const auto clip : { "a013_000000", "a036_032500", "a036_032505" })
                    for (float frame : { 0.f, 5.f, 12.f, 25.f })
                    {
                        const auto* source = bank->find(clip);
                        check(source, "left spear source clip missing");
                        check(rig.mAnimation->play(clip, frame, false, 1.f, 0.f, {}, {}, "a013_000000",
                                  true, false, false, false, 0.f, 0.f, true),
                            "left two-handed spear pose rejected");
                        rig.update();
                        bank->sample(*source, frame, false, sample);
                        const auto axis = *bank->axesOffset("R_Weapon") + 3;
                        osg::Vec3f expected(sample[axis], sample[axis + 2], sample[axis + 1]);
                        expected = neck->mPitch * expected;
                        check(close(rig.modelRotation(rig.mShield) * osg::Vec3f(0, 1, 0), expected),
                            "left spear did not follow the reflected source shaft during first-person aiming");
                        const auto inverseWeapon = osg::Matrixf::inverse(rig.modelMatrix(rig.mShield));
                        const auto held = (leftPalm * rig.modelMatrix(left)) * inverseWeapon;
                        const auto support = (rightPalm * rig.modelMatrix(rig.mHand)) * inverseWeapon;
                        const float front = std::max(held.y(), support.y());
                        const float rear = std::min(held.y(), support.y());
                        check(rear >= -.92f * 90.f - 3e-4f,
                            "left spear placed the supporting hand behind the butt");
                        check(std::abs(front + .2f * 90.f) < 3e-4f || std::abs(rear + .92f * 90.f) < 3e-4f,
                            "left spear lost the front grip or short-shaft rear margin");
                        check(std::abs(held.x() - .25f) < 3e-4f && std::abs(held.z() - .5f) < 3e-4f,
                            "left spear shaft moved away from the selected palm");
                        check(close(rig.mWeapon->getMatrix().getTrans(), rig.mNative->mTranslation)
                                && close(rig.mWeapon->mRotationScale.toOsgMatrix().getRotate(), rig.mNative->mRotation),
                            "left spear overrode the stowed right weapon");
                        const auto before = rig.modelMatrix(rig.mShield);
                        rig.update();
                        check(close(before.getTrans(), rig.modelMatrix(rig.mShield).getTrans()),
                            "left spear accumulated its attachment translation while paused");
                    }
                rig.mAnimation->clear();
                check(close(rig.mShield->getMatrix().getTrans(), rig.mNativeShield->mTranslation)
                        && close(rig.mShield->mRotationScale.toOsgMatrix().getRotate(), rig.mNativeShield->mRotation),
                    "left spear did not restore the original native shield attachment");
            }

        // Shield attacks and the left-specific swap gesture are already authored
        // for that side: enabling a left grip must not mirror their skeleton.
        for (const auto clip : { "a015_000100", "a000_029080" })
        {
            if (!bank->find(clip))
                continue;
            Rig authored(bank, false, true), selected(bank, false, true);
            for (float frame : { 0.f, 2.f, 5.f })
            {
                check(authored.mAnimation->play(clip, frame, false, 1.f), "authored left gesture rejected");
                check(selected.mAnimation->play(clip, frame, false, 1.f, 0.f, {}, {}, {}, false,
                          false, false, false, 0.f, 0.f, true), "selected left gesture rejected");
                authored.update();
                selected.update();
                for (const auto name : { "bip01 l upperarm", "bip01 l forearm", "bip01 l hand",
                         "bip01 r upperarm", "bip01 r forearm", "bip01 r hand" })
                    check(close(authored.modelRotation(authored.mNodes.at(name)),
                              selected.modelRotation(selected.mNodes.at(name))),
                        "authored left shield/swap gesture was reflected twice");
            }
        }

        Rig guarded(bank, false, true), reference(bank, false, true);
        guarded.mWeapon->addChild(spearGeometry(false));
        const std::string_view thrust = bank->find("a036_030000") ? "a036_030000" : "a036_032500";
        const std::string_view grip = bank->find("a003_000000") ? "a003_000000" : "a000_000000";
        for (float frame : { 0.f, 5.f, 12.f, 18.f })
        {
            check(guarded.mAnimation->play(thrust, frame, false, 1.f, 0.f, "a000_000100", {}, grip, true),
                "guarded spear thrust rejected");
            check(reference.mAnimation->play(thrust, frame, false, 1.f, 0.f, "a000_000100"),
                "guarded spear reference pose rejected");
            guarded.update();
            reference.update();
            for (const auto name : { "bip01 l upperarm", "bip01 l forearm", "bip01 l hand",
                     "bip01 l finger0", "bip01 l finger01", "bip01 l finger1", "bip01 l finger11",
                     "bip01 l finger2", "bip01 l finger21" })
                check(close(guarded.modelRotation(guarded.mNodes.at(name)),
                          reference.modelRotation(reference.mNodes.at(name))),
                    "spear thrust replaced the shield arm or shield-hand curl");
            check(close(guarded.modelRotation(guarded.mShield), reference.modelRotation(reference.mShield)),
                "guarded thrust rotated the shield away from its guard frame");
            const auto contact = osg::Vec3f(.25f, -10.f, .5f) * guarded.modelMatrix(guarded.mWeapon);
            check(close(contact, guarded.mNative->mTranslation * guarded.modelMatrix(guarded.mHand)),
                "one-handed guard thrust anchored the spear to the shield hand");
        }
    }
}
