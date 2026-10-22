import importlib.util
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "prepare_native_elf_sources.py"
spec = importlib.util.spec_from_file_location("native_elf", SCRIPT)
native_elf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native_elf)


class ElfAdaptationTests(unittest.TestCase):
    def test_32_bit_long_without_narrowing_64_bit_types(self):
        source = "long a; unsigned long b; long long c; unsigned long long d; long int e; long double f;"
        self.assertEqual(native_elf.adapt_source(source),
                         "int a; unsigned int b; long long c; unsigned long long d;  int e;  double f;")

    def test_keep_literals_comments_and_identifier_names(self):
        source = '/* long long */ const char *s = "long double"; int longitudinal; // long\n'
        self.assertEqual(native_elf.adapt_source(source), source)

    def test_32_bit_long_literal(self):
        self.assertEqual(native_elf.adapt_source("0xffffffffUL 0xffffffffL 1LL 1ULL"),
                         "0xffffffffU 0xffffffff 1LL 1ULL")

    def test_commented_type(self):
        self.assertEqual(native_elf.adapt_source("long /* keep */ long x; long /* note */ int y;"),
                         "long /* keep */ long x;  /* note */ int y;")

    def test_gc_retained_elf_tables_and_unoptimized_callbacks(self):
        result = native_elf.adapt_source(
            '#pragma section(".khdi$m", read, write)\n'
            '__declspec(allocate(".khdi$m")) int data;\n'
            '__declspec(allocate(".khfr$m")) int functions;\n'
            '#pragma optimize("", off)\nvoid callback(void) {}\n#pragma optimize("", on)\n')
        self.assertIn('section("khdays_data"), aligned(8), used', result)
        self.assertIn('section("khdays_functions"), aligned(8), used', result)
        self.assertIn('#pragma clang optimize off', result)
        self.assertIn('#pragma clang optimize on', result)
        self.assertNotIn('__declspec', result)

    def test_incomplete_generator_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(FileNotFoundError, "Generator has not completed"):
                native_elf.prepare(root / "input", root / "output")

    def test_nitro_inline_helpers_keep_local_c99_bodies(self):
        source = (
            "extern khdays_ref NNS_G3dGetMat(khdays_ref mdl);\n"
            "inline khdays_ref NNS_G3dGetMat(khdays_ref mdl);\n"
            "inline khdays_ref NNS_G3dGetMat(khdays_ref mdl) { return mdl + 4; }\n"
            "static inline u8 NNS_G2dFontGetBpp(khdays_ref p) { return 2; }\n"
            "inline int unrelated(int value) { return value; }\n"
        )
        result = native_elf.patch_generated_prototypes(
            Path("rewritten/libs/nns/g3d/calls/example.c"), source)
        self.assertNotIn("extern khdays_ref NNS_G3dGetMat", result)
        self.assertIn("static inline khdays_ref NNS_G3dGetMat", result)
        self.assertIn("inline int unrelated", result)
        self.assertNotIn("static static", result)
        self.assertEqual(native_elf.patch_generated_prototypes(
            Path("rewritten/libs/nns/g3d/calls/example.c"), result), result)

    def test_nitro_incremental_repair_only_touches_affected_sdk_unit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "rewritten/libs/nns/g2d/calls/font.c"
            target.parent.mkdir(parents=True)
            target.write_text("inline u8 NNS_G2dFontGetBpp(khdays_ref p) { return 2; }\n")
            unrelated = target.with_name("other.c")
            unrelated.write_text("int other;\n")
            unchanged_mtime = unrelated.stat().st_mtime_ns
            report = native_elf.repair_nitro_inline_tree(root)
            self.assertEqual(report["repaired_count"], 1)
            self.assertEqual(unrelated.stat().st_mtime_ns, unchanged_mtime)
            self.assertEqual(native_elf.repair_nitro_inline_tree(root)["repaired_count"], 0)


if __name__ == "__main__":
    unittest.main()
