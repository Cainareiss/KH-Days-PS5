from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("validate_ps5_hd_assets", ROOT / "scripts/validate_ps5_hd_assets.py")
assert SPEC and SPEC.loader
ASSETS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ASSETS)


def png(path: Path, width: int = 64, height: int = 64) -> None:
    def chunk(kind: bytes, content: bytes) -> bytes:
        return struct.pack(">I", len(content)) + kind + content + struct.pack(">I", zlib.crc32(kind + content))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" +
                     chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress((b"\0" + bytes(width * 4)) * height)) + chunk(b"IEND", b""))


def fixture(root: Path) -> None:
    title = root / ASSETS.TITLE
    title.mkdir(parents=True)
    png(title / "atlas.png")
    for name in ASSETS.TITLE_NAMES:
        text = "picture 1920 1080\natlas atlas.png\n"
        if name.startswith(("root_", "story_", "mission_")):
            text += "icon playstation:north 2 3 36 36\nbutton open_settings 1386 40 1934 104\n"
            for clip in ("clip 44 after none intro", "loop 1"):
                text += (clip + "\npiece 2 3 36 36 over\nprompt open_settings\n"
                         "key 0 1468 51 1510 93 1 1 1 1\n")
        else:
            text += "loop 1\n"
        (title / f"{name}.txt").write_text(text, encoding="utf-8")
    cinematics = root / ASSETS.CINEMATICS
    cinematics.mkdir(parents=True)
    movie = cinematics / "hd802.mpg"
    movie.write_bytes(b"\0\0\1\xbaMPEG fixture")
    (root / ASSETS.MANIFEST).write_text(json.dumps({"movies": [{
        "id": "802", "path": "data/cutscenes/cinematics/hd802.mpg",
        "bytes": movie.stat().st_size, "sha256": ASSETS.sha256(movie),
    }]}), encoding="utf-8")
    subtitles = cinematics / "subtitles/en"
    subtitles.mkdir(parents=True)
    (subtitles / "hd802.txt").write_text("subtitle schedule", encoding="utf-8")
    png(subtitles / "hd802.png")


