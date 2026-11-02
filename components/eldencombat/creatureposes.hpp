#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_CREATUREPOSES_H
#define OPENMW_COMPONENTS_ELDENCOMBAT_CREATUREPOSES_H

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace EldenCombat
{
    // These native upright humanoid rigs omit Bethesda's Bipedal gameplay flag.
    // Their two arms, two legs and torso match the existing player pose bank.
    // Admission still requires the renderer's complete, non-degenerate rig
    // check. Do not infer quadruped support from similarly named Bip01 bones.
    inline std::string normalizedCreatureModel(std::string model)
    {
        std::transform(model.begin(), model.end(), model.begin(), [](unsigned char c) {
            return c == '\\' ? '/' : (c >= 'A' && c <= 'Z' ? char(c + 'a' - 'A') : char(c));
        });
        if (model.starts_with("meshes/"))
            model.erase(0, 7);
        return model;
    }

    inline bool nativeCreatureModel(std::string_view model, std::string_view expected)
    {
        // Objects::insertCreature selects the native x-prefixed mesh whenever
        // its separate .kf exists. Match that exact alias as well as the
        // record's original mesh; do not admit arbitrary replacement paths.
        return model == expected
            || (model.starts_with("r/x") && expected.starts_with("r/") && model.substr(3) == expected.substr(2));
    }

    // Only the Guile encounter uses this adapter. Hircine's dialogue forms,
    // bear and wolf aspects retain their native poses and quest choreography.
    inline bool hircineGuileModel(std::string_view actorId, std::string model)
    {
        constexpr std::string_view guile = "bm_hircine_huntaspect";
        const bool matches = actorId.size() == guile.size()
            && std::equal(actorId.begin(), actorId.end(), guile.begin(), [](unsigned char a, char b) {
                return (a >= 'A' && a <= 'Z' ? char(a + 'a' - 'A') : char(a)) == b;
            });
        return matches && nativeCreatureModel(normalizedCreatureModel(std::move(model)), "r/hircine.nif");
    }

    inline bool humanoidCreatureModel(std::string model)
    {
        model = normalizedCreatureModel(std::move(model));
        constexpr std::array<std::string_view, 6> models{
            "r/ashslave.nif", "r/ashzombie.nif", "r/ashvampire.nif",
            "r/dagothr.nif", "r/cr_draugr.nif", "r/draugrlord.nif",
        };
        return std::any_of(models.begin(), models.end(), [&](std::string_view expected) {
            return nativeCreatureModel(model, expected);
        });
    }
}
#endif
