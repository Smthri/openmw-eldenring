#!/usr/bin/env python3
"""Check shareable package isolation, provenance and rejection of private data."""

import argparse
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile


spec = importlib.util.spec_from_file_location(
    "package_eldenring_release", Path(__file__).resolve().parents[1] / "scripts/package_eldenring_release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleasePackage(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.args = argparse.Namespace(**{
            name: self.root / name for name in (
                "source", "install", "output", "build", "dependencies", "qt_notices",
                "reference_source", "keys_source")})
        self.args.archive = self.root / "release.zip"
        self.args.source_archive = self.root / "source.zip"
        self.args.msvc_notice = self.root / "Redist.txt"
        self.args.msvc_notice.write_text("Distributable Code for Microsoft Visual Studio\n")
        self.args.revision = "a" * 40
        pe = bytearray(128)
        pe[:2] = b"MZ"
        pe[0x3c:0x40] = struct.pack("<I", 64)
        pe[64:70] = b"PE\0\0\x64\x86"
        for name in release.RUNTIME_REQUIRED:
            self.write(self.args.install / name, bytes(pe) if name.endswith(".exe") else b"runtime")
        self.write(self.args.install / "openmw.cfg",
                   'config="?userconfig?"\nresources=./resources\ndata=./resources/vfs-mw\n')
        for name in release.EXECUTABLES:
            self.write(self.args.build / "Release" / name, pe)
        self.write(self.args.build / "CMakeCache.txt",
                   "OPENMW_ELDENCOMBAT_COMPILED_TIMINGS:BOOL=OFF\nOPENMW_LOCAL_INSTALL:BOOL=ON\n")
        self.write(self.args.install / "resources/defaultfilters", "editor-only")
        self.write(self.args.install / "share/doc/OpenMW/README.md", "developer README")
        self.write(self.args.install / "resources/lua_api/openmw/core.lua", "runtime helper docs")
        for name in release.SCRIPTS:
            self.write(self.args.source / "scripts" / name, "# authored extraction tool\n")
        for name in release.SOURCE_REQUIRED:
            self.write(self.args.source / name, "// authored release implementation\n")
        for name in ("LICENSE", "AUTHORS.md", "components/eldencombat/LICENSE-MIT.txt",
                     "extern/osg-ffmpeg-videoplayer/License.txt",
                     "scripts/eldenring_music/LICENSE-MIT.txt",
                     "CI/eldenring-audit-animations.py", "CI/eldenring-setup-windows.ps1",
                     "CI/eldenring-import-assets-windows.ps1", "docs/eldenring-windows-release.txt"):
            self.write(self.args.source / name, "authored source or notice\n")
        for name in ("extern/oics/include/oics/ICSInputControlSystem.h",
                     "extern/oics/tinyxml/tinyxml.h", "extern/sol3/sol/sol.hpp",
                     "extern/Base64/Base64.h", "extern/smhasher/MurmurHash3.cpp"):
            self.write(self.args.source / name, "// copyright and license\n#include <header>\n")
        self.write(self.args.dependencies / "share/sdl2/copyright", "SDL notice\n")
        self.write(self.args.qt_notices / "qtbase/LICENSES/LGPL-3.0-only.txt", "Qt LGPL notice\n")
        for name in release.REFERENCE_TOOLS:
            self.write(self.args.reference_source / "tools" / name, "# authored reference tool\n")
        codebook = Path(__file__).resolve().parents[1] / release.CODEBOOK_PATH
        self.write(self.args.source / release.CODEBOOK_PATH, codebook.read_bytes())
        self.write(self.args.source / "scripts/eldenring_music/ww2ogg/COPYING", "BSD notice\n")
        for name in ("setup.py", "bake_sounds.py", "wem.py", "wwise.py"):
            self.write(self.args.reference_source / "tools" / name, "# unused sandbox or sound tool\n")
        self.write(self.args.reference_source / "LICENSE.md", "MIT\n")
        self.write(self.args.keys_source / "LICENSE", "Public key license\n")
        self.write(self.args.keys_source / "EldenRing_PC/Key/Data0.pem",
                   "-----BEGIN RSA PUBLIC KEY-----\npublic fixture\n-----END RSA PUBLIC KEY-----\n")
        with zipfile.ZipFile(self.args.source_archive, "w") as zipped:
            for path in release.files(self.args.source):
                zipped.write(path, path.relative_to(self.args.source).as_posix())

    @staticmethod
    def write(path, contents):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(contents.encode() if isinstance(contents, str) else bytes(contents))

    def test_clean_package_contains_complete_runtime_and_sanitized_provenance(self):
        before = {str(path): release.digest(path) for path in release.files(self.args.install)}
        report = release.package(self.args)
        self.assertTrue(report["passed"])
        self.assertFalse((self.args.output / "resources/defaultfilters").exists())
        self.assertFalse((self.args.output / "share/doc/OpenMW/README.md").exists())
        self.assertTrue((self.args.output / "resources/lua_api/openmw/core.lua").exists())
        self.assertFalse((self.args.output / "elden-profile").exists())
        self.assertTrue((self.args.output / "tools/reference/LICENSE.md").is_file())
        self.assertFalse((self.args.output / "tools/reference/tools/ww2ogg").exists())
        for name in ("setup.py", "bake_sounds.py", "wem.py", "wwise.py"):
            self.assertFalse((self.args.output / "tools/reference/tools" / name).exists())
        self.assertEqual(release.digest(self.args.output / "tools" / release.CODEBOOK_PATH),
                         release.CODEBOOK_SHA256)
        self.assertTrue((self.args.output / "tools/scripts/eldenring_music/ww2ogg/COPYING").is_file())
        self.assertEqual(before, {str(path): release.digest(path) for path in release.files(self.args.install)})
        manifest = json.loads((self.args.output / "release-manifest.json").read_text())
        self.assertNotIn(str(self.root), json.dumps(manifest))
        self.assertFalse(manifest["compiled_game_timings"])
        self.assertEqual(manifest["source_archive"]["sha256"], release.digest(self.args.source_archive))
        with zipfile.ZipFile(self.args.archive) as zipped:
            self.assertEqual(set(zipped.namelist()),
                             {path.relative_to(self.args.output).as_posix() for path in release.files(self.args.output)})
        for name in ("Setup-EldenCombat.cmd", "Start-EldenCombat.cmd", "Start-EldenCombat-Launcher.cmd"):
            wrapper = (self.args.output / name).read_bytes()
            self.assertIn(b'setlocal DisableDelayedExpansion\r\n', wrapper)
            self.assertIn(b'pushd "%~dp0"', wrapper)
            self.assertNotIn(b"\n", wrapper.replace(b"\r\n", b""))

    def test_personal_profile_is_rejected_without_touching_install_or_output(self):
        profile = self.args.install / "elden-profile/openmw.cfg"
        self.write(profile, 'data="D:/my game"\n')
        with self.assertRaisesRegex(ValueError, "Personal"):
            release.package(self.args)
        self.assertEqual(profile.read_text(), 'data="D:/my game"\n')
        self.assertFalse(self.args.output.exists())
        self.assertFalse(self.args.archive.exists())

    def test_game_banks_tables_recordings_saves_and_logs_are_rejected(self):
        for name in ("resources/vfs/animations/eldencombat/player_anims.bin",
                     "resources/vfs/animations/eldencombat/action_timings.bin",
                     "resources/vfs/music/eldenring/theme.ogg", "extracted.rs",
                     "imported_timings.hpp", "saves/character/test.omwsave", "openmw.log",
                     "regulation.bin", "oo2core_6_win64.dll", "unknown-data.bin"):
            with self.subTest(name=name):
                path = self.args.install / name
                self.write(path, "private data")
                with self.assertRaises(ValueError):
                    release.package(self.args)
                self.assertTrue(path.exists())
                path.unlink()
        self.assertFalse(self.args.output.exists())

    def test_compiled_game_timings_cannot_be_marketed_as_shareable(self):
        cache = self.args.build / "CMakeCache.txt"
        cache.write_text(cache.read_text().replace("TIMINGS:BOOL=OFF", "TIMINGS:BOOL=ON"))
        with self.assertRaisesRegex(ValueError, "COMPILED_TIMINGS=OFF"):
            release.package(self.args)

    def test_stale_or_wrong_architecture_executable_is_rejected(self):
        self.write(self.args.install / "openmw.exe", b"not the release executable")
        with self.assertRaisesRegex(ValueError, "Not a Windows executable"):
            release.package(self.args)

    def test_personal_binary_paths_are_rejected_but_vendor_paths_are_permitted(self):
        private = self.args.install / "Qt6Core.dll"
        self.write(private, b"C:\\Users\\Personal Name\\source\\private.cpp\0")
        with self.assertRaisesRegex(ValueError, "Personal Windows"):
            release.package(self.args)
        self.write(private, b"C:\\Users\\qt\\work\\QtCore.pdb\0C:\\Users\\Public\\OpenMW-Release\\source\0")
        release.package(self.args)

    def test_existing_destinations_are_never_overwritten(self):
        self.args.output.mkdir()
        marker = self.args.output / "mine"
        marker.write_text("preserve")
        with self.assertRaisesRegex(ValueError, "Output already exists"):
            release.package(self.args)
        self.assertEqual(marker.read_text(), "preserve")

    def test_private_key_is_rejected_before_publishing_output(self):
        self.write(self.args.keys_source / "EldenRing_PC/Key/Data0.pem", "-----BEGIN " + "PRIVATE KEY-----")
        with self.assertRaisesRegex(ValueError, "Not a public"):
            release.package(self.args)
        self.assertFalse(self.args.output.exists())
        self.assertFalse(self.args.archive.exists())

    def test_symlink_to_external_files_is_rejected(self):
        link = self.args.install / "private-data"
        try:
            link.symlink_to(self.args.source_archive)
        except OSError:
            self.skipTest("Symlinks are unavailable for this account")
        with self.assertRaisesRegex(ValueError, "Symlink"):
            release.package(self.args)

    def test_source_archive_rejects_game_tables_even_with_an_innocent_filename(self):
        path = self.args.source / "components/innocent.hpp"
        self.write(path, "// Generated from a local " + "extracted.rs.\n")
        with zipfile.ZipFile(self.args.source_archive, "a") as zipped:
            zipped.write(path, "components/innocent.hpp")
        with self.assertRaisesRegex(ValueError, "Generated game table"):
            release.package(self.args)

    def test_source_archive_rejects_generated_cell_lookup_and_traversal(self):
        for name in ("files/data-mw/scripts/omw/music/eldenring_cells.lua", "../private.txt"):
            with self.subTest(name=name):
                bad = self.root / "bad-source.zip"
                with zipfile.ZipFile(bad, "w") as zipped:
                    zipped.writestr(name, "game data")
                with self.assertRaises(ValueError):
                    release.validate_source_archive(self.args.source, bad)

    def test_source_archive_rejects_stale_source_and_missing_runtime_loader(self):
        original = (self.args.source / "components/eldencombat/timingdata.hpp").read_text()
        self.write(self.args.source / "components/eldencombat/timingdata.hpp", "updated implementation")
        with self.assertRaisesRegex(ValueError, "differs from source"):
            release.package(self.args)
        self.write(self.args.source / "components/eldencombat/timingdata.hpp", original)
        bad = self.root / "bad-source.zip"
        with zipfile.ZipFile(self.args.source_archive) as source, zipfile.ZipFile(bad, "w") as target:
            for name in source.namelist():
                if name != "components/eldencombat/timingdata.hpp":
                    target.writestr(name, source.read(name))
        with self.assertRaisesRegex(ValueError, "missing required"):
            release.validate_source_archive(self.args.source, bad)


if __name__ == "__main__":
    unittest.main()
