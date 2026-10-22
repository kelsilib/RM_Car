"""检查日志工具不会把空日志、断线、故障或提前解锁误判成测试通过。"""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from analyze_log import BOOT_MARK, analyze

SAMPLES = Path(__file__).resolve().parent / "logs"


class LogChecks(unittest.TestCase):
    def setUp(self):
        self.startup = (SAMPLES / "simulated-startup.log").read_text(encoding="utf-8")
        self.motor = (SAMPLES / "simulated-motor.log").read_text(encoding="utf-8")

    def test_board_only_expected_inhibits(self):
        report = analyze(self.startup, require_boot=True)
        self.assertTrue(report["passed"])
        self.assertEqual(report["status_lines"], 3)
        self.assertEqual(report["scope"], "log_checks_only")

    def test_motor_can_become_ready_after_startup(self):
        report = analyze(self.motor, "motor", require_boot=True)
        self.assertTrue(report["passed"])
        self.assertEqual(report["latest"]["unknown"], 1)

    def test_no_log_or_only_ok_is_not_success(self):
        for text in ("", "OK\nport opened\n", BOOT_MARK):
            with self.subTest(text=text):
                self.assertFalse(analyze(text)["passed"])

    def test_not_enough_status_samples(self):
        self.assertFalse(analyze("\n".join(self.startup.splitlines()[:-1]))["passed"])

    def test_motor_timeout_is_detected(self):
        self.assertFalse(analyze(self.startup, "motor")["passed"])
        self.assertFalse(analyze(self.motor.replace("age_ms=8", "age_ms=201"), "motor")["passed"])

    def test_motor_seen_bit_and_inhibit_must_agree(self):
        self.assertFalse(analyze(self.motor.replace("motor_seen=1", "motor_seen=0"), "motor")["passed"])
        self.assertFalse(analyze(self.motor.replace("inhibit=0x03", "inhibit=0x07"), "motor")["passed"])

    def test_fault_bits_fail_the_check(self):
        for mask in ("0F", "17", "27"):
            with self.subTest(mask=mask):
                self.assertFalse(analyze(self.startup.replace("0x07", "0x" + mask))["passed"])

    def test_bad_frame_counter_must_stay_zero(self):
        self.assertFalse(analyze(self.startup.replace("bad=0", "bad=1"))["passed"])

    def test_unexpected_arm_or_estop_is_rejected(self):
        for state in (1, 3):
            self.assertFalse(analyze(self.startup.replace("state=2", f"state={state}"))["passed"])

    def test_configuration_inhibit_cannot_be_bypassed(self):
        self.assertFalse(analyze(self.motor.replace("0x03", "0x02"))["passed"])

    def test_cut_off_or_unrecognized_status_is_rejected(self):
        for extra in ("state=2 inhibit=0x07", "state=4 inhibit=0x07 motor_seen=0 age_ms=0 bad=0 unknown=0",
                      "state=2 inhibit=0x80 motor_seen=0 age_ms=0 bad=0 unknown=0"):
            with self.subTest(extra=extra):
                self.assertFalse(analyze(self.startup + "\n" + extra)["passed"])

    def test_overflow_and_extremely_long_garbage_do_not_pass(self):
        for value in ("4294967296", "9" * 5000):
            self.assertFalse(analyze(self.startup.replace("age_ms=500", "age_ms=" + value))["passed"])

    def test_missing_boot_is_optional_and_can_be_required(self):
        text = self.startup.replace(BOOT_MARK, "")
        self.assertTrue(analyze(text)["passed"])
        self.assertFalse(analyze(text, require_boot=True)["passed"])

    def test_multiple_boots_fail_the_check(self):
        self.assertFalse(analyze(self.startup + "\n" + BOOT_MARK)["passed"])

    def test_timestamp_prefixes_are_accepted(self):
        text = "\n".join("[00:01:00] " + line for line in self.motor.splitlines())
        self.assertTrue(analyze(text, "motor")["passed"])

    def test_calibration_is_not_default_startup(self):
        self.assertFalse(analyze(self.startup.replace("state=2", "state=4"))["passed"])

    def test_console_fault_is_not_ignored(self):
        text=self.startup.replace("unknown=0", "unknown=0 cal=0 console_fault=1")
        self.assertFalse(analyze(text)["passed"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
