"""Runtime preparation contracts: immutable inputs, ELF ABI and scoped edits."""
from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/prepare_native_runtime_sources.py"
SPEC = importlib.util.spec_from_file_location("prepare_native_runtime_sources", SCRIPT)
assert SPEC and SPEC.loader
ADAPTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ADAPTER)


HAL = """#ifndef TEST_HAL_H
#define TEST_HAL_H
typedef unsigned int u32;
#if defined(_WIN64)
typedef unsigned long long khdays_uptr;
#else
typedef unsigned int khdays_uptr;
#endif
__declspec(noreturn) void khdays_hal_unimplemented(const char *what);
#endif
"""


def host_environment() -> dict[str, str]:
    environment = dict(os.environ)
    if os.name == "nt" and not environment.get("INCLUDE"):
        visual_studio = Path(environment.get("ProgramFiles", "C:/Program Files")) / "Microsoft Visual Studio"
        setups = sorted(visual_studio.glob("*/VC/Auxiliary/Build/vcvars64.bat"))
        setups += sorted(visual_studio.glob("*/*/VC/Auxiliary/Build/vcvars64.bat"))
        if not setups:
            raise RuntimeError("MSVC/Windows SDK headers are required for host graphics tests")
        setup = subprocess.run(f'cmd.exe /d /s /c ""{setups[-1]}" >nul && set"',
                               capture_output=True, text=True, timeout=60)
        if setup.returncode:
            raise RuntimeError("could not initialize MSVC/Windows SDK environment")
        for line in setup.stdout.splitlines():
            key, separator, value = line.partition("=")
            if separator and key and not key.startswith("="):
                environment[key] = value
    return environment


