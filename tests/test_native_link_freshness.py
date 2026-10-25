import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[1] / "scripts/native_link_freshness.py"
spec = importlib.util.spec_from_file_location("native_link_freshness", SCRIPT)
freshness = importlib.util.module_from_spec(spec)
spec.loader.exec_module(freshness)


class NativeLinkFreshnessTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.project = Path(self.directory.name)
        self.build = self.project / "build"
        for relative in ("native/ps5", "build/runtime-sources", "build/vulkan-sources", "scripts"):
            (self.project / relative).mkdir(parents=True, exist_ok=True)
        for relative in freshness.PREPARATION_SCRIPTS:
            (self.project / relative).write_text("# fixture\n")
        (self.build / "CMakeCache.txt").write_text("KHDAYS_NATIVE_BUILD_RUNTIME:BOOL=ON\n")
        self.source = self.project / "native/ps5/one_screen_host_stubs_ps5.c"
        self.source.write_text('const char *button = "ESC";\n')
        self.archive = self.build / "libkhdays-native-ps5-runtime.a"
        self.archive.write_bytes(b"!<arch>\nfixture-runtime")
        self.elf = self.build / "game.elf"
        self.elf.write_bytes(b"fixture-elf")
        self.fself = self.build / "eboot.bin"
        self.fself.write_bytes(b"fixture-eboot")

    def capture(self):
        return freshness.snapshot(self.project, self.build, [self.archive], [])

    def sealed(self):
        return freshness.seal(self.capture(), self.project, self.elf, self.fself)

    def verify(self, proof):
        freshness.verify(proof, self.project, freshness.digest(self.elf), freshness.digest(self.fself))

    def test_current_inputs_and_binary_are_accepted(self):
        proof = self.sealed()
        self.verify(proof)
        self.assertEqual(proof["link_inputs"][str(self.archive.resolve())]["sha256"],
                         freshness.digest(self.archive))

    def test_old_eboot_cannot_pass_with_new_source_even_if_timestamp_is_preserved(self):
        proof = self.sealed()
        before = self.source.stat()
        self.source.write_text('const char *button = "CIRCLE";\n')
        os.utime(self.source, ns=(before.st_atime_ns, before.st_mtime_ns))
        with self.assertRaisesRegex(ValueError, "sources changed"):
            self.verify(proof)

    def test_new_and_removed_sources_invalidate_the_old_link(self):
        proof = self.sealed()
        extra = self.source.with_name("movie_hd_ps5.c")
        extra.write_text("int decode_movie;\n")
        with self.assertRaisesRegex(ValueError, "sources changed"):
            self.verify(proof)
        extra.unlink()
        self.source.unlink()
        with self.assertRaisesRegex(ValueError, "sources changed"):
            self.verify(proof)

    def test_new_runtime_archive_invalidates_an_old_eboot(self):
        proof = self.sealed()
        self.archive.write_bytes(b"!<arch>\nnew-runtime-archive")
        with self.assertRaisesRegex(ValueError, "link input changed"):
            self.verify(proof)

    def test_small_archive_changes_are_hashed_even_with_same_size_and_timestamp(self):
        proof = self.sealed()
        before = self.archive.stat()
        self.archive.write_bytes(b"!<arch>\nfixture-RUNTIME")
        os.utime(self.archive, ns=(before.st_atime_ns, before.st_mtime_ns))
        with self.assertRaisesRegex(ValueError, "link input changed"):
            self.verify(proof)

    def test_large_archive_records_hash_but_does_not_rehash_on_verification(self):
        with patch.object(freshness, "HASH_BINARY_LIMIT", 8):
            proof = self.sealed()
            original_digest = freshness.digest
            calls = []

            def record_digest(path):
                calls.append(path)
                return original_digest(path)

            with patch.object(freshness, "digest", side_effect=record_digest):
                self.verify(proof)
            self.assertNotIn(self.archive, calls)
            self.assertIn("sha256", proof["link_inputs"][str(self.archive.resolve())])
            before = self.archive.stat()
            os.utime(self.archive, ns=(before.st_atime_ns, before.st_mtime_ns + 1_000_000))
            with self.assertRaisesRegex(ValueError, "link input changed"):
                self.verify(proof)

    def test_changes_during_link_prevent_sealing(self):
        captured = self.capture()
        self.source.write_text("int edited_while_linking;\n")
        with self.assertRaisesRegex(ValueError, "sources changed"):
            freshness.seal(captured, self.project, self.elf, self.fself)

    def test_foreign_or_unsealed_binary_proof_is_rejected(self):
        proof = self.sealed()
        with self.assertRaisesRegex(ValueError, "different project"):
            freshness.verify(proof, self.project / "other", freshness.digest(self.elf),
                             freshness.digest(self.fself))
        proof["passed"] = False
        with self.assertRaisesRegex(ValueError, "not sealed"):
            self.verify(proof)

    def test_valid_sources_cannot_authorize_another_eboot(self):
        proof = self.sealed()
        self.fself.write_bytes(b"older-eboot")
        with self.assertRaisesRegex(ValueError, "does not match"):
            self.verify(proof)

    def test_missing_or_legacy_input_proof_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "unsupported"):
            freshness.validate_inputs({}, self.project)

    @unittest.skipUnless(shutil.which("powershell"), "PowerShell is needed for packaging preflight")
    def test_packaging_rejects_legacy_validation_before_creating_an_image(self):
        self.fself.write_bytes(b"old-signed-eboot" * 1024)
        validation = self.build / "old-elf-validation.json"
        validation.write_text(json.dumps({
            "fselfSha256": freshness.digest(self.fself),
            "fselfIntegrity": "valid", "directSyscallInstructions": 0,
        }))
        image = self.build / "test.exfat"
        stage = self.build / "stage"
        script = SCRIPT.parent / "build_native_exfat.ps1"
        result = subprocess.run([
            shutil.which("powershell"), "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script),
            "-EbootPath", str(self.fself), "-DataExtractedRoot", str(self.project / "native"),
            "-LibcPrxPath", str(self.archive), "-ExecutableValidationPath", str(validation),
            "-IconPath", str(self.archive), "-BackgroundPath", str(self.archive),
            "-ImagePath", str(image), "-StageRoot", str(stage), "-ImageBuilder", str(script),
        ], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("has no input proof", result.stdout + result.stderr)
        self.assertFalse(image.exists())
        self.assertFalse(stage.exists())


if __name__ == "__main__":
    unittest.main()
