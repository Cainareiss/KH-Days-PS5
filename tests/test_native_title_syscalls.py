import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LLVM = ROOT / 'toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc/bin'
SPEC = importlib.util.spec_from_file_location('syscall_audit', ROOT / 'scripts/validate_native_title_syscalls.py')
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


class NativeTitleSyscalls(unittest.TestCase):
    def fixture(self, forbidden, data_only=False):
        with tempfile.TemporaryDirectory(prefix='khdays-syscall-audit-') as folder:
            obj = Path(folder) / 'fixture.o'
            command = [str(LLVM / 'clang.exe'), '--target=x86_64-sie-ps5', '-c',
                       str(ROOT / 'tests/native_title_syscall_fixture.S'), '-o', str(obj)]
            if forbidden:
                command.append('-DFIXTURE_FORBIDDEN')
            if data_only:
                command.append('-DFIXTURE_DATA_ONLY')
            subprocess.run(command, check=True, capture_output=True)
            return AUDIT.audit(obj, LLVM / 'llvm-objdump.exe')

    def test_rejects_decoded_syscall(self):
        report = self.fixture(True)
        self.assertFalse(report['passed'])
        self.assertEqual(report['direct_syscall_count'], 1)
        self.assertEqual(report['direct_syscalls'][0]['symbol'], 'native_title_fixture')

    def test_data_pattern_is_not_an_instruction(self):
        report = self.fixture(False)
        self.assertTrue(report['passed'])
        self.assertEqual(report['direct_syscall_count'], 0)

    def test_refuses_empty_disassembly(self):
        with self.assertRaisesRegex(RuntimeError, 'No executable instructions'):
            self.fixture(False, data_only=True)


if __name__ == '__main__':
    unittest.main()
