"""Run actual adapter against Vulkan mocks enforcing resource ownership."""
from pathlib import Path
import subprocess
import unittest

from test_native_runtime_sources import ROOT, host_environment


class VulkanLifetime(unittest.TestCase):
    def test_release_cancel_and_failure_contracts(self):
        clang = ROOT / "toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc/bin/clang.exe"
        if not clang.is_file():
            self.skipTest("host clang is required")
        out = ROOT / "build-native-vulkan-tests"
        out.mkdir(exist_ok=True)
        volk = ROOT / "toolchains/sources/PS5_VulkanTemplate/ps5/third_party/volk"
        includes = [ROOT / "native/ps5", ROOT / "native/ps5/vulkan_compat", volk,
                    ROOT / "toolchains/sources/PS5_Vulkan/.deps/native/radv-release/include"]
        binary = out / "lifetime_test.exe"
        command = [str(clang), "--target=x86_64-pc-windows-msvc", "-std=c17", "-O1"]
        for include in includes:
            command += ["-I", str(include)]
        command += [str(ROOT / "tests/native_vulkan_lifetime_test.c"), str(volk / "volk.c"), "-o", str(binary)]
        built = subprocess.run(command, env=host_environment(), capture_output=True, text=True, timeout=120)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        print(ran.stdout.strip())


if __name__ == "__main__":
    unittest.main()
