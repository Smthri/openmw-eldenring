# OpenMW Elden Ring fork

Read `../AGENTS.md` and `../HANDOFF.txt` in the combined workspace. This is its
only engine checkout; the original sibling `openmw/` was removed at the user's
request on 2026-10-19. `../ELDEN-RING-Combat-Rewrite/` remains the asset/reference
source. Check branch, status and recent log before editing. In a standalone
clone use this file, README, ELDEN-COMBAT.txt, component docs and CONTRIBUTING.md.

## Implementation and assets

Production is C++20 OpenMW. Keep standalone combat in `components/eldencombat`
and native ownership in adapters. Preserve physics, health/damage, spells,
equipment, quests, saves, custom profiles and fallback behavior without assets
or with Elden combat disabled. Almalexia keeps native speech pacing outside combat.
Disable paralysis zeroes effect duration for everyone through native cleanup.
Use the existing reference bank for imported poses; no external creature banks.
Reference tool changes are carried in `scripts/eldenring_reference.patch`.

Generated tables, banks/audio, extracted archives, game fixtures, packages and
saves stay local/ignored; never force-add them. Keep GPL/MIT/third-party notices.
Before publication review staged diff/outgoing history and run
`CI/eldenring-audit-source.py`. Prefer explicit reviewed Git commands over the
legacy publication helper, which changes remote/default-branch configuration.
Commit/push/publication and Windows package updates require a user request.

## Local validation

The local toolchain, headers and OSG libraries now live in `build-tools-local/`;
audio is in `build-eldenring-assets/{music,sound}`. No sibling engine is required.

```sh
source CI/eldenring-build-env.sh
cmake -S . -B build-eldencombat-tests -G Ninja -DBUILD_ELDENCOMBAT_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-eldencombat-tests --parallel 2
ctest --test-dir build-eldencombat-tests --output-on-failure --no-tests=error
python3 scripts/import_eldenring_timings.py --self-test
python3 CI/eldenring-test-copy.py
python3 CI/eldenring-test-animation-install.py
python3 CI/eldenring-test-profile.py
python3 CI/eldenring-test-weapon-visibility.py
python3 CI/eldenring-test-grip-state.py
# Requires desktop OpenGL 4.3 and EGL:
python3 CI/eldenring-test-map-lighting.py
git diff --check
```

Use C++20. Test fallback without imported_timings.hpp in an isolated source copy;
do not remove the active checkout's generated inputs. Actual-bank tests accept
`../ELDEN-RING-Combat-Rewrite/assets/player_anims.bin`. Local renderer callback
runner: `python3 build-tools-local/run-renderer-validation.py . renderer-check`.
`CI/eldenring-compile-objects.py` performs object compilation, not a full link.

## Current state

2026-10-20 problems.txt follow-up: exterior local maps skip point-light buffer
reads when lighting is excluded; normal and inventory cameras retain lighting.
Right-hand blades follow authored weapon axes. Imported callbacks preserve the
native pose across draw/sheath handoffs. Inventory uses a frozen imported ready
pose and current grip. Grip swaps keep gait/facing; cell transitions preserve
the applied grip while clearing pending actions. Staada's health multiplier,
save-load upgrade and compensating poise adjustment are reverted.

The preceding creature/boss/guard, paired controls, left-weapon orientation,
ground recovery and paralysis work is retained as a separate prerequisite commit.
LMB/RB remains right-hand light; RMB/LB uses matching paired chains. Current local
bank: 1,556 clips, 1,085 actions including 175 paired; assets are unchanged.

Evidence: `build-eldencombat/problems-20261020/validation-summary.json`.
The checks distinguish standalone suites, shader rendering, CPU pose callbacks,
production methods with service stubs, and native object compilation. Full engine
linking and live gameplay checks of this follow-up remain pending. No installed
Windows package/profile/save update was requested or performed.

After substantive work replace current state/pending checks in the shared handoff;
keep this file concise and do not append transcripts or superseded reports.
