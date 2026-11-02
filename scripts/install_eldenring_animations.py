#!/usr/bin/env python3
"""Validate and install a user's locally baked Elden Ring animations.

Example:
    python scripts/install_eldenring_animations.py --source ../ELDEN-RING-Combat-Rewrite/assets/player_anims.bin --package build-eldenring-package-x64

Only the animation bank is installed; profiles, settings and saves are untouched.
Existing banks are backed up before an atomic replacement. No game assets belong
in the source repository. Python 3.10+ and its standard library are sufficient.
"""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
DESTINATION = Path("resources/vfs/animations/eldencombat/player_anims.bin")
MAX_BYTES = 256 * 1024 * 1024
POSITION_JOINTS = {
    "Pelvis", "Spine", "Spine1", "Spine2", "Neck", "Head",
    *(side + bone for side in ("L_", "R_") for bone in (
        "Clavicle", "UpperArm", "Forearm", "Hand", "Thigh", "Calf", "Foot", "Toe0", "Weapon",
        "Finger0", "Finger01", "Finger02", "Finger1", "Finger2", "Finger21", "Finger22")),
}
ORIENTED_JOINTS = {"Head", "R_Weapon", "L_Weapon"}
LOCOMOTION = (*range(20000, 20004), *range(20100, 20104), 20200, *range(22100, 22104))
BASE_ANIMATIONS = (
    0, 100, *LOCOMOTION, 300000, *range(320000, 320004), *range(320100, 320104),
    322100, 390000, 390001, 202040, 4000, 17002, 50170,
    29010, 29020, 29040, 29050, 29060, 29080,  # Right/left grip changes and their completion phases.
)
# Wind-up and uncharged release phases used by spellcasting.hpp. Keep these
# mandatory even in a copied Windows checkout without the recreation sources.
SPELL_ANIMATIONS = {
    "a433_045000", "a433_045010",  # Urgent Heal: self
    "a506_045000", "a506_045010",  # Catch Flame: touch
    "a417_045000", "a417_045011",  # Flame Sling: target
    "a426_045000", "a426_045010",  # Shadow Bait / Darkness: conjuration and utility
    "a431_045000", "a431_045010",  # Flame, Grant Me Strength: fortification
    "a434_045000", "a434_045011",  # Heal: area restoration
    "a436_045000", "a436_045011",  # Lightning Spear: shock projectile
}
# Native bows use the longbow's draw/held-aim/release phases and a014 carry.
# They are outside imported_timings.hpp, which contains melee actions only.
BOW_ANIMATIONS = {
    "a044_036010", "a044_036020", "a044_036000",
    *(f"a014_{animation:06d}" for animation in (
        0, *LOCOMOTION, 300000, *range(320000, 320004), *range(320100, 320104),
        322100, 390000, 390001)),
}
# The local extractor's supported paired movesets. Torch/shield have no paired attacks.
PAIRED_LIGHT_CHAINS = {
    **dict.fromkeys(("Dagger", "StraightSword", "ThrustingSword", "Katana", "Hammer", "Axe",
                     "CurvedSword", "Flail"), 4),
    **dict.fromkeys(("Greatsword", "ColossalSword", "Spear", "Halberd", "HeavyThrustingSword", "CurvedGreatsword", "Twinblade",
                     "GreatHammer", "Greataxe", "GreatSpear", "Reaper", "Whip", "ColossalWeapon"), 3),
    "Fist": 6, "Claw": 6,
}
PAIRED_ACTIONS = {
    (weapon, action)
    for weapon, count in PAIRED_LIGHT_CHAINS.items()
    for action in (*(f"PairedLight{index}" for index in range(1, count + 1)),
                   "PairedRun", "PairedRoll", "PairedBackstep", "PairedJump")
}


