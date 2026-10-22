"""Reproduce the null WSI callback using real ELF objects and the PS5 linker."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from validate_native_vulkan_link import audit


class MesaDispatchLink(unittest.TestCase):
    def test_reject_lazy_archive_and_accept_complete_dispatch(self):
        llvm = ROOT / "toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc/bin"
        if not (llvm / "ld.lld.exe").is_file():
            self.skipTest("PS5 cross tools are required")
        scratch = ROOT / "build-native-vulkan-tests"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="mesa-dispatch-", dir=scratch) as temporary:
            folder = Path(temporary)
            anchor, impl, archive = folder / "anchor.o", folder / "impl.o", folder / "mesa.a"
            for target, definitions in ((anchor, []), (impl, ["-DFIXTURE_IMPLEMENTATION"])):
                built = subprocess.run([str(llvm / "clang.exe"), "--target=x86_64-sie-ps5", "-fPIC", "-c",
                                        str(ROOT / "tests/native_vulkan_dispatch_fixture.c"), "-o", str(target)]
                                       + definitions, capture_output=True, text=True)
                self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            subprocess.run([str(llvm / "llvm-ar.exe"), "rcs", str(archive), str(impl)],
                           check=True, capture_output=True)
            reports = []
            for name, flags in (("broken", [str(archive)]),
                                ("fixed", ["--whole-archive", str(archive), "--no-whole-archive"])):
                output = folder / (name + ".elf")
                built = subprocess.run([str(llvm / "ld.lld.exe"), "-pie", "-e", "main",
                                        "--exclude-libs=ALL", str(anchor)] + flags + ["-o", str(output)],
                                       capture_output=True, text=True)
                self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                reports.append(audit(output, archive))
            broken, fixed = reports
            self.assertFalse(broken["passed"])
            self.assertEqual(broken["omitted_functions"], ["vk_common_GetPhysicalDeviceProperties2"])
            self.assertFalse(broken["properties2_common_dispatch_relocated"])
            self.assertTrue(fixed["passed"], fixed)
            self.assertEqual(fixed["omitted_functions"], [])
            self.assertTrue(fixed["properties2_common_dispatch_relocated"])


if __name__ == "__main__":
    unittest.main()
