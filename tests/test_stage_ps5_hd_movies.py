from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("stage_ps5_hd_movies", ROOT / "scripts/stage_ps5_hd_movies.py")
assert SPEC and SPEC.loader
STAGER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(STAGER)


class StagePs5HdMovies(unittest.TestCase):
    def test_stages_only_hash_verified_decoder_passes_at_runtime_paths(self):
        with tempfile.TemporaryDirectory(prefix="stage-hd-") as temporary:
            root = Path(temporary)
            source = root / "hd802.mpg"
            source.write_bytes(b"verified mpeg data")
            subs = root / "subtitles" / "en"
            subs.mkdir(parents=True)
            (subs / "hd802.txt").write_text("schedule", encoding="utf-8")
            (subs / "atlas.png").write_bytes(b"png")
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({
                "settings": {"audio": "mp2/48000/stereo/192k"},
                "movies": {"802": {
                    "id": "802", "output_path": str(source), "decoder_ready": True,
                    "output": {
                        "sha256": STAGER.sha256(source),
                        "media": {
                            "video": {"codec_name": "mpeg1video"},
                            "audio": {"codec_name": "mp2", "sample_rate": "48000", "channels": 2},
                        },
                        "pl_mpeg": {"video_frames": 3, "audio_samples": 1152},
                    },
                }},
            }), encoding="utf-8")
            destination = root / "assets"
            report = STAGER.stage(manifest, root / "subtitles", destination)
            movie = destination / "data/cutscenes/cinematics/hd802.mpg"
            self.assertEqual(movie.read_bytes(), source.read_bytes())
            self.assertTrue((destination / "data/cutscenes/cinematics/subtitles/en/hd802.txt").is_file())
            self.assertEqual(report["movie_count"], 1)
            with self.assertRaisesRegex(ValueError, "already exists"):
                STAGER.stage(manifest, root / "subtitles", destination)

    def test_rejects_changed_movie_hash(self):
        with tempfile.TemporaryDirectory(prefix="stage-hd-hash-") as temporary:
            root = Path(temporary)
            movie = root / "hd802.mpg"
            movie.write_bytes(b"changed")
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({
                "settings": {"audio": "mp2/48000/stereo/192k"},
                "movies": {"802": {
                    "id": "802", "output_path": str(movie), "decoder_ready": True,
                    "output": {"sha256": "not-the-current-hash",
                               "media": {"video": {"codec_name": "mpeg1video"},
                                         "audio": {"codec_name": "mp2", "sample_rate": "48000", "channels": 2}},
                               "pl_mpeg": {"video_frames": 1, "audio_samples": 1}},
                }},
            }), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "decoder pass"):
                STAGER.validate_manifest(manifest)


if __name__ == "__main__":
    unittest.main()
