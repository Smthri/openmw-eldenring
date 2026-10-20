#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_BOSSARTS_H
#define OPENMW_COMPONENTS_ELDENCOMBAT_BOSSARTS_H

#include "combat.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace EldenCombat
{
    // Authored OpenMW abilities, using native magic effects at melee contact.
    // These are not extracted Elden Ring weapon skills or new native spells
    // added to the actor's spellbook. Ordinary attack damage remains unchanged.
    enum class BossArtEffect { Fire, Frost, Shock, AbsorbHealth, DamageFatigue, Poison, Burden };

    struct BossArtMagic
    {
        BossArtEffect mEffect;
        int mMagnitude;
        int mDuration;
    };

    struct BossArtProfile
    {
        std::string_view mId;
        std::string_view mName;
        float mTellSeconds;
        float mCooldownSeconds;
        std::array<BossArtMagic, 2> mEffects;
        std::size_t mEffectCount;
        bool mSpearThrust = false;
        float mChance = 1.f;
    };

    inline constexpr std::array<BossArtProfile, 11> BossArts{{
        { "ash_grasp", "Ashen Grasp", 1.1f, 16.f,
            {{{ BossArtEffect::AbsorbHealth, 6, 3 }, { BossArtEffect::DamageFatigue, 10, 2 }}}, 2 },
        { "divine_flame", "Divine Flame", 0.9f, 14.f,
            {{{ BossArtEffect::Fire, 12, 2 }, {}}}, 1 },
        { "divine_spear", "Divine Spear", 0.9f, 14.f,
            {{{ BossArtEffect::Shock, 12, 2 }, {}}}, 1 },
        { "clockwork_surge", "Clockwork Surge", 1.2f, 16.f,
            {{{ BossArtEffect::Shock, 8, 3 }, { BossArtEffect::DamageFatigue, 15, 1 }}}, 2 },
        { "winter_crush", "Winter Crush", 1.2f, 16.f,
            {{{ BossArtEffect::Frost, 10, 2 }, { BossArtEffect::Burden, 20, 3 }}}, 2 },
        { "hunt_rend", "Hunter's Rend", 0.7f, 12.f,
            {{{ BossArtEffect::DamageFatigue, 25, 1 }, { BossArtEffect::Burden, 15, 2 }}}, 2 },
        { "soul_rend", "Soul Rend", 1.f, 16.f,
            {{{ BossArtEffect::AbsorbHealth, 5, 3 }, {}}}, 1 },
        { "arcane_strike", "Arcane Strike", 0.9f, 14.f,
            {{{ BossArtEffect::Shock, 8, 2 }, {}}}, 1 },
        { "venom_cut", "Venom Cut", 0.65f, 14.f,
            {{{ BossArtEffect::Poison, 4, 4 }, {}}}, 1 },
        { "earth_splitter", "Earth Splitter", 0.85f, 12.f,
            {{{ BossArtEffect::DamageFatigue, 20, 1 }, { BossArtEffect::Burden, 10, 2 }}}, 2 },
        { "hunters_thrust", "Hunter's Thrust", 0.25f, 12.f,
            {{{ BossArtEffect::DamageFatigue, 15, 1 }, {}}}, 1, true, 0.3f },
    }};

    inline const BossArtProfile* bossArtForRecord(std::string_view record)
    {
        struct Mapping { std::string_view mRecord; std::size_t mArt; };
        // Exact unique opponents from the official game/expansion encounter
        // list. Similar names and generic creatures deliberately do not match.
        static constexpr Mapping mappings[]{
            { "dagoth_ur_1", 0 }, { "dagoth_ur_2", 0 },
            { "dagoth gares", 0 }, { "dagoth uthol", 0 }, { "dagoth odros", 0 },
            { "dagoth vemyn", 0 }, { "dagoth endus", 0 }, { "dagoth tureynul", 0 },
            { "dagoth gilvoth", 0 }, { "dagoth araynys", 0 },
            { "almalexia", 1 }, { "almalexia_warrior", 1 }, { "vivec_god", 2 }, { "imperfect", 3 },
            { "bm_frost_giant", 4 }, { "draugr_aesliip", 4 },
            { "bm_hircine_huntaspect", 10 }, { "bm_hircine_straspect", 5 }, { "bm_hircine_spdaspect", 5 },
            { "tharsten heart-fang", 5 }, { "tharsten heart-fang2", 5 }, { "thauraver", 5 },
            { "lich_relvel", 6 }, { "lich_barilzar", 6 }, { "dahrk mezalf", 6 },
            { "mastrius", 6 }, { "raxle berne", 6 }, { "dhaunayne aundae", 6 }, { "volrina quarra", 6 },
            { "umbra", 6 }, { "divayth fyr", 7 }, { "gothren", 7 }, { "trebonius artorius", 7 },
            { "golena sadri_a", 7 }, { "golena sadri", 7 },
            { "dandras vules", 8 }, { "king hlaalu helseth", 8 }, { "carnius magius", 8 },
            { "gaenor_b", 9 }, { "bolvyn venim", 9 }, { "sjoring hard-heart", 9 },
            { "orvas dren druglord", 9 }, { "karrod", 9 }, { "salas valor", 9 },
            { "ulfgar the unending", 9 }, { "falx carius2", 9 }, { "golden saint_staada", 7 },
            { "dremora_special_fyr", 1 }, { "dremora_ttmg", 1 }, { "dremora_lord_khash_uni", 1 },
            { "goblin_warchief1", 9 }, { "goblin_warchief2", 9 }, { "armion", 9 }, { "yarnar", 9 },
        };
        for (const auto& mapping : mappings)
            if (record.size() == mapping.mRecord.size()
                && std::equal(record.begin(), record.end(), mapping.mRecord.begin(), [](char a, char b) {
                    return (a >= 'A' && a <= 'Z' ? a + ('a' - 'A') : a) == b;
                }))
                return &BossArts[mapping.mArt];
        return nullptr;
    }

    class BossArtState
    {
        enum class Phase { None, Tell, Attack };
        Phase mPhase = Phase::None;
        const BossArtProfile* mProfile = nullptr;
        float mCooldown = 4.f;
        float mTellRemaining = 0.f;
        float mAttackWait = 0.f;
        std::uint64_t mSequence = 0;

        bool matches(const Attack& attack) const
        {
            if (mProfile && mProfile->mSpearThrust)
                return attack.mAction == Action::SpearThrust && attack.mSequence == mSequence + 1;
            // Native creatures go straight to Heavy1; imported humanoids pass
            // through Heavy1Charge first. Never attach the effect to a combo.
            return (attack.mAction == Action::Heavy1
                       && (attack.mSequence == mSequence + 1 || attack.mSequence == mSequence + 2))
                || (attack.mAction == Action::Heavy1Charge && attack.mSequence == mSequence + 1);
        }

    public:
        void reset() { *this = {}; }
        void cancel() { mPhase = Phase::None; mProfile = nullptr; }
        bool preparing() const { return mPhase == Phase::Tell; }
        bool ready() const { return preparing() && mTellRemaining <= 0.f; }
        bool active() const { return mPhase != Phase::None; }
        float cooldown() const { return mCooldown; }
        const BossArtProfile* profile() const { return mProfile; }

        bool begin(const BossArtProfile& profile, const Controller& combat)
        {
            if (active() || mCooldown > 0.f || combat.action() != Action::Idle)
                return false;
            mProfile = &profile;
            mPhase = Phase::Tell;
            mTellRemaining = profile.mTellSeconds;
            mCooldown = profile.mCooldownSeconds;
            mSequence = combat.sequence();
            return true;
        }

        bool launch(const Controller& combat)
        {
            if (!ready() || combat.action() != Action::Idle || combat.sequence() != mSequence)
                return false;
            mPhase = Phase::Attack;
            mAttackWait = 0.5f;
            return true;
        }

        void update(const Controller& combat, float seconds, bool available)
        {
            const float step = std::isfinite(seconds) ? std::clamp(seconds, 0.f, 1.f) : 0.f;
            mCooldown = std::max(0.f, mCooldown - step);
            if (!available)
            {
                cancel();
                return;
            }
            if (preparing())
            {
                // A reaction, teleport/reset, spell, or unrelated action
                // interrupts the tell without refunding its cooldown.
                if (combat.action() != Action::Idle || combat.sequence() != mSequence)
                    cancel();
                else
                    mTellRemaining = std::max(0.f, mTellRemaining - step);
            }
            else if (mPhase == Phase::Attack)
            {
                const auto hit = combat.activeHit();
                if (hit && matches(*hit))
                    return; // Preserve an impact crossed by a long update.
                const auto sequence = combat.sequence();
                const bool charging = combat.action() == Action::Heavy1Charge && sequence == mSequence + 1;
                const bool attacking = matches(combat.attack());
                if ((charging && !mProfile->mSpearThrust) || attacking)
                    return;
                mAttackWait -= step;
                if (sequence != mSequence || combat.action() != Action::Idle || mAttackWait <= 0.f)
                    cancel();
            }
        }

        const BossArtProfile* consume(const Attack& attack)
        {
            if (mPhase != Phase::Attack || !matches(attack))
                return nullptr;
            const auto* result = mProfile;
            cancel(); // One contact per art, including a blocked contact.
            return result;
        }
    };
}

#endif
