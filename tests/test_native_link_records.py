"""Reproduce dropped data-only archive members with the actual PS5 linker."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("native_records", ROOT / "scripts/validate_native_link_records.py")
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


class GameRecordLink(unittest.TestCase):
    def test_archive_extraction_and_relocated_bg_table(self):
        llvm = ROOT / "toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc/bin"
        sdk = ROOT / "toolchains/sources/PS5_Vulkan/.deps/native/ps5-payload-sdk/target"
        if not (llvm / "ld.lld.exe").is_file():
            self.skipTest("PS5 cross tools are required")
        with tempfile.TemporaryDirectory(prefix="khdays-link-records-") as temporary:
            folder = Path(temporary)
            obj, archive = folder / "records.o", folder / "game.a"
            subprocess.run([str(llvm / "clang.exe"), "--target=x86_64-sie-ps5",
                            "-isystem", str(sdk / "include"), "-I", str(ROOT / "native/ps5"),
                            "-fpack-struct=4", "-ffunction-sections", "-fdata-sections", "-fPIC", "-c",
                            str(ROOT / "tests/native_elf_bg_table_fixture.c"), "-o", str(obj)],
                           check=True, capture_output=True)
            subprocess.run([str(llvm / "llvm-ar.exe"), "rcs", str(archive), str(obj)],
                           check=True, capture_output=True)
            anchor = folder / "anchor.o"
            subprocess.run([str(llvm / "clang.exe"), "--target=x86_64-sie-ps5", "-fPIC", "-c",
                            str(ROOT / "tests/native_elf_record_anchor.c"), "-o", str(anchor)],
                           check=True, capture_output=True)
            common = [str(llvm / "ld.lld.exe"), "-shared", "--gc-sections", "--exclude-libs=ALL",
                      "--undefined=fixture_anchor",
                      "-T", str(ROOT / "native/ps5/game_tables.ld"), str(anchor)]
            broken, fixed = folder / "broken.elf", folder / "fixed.elf"
            for flags, output in (([str(archive)], broken),
                                  (["--whole-archive", str(archive), "--no-whole-archive"], fixed)):
                linked = subprocess.run(common + flags + ["-o", str(output)], capture_output=True, text=True)
                self.assertEqual(linked.returncode, 0, linked.stdout + linked.stderr)
            before = AUDIT.validate(archive, broken, True)
            self.assertFalse(before["passed"])
            self.assertEqual(before["tables"]["khdays_data"]["missing"], 1)
            self.assertEqual(before["tables"]["khdays_functions"]["missing"], 1)
            after = AUDIT.validate(archive, fixed, True)
            self.assertTrue(after["passed"], after)
            self.assertEqual(after["tables"]["khdays_data"]["linked"], 1)
            self.assertIn("all eight", after["bg_register_table"])


if __name__ == "__main__":
    unittest.main()
