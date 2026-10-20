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
No commit, push, publication or Windows package update is currently requested.

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
git diff --check
```

Use C++20. Test fallback without imported_timings.hpp in an isolated source copy;
do not remove the active checkout's generated inputs. Actual-bank tests accept
`../ELDEN-RING-Combat-Rewrite/assets/player_anims.bin`. Local renderer callback
runner: `python3 build-tools-local/run-renderer-validation.py . renderer-check`.
`CI/eldenring-compile-objects.py` performs object compilation, not a full link.

## Current state

2026-10-20 controls/sheathing correction: LMB/RB uses the right-hand light
chain; RMB/LB uses matching weapons' paired chain/contextual moves, with each
button's authored cancel windows. Off-hand weapons now follow native weapon
attach/detach events; sheathing hides them while shields/torches retain their
own visibility. Loaded actors with only a drawn off-hand weapon are supported.
Evidence: `build-eldencombat/paired-controls-sheathing-20261020/validation-summary.json`.
Full engine link and in-game checks remain pending; no installed package change.

2026-10-19 extraction: the generated data retains
explicit hit hands and all air contacts; cross-category TAE imports recover
seven curved-greatsword clips. Current local bank: 1,556 clips; 1,085 actions,
including 175 paired across 23 source classes. Prior 1,549 clips are unchanged.
Both copy helpers reject stale paired timings/assets before destination writes.
Native misses allow other contacts in the same update; landing retains per-hand
consumption and prepaid stamina. Source/asset/controller/copy/CPU callback
evidence: `build-eldencombat/dual-wield-20261019/validation-summary.json`.
Windows copy/rebuild/install was not requested or performed; full link and
in-game paired attack checks remain pending. Reference tool/schema changes are
carried in `scripts/eldenring_reference.patch`.

2026-10-19: branch `feat/creature-boss-arts-guard`, HEAD `d6e546d358`; uncommitted
creature/boss/guard, spear, ground-recovery and paralysis work remains. New combo
arm corrections, timed right/left two-hand grips, right-first inventory equipping,
dual-weapon attachment alignment and shielded spear thrusts are source-only.
They use the existing unchanged bank. Local controller, actual-bank and native
skeleton/callback regressions pass; affected native units compile. Evidence:
`build-eldencombat/grip-combos-20261019/validation-summary.json`.
Follow-up: user confirmed reversed left longsword orientation during dual wield,
with left two-handing correct. Ordinary off-hand attachment now accounts for the
authored left socket's reversed blade axis; the two-hand path is unchanged.
Its physical blade/hand regression and evidence are in
`build-eldencombat/left-sword-edge-20261019/validation-summary.json`.
Full engine link, installed Windows rebuild and rendered/live inventory checks
for these new changes remain pending. No package update was requested.

The previous Guile/paralysis/ground-recovery state passed a Windows Release link,
23 Windows suites and selected actual-engine/rendered checks. Those results do
not validate the new combo/grip work. Original boss quest/subjective feel and an
active player strike visual remain unverified. Details are in parent HANDOFF.

Cleanup moved required tools/audio here and removed duplicate checkout/builds.
Fresh cleanup checks pass 19 CTest suites, 110 helper tests, 9 reference tests,
renderer/bank checks and offline decoder rebuild. All 85 symlinks resolve.
Evidence: `build-eldencombat/workspace-cleanup-20261019/validation-summary.json`.
Previous gameplay evidence is in `build-eldencombat/guile-speed-stab-20261019/`.
After substantive work replace current state/pending checks in the shared handoff;
keep this file concise and do not append transcripts or superseded reports.
