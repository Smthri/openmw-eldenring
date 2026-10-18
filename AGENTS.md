# OpenMW Elden Ring source fork

Before working, read **[`../AGENTS.md`](../AGENTS.md)** when this checkout is
part of the combined workspace. It records local assets, build paths and prior
validation. A standalone clone should use this file, `README.md`,
`ELDEN-COMBAT.txt`, `components/eldencombat/README.txt`, and `CONTRIBUTING.md`.
Check the branch, status and recent log before editing.

## Ownership and repository boundaries

Production is C++20 OpenMW, not the Rust reference process. Keep standalone
combat behavior in `components/eldencombat` and engine ownership in adapters.
Preserve native physics, damage, spells, equipment, saves, custom profiles and
fallback combat without imported data.

In the combined workspace, the sibling `../openmw` checkout has a different
upstream base and local validation tools/audio assets. Port targeted changes;
do not replace whole upstream files or discard that checkout's local work.
The reference extraction/baking changes, including their tests, are carried by
`scripts/eldenring_reference.patch` so a fresh clone can reproduce the tools.

## Source publication

Generated game tables, animation/audio banks, extracted archives, installed-game
fixtures, runtime packages, saves and credentials stay local and ignored.
Never force-add them. Retain the GPL, component/reference MIT and third-party
license notices. Read `CI/eldenring-audit-source.py` before publishing and audit
both the staged diff and outgoing history. Passing the guard does not replace
manual source-provenance review.

The older `scripts/publish_eldenring_fork.py` changes remote/default-branch
configuration and has an unresolved remote-parsing defect. Use reviewed,
explicit Git and GitHub CLI commands for routine branches and pull requests.

## Validation

Use the checkout's configured toolchain, or in the combined workspace source
`../openmw/CI/eldenring-build-env.sh`. Standalone checks:

```sh
cmake -S . -B build-eldencombat-tests -G Ninja -DBUILD_ELDENCOMBAT_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-eldencombat-tests --parallel 2
ctest --test-dir build-eldencombat-tests --output-on-failure --no-tests=error
python3 scripts/import_eldenring_timings.py --self-test
git diff --check
```

Use a C++20 compiler. Validate fallback behavior without generated
`components/eldencombat/imported_timings.hpp` using an isolated source copy;
do not remove local assets from an active checkout just to test fallback.
Relevant Python regressions cover profiles, copy integrity and animation
installation. Actual-bank tests accept a separately supplied local bank.

## Current work and verification

The October 17-18 follow-ups cover bows, shield/carry poses, effect-based casting,
levitation, native creature attack timing and movement, Almalexia's native
out-of-combat pacing, circling AI, configurable poise/dodge stamina, sustained
player passage through blocking actors, and customized fresh-profile defaults.
Implementation and remaining gameplay checks are described in `ELDEN-COMBAT.txt`.

Publication validation on 2026-10-18 passed a fresh Release build and 17/17
standalone suites with local timing data, plus 13/13 suites in a separate
source-only build without generated timings. All 60 Python regressions and
actual-bank controller, bow, casting, locomotion, throwing and parser checks
passed. Local evidence is under `build-eldencombat/publication-20261018/`.
These are CPU/helper checks; this publication task did not rebuild native engine
objects, deploy Windows, or perform interactive gameplay/rendering validation.
Prior full Windows Release build/link and 20/20 suites are recorded in the
combined workspace handoff; live gameplay remains a separate check.

After substantive work, update the shared handoff if available and relevant
checkout documentation. Record what changed, checks actually run, their scope,
remaining verification and any required source/asset/Windows synchronization.
