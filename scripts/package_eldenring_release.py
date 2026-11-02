#!/usr/bin/env python3
"""Package a clean Windows Release install without profiles or game-derived data.

The installed tree is read-only. A new output directory and ZIP are created only
after input validation; existing destinations are never overwritten.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import tempfile
import zipfile


EXECUTABLES = ("openmw.exe", "openmw-launcher.exe", "openmw-wizard.exe",
               "openmw-navmeshtool.exe", "openmw-iniimporter.exe")
RUNTIME_REQUIRED = (*EXECUTABLES, "defaults.bin", "openmw.cfg", "gamecontrollerdb.txt",
                    "resources/vfs/builtin.omwscripts", "Qt6Core.dll", "Qt6Gui.dll",
                    "Qt6Widgets.dll", "Qt6Svg.dll", "platforms/qwindows.dll")
SCRIPTS = ("unpack_eldenring_assets.py", "import_eldenring_timings.py",
           "import_eldenring_music.py", "import_eldenring_sounds.py",
           "install_eldenring_animations.py", "install_eldenring_music.py",
           "install_eldenring_sounds.py")
GENERATED_NAMES = {"regulation.bin", "player_anims.bin", "player_sounds.bin", "action_timings.bin",
                   "timings.bin", "imported_timings.hpp", "extracted.rs",
                   "interior_cells.lua", "eldenring_cells.lua", "global_storage.bin", "player_storage.bin",
                   "input.xml", "input_v3.xml", "console_history.txt", "launcher.cfg",
                   "settings.cfg", "shaders.yaml"}
FORBIDDEN_SUFFIXES = {".omwsave", ".ess", ".esm", ".esp", ".bsa", ".bhd", ".bdt",
                      ".wem", ".ogg", ".mp3", ".flac", ".nif", ".hkx", ".dcx",
                      ".pdb", ".lib", ".obj", ".ilk", ".exp", ".log", ".wav",
                      ".tae", ".param", ".bnk", ".bnd", ".anibnd"}
REFERENCE_TOOLS = ("bake_anims.py", "erfmt.py", "extract.py", "hkanim.py",
                   "hkx.py", "param.py", "paths.py", "skel.py", "tae.py")
SOURCE_REQUIRED = {"CMakeLists.txt", "components/CMakeLists.txt",
                   "components/eldencombat/combat.cpp", "components/eldencombat/timingdata.hpp",
                   "apps/openmw/mwrender/animation.cpp", "scripts/import_eldenring_timings.py",
                   "scripts/package_eldenring_release.py", "CI/eldenring-setup-windows.ps1",
                   "CI/eldenring-import-assets-windows.ps1", "docs/eldenring-windows-release.txt"}
CODEBOOK_PATH = "scripts/eldenring_music/ww2ogg/packed_codebooks_aoTuV_603.bin"
CODEBOOK_SHA256 = "00a93eab267d281401b1efd54e888a2e183299b9e6c446c48d09f701a89d9d27"
CODEBOOK_PATHS = {CODEBOOK_PATH, "tools/" + CODEBOOK_PATH}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def files(root: Path) -> list[Path]:
    require(root.is_dir() and not root.is_symlink(), f"Missing regular directory: {root}")
    result = []
    for path in sorted(root.rglob("*")):
        require(not path.is_symlink(), f"Symlink is not a release input: {path}")
        if path.is_file():
            result.append(path)
    return result


def check_path(relative: Path) -> None:
    parts = {part.lower() for part in relative.parts}
    require(not (parts & {"elden-profile", "saves", "screenshots", "__pycache__",
                          ".git", ".venv", "er-files", "local-assets", ".elden-assets"}),
            f"Personal or generated directory in release input: {relative}")
    require(relative.name.lower() not in GENERATED_NAMES,
            f"Personal or game-derived file in release input: {relative}")
    require(not (relative.name.lower().startswith("oo2core") and relative.suffix.lower() == ".dll"),
            f"Proprietary decoder DLL in release input: {relative}")
    require(relative.suffix.lower() != ".bin"
            or relative.as_posix() in {"defaults.bin", *CODEBOOK_PATHS},
            f"Unreviewed binary data in release input: {relative}")
    require(relative.suffix.lower() not in FORBIDDEN_SUFFIXES,
            f"Game data, build output or log in release input: {relative}")
    require(not ({"music", "sound", "animations"} & parts and "eldenring" in parts),
            f"Imported game assets in release input: {relative}")
    require(not ("eldencombat" in parts and relative.suffix.lower() == ".bin"),
            f"Imported combat bank in release input: {relative}")


def check_personal_paths(path: Path) -> None:
    data = path.read_bytes()
    # Build paths are diagnostic metadata, not runtime dependencies. Reject
    # personal user directories while allowing neutral/public and vendor builds.
    for encoding in ("utf-8", "utf-16-le"):
        text = data.decode(encoding, errors="ignore")
        for match in re.finditer(r"(?i)[a-z]:[\\/]Users[\\/]([^\\/\x00\r\n\"<>]+)", text):
            require(match[1].lower() in {"public", "qt", "default", "all users"},
                    f"Personal Windows build/configuration path in {path.name}")
        require(not re.search(r"/home/(?!<|USER|user|username)[^/\x00\r\n\"<>]+/", text),
                f"Personal Linux build/configuration path in {path.name}")


def check_pe(path: Path) -> None:
    with path.open("rb") as stream:
        require(stream.read(2) == b"MZ", f"Not a Windows executable: {path.name}")
        stream.seek(0x3c)
        value = stream.read(4)
        require(len(value) == 4, f"Truncated executable: {path.name}")
        stream.seek(struct.unpack("<I", value)[0])
        require(stream.read(6) == b"PE\0\0\x64\x86", f"Not Windows x64: {path.name}")


def developer_file(relative: Path) -> bool:
    return (relative.as_posix() in {"resources/defaultfilters", "ELDEN-COMBAT.txt", "EldenCombat-README.txt"}
            or relative.parts[:2] == ("share", "doc"))


def validate_install(install: Path, build: Path) -> list[Path]:
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8-sig")
    require(re.search(r"^OPENMW_ELDENCOMBAT_COMPILED_TIMINGS:BOOL=OFF$", cache, re.M) is not None,
            "Release requires OPENMW_ELDENCOMBAT_COMPILED_TIMINGS=OFF in its build cache")
    require(re.search(r"^OPENMW_LOCAL_INSTALL:BOOL=ON$", cache, re.M) is not None,
            "Release requires OPENMW_LOCAL_INSTALL=ON")
    for relative in RUNTIME_REQUIRED:
        require((install / relative).is_file(), f"Missing runtime file: {relative}")
    inventory = files(install)
    for path in inventory:
        relative = path.relative_to(install)
        check_path(relative)
        if not developer_file(relative):
            check_personal_paths(path)
    for name in EXECUTABLES:
        check_pe(install / name)
        require((build / "Release" / name).is_file()
                and digest(install / name) == digest(build / "Release" / name),
                f"Installed {name} differs from its Release build")
    return inventory


def validate_source_archive(source: Path, archive: Path) -> int:
    """Compare every archived file with canonical source, including new files."""
    names = set()
    folded_names = set()
    with zipfile.ZipFile(archive) as zipped:
        for entry in zipped.infolist():
            if entry.is_dir():
                continue
            name = entry.filename
            relative = Path(name)
            require("\\" not in name and ":" not in name and not relative.is_absolute()
                    and ".." not in relative.parts and not any(ord(char) < 32 for char in name),
                    "Unsafe source archive entry")
            require(name not in names and name.casefold() not in folded_names, "Duplicate source archive entry")
            names.add(name)
            folded_names.add(name.casefold())
            require(not stat.S_ISLNK(entry.external_attr >> 16), "Source archive contains a symlink")
            check_path(relative)
            require(not any(part.lower().startswith("build-") or part.lower() in
                            {"build", "deps", ".aws", ".ssh", ".codex"} for part in relative.parts),
                    f"Private/build directory in source archive: {name}")
            require(relative.name != ".env" and not relative.name.startswith(".env.")
                    and relative.suffix.lower() not in {".dll", ".exe", ".zip", ".7z", ".pyc", ".key", ".pem"},
                    f"Private/build file in source archive: {name}")
            require(relative.suffix.lower() != ".bin" or name == CODEBOOK_PATH,
                    f"Unreviewed binary in source archive: {name}")
            original = source / relative
            require(original.is_file() and not original.is_symlink(), f"Source archive has no matching source: {name}")
            data = zipped.read(entry)
            archive_hash = hashlib.sha256(data).hexdigest()
            require(archive_hash == digest(original), f"Source archive differs from source: {name}")
            if name == CODEBOOK_PATH:
                require(archive_hash == CODEBOOK_SHA256, "Decoder codebook differs from its reviewed BSD source")
            require(re.search(
                rb"(?m)^(?:// Generated from a local extracted\.rs\.|"
                rb"#define OPENMW_COMPONENTS_ELDENCOMBAT_IMPORTED_TIMINGS_H\b|"
                rb"-- Interior record IDs from Morrowind\.esm)", data) is None,
                f"Generated game table in source archive: {name}")
            require(re.search(rb"-----BEGIN (?:RSA |OPENSSH |EC |DSA )?PRIVATE KEY-----", data) is None,
                    f"Private key in source archive: {name}")
    require(SOURCE_REQUIRED <= names, "Source archive is missing required release implementation")
    if (source / ".git").exists():
        result = subprocess.run(["git", "-C", str(source), "ls-files", "-z", "--cached", "--others",
                                 "--exclude-standard"], capture_output=True, check=True)
        expected = {name.decode("utf-8") for name in result.stdout.split(b"\0")
                    if name and (source / name.decode("utf-8")).is_file()}
        require(names == expected, "Source archive inventory differs from tracked and new authored source")
    return len(names)


def copy_file(source: Path, target: Path) -> None:
    require(source.is_file() and not source.is_symlink(), f"Missing regular source: {source}")
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)


def copy_tree(source: Path, target: Path) -> None:
    for path in files(source):
        copy_file(path, target / path.relative_to(source))


def cmd_wrapper(command: str, setup: bool = False) -> bytes:
    lines = ["@echo off", "setlocal DisableDelayedExpansion", 'pushd "%~dp0" || exit /b 1']
    if setup:
        lines.extend(['if not exist "elden-profile\\openmw.cfg" (',
                      '  powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\\Setup-EldenCombat.ps1"',
                      "  if errorlevel 1 (", "    popd", "    exit /b 1", "  )", ")"])
    lines.extend([command, 'set "result=%errorlevel%"', "popd",
                  "if not %result%==0 pause", "exit /b %result%", ""])
    return "\r\n".join(lines).encode("ascii")


def add_entrypoints(source: Path, stage: Path) -> None:
    for original, target in (("eldenring-setup-windows.ps1", "Setup-EldenCombat.ps1"),
                             ("eldenring-import-assets-windows.ps1", "Import-EldenRing.ps1")):
        copy_file(source / "CI" / original, stage / target)
        (stage / target.replace(".ps1", ".cmd")).write_bytes(cmd_wrapper(
            f'powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\\{target}" %*'))
    (stage / "Start-EldenCombat.cmd").write_bytes(cmd_wrapper(
        '".\\openmw.exe" --replace config --config "elden-profile" %*', setup=True))
    (stage / "Start-EldenCombat-Launcher.cmd").write_bytes(cmd_wrapper(
        '".\\openmw-launcher.exe" %*', setup=True))
    config = (stage / "openmw.cfg").read_text(encoding="utf-8-sig")
    config = re.sub(r"(?m)^\s*config\s*=.*$", 'config="elden-profile"', config)
    if not re.search(r"(?m)^config=", config):
        config += '\nconfig="elden-profile"\n'
    (stage / "openmw.cfg").write_text(config, encoding="utf-8")


def add_tools(source: Path, reference: Path, keys: Path, stage: Path) -> None:
    target = stage / "tools"
    for name in SCRIPTS:
        copy_file(source / "scripts" / name, target / "scripts" / name)
    for path in files(source / "scripts/eldenring_music"):
        if ("__pycache__" not in path.parts and path.suffix != ".pyc"
                and path.name not in {"generate_cells.py", "README.txt"}):
            copy_file(path, target / "scripts/eldenring_music" / path.relative_to(source / "scripts/eldenring_music"))
    copy_file(source / "CI/eldenring-audit-animations.py", target / "CI/eldenring-audit-animations.py")
    for name in REFERENCE_TOOLS:
        copy_file(reference / "tools" / name, target / "reference/tools" / name)
    copy_file(reference / "LICENSE.md", target / "reference/LICENSE.md")
    copy_file(keys / "LICENSE", target / "BinderKeys/LICENSE")
    key_files = sorted((keys / "EldenRing_PC/Key").glob("*.pem"))
    require(key_files, "BinderKeys Elden Ring public keys are missing")
    for path in key_files:
        contents = path.read_text(encoding="ascii")
        require("PRIVATE KEY" not in contents and
                ("BEGIN PUBLIC KEY" in contents or "BEGIN RSA PUBLIC KEY" in contents),
                f"Not a public archive key: {path.name}")
        copy_file(path, target / "BinderKeys/EldenRing_PC/Key" / path.name)


def add_notices(source: Path, dependencies: Path, qt_notices: Path, msvc_notice: Path, stage: Path) -> None:
    target = stage / "licenses"
    copy_file(source / "LICENSE", stage / "LICENSE.txt")
    copy_file(source / "components/eldencombat/LICENSE-MIT.txt", stage / "EldenCombat-LICENSE-MIT.txt")
    copy_file(source / "LICENSE", target / "OpenMW-GPL-3.0.txt")
    copy_file(source / "AUTHORS.md", target / "OpenMW-AUTHORS.md")
    copy_file(source / "components/eldencombat/LICENSE-MIT.txt", target / "EldenCombat-MIT.txt")
    copy_file(source / "extern/osg-ffmpeg-videoplayer/License.txt", target / "osg-ffmpeg-videoplayer.txt")
    notices = sorted((dependencies / "share").glob("*/copyright"))
    require(notices, "Dependency copyright notices are missing")
    for path in notices:
        copy_file(path, target / "dependencies" / (path.parent.name + ".txt"))
    require((qt_notices / "qtbase/LICENSES/LGPL-3.0-only.txt").is_file(),
            "Pinned Qt license/attribution directory is incomplete")
    copy_tree(qt_notices, target / "Qt-6.6.3")
    copy_file(msvc_notice, target / "Microsoft-Visual-Studio-Redist.txt")
    # These vendored implementations carry their notices in source headers.
    for name, relative in (("OICS", "extern/oics/include/oics/ICSInputControlSystem.h"),
                           ("TinyXML", "extern/oics/tinyxml/tinyxml.h"),
                           ("sol3", "extern/sol3/sol/sol.hpp"),
                           ("Base64", "extern/Base64/Base64.h"),
                           ("MurmurHash3", "extern/smhasher/MurmurHash3.cpp")):
        text = (source / relative).read_text(encoding="utf-8")
        marker = re.search(r"(?m)^#include", text)
        require(marker is not None, f"Cannot identify notice in {relative}")
        (target / (name + ".txt")).write_text(text[:marker.start()], encoding="utf-8")
    (target / "README.txt").write_text(
        "OpenMW and the combined application are GPLv3. Elden combat and the\n"
        "included extraction tools retain their individual notices. Qt 6.6.3 is\n"
        "dynamically linked; its LGPLv3/GPLv3 texts and third-party notices are in\n"
        "Qt-6.6.3. The dependency notices identify the bundled runtime libraries.\n"
        "The accompanying source archive contains this release's engine sources.\n"
        "Game assets are not included and their copyrights remain with their owners.\n",
        encoding="utf-8")


def package(args: argparse.Namespace) -> dict:
    source, install, output, archive = (Path(getattr(args, name)).resolve()
                                         for name in ("source", "install", "output", "archive"))
    require(not output.exists(), f"Output already exists; choose a new directory: {output}")
    require(not archive.exists(), f"Archive already exists; choose a new filename: {archive}")
    require(not output.is_relative_to(install) and not install.is_relative_to(output),
            "Install and output directories must be separate")
    require(re.fullmatch(r"[0-9a-f]{40}", args.revision) is not None, "Expected full source revision")
    require(Path(args.source_archive).is_file(), "Matching source archive is required")
    source_files = validate_source_archive(source, Path(args.source_archive))
    inventory = validate_install(install, Path(args.build))
    output.parent.mkdir(parents=True, exist_ok=True)
    archive.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".elden-release-", dir=output.parent) as temporary:
        stage = Path(temporary) / output.name
        stage.mkdir()
        removed = []
        for path in inventory:
            relative = path.relative_to(install)
            if developer_file(relative):
                removed.append(relative.as_posix())
                continue
            copy_file(path, stage / relative)
        add_entrypoints(source, stage)
        add_tools(source, Path(args.reference_source), Path(args.keys_source), stage)
        add_notices(source, Path(args.dependencies), Path(args.qt_notices), Path(args.msvc_notice), stage)
        copy_file(source / "docs/eldenring-windows-release.txt", stage / "README-Windows.txt")
        # Validate all additions too. Public reusable ww2ogg codebooks are the
        # sole extraction binary and are preserved together with their license.
        for path in files(stage):
            check_path(path.relative_to(stage))
            check_personal_paths(path)
        manifest = {
            "format_version": 1, "platform": "windows-x64", "configuration": "Release",
            "repository": "https://github.com/Smthri/openmw-eldenring",
            "base_revision": args.revision, "compiled_game_timings": False,
            "game_assets_included": False, "profiles_included": False,
            "source_archive": {"file": Path(args.source_archive).name,
                               "sha256": digest(Path(args.source_archive)), "files": source_files},
            "files": {path.relative_to(stage).as_posix():
                      {"size": path.stat().st_size, "sha256": digest(path)} for path in files(stage)},
        }
        (stage / "release-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zipped:
            for path in files(stage):
                zipped.write(path, path.relative_to(stage).as_posix())
        with zipfile.ZipFile(archive) as zipped:
            require(zipped.testzip() is None, "ZIP integrity verification failed")
            expected = {path.relative_to(stage).as_posix(): digest(path) for path in files(stage)}
            actual = {name: hashlib.sha256(zipped.read(name)).hexdigest() for name in zipped.namelist()}
            require(actual == expected, "ZIP does not match the clean staging tree")
        stage.rename(output)
    checksum = digest(archive)
    archive.with_name(archive.name + ".sha256").write_text(f"{checksum}  {archive.name}\n", encoding="ascii")
    return {"passed": True, "files": len(expected), "archive_sha256": checksum,
            "removed_developer_files": removed, "compiled_game_timings": False,
            "game_assets_included": False, "profiles_included": False}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "install", "output", "archive", "build", "dependencies",
                 "qt-notices", "msvc-notice", "reference-source", "keys-source", "source-archive"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        report = package(args)
    except (ValueError, OSError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Release packaging failed: {error}\n")
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
