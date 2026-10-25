"""Exercise real PS5 prompt selection and local HD title adaptation."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

from PIL import Image
from test_native_runtime_sources import ROOT, host_environment

SPEC = importlib.util.spec_from_file_location("prepare_ps5_hd_layouts", ROOT / "scripts/prepare_ps5_hd_layouts.py")
assert SPEC and SPEC.loader
ADAPTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ADAPTER)


ICON_LINES = """icon playstation:east 1 1 36 36
icon playstation:south 40 1 36 36
icon playstation:north 361 2041 36 36
icon playstation:start 1760 1975 62 38
icon xbox:start 517 2041 32 32
"""


def layout() -> str:
    text = "picture 1920 1080\natlas atlas.png\ngroup root\n" + ICON_LINES
    text += "button open_settings 1300 40 1900 110\n"
    for clip in ("clip 10 after none", "loop 10"):
        text += clip + "\n"
        for x, width, prompt in ((1300, 600, False), (1450, 250, False), (1400, 42, True)):
            text += f"piece {x} 1 {width} 60 over " + " ".join(["1"] * 16) + "\n"
            if prompt:
                text += "prompt open_settings\n"
            text += f"key 0 {x} 40 {x + width} 100 1 1 1 0\n"
            text += f"key 9 {x} 40 {x + width} 100 1 1 1 1\n"
        # A real menu action whose pixels overlap the settings area must not
        # be deleted unless the whole part belongs to that settings control.
        text += "piece 100 100 300 80 over " + " ".join(["1"] * 16) + "\n"
        text += "key 0 1200 100 1900 500 1 1 1 1\n"
    return text


class NativeDualSensePrompts(unittest.TestCase):
    def test_real_composition_uses_ps5_triangle_glyph(self):
        clang = ROOT / "toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc/bin/clang.exe"
        port = ROOT / ".cache/khdays-native-v0.1.1/port"
        if not clang.is_file() or not (port / "ui/composition.c").is_file():
            self.skipTest("host clang and pinned upstream composition parser required")
        scratch = ROOT / "build-native-vulkan-tests"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="dualsense-", dir=scratch) as temporary:
            folder = Path(temporary)
            sdl = folder / "SDL3/SDL.h"
            sdl.parent.mkdir()
            sdl.write_text('#include "vulkan_compat/SDL3/SDL.h"\n'
                           'typedef struct SDL_Thread SDL_Thread;\n#define SDLCALL\n'
                           'SDL_Thread *SDL_CreateThread(int (*)(void *), const char *, void *);\n'
                           'void SDL_WaitThread(SDL_Thread *, int *);\n', encoding="utf-8")
            source = (port / "host/render/onescreen.c").read_text(encoding="utf-8")
            begin = source.index("static void settings_prompt(void)")
            end = source.index("\n}", begin) + 2
            (folder / "native_settings_prompt.h").write_text(source[begin:end], encoding="utf-8")
            title = folder / "layouts/title/en/root_2_0.txt"
            title.parent.mkdir(parents=True)
            title.write_text("picture 1920 1080\natlas atlas.png\ngroup root\n" + ICON_LINES
                             + "button open_settings 1300 40 1900 110\n"
                             + "loop 10\npiece 7 8 42 42 over " + " ".join(["1"] * 16)
                             + "\nprompt open_settings\nkey 0 0 0 100 42 1 1 1 1\n", encoding="utf-8")
            (title.parent / "atlas.png").touch()
            binary = folder / "prompt.exe"
            command = [str(clang), "--target=x86_64-pc-windows-msvc", "-std=c17", "-O1", "-fdeclspec"]
            for include in (folder, ROOT / "native/ps5", port, port / "host/app"):
                command += ["-I", str(include)]
            command += [str(port / "ui/composition.c"), str(port / "ui/situation.c"),
                        str(ROOT / "native/ps5/one_screen_host_stubs_ps5.c"),
                        str(ROOT / "native/ps5/camp_screen_ps5.c"),
                        str(ROOT / "tests/native_dualsense_prompt_test.c"), "-o", str(binary)]
            built = subprocess.run(command, env=host_environment(), capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary), str(folder)], capture_output=True, text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("native DualSense prompt integration: PASS", ran.stdout)
            print(ran.stdout.strip())

    def test_local_title_conversion_preserves_options_and_sources(self):
        with tempfile.TemporaryDirectory(prefix="ps5-layout-") as temporary:
            base = Path(temporary)
            source, output = base / "original", base / "ps5"
            title = source / "layouts/title/en"
            title.mkdir(parents=True)
            (title / "root_2_0.txt").write_text(layout(), encoding="utf-8")
            (title / "story_1_0.txt").write_text(layout().replace("group root", "group story"), encoding="utf-8")
            Image.new("RGBA", (2048, 2080), (0, 0, 0, 255)).save(title / "atlas.png")
            settings = source / "layouts/settings/en"
            settings.mkdir(parents=True)
            glyphs = [f"glyph {ord(ch)} {i * 24} 0 20" for i, ch in enumerate(sorted(set("CONFIRMBACK")))]
            (settings / "settings.txt").write_text("font settings_font.png 32\n" + "\n".join(glyphs), encoding="utf-8")
            Image.new("L", (512, 32), 255).save(settings / "settings_font.png")
            original = (title / "root_2_0.txt").read_bytes()
            atlas_hash = ADAPTER.digest(title / "atlas.png")
            report = ADAPTER.adapt_pack(source, output)
            self.assertEqual(len(report["layouts"]), 2)
            converted = (output / "layouts/title/en/root_2_0.txt").read_text(encoding="utf-8")
            self.assertEqual(converted.count("prompt open_settings"), 2)
            self.assertIn("button open_settings 1300 40 1900 110", converted)
            self.assertEqual(converted.count("key 0 1450 40"), 2)
            self.assertEqual(converted.count("piece 361 2041 36 36 over"), 2)
            self.assertEqual(converted.count("key 0 1382.00 40 1442 100"), 2)
            self.assertEqual(converted.count("piece 100 100 300 80"), 2)
            self.assertEqual(converted.count("key 0 48 1008"), 2)
            for clip in (converted.split("clip 10 after none\n")[1].split("loop 10\n")[0],
                         converted.split("loop 10\n")[1]):
                self.assertGreater(clip.index("key 0 48 1008"), clip.index("piece 100 100 300 80"))
            self.assertEqual((title / "root_2_0.txt").read_bytes(), original)
            self.assertEqual(ADAPTER.digest(title / "atlas.png"), atlas_hash)
            with Image.open(output / "layouts/title/en/atlas.png") as enlarged:
                self.assertGreater(enlarged.height, 2080)
            for entry in report["layouts"]:
                self.assertEqual(entry["adapted_settings_prompts"], 2)
                self.assertTrue(entry["settings_action_preserved"])
            with self.assertRaisesRegex(ValueError, "already exists"):
                ADAPTER.adapt_pack(source, output)

    def test_title_settings_animation_and_action_are_preserved(self):
        original = layout()
        converted, fades, adapted = ADAPTER.adapt_settings_prompt(original)
        text = "\n".join(converted)
        self.assertEqual(adapted, 2)
        self.assertEqual(len(fades), 2)
        self.assertEqual(text.count("piece "), original.count("piece "))
        self.assertEqual(text.count("key "), original.count("key "))
        self.assertIn("key 9 1382.00 40 1442 100 1 1 1 1", text)
        self.assertIn("button open_settings 1300 40 1900 110", text)

    def test_invalid_settings_are_rejected_before_copying_output(self):
        with tempfile.TemporaryDirectory(prefix="ps5-layout-invalid-") as temporary:
            base = Path(temporary)
            source, output = base / "original", base / "ps5"
            title = source / "layouts/title/en"
            title.mkdir(parents=True)
            (title / "root_2_0.txt").write_text(layout(), encoding="utf-8")
            Image.new("RGBA", (2048, 2080), (0, 0, 0, 255)).save(title / "atlas.png")
            # A previously stripped PS5 pack cannot quietly masquerade as
            # the original source and produce an actionable Settings title.
            (title / "root_2_0.txt").write_text(
                layout().replace("prompt open_settings\n", ""), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "prompt/fade records"):
                ADAPTER.adapt_settings_prompt((title / "root_2_0.txt").read_text(encoding="utf-8"))
            self.assertFalse(output.exists())
        with self.assertRaisesRegex(ValueError, "playstation:north"):
            ADAPTER.adapt_settings_prompt(layout().replace("icon playstation:north 361 2041 36 36\n", ""))


if __name__ == "__main__":
    unittest.main()
