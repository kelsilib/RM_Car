"""检查USB联调工具的哈希兼容、拒绝错版本和保留旧报告，不连接主板。"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]
POWERSHELL = Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
sys.path.insert(0, str(PROJECT / "tools"))
from verify_firmware import verify


@unittest.skipUnless(POWERSHELL.is_file(), "需要Windows PowerShell检查board.ps1")
class HostCheckTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.project = Path(self.temporary.name)
        for directory in ("tools", "reports", "build"):
            (self.project / directory).mkdir()
        shutil.copyfile(PROJECT / "board.ps1", self.project / "board.ps1")
        shutil.copyfile(PROJECT / "tools/console_reply.ps1", self.project / "tools/console_reply.ps1")
        # 用本地假工具代替USB枚举程序，只输出文字，不打开任何串口。
        stub = self.project / "inventory.ps1"
        stub.write_text("Write-Output 'TEST_ONLY no devices'\nexit 0\n", encoding="ascii")
        config = {"programmer": str(stub), "python": sys.executable,
                  "openocd": "missing-openocd", "openocd_scripts": "missing-scripts"}
        self.write_json("tools/board-tools.json", config)
        self.write_json("PROJECT_PROFILE.json", {"latest_artifact_report": "reports/manifest.json"})
        binary = b"test artifact only"
        (self.project / "build/rm_rct6.bin").write_bytes(binary)
        self.sha256 = hashlib.sha256(binary).hexdigest()

    def write_json(self, name, value):
        # PS5.1读取含中文路径的JSON时使用BOM，防止电脑系统代码页影响测试。
        (self.project / name).write_text(json.dumps(value), encoding="utf-8-sig")

    def check_manifest(self, manifest):
        self.write_json("reports/manifest.json", manifest)
        result = subprocess.run([str(POWERSHELL), "-NoProfile", "-File",
                                 str(self.project / "board.ps1"), "-Mode", "Check"],
                                capture_output=True, timeout=30)
        reports = list((self.project / "reports").glob("host-check-*.json"))
        report = json.loads(reports[0].read_text(encoding="utf-8-sig")) if reports else None
        return result, report

    def test_legacy_hash_format(self):
        result, report = self.check_manifest({"sha256": {"rm_rct6.bin": self.sha256}})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(report["firmware_hash_matches_last_verification"])
        self.assertEqual(report["interface"], "UART")
        self.assertFalse(report["swd_config_parsed"])

    def test_variant_hash_format_without_openocd(self):
        result, report = self.check_manifest({"default": {"sha256": {"rm_rct6.bin": self.sha256}}})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(report["firmware_hash_matches_last_verification"])
        self.assertFalse(report["target_connected"])

    def test_changed_binary_fails(self):
        result, report = self.check_manifest({"default": {"sha256": {"rm_rct6.bin": "0" * 64}}})
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(report["firmware_hash_matches_last_verification"])

    def test_missing_hash_fails(self):
        result, report = self.check_manifest({"default": {"sha256": {}}})
        self.assertNotEqual(result.returncode, 0)
        self.assertIsNone(report)


class FirmwareCheckTests(unittest.TestCase):
    def test_default_version_comes_from_project_profile(self):
        with tempfile.TemporaryDirectory() as temporary:
            project = Path(temporary)
            profile = {"compiler": "unused", "firmware_id": "expected-safe"}
            (project / "PROJECT_PROFILE.json").write_text(json.dumps(profile), encoding="utf-8")
            (project / "build").mkdir()
            (project / "build/rm_rct6.bin").write_bytes(b"version=other-build\r\n\0")
            with self.assertRaisesRegex(AssertionError, "固件版本"):
                verify(project)

    def test_version_prefix_is_rejected_before_reading_elf(self):
        with tempfile.TemporaryDirectory() as temporary:
            project = Path(temporary)
            (project / "PROJECT_PROFILE.json").write_text('{"compiler": "unused"}', encoding="utf-8")
            build = project / "separate-build"
            build.mkdir()
            (build / "rm_rct6.bin").write_bytes(b"version=example-ps2\r\n\0")
            with self.assertRaisesRegex(AssertionError, "固件版本"):
                verify(project, build, "example")

    def test_existing_report_is_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / "existing.json"
            report.write_bytes(b"keep this historical report")
            result = subprocess.run([sys.executable, str(PROJECT / "tools/verify_firmware.py"),
                                     "--json", str(report)], capture_output=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(report.read_bytes(), b"keep this historical report")


if __name__ == "__main__":
    unittest.main(verbosity=2)
