"""Render with the actual production backend and PC Vulkan GPU, then compare pixels."""
from pathlib import Path
import subprocess
import unittest

from test_native_runtime_sources import ROOT, host_environment


class VulkanOffscreen(unittest.TestCase):
    def test_real_gpu_renderers_and_readback(self):
        clang = ROOT / "toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc/bin/clang.exe"
        if not clang.is_file() or not Path("C:/Windows/System32/vulkan-1.dll").is_file():
            self.skipTest("Windows host Vulkan and clang are required")
        out = ROOT / "build-native-vulkan-tests"
        out.mkdir(exist_ok=True)
        port = ROOT / ".cache/khdays-native-v0.1.1/port"
        prepared = ROOT / "build-native-vulkan-ps5/vulkan-sources"
        volk = ROOT / "toolchains/sources/PS5_VulkanTemplate/ps5/third_party/volk"
        includes = [ROOT / "native/ps5", ROOT / "native/ps5/vulkan_compat", volk,
                    ROOT / "toolchains/sources/PS5_Vulkan/.deps/native/radv-release/include",
                    prepared, port, port / "host/render", port / "runtime/mods"]
        binary = out / "offscreen_probe.exe"
        command = [str(clang), "--target=x86_64-pc-windows-msvc", "-std=c17", "-O1", "-g"]
        for include in includes:
            command += ["-I", str(include)]
        command += [str(ROOT / "tests/native_vulkan_offscreen_probe.c"), str(volk / "volk.c")]
        command += [str(prepared / (name + ".c")) for name in ("gfx_render", "gpu_raster3d", "gpu_hd")]
        command += ["-o", str(binary)]
        built = subprocess.run(command, env=host_environment(), capture_output=True, text=True, timeout=120)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=120)
        (out / "offscreen-probe.log").write_text(ran.stdout + ran.stderr, encoding="utf-8")
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        print(ran.stdout.strip())


if __name__ == "__main__":
    unittest.main()