class RuntimePreparation(unittest.TestCase):
    def test_input_layout_chord_preserves_other_controls_and_audio(self) -> None:
        port = ROOT / ".cache/khdays-native-v0.1.1/port"
        clang = ROOT / "toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc/bin/clang.exe"
        if not clang.is_file():
            self.skipTest("host clang is required")
        with tempfile.TemporaryDirectory(prefix="khdays-layout-input-") as temporary:
            binary = Path(temporary) / "host_io.exe"
            command = [str(clang), "--target=x86_64-pc-windows-msvc", "-std=c17", "-fdeclspec",
                       "-I", str(ROOT / "tests/native_ps5_host_io_stubs"),
                       "-I", str(port), "-I", str(port / "host/app"), "-I", str(port / "runtime/core"),
                       "-I", str(port / "runtime/sound"), "-I", str(port / "hal"),
                       "-I", str(ROOT / "toolchains/sources/PS5_VulkanTemplate/ps5/src"),
                       str(ROOT / "tests/native_ps5_host_io_test.c"), "-o", str(binary)]
            environment = host_environment()
            built = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("native PS5 host I/O: PASS", ran.stdout)

    def test_cpu_compositor_avoids_per_pixel_emulated_tls(self) -> None:
        port = ROOT / ".cache/khdays-native-v0.1.1/port"
        clang = ROOT / "toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc/bin/clang.exe"
        if not clang.is_file() or not (port / "gfx/compose_cpu.c").is_file():
            self.skipTest("host clang and the real pixel compositor are required")
        original = (port / "gfx/compose_cpu.c").read_text(encoding="utf-8")
        header = (port / "gfx/compose.h").read_text(encoding="utf-8")
        adapted, shared = ADAPTER.adapt_cpu_compositor(original, header)
        with tempfile.TemporaryDirectory(prefix="khdays-compose-context-") as temporary:
            folder = Path(temporary)
            (folder / "compose_cpu.c").write_text(adapted, encoding="utf-8")
            (folder / "compose_context.h").write_text(shared, encoding="utf-8")
            environment = host_environment()
            common = [str(clang), "--target=x86_64-pc-windows-msvc", "-std=c17", "-O3", "-fdeclspec",
                      "-I", str(port), "-I", str(port / "gfx")]
            names = {"khdays_gfx_compose_cpu": "reference_compose", "khdays_gfx_compose_cpu_rect": "reference_rect",
                     "khdays_gfx_pixel_cpu": "reference_pixel", "khdays_gfx_describe_wide": "reference_wide",
                     "kd_hd_none": "reference_hd_none"}
            original_obj, adapted_obj = folder / "original.obj", folder / "adapted.obj"
            commands = [common + ["-femulated-tls"] + [f"-D{k}={v}" for k, v in names.items()] +
                        ["-c", str(port / "gfx/compose_cpu.c"), "-o", str(original_obj)],
                        common + ["-c", str(folder / "compose_cpu.c"), "-o", str(adapted_obj)]]
            for command in commands:
                built = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=120)
                self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            binary = folder / "compose_test.exe"
            built = subprocess.run(common + [str(ROOT / "tests/native_compose_context_test.c"),
                                              str(original_obj), str(adapted_obj), "-o", str(binary)],
                                   env=environment, capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary)], env=environment, capture_output=True, text=True, timeout=90)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("TLS lookups/frame=", ran.stdout)
            print(ran.stdout.strip())

    def test_crash_record_reaches_persistent_descriptor(self) -> None:
        clang = ROOT / "toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc/bin/clang.exe"
        if not clang.is_file():
            self.skipTest("host clang is required")
        source = (ROOT / "native/ps5/diag_ps5.c").read_text(encoding="utf-8")
        functions = []
        for declaration in ("static void persist(", "static size_t add_text(",
                            "static size_t add_hex(", "void khdays_ps5_diag_fault(",
                            "void khdays_ps5_diag_null_caller(",
                            "void khdays_ps5_diag_note("):
            start = source.index(declaration)
            end = source.index("\n}", start) + 2
            functions.append(source[start:end])
        with tempfile.TemporaryDirectory(prefix="khdays-crash-capture-") as temporary:
            folder = Path(temporary)
            (folder / "diag_fault_functions.h").write_text("\n".join(functions), encoding="utf-8")
            binary = folder / "diag_fault_test.exe"
            environment = host_environment()
            built = subprocess.run([str(clang), "--target=x86_64-pc-windows-msvc", "-std=c17",
                                    "-I", str(folder), str(ROOT / "tests/native_diag_fault_test.c"),
                                    "-o", str(binary)], env=environment,
                                   capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("persistent crash: PASS", ran.stdout)

    def test_presenter_lifecycle_and_bounded_capture(self) -> None:
        port = ROOT / ".cache/khdays-native-v0.1.1/port"
        clang = ROOT / "toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc/bin/clang.exe"
        sdk = ROOT / "toolchains/sources/PS5_Vulkan/.deps/native/ps5-payload-sdk/target/include/ps5platform"
        if not (sdk / "videoout.h").is_file() or not clang.is_file():
            self.skipTest("public VideoOut declarations and host clang are required")
        with tempfile.TemporaryDirectory(prefix="khdays-video-capture-") as temporary:
            folder = Path(temporary)
            declarations = folder / "ps5platform"
            declarations.mkdir()
            # Use the real public declarations without overriding the host CRT.
            for name in ("videoout.h", "kernel.h"):
                shutil.copyfile(sdk / name, declarations / name)
            binary = folder / "video_test.exe"
            command = [str(clang), "--target=x86_64-pc-windows-msvc", "-std=c17", "-fdeclspec",
                       "-I", str(folder), "-I", str(ROOT / "tests/native_memory_stubs"),
                       "-I", str(port), "-I", str(port / "host/app"),
                       "-I", str(ROOT / "toolchains/sources/PS5_VulkanTemplate/ps5/src"),
                       str(ROOT / "tests/native_ps5_video_test.c"),
                       str(port / "gpu/vram_map.c"), "-o", str(binary)]
            environment = host_environment()
            built = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=180)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary), str(folder)], env=environment,
                                 capture_output=True, text=True, timeout=60)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("bounded diagnostics", ran.stdout)

    def test_default_ui_renders_pixels_on_bitmap_host(self) -> None:
        port = ROOT / ".cache/khdays-native-v0.1.1/port"
        clang = ROOT / "toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc/bin/clang.exe"
        if not (port / "runtime/display/display.c").is_file() or not clang.is_file():
            self.skipTest("pinned runtime and host clang are required for pixel regression")
        original = (port / "runtime/display/display.c").read_text(encoding="utf-8")
        adapted = ADAPTER.adapt("runtime/display/display.c", original)

        def decision(text: str) -> str:
            start = text.index("static int reference_draws(")
            end = text.index("\n}", start) + 2
            return text[start:end]

        with tempfile.TemporaryDirectory(prefix="khdays-reference-pixels-") as temporary:
            folder = Path(temporary)
            (folder / "display_decisions.h").write_text(
                decision(original).replace("reference_draws(", "upstream_reference_draws(") +
                "\n" + decision(adapted), encoding="utf-8")
            binary = folder / "reference_pixels.exe"
            command = [str(clang), "--target=x86_64-pc-windows-msvc", "-std=c17", "-fdeclspec",
                       "-I", str(port), "-I", str(folder),
                       str(ROOT / "tests/native_reference_pixels_test.c"),
                       str(port / "gpu/gpu2d.c"), str(port / "gpu/vram_map.c"),
                       str(port / "ui/parts.c"), "-o", str(binary)]
            environment = host_environment()
            built = subprocess.run(command, env=environment, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            environment.pop("KHDAYS_UI", None)
            ran = subprocess.run([str(binary)], env=environment, capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("bitmap fallback renders both screens", ran.stdout)

    def test_paths_do_not_rewrite_textual_escapes(self) -> None:
        literals = r'const char *escape = "\\n \\t \\x41";'
        mods = literals + '\n' + r'const char *folder = "mods\\%s\\sounds";'
        adapted = ADAPTER.adapt("runtime/sound/sound_mods.c", mods)
        self.assertIn(literals, adapted)
        self.assertIn('"mods/%s/sounds"', adapted)
        dump = literals + '\n' + r'const char *folder = "%s\\.git";'
        adapted = ADAPTER.adapt("runtime/sound/sound_dump.c", dump)
        self.assertIn(literals, adapted)
        self.assertIn('"%s/.git"', adapted)
        names = literals + '\n' + r'auto path = base + "\\" + name;'
        adapted = ADAPTER.adapt("runtime/mods/names.cpp", names)
        self.assertIn(literals, adapted)
        self.assertIn('base + "/" + name', adapted)
        self.assertEqual(names, ADAPTER.adapt("runtime/mods/other.cpp", names))

    @staticmethod
    def fixture(folder: Path) -> dict[str, str]:
        files = {
            "port/runtime/core/runtime.c": "int runtime_fixture;\n",
            "port/runtime/core/start.c": "#error must use PS5 entrypoint\n",
            "port/runtime/core/rom.cpp": "int original_rom_fixture;\n",
            "port/hal/hal.h": HAL,
            "port/hal/sdk_bios.c": "#error must use rewritten HAL from the game archive\n",
            "port/runtime/core/diag.c": """/* KHDAYS_TRACE_SCRIPT: fixture */
static void check_update(int fn, int next)
{
    extern char __ImageBase;
    const IMAGE_NT_HEADERS *nt;
    const u8 *base = (const u8 *)&__ImageBase;
    nt = (const IMAGE_NT_HEADERS *)base;
}
""",
            "port/ui/fixture.c": "int ui_fixture;\n",
            "port/host/net/net_posix.c": "int net_fixture;\n",
            "port/host/render/png_io.c": "int png_fixture;\n",
            "port/host/screens/ui_text.c": """int khdays_language_of_system(void)
{
    return 0;
}
int khdays_language_choose(int setting) { return setting; }
""",
            "assets/src/fixture.cpp": "int asset_fixture;\n",
        }
        for relative, source in files.items():
            path = folder / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source, encoding="utf-8", newline="\n")
        return files

    def test_prepare_is_reproducible_and_never_mutates_upstream(self) -> None:
        with tempfile.TemporaryDirectory(prefix="khdays-runtime-adapter-") as temporary:
            base = Path(temporary)
            upstream, output = base / "upstream", base / "generated"
            originals = self.fixture(upstream)
            command = [sys.executable, str(SCRIPT), "--source", str(upstream), "--out", str(output)]
            subprocess.run(command, check=True, capture_output=True, text=True)
            first = {p.relative_to(output): (p.read_bytes(), p.stat().st_mtime_ns)
                     for p in output.rglob("*") if p.is_file()}
            subprocess.run(command, check=True, capture_output=True, text=True)
            second = {p.relative_to(output): (p.read_bytes(), p.stat().st_mtime_ns)
                      for p in output.rglob("*") if p.is_file()}
            self.assertEqual(first, second, "unchanged outputs must retain contents and timestamps")
            for relative, source in originals.items():
                self.assertEqual((upstream / relative).read_text(encoding="utf-8"), source)
            manifest = json.loads((output / "adaptation.json").read_text(encoding="utf-8"))
            self.assertNotIn("runtime/core/start.c", manifest["sources"])
            self.assertFalse((output / "hal/sdk_bios.c").exists())
            self.assertEqual(manifest["replaced"]["runtime/core/start.c"], "native/ps5/start_ps5.c")
            self.assertEqual(manifest["replaced"]["runtime/core/rom.cpp"], "native/ps5/rom_ps5.c")
            self.assertNotIn("runtime/core/rom.cpp", manifest["included_cpp_sources"])
            self.assertNotIn("pending_cpp", manifest)
            self.assertIn("typedef uintptr_t khdays_uptr;", (output / "hal/hal.h").read_text())

    def test_unknown_sdl_service_stops_preparation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="khdays-runtime-unsupported-") as temporary:
            base = Path(temporary)
            upstream = base / "upstream"
            self.fixture(upstream)
            (upstream / "port/ui/fixture.c").write_text("void f(void) { SDL_RenderTexture(); }\n")
            result = subprocess.run([sys.executable, str(SCRIPT), "--source", str(upstream),
                                     "--out", str(base / "generated")], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unadapted SDL service", result.stderr)

    def test_native_address_type_is_pointer_width_on_ps5(self) -> None:
        clang = ROOT / "toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc/bin/clang.exe"
        sdk = ROOT / "toolchains/sources/PS5_Vulkan/.deps/native/ps5-payload-sdk/target"
        if not clang.is_file() or not (sdk / "include/stdint.h").is_file():
            self.skipTest("PS5 clang/sysroot are not installed in this checkout")
        with tempfile.TemporaryDirectory(prefix="khdays-runtime-abi-") as temporary:
            folder = Path(temporary)
            (folder / "hal.h").write_text(ADAPTER.adapt("hal/hal.h", HAL), encoding="utf-8")
            source = folder / "abi.c"
            source.write_text('''#include "hal.h"
#include <stdint.h>
typedef unsigned int KhdaysRef;
_Static_assert(sizeof(void *) == 8, "PS5 native pointers must be 64 bit");
_Static_assert(sizeof(khdays_uptr) == sizeof(void *), "diagnostic addresses may not truncate pointers");
_Static_assert(sizeof(KhdaysRef) == 4, "DS references must remain 32 bit");
static void mix(void *context, uint64_t cycle) { (void)context; (void)cycle; }
static void (*callback)(void *, uint64_t) = mix;
''', encoding="utf-8")
            subprocess.run([str(clang), "--target=x86_64-sie-ps5", f"--sysroot={sdk}",
                            "-isystem", str(sdk / "include"), "-std=c17", "-Werror",
                            "-fsyntax-only", str(source)], check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
