#include <components/eldencombat/animations.hpp>
#include <components/eldencombat/bow.hpp>

#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    int checks = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
        ++checks;
    }

    void pose(const std::optional<EldenCombat::BowPose>& actual, std::string_view clip, float frame, bool loop,
        const char* message)
    {
        check(actual && actual->mClip == clip && actual->mLoop == loop && std::isfinite(actual->mFrame)
                && std::abs(actual->mFrame - frame) < 0.001f,
            message);
    }

    void nativeTiming()
    {
        using namespace EldenCombat;
        for (const float start : { 0.f, 5.f, 20.f })
            for (const float windup : { 0.1f, 0.5f, 2.f })
                for (const float charge : { 0.f, 0.5f, 3.f })
                    for (const float releaseDuration : { 0.1f, 0.5f })
                        for (const float gap : { 0.f, 0.25f, 2.f })
                        {
                            const float minAttack = start + windup;
                            const float maxAttack = minAttack + charge;
                            const float release = maxAttack + releaseDuration;
                            const float followStart = release + gap;
                            const float followStop = followStart + 1.f;
                            const NativeBowTiming timing{
                                start, minAttack, maxAttack, release, followStart, followStop };
                            const auto sample = [&](float time, NativeBowSection section, float hold = 0.f) {
                                return bowAnimationPose(time, section, timing, { 30.f, 60.f, 50.f }, hold);
                            };
                            pose(sample(start - 10.f, NativeBowSection::Windup), BowDrawAnimation, 0.f, false,
                                "draw does not clamp before native start");
                            pose(sample(start + windup / 2.f, NativeBowSection::Windup), BowDrawAnimation, 15.f, false,
                                "native preparation does not draw the bow");
                            check(sample(start - 10.f, NativeBowSection::Windup)->mDrawWeight == 0.f,
                                "native nock correction is already active before drawing");
                            check(std::abs(sample(start + windup / 4.f, NativeBowSection::Windup)->mDrawWeight
                                          - 0.15625f) < 0.001f,
                                "drawing hand does not ease onto the native string nock");
                            pose(sample(minAttack, NativeBowSection::Windup), BowHoldAnimation, 0.f, true,
                                "minimum native charge does not reach the drawn pose");
                            check(sample(minAttack, NativeBowSection::Windup)->mDrawWeight == 1.f,
                                "drawing hand does not follow the native string during charge");
                            pose(sample(maxAttack + 100.f, NativeBowSection::Windup, 125.f),
                                BowHoldAnimation, 5.f, true,
                                "holding beyond maximum native charge fires or freezes the held loop");
                            pose(sample(release, NativeBowSection::Release), BowReleaseAnimation, 0.f, false,
                                "split native projectile release misses the source event at frame zero");
                            for (auto section : { NativeBowSection::Release, NativeBowSection::FollowThrough,
                                     NativeBowSection::ReleaseAndFollow })
                                check(sample(release, section)->mDrawWeight == 0.f,
                                    "released hand continues tracking the returning native string");
                            pose(sample(release + 100.f, NativeBowSection::Release), BowReleaseAnimation, 0.f, false,
                                "split release predicts recoil before the native follow section starts");
                            pose(sample(followStart - 10.f, NativeBowSection::FollowThrough), BowReleaseAnimation, 0.f,
                                false, "split follow section does not clamp before its start");
                            pose(sample(followStart + 0.5f, NativeBowSection::FollowThrough), BowReleaseAnimation, 25.f,
                                false, "split follow section does not fit the full source recoil");
                            pose(sample(release, NativeBowSection::ReleaseAndFollow), BowReleaseAnimation, 0.f, false,
                                "continuous native release does not match the source event");
                            pose(sample((release + followStop) / 2.f, NativeBowSection::ReleaseAndFollow),
                                BowReleaseAnimation, 25.f, false, "continuous recoil starts from the wrong text key");
                            for (auto section : { NativeBowSection::FollowThrough, NativeBowSection::ReleaseAndFollow })
                                pose(sample(followStop + 100.f, section), BowReleaseAnimation, 50.f, false,
                                    "recovery does not clamp at source carry pose");
                            if (gap > 0.f)
                                check(sample((release + followStart) / 2.f,
                                          NativeBowSection::ReleaseAndFollow)->mFrame > 0.f,
                                    "continuous native playback waits for follow start before recoiling");

                            for (const float fps : { 15.f, 60.f, 144.f })
                                for (const float speed : { 0.5f, 1.f, 2.f })
                                {
                                    const float step = speed / fps;
                                    for (float current = start; current < followStop; current += step)
                                    {
                                        const float time = current + step;
                                        const auto result = sample(time, NativeBowSection::ReleaseAndFollow,
                                            std::max(0.f, time - minAttack) * 30.f);
                                        check(result.has_value(), "continuous shooting produces no pose");
                                        check((result->mClip == BowReleaseAnimation) == (time >= release),
                                            "continuous release state recoils before or after its native release key");
                                        check(result->mFrame >= 0.f && result->mFrame <= 60.f,
                                            "bow pose exceeds its source clip");
                                        check(result->mDrawWeight >= 0.f && result->mDrawWeight <= 1.f,
                                            "drawing-hand correction exceeds its blend interval");
                                    }
                                }
                        }
    }

    void invalidTracks()
    {
        using namespace EldenCombat;
        using Args = std::array<float, 11>;
        const Args valid{ 1.f, 0.f, 1.f, 2.f, 3.f, 3.5f, 4.f, 30.f, 60.f, 50.f, 0.f };
        const auto sample = [](const Args& args, NativeBowSection section) {
            return bowAnimationPose(args[0], section, { args[1], args[2], args[3], args[4], args[5], args[6] },
                { args[7], args[8], args[9] }, args[10]);
        };
        for (auto section : { NativeBowSection::Windup, NativeBowSection::Release, NativeBowSection::FollowThrough,
                 NativeBowSection::ReleaseAndFollow })
        {
            for (std::size_t index = 0; index < valid.size(); ++index)
                for (const float invalid : { std::numeric_limits<float>::quiet_NaN(),
                         std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() })
                {
                    Args args = valid;
                    args[index] = invalid;
                    check(!sample(args, section), "non-finite bow timing failed to request native fallback");
                }
            for (const auto& [index, value] : std::array<std::pair<std::size_t, float>, 11>{ {
                     { 1, -1.f }, { 2, 0.f }, { 3, 0.5f }, { 4, 2.f }, { 5, 2.5f }, { 6, 3.5f },
                     { 7, 0.f }, { 8, 0.f }, { 9, 0.f }, { 10, -1.f }, { 8, -1.f } } })
            {
                Args args = valid;
                args[index] = value;
                check(!sample(args, section), "missing clip or malformed shoot track failed to retain native poses");
            }
        }
    }

    void actualClips(const EldenCombat::AnimationBank& bank)
    {
        using namespace EldenCombat;
        std::vector<float> values;
        for (const auto& [name, endFrame] : std::array<std::pair<std::string_view, float>, 3>{ {
                 { BowDrawAnimation, 30.f }, { BowHoldAnimation, 60.f }, { BowReleaseAnimation, 50.f } } })
        {
            const auto* clip = bank.find(name);
            check(clip && clip->mFrames == static_cast<std::size_t>(endFrame) + 1,
                "source bow clip is missing or truncated");
            for (float frame = 0.f; frame <= endFrame; frame += 0.25f)
            {
                bank.sample(*clip, frame, name == BowHoldAnimation, values);
                check(values.size() == bank.stride(), "bow pose does not match the imported skeleton");
                for (float value : values)
                    check(std::isfinite(value), "bow pose contains invalid joint data");
            }
        }
        for (unsigned id : { 0u, 20000u, 20001u, 20002u, 20003u, 20100u, 20101u, 20102u, 20103u, 20200u,
                 22100u, 22101u, 22102u, 22103u, 300000u, 320000u, 320001u, 320002u, 320003u, 320100u,
                 320101u, 320102u, 320103u, 322100u, 390000u, 390001u })
        {
            const auto* clip = bank.find(bowStanceAnimation(id));
            check(clip && clip->mFrames > 1, "bow carry, crouch or directional gait is missing");
            for (float frame : { 0.f, static_cast<float>(clip->mFrames - 1) / 2.f })
            {
                bank.sample(*clip, frame, true, values);
                for (float value : values)
                    check(std::isfinite(value), "bow carry contains invalid joint data");
            }
        }
        std::vector<float> from;
        std::vector<float> to;
        const auto seam = [&](std::string_view fromClip, float fromFrame, std::string_view toClip, float toFrame) {
            bank.sample(*bank.find(fromClip), fromFrame, false, from);
            bank.sample(*bank.find(toClip), toFrame, false, to);
            for (std::size_t index = 0; index < bank.jointNames().size() * 3; ++index)
                check(std::abs(from[index] - to[index]) < 0.005f,
                    "bow draw, hold, release or carry seam exceeds source compression tolerance");
        };
        seam(BowDrawAnimation, 30.f, BowHoldAnimation, 0.f);
        seam(BowDrawAnimation, 30.f, BowReleaseAnimation, 0.f);
        seam(BowReleaseAnimation, 50.f, BowDrawAnimation, 0.f);
        seam(BowHoldAnimation, 60.f, BowHoldAnimation, 0.f);
        std::cout << "29 bow clips: source draw/hold/release and carry seams passed\n";
    }
}

int main(int argc, char** argv)
{
    try
    {
        nativeTiming();
        invalidTracks();
        check(EldenCombat::bowStanceAnimation(0) == EldenCombat::BowIdleAnimation,
            "bow carry selected the wrong stance category");
        check(EldenCombat::bowStanceAnimation(1000000).empty(), "invalid bow gait identifier was accepted");
        if (argc > 1)
        {
            std::ifstream stream(argv[1], std::ios::binary);
            check(static_cast<bool>(stream), "cannot open requested animation bank");
            actualClips(EldenCombat::AnimationBank(stream));
        }
        std::cout << checks << " bow animation checks passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Bow animation test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
