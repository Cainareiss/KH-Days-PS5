"""Exercise HD selection with an arbitrary loader cwd and session logging off."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_native_runtime_sources import ADAPTER, ROOT, host_environment


class Ps5HdRuntimeTests(unittest.TestCase):
    def test_adapted_runtime_finds_app0_movies_and_persists_all_outcomes(self) -> None:
        port = ROOT / ".cache/khdays-native-v0.1.1/port"
        clang = ROOT / "toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc/bin/clang.exe"
        if not clang.is_file() or not (port / "runtime/movies/movie_hd.c").is_file():
            self.skipTest("pinned runtime and host clang are required")
        original = (port / "runtime/movies/movie_hd.c").read_text(encoding="utf-8")
        adapted = ADAPTER.adapt("runtime/movies/movie_hd.c", original)
        # Compile the complete adapted unit with host service shims. Removing
        # its includes avoids requiring PS5 pthread headers in the MSVC target.
        body = re.sub(r'^\s*#include[^\n]*\n', '', adapted, flags=re.M)
        with tempfile.TemporaryDirectory(prefix="khdays-hd-runtime-") as temporary:
            folder = Path(temporary)
            app, saves, unrelated = folder / "app0", folder / "saves", folder / "loader-cwd"
            movies = app / "data/cutscenes/cinematics"
            for path in (movies, saves, unrelated):
                path.mkdir(parents=True)
            for name in ("hd802.mpg", "hd803.mpg"):
                (movies / name).write_bytes(b"decoder fixture")
            (app / "mod-hd802.mpg").write_bytes(b"mod fixture")
            (folder / "movie_runtime_body.h").write_text(body, encoding="utf-8")
            binary = folder / "hd_runtime.exe"
            built = subprocess.run([
                str(clang), "--target=x86_64-pc-windows-msvc", "-std=c17", "-Wall", "-Wextra",
                "-I", str(port), "-I", str(folder),
                str(ROOT / "tests/native_ps5_hd_runtime_test.c"), "-o", str(binary),
            ], env=host_environment(), capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary), str(app), str(saves)], cwd=unrelated,
                                 capture_output=True, text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("persistent fallback: PASS", ran.stdout)
            log = (saves / "boot.log").read_text(encoding="utf-8")
            self.assertIn("/app0/data/cutscenes/cinematics/hd802.mpg plays (1920x1080", log)
            self.assertEqual(log.count("movie 999: no HD video"), 1)
            self.assertIn("/app0/data/cutscenes/cinematics/hd999.mpg", log)
            self.assertIn("hd803.mpg cannot be played (test decoder rejected video headers)", log)
            self.assertIn("HD video ended", log)
            self.assertIn("mod-hd802.mpg plays", log)

    def test_movie_changes_invalidate_sealed_link_proof(self) -> None:
        spec = importlib.util.spec_from_file_location("hd_link_freshness", ROOT / "scripts/native_link_freshness.py")
        assert spec and spec.loader
        freshness = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(freshness)
        with tempfile.TemporaryDirectory(prefix="khdays-hd-link-proof-") as temporary:
            project = Path(temporary)
            build = project / "build"
            for path in (project / "native", project / "scripts",
                         build / "runtime-sources/runtime/movies", build / "vulkan-sources"):
                path.mkdir(parents=True, exist_ok=True)
            for relative in freshness.PREPARATION_SCRIPTS:
                (project / relative).write_text("# fixture\n", encoding="utf-8")
            (build / "CMakeCache.txt").write_text("KHDAYS_NATIVE_BUILD_RUNTIME:BOOL=ON\n")
            movie = build / "runtime-sources/runtime/movies/movie_hd.c"
            movie.write_text('const char *movie = "data/cutscenes/cinematics/hd%s.mpg";\n')
            archive, elf, eboot = (build / name for name in ("runtime.a", "game.elf", "eboot.bin"))
            for path in (archive, elf, eboot):
                path.write_bytes(b"fixture")
            proof = freshness.seal(freshness.snapshot(project, build, [archive], []), project, elf, eboot)
            movie.write_text('const char *movie = "/app0/data/cutscenes/cinematics/hd%s.mpg";\n')
            with self.assertRaisesRegex(ValueError, "sources changed"):
                freshness.verify(proof, project, freshness.digest(elf), freshness.digest(eboot))


if __name__ == "__main__":
    unittest.main()
