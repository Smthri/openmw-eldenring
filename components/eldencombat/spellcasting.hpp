#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_SPELLCASTING_H
#define OPENMW_COMPONENTS_ELDENCOMBAT_SPELLCASTING_H

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

namespace EldenCombat
{
    enum class SpellGesture
    {
        Generic,
        Restoration,
        RestorationArea,
        Support,
        Spectral,
        Fire,
        Frost,
        Shock,
    };

    struct SpellAnimation
    {
        std::string_view mWindup;
        std::string_view mCast;
        float mReleaseFrame; // Event 64 in the cast continuation, at 30 fps.
    };

    // The adapter captures the effect and native range when casting starts.
    // Each gesture has a preparation clip and a release/recovery continuation;
    // the native range still selects the text keys that actually release magic.
    inline std::optional<SpellAnimation> spellAnimation(
        std::string_view range, SpellGesture gesture = SpellGesture::Generic)
    {
        if (range != "self" && range != "touch" && range != "target")
            return std::nullopt;
        if (gesture == SpellGesture::Restoration)
            return SpellAnimation{ "a433_045000", "a433_045010", 30.f };
        if (gesture == SpellGesture::RestorationArea)
            return SpellAnimation{ "a434_045000", "a434_045011", 20.f }; // Heal.
        if (gesture == SpellGesture::Support)
            return SpellAnimation{ "a431_045000", "a431_045010", 15.f }; // Protection / fortification.
        if (gesture == SpellGesture::Spectral || gesture == SpellGesture::Frost)
            return SpellAnimation{ "a426_045000", "a426_045010", 18.f }; // Shadow Bait / Darkness.
        if (gesture == SpellGesture::Shock && range == "target")
            return SpellAnimation{ "a436_045000", "a436_045011", 15.f }; // Lightning Spear, uncharged.
        if ((gesture == SpellGesture::Fire || gesture == SpellGesture::Shock) && range == "self")
            return SpellAnimation{ "a506_045000", "a506_045010", 5.f }; // Close-range elemental burst.
        if (range == "self") // Urgent Heal.
            return SpellAnimation{ "a433_045000", "a433_045010", 30.f };
        if (range == "touch") // Catch Flame.
            return SpellAnimation{ "a506_045000", "a506_045010", 5.f };
        if (range == "target") // Flame Sling, uncharged release.
            return SpellAnimation{ "a417_045000", "a417_045011", 18.f };
        return std::nullopt;
    }

    struct SpellAnimationPose
    {
        std::string_view mClip;
        float mFrame;
    };

    // Native spellcast text keys retain ownership of casting and recovery.
    // Align the imported release gesture with that native release event even
    // when a replacement NIF changes the wind-up/recovery proportions.
    inline std::optional<float> spellAnimationFrame(float time, float start, float release, float stop,
        float releaseFrame, float endFrame)
    {
        if (!std::isfinite(time) || !std::isfinite(start) || !std::isfinite(release) || !std::isfinite(stop)
            || !std::isfinite(releaseFrame) || !std::isfinite(endFrame)
            || start < 0.f || release <= start || stop <= release || releaseFrame <= 0.f || endFrame <= releaseFrame)
            return std::nullopt;

        time = std::clamp(time, start, stop);
        if (time <= release)
            return releaseFrame * (time - start) / (release - start);
        return releaseFrame + (endFrame - releaseFrame) * (time - release) / (stop - release);
    }

    inline std::optional<SpellAnimationPose> spellAnimationPose(const SpellAnimation& animation,
        float time, float start, float release, float stop, float windupLength, float castLength)
    {
        if (!std::isfinite(windupLength) || !std::isfinite(castLength) || windupLength <= 0.f
            || !std::isfinite(animation.mReleaseFrame) || animation.mReleaseFrame <= 0.f
            || castLength <= animation.mReleaseFrame)
            return std::nullopt;

        const auto frame = spellAnimationFrame(time, start, release, stop,
            windupLength + animation.mReleaseFrame, windupLength + castLength);
        if (!frame)
            return std::nullopt;
        if (*frame < windupLength)
            return SpellAnimationPose{ animation.mWindup, *frame };
        return SpellAnimationPose{ animation.mCast, *frame - windupLength };
    }
}

#endif