def load_auditor():
    spec = importlib.util.spec_from_file_location("eldenring_animation_audit", ROOT / "CI/eldenring-audit-animations.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha256(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def required_clips(timings_header: Path) -> set[str]:
    """The shipped engine's actions and stance categories must all be available."""
    source = timings_header.read_text(encoding="utf-8")
    actions = set(re.findall(r'\bmAnimation\s*=\s*"(a\d{3}_\d{6})"', source))
    stances = re.findall(r'\breturn\s+twoHanded\s*\?\s*(\d+)u\s*:\s*(\d+)u\s*;', source)
    if not actions or not stances:
        raise ValueError(f"no imported animation metadata in {timings_header}; regenerate the timing header first")
    expected = actions | {f"a000_{animation:06d}" for animation in BASE_ANIMATIONS} | SPELL_ANIMATIONS | BOW_ANIMATIONS
    for two, one in stances:
        for stance in (int(one), int(two)):
            expected.update(f"a{stance:03d}_{animation:06d}" for animation in (0, 100, *LOCOMOTION))
    return expected


def paired_requirements(timings_header: Path) -> dict:
    """Reject stale generated tables that cannot drive both paired weapons.

    This reads only the importer's generated C++ layout, without executing it.
    Clip inventory alone cannot detect an old table that discarded hit hands.
    """
    source = timings_header.read_text(encoding="utf-8")
    dispatch = re.findall(
        r'if \(action == Action::(Paired\w+) && config\.mWeaponClass == WeaponClass::(\w+) && !twoHanded\)'
        r'\s*return ImportedTimingsDetail::(definition\w+)\(config\);', source)
    mapped = {(weapon, action): name for action, weapon, name in dispatch}
    missing = sorted(PAIRED_ACTIONS - mapped.keys())
    if missing:
        examples = ", ".join(f"{weapon}/{action}" for weapon, action in missing[:8])
        raise ValueError(f"timing header is missing {len(missing)} paired actions: {examples}; regenerate timings first")
    if len(mapped) != len(dispatch):
        raise ValueError("timing header has duplicate paired actions; regenerate timings first")
    definitions = dict(re.findall(
        r'inline std::optional<ActionDef> (definition\w+)\([^)]*\)\s*\{(.*?)^        \}',
        source, flags=re.DOTALL | re.MULTILINE))
    jump_definitions = {name for (weapon, action), name in mapped.items() if action == "PairedJump"}
    pending = set(mapped.values())
    visited = set()
    clips = set()
    hit_windows = 0
    while pending:
        name = min(pending)
        pending.remove(name)
        if name in visited:
            continue
        visited.add(name)
        body = definitions.get(name, "")
        animation = re.search(r'\bmAnimation\s*=\s*"(a\d{3}_\d{6})"', body)
        count = re.search(r'\bmHitCount\s*=\s*(\d+)\s*;', body)
        hits = re.findall(r'\bmHits\[(\d+)\]\s*=\s*\{([^}]+)\}\s*;', body)
        if (not animation or not count or len(hits) != int(count[1])
                or {int(index) for index, _ in hits} != set(range(int(count[1])))):
            raise ValueError(f"paired timing definition {name} is incomplete; regenerate timings first")
        if int(count[1]) == 0 and not name.endswith("LandingShort"):
            raise ValueError(f"paired timing definition {name} has no hit windows; regenerate timings first")
        for index, values in hits:
            fields = [field.strip() for field in values.split(",")]
            if len(fields) != 9 or fields[-2] not in ("true", "false") or fields[-1] != "true":
                raise ValueError(f"paired timing definition {name} hit {index} lacks authored hand metadata; "
                                 "regenerate timings with the updated extractor and importer first")
        clips.add(animation[1])
        hit_windows += int(count[1])
        landing = dict(re.findall(
            r'\bm(Landing(?:Short)?)\s*=\s*std::make_shared<ActionDef>\(\*(definition\w+)\(config\)\);', body))
        if name in jump_definitions:
            if landing.keys() != {"Landing", "LandingShort"}:
                raise ValueError(f"paired timing definition {name} lacks jump landing phases; regenerate timings first")
        pending.update(landing.values())
    return {"paired_actions": len(mapped), "paired_hit_windows": hit_windows, "paired_clips": sorted(clips)}


def validate(source: Path, timings_header: Path, expected_hash: str | None = None,
             require_paired: bool = False) -> dict:
    if not source.is_file() or not 16 <= source.stat().st_size <= MAX_BYTES:
        raise ValueError(f"animation source must be a regular ERAN file of at most {MAX_BYTES} bytes: {source}")
    auditor = load_auditor()
    report, clips = auditor.read_clips(source)
    if expected_hash is not None and report["sha256"].lower() != expected_hash.lower():
        raise ValueError("animation source changed since validation (SHA256 mismatch)")
    if not POSITION_JOINTS <= set(report["position_joints"]) or not ORIENTED_JOINTS <= set(report["oriented_joints"]):
        raise ValueError("animation bank lacks the humanoid joints or weapon/head axes needed for retargeting")
    expected = required_clips(timings_header)
    if require_paired:
        report.update(paired_requirements(timings_header))
        expected.update(report["paired_clips"])
    recreation = source.parent.parent
    if (recreation / "src/sim/extracted.rs").is_file() and (recreation / "tools/bake_anims.py").is_file():
        expected |= auditor.expected_clips(recreation)
        report["inventory"] = "engine requirements and local recreation's complete clip inventory"
    else:
        report["inventory"] = "engine action, stance and locomotion requirements"
    missing = sorted(expected - clips)
    if missing:
        raise ValueError(f"animation bank is missing {len(missing)} required clips: {', '.join(missing[:12])}")
    report["required_clips"] = len(expected)
    report["spellcasting_clips"] = sorted(SPELL_ANIMATIONS)
    report["bow_clips"] = sorted(BOW_ANIMATIONS)
    return report


def install(source: Path, package: Path, report: dict, backup_directory: Path | None = None) -> dict:
    destination = package / DESTINATION
    if destination.exists() and not destination.is_file():
        raise ValueError(f"animation destination is not a file: {destination}")
    old_hash = sha256(destination) if destination.is_file() else None
    if old_hash == report["sha256"]:
        return {**report, "destination": str(destination), "changed": False}

    # Check the copied bytes too: the source may change after validation or during a network/UNC copy.
    destination.parent.mkdir(parents=True, exist_ok=True)
    stage = None
    backup = None
    try:
        with tempfile.NamedTemporaryFile(prefix=".player_anims-", suffix=".tmp", dir=destination.parent, delete=False) as stream:
            stage = Path(stream.name)
            with source.open("rb") as original:
                shutil.copyfileobj(original, stream)
            stream.flush()
            os.fsync(stream.fileno())
        if sha256(stage) != report["sha256"]:
            raise ValueError("animation source changed while copying; the installed bank was preserved")
        if old_hash is not None:
            if backup_directory is None:
                backup_directory = package.parent / "build-eldenanimation-backups" / package.name
            backup_directory.mkdir(parents=True, exist_ok=True)
            backup = backup_directory / f"player_anims-{old_hash}.bin"
            if not backup.exists():
                shutil.copyfile(destination, backup)
            if sha256(backup) != old_hash or sha256(destination) != old_hash:
                raise ValueError("animation backup or installed bank changed; replacement cancelled")
        elif destination.exists():
            raise ValueError("animation destination appeared during copying; replacement cancelled")
        os.replace(stage, destination)
        stage = None
    finally:
        if stage is not None:
            stage.unlink(missing_ok=True)
    return {**report, "destination": str(destination), "changed": True,
            "backup": str(backup) if backup is not None else None}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", required=True, type=Path, help="locally baked player_anims.bin")
    parser.add_argument("--package", type=Path, help="OpenMW runtime package directory")
    parser.add_argument("--timings-header", type=Path, default=ROOT / "components/eldencombat/imported_timings.hpp")
    parser.add_argument("--check-only", action="store_true", help="validate without writing any files")
    parser.add_argument("--require-paired", action="store_true",
                        help="require complete paired actions with authored per-hit hand assignments")
    parser.add_argument("--source-sha256", help="require the hash recorded during a previous validation")
    parser.add_argument("--backup-directory", type=Path, help="optional directory for previous animation banks")
    parser.add_argument("--json", action="store_true", help="print machine-readable validation/install results")
    args = parser.parse_args()
    if not args.check_only and args.package is None:
        parser.error("--package is required unless --check-only is used")
    try:
        source = args.source.resolve()
        report = validate(source, args.timings_header.resolve(), args.source_sha256, args.require_paired)
        if not args.check_only:
            report = install(source, args.package.resolve(), report, args.backup_directory)
    except (OSError, ValueError, SyntaxError, TypeError) as error:
        print(f"Animation installation failed: {error}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(f"Validated {report['clips']} clips and {report['frames']} frames; {report['required_clips']} required clips present.")
        if "paired_actions" in report:
            print(f"Paired weapons: {report['paired_actions']} actions, {report['paired_hit_windows']} authored hit windows, "
                  f"{len(report['paired_clips'])} clips.")
        print(f"SHA256: {report['sha256']}")
        if not args.check_only:
            print(f"{'Installed' if report['changed'] else 'Already installed'}: {report['destination']}")
            if report.get("backup"):
                print(f"Previous bank backup: {report['backup']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
