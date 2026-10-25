from __future__ import annotations

import json
from pathlib import Path
import shutil
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("powershell.exe"), "Windows PowerShell is required")
class NativeExfatReadbackCleanup(unittest.TestCase):
    def cleanup(self, mounted: bool, graceful: int, forced: int, readback_failed: bool = False) -> dict:
        # Execute the actual finally block with a mock command. No real image,
        # drive letter or OSFMount process is accessed by this test.
        command = r"""
$taskParserErrors = $null
$taskAst = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PWD 'scripts/build_native_exfat.ps1'), [ref]$null, [ref]$taskParserErrors)
if ($taskParserErrors.Count) { throw 'Packaging script does not parse' }
$taskFinally = $taskAst.Find({ param($node)
    $node -is [System.Management.Automation.Language.TryStatementAst] -and
    $null -ne $node.Finally -and $node.Finally.Extent.Text.Contains('if ($mountedForVerification)')
}, $true).Finally.Extent.Text
if (-not $taskFinally) { throw 'Readback cleanup block not found' }
$global:taskUnmountCalls = [Collections.Generic.List[string]]::new()
$osfMount = {
    [void]$global:taskUnmountCalls.Add(($args -join ' '))
    $global:LASTEXITCODE = if ($args[0] -ceq '-d') { TASK_GRACEFUL } else { TASK_FORCED }
}
$verifyDrive = 'Q:'
$mountedForVerification = TASK_MOUNTED
$readbackForcedDismount = $false
$WarningPreference = 'SilentlyContinue'
$taskError = $null
try {
    $taskBody = 'try { TASK_BODY } finally ' + $taskFinally
    . ([scriptblock]::Create($taskBody))
} catch { $taskError = $_.Exception.Message }
[ordered]@{ calls = @($global:taskUnmountCalls); error = $taskError;
    forcedDismount = $readbackForcedDismount } | ConvertTo-Json -Compress
"""
        command = (command.replace("TASK_GRACEFUL", str(graceful)).replace("TASK_FORCED", str(forced))
                   .replace("TASK_MOUNTED", "$true" if mounted else "$false")
                   .replace("TASK_BODY", 'throw "Image readback SHA-256 mismatch: movie.mpg"' if readback_failed else "$null = 1"))
        result = subprocess.run(["powershell.exe", "-NoProfile", "-Command", command], cwd=ROOT,
                                capture_output=True, text=True, check=True)
        return json.loads(result.stdout)

    def test_normal_dismount_needs_no_force(self):
        result = self.cleanup(True, 0, 9)
        self.assertEqual(result, {"calls": ["-d -m Q:"], "error": None, "forcedDismount": False})

    def test_owned_readonly_mount_uses_force_after_normal_failure(self):
        result = self.cleanup(True, 2, 0)
        self.assertEqual(result, {"calls": ["-d -m Q:", "-D -m Q:"], "error": None, "forcedDismount": True})

    def test_force_failure_still_aborts_reporting(self):
        result = self.cleanup(True, 2, 3)
        self.assertEqual(result["calls"], ["-d -m Q:", "-D -m Q:"])
        self.assertIn("normal exit 2; force exit 3", result["error"])
        self.assertFalse(result["forcedDismount"])

    def test_failed_mount_never_dismounts_a_drive(self):
        result = self.cleanup(False, 2, 0)
        self.assertEqual(result, {"calls": [], "error": None, "forcedDismount": False})

    def test_successful_cleanup_preserves_readback_hash_failure(self):
        result = self.cleanup(True, 2, 0, readback_failed=True)
        self.assertEqual(result["calls"], ["-d -m Q:", "-D -m Q:"])
        self.assertEqual(result["error"], "Image readback SHA-256 mismatch: movie.mpg")


if __name__ == "__main__":
    unittest.main()