class Ps5HdPackageAssets(unittest.TestCase):
    def test_complete_runtime_inventory_and_staged_copy_pass(self):
        with tempfile.TemporaryDirectory(prefix="ps5-hd-contract-") as temporary:
            root = Path(temporary) / "source"
            fixture(root)
            report = ASSETS.validate_assets(root)
            self.assertEqual(report["titleLayouts"], 14)
            self.assertEqual(report["settingsLayouts"], 10)
            self.assertEqual(report["settingsPromptClips"], 20)
            self.assertEqual(report["movieCount"], 1)
            stage = Path(temporary) / "stage"
            shutil.copytree(root, stage)
            self.assertEqual(ASSETS.validate_assets(stage, report)["files"], report["files"])

    def test_movies_only_input_is_rejected_despite_valid_hashes(self):
        with tempfile.TemporaryDirectory(prefix="ps5-hd-movies-only-") as temporary:
            root = Path(temporary)
            fixture(root)
            shutil.rmtree(root / "mods")
            with self.assertRaisesRegex(ValueError, "required HD runtime asset.*title/en/atlas.png"):
                ASSETS.validate_assets(root)

    def test_required_title_variant_and_settings_records_cannot_be_dropped(self):
        mutations = {
            "missing title variant": ("root_3_2.txt", None, "required HD runtime asset"),
            "missing Settings action": ("root_2_0.txt", "button open_settings 1386 40 1934 104\n", "Settings button"),
            "missing prompt in one clip": ("story_1_0.txt", "prompt open_settings\n", "each HD title clip"),
            "desktop fallback": ("mission_2_0.txt", "piece 2 3 36 36", "Triangle atlas rectangle"),
        }
        for label, (name, removed, message) in mutations.items():
            with self.subTest(label), tempfile.TemporaryDirectory(prefix="ps5-hd-title-") as temporary:
                root = Path(temporary)
                fixture(root)
                path = root / ASSETS.TITLE / name
                if removed is None:
                    path.unlink()
                else:
                    replacement = "piece 1 1 36 36" if label == "desktop fallback" else ""
                    path.write_text(path.read_text(encoding="utf-8").replace(removed, replacement, 1), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, message):
                    ASSETS.validate_assets(root)

    def test_old_atlas_rejects_appended_hint_pieces(self):
        with tempfile.TemporaryDirectory(prefix="ps5-hd-atlas-") as temporary:
            root = Path(temporary)
            fixture(root)
            layout = root / ASSETS.TITLE / "root_2_0.txt"
            layout.write_text(layout.read_text(encoding="utf-8") + "piece 0 65 32 16 over\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "exceeds its atlas"):
                ASSETS.validate_assets(root)

    def test_valid_hash_cannot_authorize_a_path_the_runtime_will_not_open(self):
        for bad_path in ("data/cutscenes/cinematics/hd803.mpg", "data/cutscenes/cinematics/hd802.mp4",
                         "movies/hd802.mpg", "../hd802.mpg", "/app0/data/cutscenes/cinematics/hd802.mpg"):
            with self.subTest(bad_path), tempfile.TemporaryDirectory(prefix="ps5-hd-path-") as temporary:
                root = Path(temporary)
                fixture(root)
                manifest = root / ASSETS.MANIFEST
                data = json.loads(manifest.read_text(encoding="utf-8"))
                data["movies"][0]["path"] = bad_path
                manifest.write_text(json.dumps(data), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "must use runtime path"):
                    ASSETS.validate_assets(root)

    def test_changed_movie_and_stale_staged_title_are_rejected(self):
        with tempfile.TemporaryDirectory(prefix="ps5-hd-stale-") as temporary:
            root = Path(temporary) / "source"
            fixture(root)
            report = ASSETS.validate_assets(root)
            stage = Path(temporary) / "stage"
            shutil.copytree(root, stage)
            layout = stage / ASSETS.TITLE / "root_2_0.txt"
            layout.write_text(layout.read_text(encoding="utf-8") + "# stale source\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "staged HD assets differ"):
                ASSETS.validate_assets(stage, report)
            (root / ASSETS.CINEMATICS / "hd802.mpg").write_bytes(b"changed movie")
            with self.assertRaisesRegex(ValueError, "hash/size mismatch"):
                ASSETS.validate_assets(root)

    def test_movie_manifest_and_subtitle_pairs_are_required(self):
        for relative, message in ((ASSETS.MANIFEST, "required HD runtime asset"),
                                  (ASSETS.CINEMATICS / "subtitles/en/hd802.png", "subtitle schedules and atlases")):
            with self.subTest(relative), tempfile.TemporaryDirectory(prefix="ps5-hd-missing-") as temporary:
                root = Path(temporary)
                fixture(root)
                (root / relative).unlink()
                with self.assertRaisesRegex(ValueError, message):
                    ASSETS.validate_assets(root)

    @unittest.skipUnless(shutil.which("powershell.exe"), "Windows PowerShell is required")
    def test_packager_rejects_incomplete_hd_before_mutation_in_each_mode(self):
        for mode in (None, "-ReuseStage", "-FinalizeExistingImage"):
            with self.subTest(mode), tempfile.TemporaryDirectory(prefix="ps5-hd-preflight-") as temporary:
                base = Path(temporary)
                assets = base / "assets"
                fixture(assets)
                shutil.rmtree(assets / "mods")
                data = base / "extracted"
                data.mkdir()
                placeholder = base / "unused-input"
                placeholder.write_bytes(b"preflight must reject HD before reading this")
                stage = base / "stage"
                image = base / "image.exfat"
                if mode:
                    (stage / "PPSA35820").mkdir(parents=True)
                if mode == "-FinalizeExistingImage":
                    image.write_bytes(b"existing image must remain unchanged")
                command = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                           str(ROOT / "scripts/build_native_exfat.ps1"),
                           "-EbootPath", str(placeholder), "-LibcPrxPath", str(placeholder),
                           "-ExecutableValidationPath", str(placeholder), "-IconPath", str(placeholder),
                           "-BackgroundPath", str(placeholder), "-ImageBuilder", str(placeholder),
                           "-DataExtractedRoot", str(data), "-HdAssetsRoot", str(assets),
                           "-StageRoot", str(stage), "-ImagePath", str(image)]
                if mode:
                    command.append(mode)
                result = subprocess.run(command, capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Incomplete or invalid HD runtime inputs", result.stdout + result.stderr)
                if mode == "-FinalizeExistingImage":
                    self.assertEqual(image.read_bytes(), b"existing image must remain unchanged")
                else:
                    self.assertFalse(image.exists())
                self.assertEqual(list(stage.rglob("*")) if stage.exists() else [],
                                 [stage / "PPSA35820"] if mode else [])


if __name__ == "__main__":
    unittest.main()
