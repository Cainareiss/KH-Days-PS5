"""Compile the actual PS5 coordinator to exercise its upstream frame contract."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_native_runtime_sources import ROOT, host_environment


class VulkanFrameContract(unittest.TestCase):
    def test_frame_transition_and_one_screen_target_contract(self):
        clang = ROOT / "toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc/bin/clang.exe"
        port = ROOT / ".cache/khdays-native-v0.1.1/port"
        if not clang.is_file() or not (port / "runtime/display/frame_package.h").is_file():
            self.skipTest("host clang and pinned upstream frame declarations are required")
        scratch = ROOT / "build-native-vulkan-tests"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="frame-contract-", dir=scratch) as temporary:
            binary = Path(temporary) / "frame_contract.exe"
            command = [str(clang), "--target=x86_64-pc-windows-msvc", "-std=c17", "-O1"]
            for include in (ROOT / "native/ps5/vulkan_compat", ROOT / "native/ps5", port,
                            port / "host/render", port / "host/app"):
                command += ["-I", str(include)]
            command += [str(ROOT / "native/ps5/vulkan_game_renderer.c"),
                        str(ROOT / "tests/native_vulkan_frame_contract_test.c"), "-o", str(binary)]
            built = subprocess.run(command, env=host_environment(), capture_output=True,
                                   text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("native Vulkan frame contracts: PASS", ran.stdout)
            print(ran.stdout.strip())


if __name__ == "__main__":
    unittest.main()
