"""把当前固件日志翻译成容易看懂的结果。只分析文件，不连接或控制硬件。"""
import argparse
from dataclasses import asdict, dataclass
import json
from pathlib import Path
import re

BOOT_MARK = "RM RCT6 stage 1: default motion INHIBITED"
STATUS = re.compile(
    r"(?:^|\s)state=([0-9]{1,10}) inhibit=0x([0-9A-Fa-f]{1,8}) motor_seen=([0-9]{1,10}) "
    r"age_ms=([0-9]{1,10}) bad=([0-9]{1,10}) unknown=([0-9]{1,10})(?=\s|$)"
)
REASONS = {
    0x01: "配置还有未验收项", 0x02: "手柄无效或数据过期",
    0x04: "没有新鲜的电机反馈", 0x08: "已记住通信故障，需要排查后复位",
    0x10: "软件急停请求", 0x20: "控制循环停顿超过100ms",
}
STATES = {0: "禁动", 1: "已解锁", 2: "故障", 3: "软件急停", 4: "单轮标定"}


@dataclass(frozen=True)
class Sample:
    line: int
    state: int
    inhibit: int
    motor_seen: int
    age_ms: int
    bad: int
    unknown: int

    @property
    def motor_fresh(self):
        # 必须同时看是否收到过、最后收到多久了、安全判断是否仍报反馈超时。
        return self.motor_seen == 1 and self.age_ms <= 200 and not self.inhibit & 0x04


def analyze(text, mode="startup", require_boot=False):
    """至少要三行状态，不能因为出现OK或串口成功打开就判断启动正常。"""
    if mode not in ("startup", "motor"):
        raise ValueError("mode must be startup or motor")
    samples, errors, notes = [], [], []
    boot_count = 0
    for number, line in enumerate(text.splitlines(), 1):
        if BOOT_MARK in line:
            boot_count += 1
        match = STATUS.search(line)
        if not match:
            if "state=" in line:
                errors.append(f"第{number}行状态不完整或格式不对")
            continue
        values = [int(value, 16 if index == 1 else 10) for index, value in enumerate(match.groups())]
        item = Sample(number, *values)
        if item.state not in STATES or item.motor_seen not in (0, 1) or item.inhibit & ~0x3F:
            errors.append(f"第{number}行出现不认识的状态或标志位")
            continue
        if any(value > 0xFFFFFFFF for value in (item.age_ms, item.bad, item.unknown)):
            errors.append(f"第{number}行计数超过32位范围")
            continue
        samples.append(item)
        # 这两个模式专门验收默认禁动阶段。实际行驶日志不能拿这套规则验收。
        if item.state == 1:
            errors.append(f"第{number}行显示已经解锁，与本阶段预期不符")
        if item.state == 3:
            errors.append(f"第{number}行仍处于软件急停状态")
        if item.state == 4:
            errors.append(f"第{number}行处于单轮标定，不能按默认禁动启动流程验收")
        console_fault = re.search(r"(?:^|\s)console_fault=([0-9]+)(?=\s|$)", line)
        if console_fault and console_fault[1] != "0":
            errors.append(f"第{number}行调试串口已故障，需要排查后复位")
        if not item.inhibit & 0x01:
            errors.append(f"第{number}行配置禁动标志消失，请核对烧录版本和配置")
        if item.inhibit & 0x38:
            errors.append(f"第{number}行存在通信故障、急停或控制停顿")
        if item.bad:
            errors.append(f"第{number}行已有坏包，bad={item.bad}")
    if len(samples) < 3:
        errors.append("至少需要3行完整状态日志，当前数据不足")
    if require_boot and not boot_count:
        errors.append("没找到启动标记；需要先开始监听，再按主控复位键")
    if boot_count > 1:
        errors.append("本次日志出现多次启动标记，请检查是否反复复位")
    if mode == "motor" and (len(samples) < 3 or not all(item.motor_fresh for item in samples[-3:])):
        errors.append("最后3行没有保持新鲜电机反馈：需要motor_seen=1、age_ms<=200且无0x04")
    if samples:
        if not boot_count:
            notes.append("没有捕获到开机文字；只能评价这段运行日志，不能确认完整启动过程")
        if samples[-1].unknown:
            notes.append("unknown可能是配置回复，不等于坏包，也不等于运动命令回执")
        if mode == "startup" and samples[-1].inhibit == 0x07:
            notes.append("0x07符合默认未接手柄、未接驱动的单板状态")
        if mode == "motor":
            notes.append("反馈只能证明驱动TX到主控RX方向，不能证明主控命令线或独立停车保护正常")
    return {
        "passed": not errors, "mode": mode, "scope": "log_checks_only",
        "status_lines": len(samples), "boot_markers": boot_count,
        "latest": asdict(samples[-1]) if samples else None,
        "latest_state_text": STATES[samples[-1].state] if samples else None,
        "latest_reasons": [label for bit, label in REASONS.items() if samples and samples[-1].inhibit & bit],
        "errors": errors, "notes": notes,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="保存下来的串口日志")
    parser.add_argument("--mode", choices=("startup", "motor"), default="startup")
    parser.add_argument("--require-boot", action="store_true", help="必须捕获到启动标记")
    parser.add_argument("--json", type=Path, help="另外保存一份机器可读的结果")
    args = parser.parse_args()
    try:
        report = analyze(args.log.read_text(encoding="utf-8-sig", errors="replace"), args.mode, args.require_boot)
    except OSError as exc:
        parser.error(str(exc))
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("日志检查通过" if report["passed"] else "日志检查未通过")
    print(f"完整状态行：{report['status_lines']}；启动标记：{report['boot_markers']}")
    if report["latest"]:
        print(f"最新状态：{report['latest_state_text']}；当前原因：" + "；".join(report["latest_reasons"]))
    for message in report["errors"]:
        print("问题：" + message)
    for message in report["notes"]:
        print("说明：" + message)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
