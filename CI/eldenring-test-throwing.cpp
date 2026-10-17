#include <components/eldencombat/animations.hpp>
#include <components/eldencombat/throwing.hpp>

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

    void close(std::optional<float> actual, float expected, const char* message)
    {
        check(actual && std::isfinite(*actual) && std::abs(*actual - expected) < 0.001f, message);
    }

    void nativeTiming()
    {
        using namespace EldenCombat;
        for (const float start : { 0.f, 5.f, 20.f })
            for (const float windup : { 0.1f, 0.5f, 2.f })
                for (const float charge : { 0.f, 0.5f, 3.f })
                    for (const float throwTime : { 0.1f, 0.5f, 2.f })
                        for (const float gap : { 0.f, 0.25f, 2.f })
                        {
                            const float minAttack = start + windup;
                            const float maxAttack = minAttack + charge;
                            const float release = maxAttack + throwTime;
                            const float followStart = release + gap;
                            const float followStop = followStart + 1.f;
                            const auto sample = [&](float time, bool following = false) {
                                return throwingAnimationFrame(time, start, minAttack, maxAttack, release,
                                    followStart, followStop, following, 80.f);
                            };
                            close(sample(start), 0.f, "throw does not start at source frame zero");
                            close(sample(start - 10.f), 0.f, "throw does not clamp before native start");
                            close(sample(minAttack), 24.f, "windup misses the source ready pose");
                            close(sample((minAttack + maxAttack) / 2.f), 24.f,
                                "holding attack changes the source ready pose");
                            close(sample(maxAttack), 24.f, "maximum charge misses the source ready pose");
                            close(sample((maxAttack + release) / 2.f), 32.f,
                                "pre-release interval does not interpolate toward the throw gesture");
                            close(sample(release), 40.f, "native projectile release misses the source release event");
                            close(sample(release + 10.f), 40.f, "release section advanced into recovery early");
                            close(sample(followStart, true), 40.f,
                                "gap between native release and follow keys causes a pose jump");
                            close(sample(followStart - 10.f, true), 40.f,
                                "recovery prediction goes backward before follow start");
                            close(sample((followStart + followStop) / 2.f, true), 60.f,
                                "native recovery does not interpolate source recovery");
                            close(sample(followStop, true), 80.f, "native recovery misses the source endpoint");
                            close(sample(followStop + 10.f, true), 80.f, "long recovery update exceeds source endpoint");

                            for (const float fps : { 15.f, 60.f, 144.f })
                                for (const float speed : { 0.5f, 1.f, 2.f })
                                {
                                    float previous = 0.f;
                                    const float step = speed / fps;
                                    for (float current = start; current < release; current += step)
                                    {
                                        const float predicted = current + step;
                                        const auto frame = sample(predicted);
                                        check(frame && *frame >= previous && *frame <= 40.f,
                                            "throw reverses or overshoots during playback");
                                        check(predicted >= release ? *frame == 40.f
                                            : (release - predicted < 0.00001f || *frame < 40.f),
                                            "rendered gesture crosses release at the wrong native time");
                                        previous = *frame;
                                    }
                                }
                        }
    }

    void invalidTracks()
    {
        using Args = std::array<float, 8>;
        const Args valid{ 1.f, 0.f, 1.f, 2.f, 3.f, 3.5f, 4.f, 80.f };
        const auto sample = [](const Args& args, bool following) {
            return EldenCombat::throwingAnimationFrame(args[0], args[1], args[2], args[3], args[4],
                args[5], args[6], following, args[7]);
        };
        for (const bool following : { false, true })
        {
            for (std::size_t index = 0; index < valid.size(); ++index)
                for (const float invalid : { std::numeric_limits<float>::quiet_NaN(),
                         std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() })
                {
                    Args args = valid;
                    args[index] = invalid;
                    check(!sample(args, following), "invalid timing failed to request native fallback");
                }
            for (const Args& invalid : {
                     Args{ 1.f, -1.f, 1.f, 2.f, 3.f, 3.5f, 4.f, 80.f },
                     Args{ 1.f, 1.f, 1.f, 2.f, 3.f, 3.5f, 4.f, 80.f },
                     Args{ 1.f, 0.f, 2.f, 1.f, 3.f, 3.5f, 4.f, 80.f },
                     Args{ 1.f, 0.f, 1.f, 2.f, 2.f, 3.5f, 4.f, 80.f },
                     Args{ 1.f, 0.f, 1.f, 2.f, 3.f, 2.5f, 4.f, 80.f },
                     Args{ 1.f, 0.f, 1.f, 2.f, 3.f, 3.5f, 3.5f, 80.f },
                     Args{ 1.f, 0.f, 1.f, 2.f, 3.f, 3.5f, 4.f, 40.f },
                     Args{ 1.f, 0.f, 1.f, 2.f, 3.f, 3.5f, 4.f, 0.f },
                 })
                check(!sample(invalid, following), "malformed animation track did not retain native poses");
        }
    }

    void actualClip(const EldenCombat::AnimationBank& bank)
    {
        using namespace EldenCombat;
        const auto* clip = bank.find(DartThrowAnimation.mClip);
        check(clip, "animation bank has no imported dart throw");
        const float endFrame = static_cast<float>(clip->mFrames - 1);
        check(endFrame > DartThrowAnimation.mReleaseFrame, "dart throw is truncated before source release");
        std::vector<float> pose;
        std::vector<float> ready;
        std::vector<float> release;
        bank.sample(*clip, DartThrowAnimation.mReadyFrame, false, ready);
        bank.sample(*clip, DartThrowAnimation.mReleaseFrame, false, release);
        check(ready != release, "dart source preparation and release have the same pose");
        for (const bool following : { false, true })
            for (float time = following ? 3.f : 0.f; time < (following ? 4.1f : 2.1f); time += 1.f / 120.f)
            {
                const auto frame = throwingAnimationFrame(time, 0.f, 0.5f, 1.5f, 2.f, 3.f, 4.f,
                    following, endFrame);
                check(frame.has_value(), "actual throw clip produced no pose");
                bank.sample(*clip, *frame, false, pose);
                check(pose.size() == bank.stride(), "throwing pose does not match imported skeleton");
                for (const float value : pose)
                    check(std::isfinite(value), "throwing pose contains invalid joint data");
            }
        std::cout << DartThrowAnimation.mClip << ": ready " << DartThrowAnimation.mReadyFrame
                  << ", release " << DartThrowAnimation.mReleaseFrame << ", end " << endFrame << '\n';
    }
}

int main(int argc, char** argv)
{
    try
    {
        nativeTiming();
        invalidTracks();
        if (argc > 1)
        {
            std::ifstream stream(argv[1], std::ios::binary);
            check(static_cast<bool>(stream), "cannot open requested animation bank");
            actualClip(EldenCombat::AnimationBank(stream));
        }
        std::cout << checks << " dart throwing checks passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Dart throwing test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
