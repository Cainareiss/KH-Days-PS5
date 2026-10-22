"""Exercise the real backend's startup with omitted driver commands."""
from pathlib import Path
import re
import subprocess
import unittest

from test_native_runtime_sources import ROOT, host_environment


class VulkanBootstrap(unittest.TestCase):
    def test_every_backend_call_has_a_dispatch_gate(self):
        backend = (ROOT / "native/ps5/sdl_gpu_vulkan.c").read_text(encoding="utf-8")
        gates = (ROOT / "native/ps5/vulkan_dispatch_required.h").read_text(encoding="utf-8")
        called = set(re.findall(r"\bvk([A-Z]\w*)\(", backend))
        declared = set(re.findall(r"X\((\w+)\)", gates))
        self.assertEqual(called - declared, set(), "unguarded Vulkan calls")

    def test_missing_commands_fail_without_a_null_call(self):
        clang = ROOT / "toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc/bin/clang.exe"
        if not clang.is_file():
            self.skipTest("host clang is required")
        out = ROOT / "build-native-vulkan-tests"
        out.mkdir(exist_ok=True)
        volk = ROOT / "toolchains/sources/PS5_VulkanTemplate/ps5/third_party/volk"
        includes = [ROOT / "native/ps5", ROOT / "native/ps5/vulkan_compat", volk,
                    ROOT / "toolchains/sources/PS5_Vulkan/.deps/native/radv-release/include"]
        command = [str(clang), "--target=x86_64-pc-windows-msvc", "-std=c17", "-O1"]
        for include in includes:
            command += ["-I", str(include)]
        binary = out / "bootstrap_test.exe"
        command += [str(ROOT / "native/ps5/sdl_gpu_vulkan.c"), str(volk / "volk.c"),
                    str(ROOT / "tests/native_vulkan_bootstrap_test.c"), "-o", str(binary)]
        built = subprocess.run(command, env=host_environment(), capture_output=True, text=True, timeout=120)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        print(ran.stdout.strip())


if __name__ == "__main__":
    unittest.main()
