# .\build.ps1：编译STM32固件；加-Test：只在PC上跑测试；加-Rebuild：所有文件重新编译。
# 这些命令都不下载到单片机，也不会操作电机。
param([switch]$Test, [switch]$Rebuild, [switch]$Bench, [switch]$Drive, [switch]$Shovel, [switch]$ArmPair,
    [ValidateRange(0,6)][int]$ServoCal = 0, [ValidateRange(1,64)][int]$Jobs = 4)
$ErrorActionPreference = 'Stop'
if (([int][bool]$Test + [int][bool]$Bench + [int][bool]$Drive + [int][bool]$Shovel + [int][bool]$ArmPair + [int]($ServoCal -gt 0)) -gt 1) {
    throw 'Choose only one of Test, Bench, Drive, Shovel, ArmPair and ServoCal.'
}
# CubeIDE换安装位置时改这里；脚本从plugins递归查找GCC和Make，不要求全局PATH配置。
$taskPlugins = 'D:\stm32\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins'
if (-not (Test-Path -LiteralPath $taskPlugins)) { throw 'Set taskPlugins to your CubeIDE plugins directory.' }
$taskGcc = Get-ChildItem -LiteralPath $taskPlugins -Filter arm-none-eabi-gcc.exe -Recurse | Select-Object -First 1
$taskMake = Get-ChildItem -LiteralPath $taskPlugins -Filter make.exe -Recurse | Select-Object -First 1
if (-not $taskGcc -or -not $taskMake) { throw 'CubeIDE GCC or Make was not found.' }
$env:PATH = "$($taskGcc.DirectoryName);$($taskMake.DirectoryName);$env:PATH"
Push-Location $PSScriptRoot
try {
    $taskMakeArgs = @('-j', "$Jobs")
    if ($Rebuild) { $taskMakeArgs += '-B' }
    if ($Test) {
        # 用PC的GCC跑逻辑测试和模拟设备重连测试；这些exe不能烧进STM32。
        $taskHostGcc = 'C:\Users\fakel\Documents\Codex\tools\w64devkit\bin\gcc.exe'
        if (-not (Test-Path -LiteralPath $taskHostGcc)) { throw 'Host GCC not found; update taskHostGcc.' }
        $taskMakeArgs += @('test', "HOST_CC=$($taskHostGcc.Replace('\','/'))")
    } elseif ($ArmPair) {
        # □控制S2；△/○选S3/S4，十字键上下控制，车轮PWM0。
        $taskMakeArgs += @('all', 'BUILD_DIR=build-arm-dpad-r20',
            'EXTRA_CFLAGS=-DRM_ARM_PAIR_CONTROL=1 -DRM_SHOVEL_CONTROL=1 -DRM_SERVO_CAL_CHANNEL=2 -DRM_PS2_WIRED=1 -DRM_ARM_WIRED=1')
    } elseif ($Shovel) {
        # S2专用按键控制，车轮恒为PWM0。□键每次按住交替下降/上升，松开撤销PWM。
        $taskMakeArgs += @('all', 'BUILD_DIR=build-shovel-r18',
            'EXTRA_CFLAGS=-DRM_SHOVEL_CONTROL=1 -DRM_SERVO_CAL_CHANNEL=2 -DRM_PS2_WIRED=1 -DRM_ARM_WIRED=1')
    } elseif ($ServoCal) {
        # 独立单路标定入口，空载单台、固定5V，上电无PWM。confirm/arm/center后才能输出。
        $taskMakeArgs += @('all', "BUILD_DIR=build-servo-cal-$ServoCal",
            "EXTRA_CFLAGS=-DRM_SERVO_CAL_CHANNEL=$ServoCal -DRM_PS2_WIRED=1 -DRM_ARM_WIRED=1")
    } elseif ($Drive) {
        # 已实测轮向的行走构建。START切换启用，按R2给油，松R2停车并保持启用。
        # 比赛START统一许可，△/○/×切换机构姿态，L2调肩肘腕。输出等待逐路标定。
        $taskMakeArgs += @('all', 'BUILD_DIR=build-drive',
            'EXTRA_CFLAGS=-DRM_BENCH_TEST=1 -DRM_FIELD_DRIVE=1 -DRM_PS2_WIRED=1 -DRM_ARM_WIRED=0')
    } elseif ($Bench) {
        # 今晚的限时麦轮联调版单独放build-bench，启用PS2，舵机接口关闭。
        $taskMakeArgs += @('all', 'BUILD_DIR=build-bench',
            'EXTRA_CFLAGS=-DRM_BENCH_TEST=1 -DRM_PS2_WIRED=1 -DRM_ARM_WIRED=0')
    } else {
        # 真正的STM32固件会生成到build目录，文件名rm_rct6，包含ELF/HEX/BIN三种格式。
        $taskMakeArgs += 'all'
    }
    & $taskMake.FullName @taskMakeArgs
    $taskExit = $LASTEXITCODE
} finally { Pop-Location }
exit $taskExit
