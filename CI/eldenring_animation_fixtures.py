"""Synthetic paired timing headers for installer and source-copy regressions."""


def paired_timings(actions):
    weapons = sorted({weapon for weapon, _action in actions})
    categories = {weapon: 100 + index for index, weapon in enumerate(weapons)}
    categories["StraightSword"] = 23
    animation_ids = {"PairedRun": 34200, "PairedRoll": 34300, "PairedBackstep": 34400, "PairedJump": 34530,
                     **{f"PairedLight{index}": 34000 + (index - 1) * 10 for index in range(1, 7)}}
    lines = []
    dispatch = []
    for index, (weapon, action) in enumerate(sorted(actions)):
        name = f"definition{index}"
        category = categories[weapon]
        phases = [(name, animation_ids[action], 2)]
        if action == "PairedJump":
            phases += [(name + "Landing", 34570, 2), (name + "LandingShort", 34571, 0)]
        for phase, animation, hits in phases:
            lines += [f"        inline std::optional<ActionDef> {phase}(const Config& config)", "        {",
                      "            ActionDef result;", f'            result.mAnimation = "a{category:03}_{animation:06}";']
            if phase == name and action == "PairedJump":
                for suffix in ("Landing", "LandingShort"):
                    lines += [f"            result.m{suffix} = std::make_shared<ActionDef>(*{name}{suffix}(config));"]
            for hit in range(hits):
                hand = "true" if hit else "false"
                lines += [f"            result.mHits[{hit}] = {{ 10.0f, 14.0f, 1.0f, 12.0f, 4.0f, 1.0f, 0.02f, {hand}, true }};"]
            lines += [f"            result.mHitCount = {hits};", "            return result;", "        }", ""]
        dispatch += [f"        if (action == Action::{action} && config.mWeaponClass == WeaponClass::{weapon} && !twoHanded)",
                     f"            return ImportedTimingsDetail::{name}(config);"]
    return "\n".join(lines + dispatch) + "\n"
