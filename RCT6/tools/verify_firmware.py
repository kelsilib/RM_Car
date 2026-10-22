"""检查真实ARM产物的地址、向量和HEX/BIN一致性；不连接或操作开发板。"""
from pathlib import Path
import hashlib
import json
import struct
import subprocess


def verify(project):
    profile = json.loads((project / "PROJECT_PROFILE.json").read_text(encoding="utf-8-sig"))
    compiler_dir = Path(profile["compiler"]).parent
    build = project / "build"
    binary = (build / "rm_rct6.bin").read_bytes()
    elf = (build / "rm_rct6.elf").read_bytes()
    assert elf[:4] == b"\x7fELF" and elf[4:6] == b"\x01\x01", "需要32位小端ELF"
    assert struct.unpack_from("<H", elf, 18)[0] == 40, "ELF不是ARM架构"
    nm = subprocess.check_output([str(compiler_dir / "arm-none-eabi-nm.exe"), str(build / "rm_rct6.elf")], text=True)
    symbols = {}
    for line in nm.splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = int(fields[0], 16)
    word = lambda index: struct.unpack_from("<I", binary, index * 4)[0]
    assert word(0) == 0x2000C000, "RCT6初始栈不在48KiB RAM顶部"
    for index, symbol in [(1,"Reset_Handler"), (15,"SysTick_Handler"), (53,"USART1_IRQHandler"), (54,"USART2_IRQHandler")]:
        assert word(index) == symbols[symbol] | 1, f"向量不匹配：{symbol}"
    assert struct.unpack_from("<I", elf, 24)[0] == word(1), "ELF入口与复位向量不一致"
    assert len(binary) <= 256 * 1024
    assert "g_app" in symbols and "g_console" in symbols
    assert "rm_firmware_main" not in symbols and "test_config" not in symbols, "测试入口混入固件"
    memory, base = {}, 0
    eof = False
    for line in (build / "rm_rct6.hex").read_text().splitlines():
        assert line.startswith(":"), "HEX记录缺少冒号"
        record = bytes.fromhex(line[1:])
        assert len(record) == record[0] + 5 and sum(record) % 256 == 0, "HEX长度或校验和错误"
        address, kind, data = int.from_bytes(record[1:3],"big"), record[3], record[4:-1]
        if kind == 0:
            for offset, byte in enumerate(data):
                assert base + address + offset not in memory, "HEX出现重复地址"
                memory[base + address + offset] = byte
        elif kind == 4:
            assert len(data) == 2
            base = int.from_bytes(data,"big") << 16
        elif kind == 1:
            eof = True
            break
    assert eof and min(memory) == 0x08000000
    assert max(memory)-min(memory)+1 == len(binary)
    assert bytes(memory.get(0x08000000+i,0) for i in range(len(binary))) == binary
    size = subprocess.check_output([str(compiler_dir / "arm-none-eabi-size.exe"), "-A", str(build / "rm_rct6.elf")], text=True)
    sections = {}
    for line in size.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[0].startswith("."):
            sections[fields[0]] = int(fields[1])
    ram = sum(sections.get(name,0) for name in (".data", ".bss", "._user_heap_stack"))
    assert ram <= 48 * 1024
    return {"flash_bytes":len(binary), "ram_including_reserves_bytes":ram,
            "initial_stack":hex(word(0)), "reset_vector":hex(word(1)),
            "uart1_vector":"matched", "uart2_vector":"matched", "systick_vector":"matched",
            "hex_bin_equal":True, "scope":"artifact_structure_only",
            "sha256":{name:hashlib.sha256((build/name).read_bytes()).hexdigest()
                      for name in ("rm_rct6.bin","rm_rct6.elf","rm_rct6.hex")},
            "flashed":False, "hardware_tested":False}


def main():
    project = Path(__file__).resolve().parents[1]
    try:
        report = verify(project)
    except (OSError, ValueError, AssertionError, subprocess.CalledProcessError, struct.error) as exc:
        print("固件内容检查失败：" + str(exc))
        return 1
    (project / "reports/software-artifact-check.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    print(f"固件内容检查通过：Flash {report['flash_bytes']}字节，RAM含预留{report['ram_including_reserves_bytes']}字节")
    print("已核对ARM架构、初始栈、4个关键向量、HEX校验和及HEX/BIN内容。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
