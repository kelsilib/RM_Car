# .\build.ps1：编译STM32固件；加-Test：只在PC上跑测试；加-Rebuild：所有文件重新编译。
# 这些命令都不下载到单片机，也不会操作电机。
param([switch]$Test, [switch]$Rebuild, [ValidateRange(1,64)][int]$Jobs = 4)
$ErrorActionPreference = 'Stop'
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
    } else {
        # 真正的STM32固件会生成到build目录，文件名rm_rct6，包含ELF/HEX/BIN三种格式。
        $taskMakeArgs += 'all'
    }
    & $taskMake.FullName @taskMakeArgs
    $taskExit = $LASTEXITCODE
} finally { Pop-Location }
exit $taskExit
