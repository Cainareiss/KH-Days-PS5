"""Exercise subtitle timing, DS IDs, and the actual PS5 MPEG decoder contract."""
from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("ps5_hd_movies", ROOT / "scripts/prepare_ps5_hd_movies.py")
assert SPEC and SPEC.loader
MOVIES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MOVIES)


def host_environment() -> dict[str, str]:
    environment = dict(os.environ)
    if os.name == "nt" and not environment.get("INCLUDE"):
        visual_studio = Path(environment.get("ProgramFiles", "C:/Program Files")) / "Microsoft Visual Studio"
        setups = sorted(visual_studio.glob("*/VC/Auxiliary/Build/vcvars64.bat"))
        setups += sorted(visual_studio.glob("*/*/VC/Auxiliary/Build/vcvars64.bat"))
        if not setups:
            raise RuntimeError("MSVC/Windows SDK headers are required for the pl_mpeg host probe")
        setup = subprocess.run(f'cmd.exe /d /s /c ""{setups[-1]}" >nul && set"',
                               capture_output=True, text=True, timeout=60, check=True)
        for line in setup.stdout.splitlines():
            key, separator, value = line.partition("=")
            if separator and key and not key.startswith("="):
                environment[key] = value
    return environment


class HdMoviePreparation(unittest.TestCase):
    def test_subtitle_intervals_preserve_multiline_and_clear_gaps(self) -> None:
        schedule = {"rate": 30, "lines": [[3, 9], [3, 9], [6, 12], [18, 24]]}
        states = MOVIES.subtitle_states(schedule, 1.0)
        self.assertEqual(states, [(0.0, 0.1, []), (0.1, 0.2, [0, 1]), (0.2, 0.3, [0, 1, 2]),
                                  (0.3, 0.4, [2]), (0.4, 0.6, []), (0.6, 0.8, [3]), (0.8, 1.0, [])])

    def test_schedule_rejects_missing_or_escape_atlas_and_bad_rectangles(self) -> None:
        with tempfile.TemporaryDirectory(prefix="khdays-hd-schedule-") as temporary:
            folder = Path(temporary)
            source = folder / "hd802.txt"
            for text in ("atlas ../outside.png", "atlas C:\\outside.png", "picture -1 2",
                         "atlas sprites.png\nline 1 1 0 0 0 0 1 1"):
                source.write_text(text, encoding="utf-8")
                with self.assertRaises(ValueError):
                    MOVIES.read_subtitles(source)

    def test_plan_matches_cartridge_ids_and_reports_longer_theater_cuts(self) -> None:
        with tempfile.TemporaryDirectory(prefix="khdays-hd-plan-") as temporary:
            folder = Path(temporary)
            pc, ds = folder / "pc", folder / "ds"
            pc.mkdir()
            ds.mkdir()
            (pc / "hd802.mp4").write_bytes(b"local pc source")
            (pc / "hd003.mp4").write_bytes(b"other collection game")
            header = bytearray(48)
            header[:4] = b"MODS"
            struct.pack_into("<4I", header, 8, 300, 256, 160, 15 * 0x1000000)
            (ds / "802.mods").write_bytes(header)
            media = {"duration": 22.0, "video": {"width": 1920, "height": 1080}, "audio": None}
            with mock.patch.object(MOVIES, "probe_media", return_value=media):
                plan = MOVIES.make_plan(pc, ds, "en", "unused")
            self.assertEqual([r["id"] for r in plan["records"]], ["802"])
            self.assertEqual(plan["records"][0]["duration_difference_seconds"], 2.0)
            self.assertEqual(plan["skipped_pc_only"], [{"source": "hd003.mp4", "reason": "no matching DS movie"}])
            record = plan["records"][0]
            first = MOVIES.input_fingerprint(record, {"quality": 2})
            self.assertNotEqual(first, MOVIES.input_fingerprint(record, {"quality": 3}))
            record["source_sha256"] = "a different local source"
            self.assertNotEqual(first, MOVIES.input_fingerprint(record, {"quality": 2}))

    def test_ffmpeg_output_decodes_in_pinned_pl_mpeg_with_timed_png_subtitles(self) -> None:
        ffmpeg, ffprobe = shutil.which("ffmpeg"), shutil.which("ffprobe")
        clang = ROOT / "toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc/bin/clang.exe"
        decoder = ROOT / "build-native-vulkan-ps5/_deps/khdays_pl_mpeg-src"
        if not ffmpeg or not ffprobe or not clang.is_file() or not (decoder / "pl_mpeg.h").is_file():
            self.skipTest("FFmpeg and the pinned host decoder compiler are required")
        with tempfile.TemporaryDirectory(prefix="khdays-hd-decode-") as temporary:
            folder = Path(temporary)
            probe = folder / "pl_mpeg_probe.exe"
            built = subprocess.run([str(clang), "--target=x86_64-pc-windows-msvc", "-std=c17", "-O2",
                                    "-I", str(decoder), str(ROOT / "tests/native_pl_mpeg_probe.c"), "-o", str(probe)],
                                   env=host_environment(), capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            source, target = folder / "source.mp4", folder / "converted.mpg"
            subprocess.run([ffmpeg, "-v", "error", "-y", "-f", "lavfi", "-i", "color=c=black:s=160x90:r=30000/1001",
                            "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000", "-t", "1",
                            "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", str(source)],
                           check=True, capture_output=True, timeout=30)
            Image.new("RGBA", (50, 30), (255, 0, 0, 255)).save(folder / "captions.png")
            schedule_path = folder / "hd802.txt"
            schedule_path.write_text("picture 160 90\nrate 30\natlas captions.png\nline 3 15 20 10 0 0 50 30\n",
                                     encoding="utf-8")
            schedule = MOVIES.read_subtitles(schedule_path)
            concat = MOVIES.write_subtitle_concat(schedule, folder / "captions", 160, 90, 1.0)
            settings = {"width": 160, "height": 90, "fps": MOVIES.FPS, "quality": 2, "threads": 2}
            command = MOVIES.conversion_command(ffmpeg, source, target, settings, 1.0, concat, True)
            converted = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(converted.returncode, 0, converted.stdout + converted.stderr)
            checked = MOVIES.check_output(target, ffprobe, probe, settings, 1.0, True)
            self.assertEqual(checked["pl_mpeg"]["video_frames"], 3)
            self.assertGreater(checked["pl_mpeg"]["audio_samples"], 0)
            for at, caption in (("0.0", False), ("0.2", True), ("0.7", False)):
                picture = subprocess.run([ffmpeg, "-v", "error", "-ss", at, "-i", str(target), "-frames:v", "1",
                                          "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"],
                                         capture_output=True, check=True, timeout=30).stdout
                self.assertEqual(len(picture), 160 * 90 * 3)
                red, green, blue = picture[(20 * 160 + 40) * 3:(20 * 160 + 40) * 3 + 3]
                if caption:
                    self.assertGreater(red, 180)
                    self.assertLess(green, 50)
                    self.assertLess(blue, 50)
                else:
                    self.assertLess(max(red, green, blue), 30)


if __name__ == "__main__":
    unittest.main()
