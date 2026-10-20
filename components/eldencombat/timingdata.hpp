#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_TIMINGDATA_H
#define OPENMW_COMPONENTS_ELDENCOMBAT_TIMINGDATA_H

#include "combat.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <istream>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace EldenCombat
{
    // ERAT version 1 is little endian. Enum ordinals and the field order are
    // shared with scripts/import_eldenring_timings.py; change the version if
    // either changes. The file contains local game data, never engine code.
    class TimingData
    {
        using Key = std::tuple<int, int, int, int>;
        std::map<Key, ActionDef> mActions;
        std::set<std::pair<WeaponClass, bool>> mMovesets;
        std::map<std::pair<WeaponClass, bool>, unsigned int> mStances;

        class Reader
        {
            std::istream& mStream;
            std::size_t mRemaining = 64 * 1024 * 1024;

        public:
            explicit Reader(std::istream& stream)
                : mStream(stream)
            {
            }

            void read(char* data, std::size_t size)
            {
                if (size > mRemaining || !mStream.read(data, static_cast<std::streamsize>(size)))
                    throw std::runtime_error("truncated or oversized ERAT timing file");
                mRemaining -= size;
            }

            unsigned int byte()
            {
                char result;
                read(&result, 1);
                return static_cast<unsigned char>(result);
            }

            std::uint32_t integer()
            {
                std::uint32_t result = 0;
                for (unsigned int shift = 0; shift < 32; shift += 8)
                    result |= byte() << shift;
                return result;
            }

            int selector(unsigned int maximum)
            {
                const auto result = integer();
                if (result == 0xffffffffu)
                    return -1;
                if (result > maximum)
                    throw std::runtime_error("invalid ERAT action selector");
                return static_cast<int>(result);
            }

            float number(bool nonnegative = true)
            {
                const auto bits = integer();
                float result;
                static_assert(sizeof(result) == sizeof(bits));
                std::memcpy(&result, &bits, sizeof(result));
                if (!std::isfinite(result) || (nonnegative && result < 0))
                    throw std::runtime_error("invalid ERAT floating point value");
                return result;
            }

            bool boolean()
            {
                const auto value = byte();
                if (value > 1)
                    throw std::runtime_error("invalid ERAT boolean");
                return value != 0;
            }

            ActionDef definition(bool landing = false)
            {
                ActionDef result;
                result.mAnimation.resize(11);
                read(result.mAnimation.data(), result.mAnimation.size());
                for (std::size_t i = 0; i < result.mAnimation.size(); ++i)
                {
                    const char value = result.mAnimation[i];
                    if ((i == 0 && value != 'a') || (i == 4 && value != '_')
                        || (i != 0 && i != 4 && (value < '0' || value > '9')))
                        throw std::runtime_error("invalid ERAT animation identifier");
                }
                for (const auto field : scalarFields())
                    result.*field = number();
                if (result.mDuration <= 0 || result.mInvincibleFrom > result.mInvincibleTo
                    || result.mInvincibleTo > result.mDuration || result.mChargeFrom > result.mChargeTo
                    || result.mChargeTo > result.mDuration)
                    throw std::runtime_error("invalid ERAT action window");
                result.mHitCount = integer();
                if (result.mHitCount > MaximumHits)
                    throw std::runtime_error("too many ERAT hit windows");
                for (std::size_t i = 0; i < result.mHitCount; ++i)
                {
                    auto& hit = result.mHits[i];
                    hit.mFrom = number();
                    hit.mTo = number();
                    hit.mDamageMultiplier = number();
                    hit.mStaminaCost = number();
                    hit.mStanceDamage = number();
                    hit.mGuardDamage = number();
                    hit.mHitStop = number();
                    hit.mLeftHand = boolean();
                    hit.mHasHandAssignment = boolean();
                    if (hit.mFrom >= hit.mTo || hit.mTo > result.mDuration)
                        throw std::runtime_error("invalid ERAT hit window");
                }
                const auto samples = integer();
                if (samples > 32768)
                    throw std::runtime_error("too many ERAT root motion samples");
                result.mRootMotion.resize(samples);
                for (auto& sample : result.mRootMotion)
                    for (auto& coordinate : sample)
                        coordinate = number(false);
                const auto phases = byte();
                if (phases > 3 || (landing && phases != 0))
                    throw std::runtime_error("invalid ERAT landing phases");
                if (phases & 1)
                    result.mLanding = std::make_shared<ActionDef>(definition(true));
                if (phases & 2)
                    result.mLandingShort = std::make_shared<ActionDef>(definition(true));
                return result;
            }
        };

        static ActionDef scaled(const ActionDef& definition, float speed)
        {
            ActionDef result = definition;
            // Match the locally generated C++ table, including each landing's
            // independent clock. Costs, damage and spatial samples are unchanged.
            const auto fields = scalarFields();
            for (std::size_t i = 0; i < 14; ++i)
                result.*fields[i] /= speed;
            for (std::size_t i = 0; i < result.mHitCount; ++i)
            {
                result.mHits[i].mFrom /= speed;
                result.mHits[i].mTo /= speed;
            }
            result.mRootMotionSpeed = result.mAnimationSpeed = speed;
            if (result.mLanding)
                result.mLanding = std::make_shared<ActionDef>(scaled(*result.mLanding, speed));
            if (result.mLandingShort)
                result.mLandingShort = std::make_shared<ActionDef>(scaled(*result.mLandingShort, speed));
            return result;
        }

    public:
        static constexpr std::array<float ActionDef::*, 17> scalarFields()
        {
            return { &ActionDef::mDuration, &ActionDef::mInputFrom, &ActionDef::mDodgeInputFrom,
                &ActionDef::mCancelLight, &ActionDef::mCancelHeavy, &ActionDef::mCancelDodge,
                &ActionDef::mCancelJump, &ActionDef::mCancelGuard, &ActionDef::mCancelMove,
                &ActionDef::mCancelLeft, &ActionDef::mInvincibleFrom, &ActionDef::mInvincibleTo,
                &ActionDef::mChargeFrom, &ActionDef::mChargeTo, &ActionDef::mStartCost,
                &ActionDef::mDistance, &ActionDef::mStrength };
        }

        explicit TimingData(std::istream& stream)
        {
            Reader reader(stream);
            if (reader.integer() != 0x54415245u || reader.integer() != 1)
                throw std::runtime_error("unsupported ERAT timing file");
            const auto count = reader.integer();
            if (count == 0 || count > 8192)
                throw std::runtime_error("invalid ERAT action count");
            for (std::uint32_t i = 0; i < count; ++i)
            {
                const int action = reader.selector(static_cast<unsigned int>(Action::Dead));
                const int weapon = reader.selector(static_cast<unsigned int>(WeaponClass::Shield));
                const int grip = reader.selector(1);
                const int load = reader.selector(static_cast<unsigned int>(Load::Heavy));
                const int oneHand = reader.selector(255);
                const int twoHand = reader.selector(255);
                if (action < 0 || (weapon < 0) != (grip < 0)
                    || (weapon >= 0 && (load >= 0 || oneHand < 0 || twoHand < 0))
                    || (weapon < 0 && (oneHand >= 0 || twoHand >= 0)))
                    throw std::runtime_error("inconsistent ERAT action selectors");
                if (!mActions.emplace(Key{ action, weapon, grip, load }, reader.definition()).second)
                    throw std::runtime_error("duplicate ERAT action");
                if (weapon >= 0)
                {
                    mMovesets.emplace(static_cast<WeaponClass>(weapon), grip != 0);
                    for (bool twoHanded : { false, true })
                    {
                        const unsigned int category = twoHanded ? twoHand : oneHand;
                        const auto [entry, inserted] = mStances.emplace(
                            std::make_pair(static_cast<WeaponClass>(weapon), twoHanded), category);
                        if (!inserted && entry->second != category)
                            throw std::runtime_error("inconsistent ERAT weapon stance");
                    }
                }
            }
            if (stream.peek() != std::char_traits<char>::eof())
                throw std::runtime_error("trailing ERAT timing data");
        }

        std::size_t size() const { return mActions.size(); }

        bool hasWeaponMoveset(WeaponClass weapon, bool twoHanded) const
        {
            return mMovesets.find({ weapon, twoHanded }) != mMovesets.end();
        }

        unsigned int stanceCategory(WeaponClass weapon, bool twoHanded) const
        {
            const auto found = mStances.find({ weapon, twoHanded });
            return found == mStances.end() ? 0 : found->second;
        }

        std::optional<ActionDef> actionDefinition(Action action, const Config& config, bool twoHanded) const
        {
            const auto weapon = action >= Action::LeftLight1 && action <= Action::LeftLight6
                ? config.mLeftWeaponClass : config.mWeaponClass;
            const auto weaponDefinition = mActions.find(
                { static_cast<int>(action), static_cast<int>(weapon), twoHanded, -1 });
            if (weaponDefinition != mActions.end())
            {
                const float speed = std::isfinite(config.mSpeedMultiplier)
                    ? std::clamp(config.mSpeedMultiplier, 0.1f, 5.f) : 1.f;
                return scaled(weaponDefinition->second, speed);
            }
            auto base = mActions.find({ static_cast<int>(action), -1, -1, static_cast<int>(config.mLoad) });
            if (base == mActions.end())
                base = mActions.find({ static_cast<int>(action), -1, -1, -1 });
            return base == mActions.end() ? std::nullopt : std::optional<ActionDef>(base->second);
        }
    };

#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
    using RuntimeTimingStorage = std::atomic<std::shared_ptr<const TimingData>>;
#else
    // Older libc++ versions lack the C++20 specialization despite accepting
    // C++20 language mode. Retain their supported atomic shared-pointer API.
    using RuntimeTimingStorage = std::shared_ptr<const TimingData>;
#endif

    inline RuntimeTimingStorage& runtimeTimingStorage()
    {
        static RuntimeTimingStorage data;
        return data;
    }

    inline std::shared_ptr<const TimingData> runtimeTimings()
    {
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
        return runtimeTimingStorage().load();
#else
        return std::atomic_load(&runtimeTimingStorage());
#endif
    }

    // Publish only a fully validated immutable table. Existing actions own
    // their definitions and keep their captured clocks if the table changes.
    inline void setRuntimeTimings(std::shared_ptr<const TimingData> data)
    {
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
        runtimeTimingStorage().store(std::move(data));
#else
        std::atomic_store(&runtimeTimingStorage(), std::move(data));
#endif
    }
}

#endif
