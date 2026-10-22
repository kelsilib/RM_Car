# 主控联调入口。默认只检查电脑环境；没有擦除或烧录功能。
# Check检查工具和设备；ReadTarget连接SWD读芯片信息；Monitor抓日志；Analyze分析已有日志。
[CmdletBinding()]
param(
    [ValidateSet('Check','ReadTarget','Monitor','Analyze','Test','Verify','Command','Terminal')][string]$Mode = 'Check',
    [string]$Port,
    [ValidateRange(1,60)][int]$Seconds = 5,
    [ValidateSet('startup','motor')][string]$Scenario = 'startup',
    [string]$Log,
    [string]$Command,
    [switch]$RequireBoot,
    [ValidatePattern('^[A-Za-z0-9]+$')][string]$ProbeSerial
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$boardTools = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'tools\board-tools.json') -Raw | ConvertFrom-Json
$boardConfig = Join-Path $PSScriptRoot 'openocd\stlink-rct6.cfg'
. (Join-Path $PSScriptRoot 'tools\console_reply.ps1')

function Resolve-BoardLog([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return $Path }
    return Join-Path $PSScriptRoot $Path
}

function Invoke-BoardOpenOcd([string[]]$Commands) {
    $boardArgs = @('-s', $boardTools.openocd_scripts, '-f', $boardConfig,
                   '-c', 'gdb_port disabled', '-c', 'tcl_port disabled', '-c', 'telnet_port disabled')
    if ($ProbeSerial) { $boardArgs += @('-c', "adapter serial $ProbeSerial") }
    foreach ($boardCommand in $Commands) { $boardArgs += @('-c', $boardCommand) }
    # OpenOCD正常也会把版本文字写到stderr。先收集完整结果，再用退出码判断，不把它当异常。
    $boardOldPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $boardOutput = @(& $boardTools.openocd @boardArgs 2>&1 | ForEach-Object { "$_" })
        $boardCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $boardOldPreference }
    return [pscustomobject]@{ ExitCode=$boardCode; Output=$boardOutput }
}

function Get-BoardProbes {
    $boardOutput = @(& $boardTools.programmer -l stlink 2>&1 | ForEach-Object { "$_" })
    if ($LASTEXITCODE -ne 0) { throw "ST-Link枚举失败：$($boardOutput -join ' ')" }
    # 只认枚举结果里的真实序列号。不能因为错误信息里出现ST-Link/J-Link名称就说已连接。
    $boardSerials = @([regex]::Matches(($boardOutput -join "`n"), 'ST-LINK\s+SN\s*:\s*([A-Za-z0-9]+)', 'IgnoreCase') |
        ForEach-Object { $_.Groups[1].Value })
    return [pscustomobject]@{ Serials=$boardSerials; Output=$boardOutput }
}

function Invoke-BoardAnalysis([string]$Path) {
    $boardArgs = @((Join-Path $PSScriptRoot 'tools\analyze_log.py'), $Path, '--mode', $Scenario,
        '--json', [IO.Path]::ChangeExtension($Path, '.analysis.json'))
    if ($RequireBoot) { $boardArgs += '--require-boot' }
    $env:PYTHONIOENCODING = 'utf-8'
    & $boardTools.python @boardArgs | ForEach-Object { Write-Host $_ }
    return $LASTEXITCODE
}

try {
    if ($Mode -eq 'Verify') {
        $env:PYTHONIOENCODING = 'utf-8'
        & $boardTools.python (Join-Path $PSScriptRoot 'tools\verify_firmware.py')
        exit $LASTEXITCODE
    }
    if ($Mode -eq 'Test') {
        $env:PYTHONIOENCODING = 'utf-8'
        & $boardTools.python (Join-Path $PSScriptRoot 'tests\test_log_tools.py')
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & (Join-Path $PSScriptRoot 'tests\test_console_reply.ps1')
        exit 0
    }
    if ($Mode -eq 'Check') {
        $boardMissing = @()
        foreach ($boardName in @('openocd','programmer','python','openocd_scripts')) {
            $boardPath = $boardTools.$boardName
            if (-not (Test-Path -LiteralPath $boardPath)) { $boardMissing += $boardName }
        }
        if ($boardMissing.Count) { throw "找不到工具：$($boardMissing -join ', ')。请更新tools\board-tools.json里的路径。" }
        # shutdown在配置阶段直接结束，不执行init，所以这里不会连接或复位目标板。
        $boardParse = Invoke-BoardOpenOcd @('echo RM_CONFIG_OK', 'shutdown')
        if ($boardParse.ExitCode -ne 0 -or ($boardParse.Output -join "`n") -notmatch 'RM_CONFIG_OK') {
            throw "OpenOCD配置读取失败：$($boardParse.Output -join ' ')"
        }
        $boardProbes = Get-BoardProbes
        $boardPorts = @()
        if (Get-Command Get-PnpDevice -ErrorAction SilentlyContinue) {
            $boardPorts = @(Get-PnpDevice -Class Ports -PresentOnly -ErrorAction SilentlyContinue |
                Select-Object FriendlyName,InstanceId,Status)
        } else {
            $boardPorts = @([IO.Ports.SerialPort]::GetPortNames() | ForEach-Object {
                [pscustomobject]@{ FriendlyName=$_; InstanceId=''; Status='unknown' }
            })
        }
        $boardProfile = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'PROJECT_PROFILE.json') -Raw | ConvertFrom-Json
        $boardManifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot $boardProfile.latest_artifact_report) -Raw | ConvertFrom-Json
        $boardBin = Join-Path $PSScriptRoot 'build\rm_rct6.bin'
        $boardHash = (Get-FileHash -LiteralPath $boardBin -Algorithm SHA256).Hash.ToLowerInvariant()
        $boardReport = [ordered]@{
            checked_at_utc=[DateTime]::UtcNow.ToString('o'); tools_ready=$true; config_parsed=$true;
            firmware_hash_matches_last_verification=($boardHash -eq $boardManifest.sha256.'rm_rct6.bin');
            stlink_serials=@($boardProbes.Serials); serial_ports=$boardPorts;
            target_connected=$false; flashed=$false; hardware_tested=$false;
            note='Check只枚举USB和读取配置，不连接目标MCU。没有设备时也能完成工具检查。'
        }
        $boardReport | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 (Join-Path $PSScriptRoot 'reports\host-check.json')
        Write-Host '工具和OpenOCD配置已就绪。'
        Write-Host "固件与上次验证的哈希一致：$($boardReport.firmware_hash_matches_last_verification)"
        Write-Host "检测到ST-Link数量：$($boardProbes.Serials.Count)"
        foreach ($boardItem in $boardPorts) { Write-Host "串口：$($boardItem.FriendlyName)" }
        if (-not $boardProbes.Serials.Count) { Write-Host '当前没有ST-Link，实物验证待接板后进行。' }
        if (-not $boardReport.firmware_hash_matches_last_verification) {
            throw '当前BIN与上次验证记录不同，需要先重新验证固件，不能沿用旧的成功记录。'
        }
        exit 0
    }

    if ($Mode -eq 'ReadTarget') {
        $boardProbes = Get-BoardProbes
        if (-not $boardProbes.Serials.Count) { throw '没有检测到ST-Link，请先连接下载器和主控。' }
        if ($ProbeSerial) {
            if ($ProbeSerial -notin $boardProbes.Serials) { throw '指定的ST-Link序列号不在当前设备列表里。' }
        } elseif ($boardProbes.Serials.Count -gt 1) {
            throw '发现多个ST-Link，请用-ProbeSerial指定本次下载器的序列号。'
        }
        # 只连接并读ID和容量；不发reset、halt、program或erase命令。
        $boardRead = Invoke-BoardOpenOcd @('init', 'echo RM_DBGMCU_IDCODE', 'mdw 0xE0042000 1',
            'echo RM_FLASH_SIZE_KB', 'mdh 0x1FFFF7E0 1', 'shutdown')
        $boardRead.Output | Set-Content -Encoding utf8 (Join-Path $PSScriptRoot 'reports\target-info.log')
        $boardRead.Output | ForEach-Object { Write-Host $_ }
        if ($boardRead.ExitCode -ne 0) { throw '目标连接/读取失败，原始信息已保存到reports\target-info.log。' }
        Write-Host '已读取目标信息。RCT6预期Flash为256KiB；完整型号仍需结合芯片丝印核对。'
        exit 0
    }

    if ($Mode -eq 'Analyze') {
        if (-not $Log) { throw 'Analyze需要-Log指定一份已有日志。' }
        $boardCode = Invoke-BoardAnalysis (Resolve-BoardLog $Log)
        exit $boardCode
    }

    if ($Mode -eq 'Command' -and (-not $Command -or $Command.Length -gt 63 -or $Command -match '[^\x20-\x7E]')) {
        throw 'Command需要-Command指定一行1～63字节ASCII指令，不能包含回车、换行或多条指令。'
    }
    if (-not $Port -or $Port -notmatch '^COM[1-9][0-9]*$') { throw '需要明确指定-Port，例如COM7；不会自动选择蓝牙串口。' }
    if ($Port -notin [IO.Ports.SerialPort]::GetPortNames()) { throw "当前不存在串口$Port。先用Check查看设备列表。" }
    if (-not $Log) { $Log = 'reports\capture-' + [Guid]::NewGuid().ToString('N').Substring(0,8) + '.log' }
    $boardLog = Resolve-BoardLog $Log
    if (Test-Path -LiteralPath $boardLog) { throw '这份日志已存在，请换一个文件名，避免覆盖旧测试。' }
    $boardSerial = [IO.Ports.SerialPort]::new($Port,115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
    $boardSerial.Handshake = [IO.Ports.Handshake]::None
    # 打开之前就设为false，程序不主动切换板上自动BOOT/RESET用的两根控制线。
    $boardSerial.DtrEnable = $false
    $boardSerial.RtsEnable = $false
    $boardSerial.WriteTimeout = 200
    $boardText = [Text.StringBuilder]::new()
    try {
        $boardSerial.Open()
        if ($Mode -eq 'Terminal') {
            Write-Host '输入help看指令；exit退出电脑终端。标定时先cal arm，再在5秒内发送一次cal pulse。'
            while ($true) {
                $boardLine = Read-Host 'RM'
                if ($null -eq $boardLine -or $boardLine -eq 'exit') { break }
                if (-not $boardLine) { continue }
                if ($boardLine.Length -gt 63 -or $boardLine -match '[^\x20-\x7E]') {
                    Write-Host '一次只能发一行1～63字节ASCII文字。'
                    continue
                }
                # Read-Host先收齐用户输入，再整行发出，不让手动打字拖成过期半包。
                $boardSerial.DiscardInBuffer()
                $boardSerial.Write($boardLine + "`n")
                $boardReplyTimer = [Diagnostics.Stopwatch]::StartNew()
                while ($boardReplyTimer.Elapsed.TotalMilliseconds -lt 400) {
                    $boardReply = $boardSerial.ReadExisting()
                    if ($boardReply) { Write-Host -NoNewline $boardReply }
                    Start-Sleep -Milliseconds 20
                }
            }
            exit 0
        }
        if ($Mode -eq 'Command') {
            $boardSerial.DiscardInBuffer()
            $boardSerial.Write($Command + "`n")
            Write-Host "已发送到主控调试串口：$Command"
        }
        Write-Host "已开始监听$Port，115200/8N1，持续${Seconds}秒。需要开机日志时，现在按主控复位键。"
        $boardTimer = [Diagnostics.Stopwatch]::StartNew()
        while ($boardTimer.Elapsed.TotalSeconds -lt $Seconds) {
            $boardChunk = $boardSerial.ReadExisting()
            if ($boardChunk) {
                [void]$boardText.Append($boardChunk)
                Write-Host -NoNewline $boardChunk
            }
            if ($boardText.Length -gt 1048576) { throw '日志超过1MiB，可能收到持续乱码，请先核对串口和波特率。' }
            Start-Sleep -Milliseconds 20
        }
    } finally {
        if ($boardSerial.IsOpen) { $boardSerial.Close() }
        $boardSerial.Dispose()
    }
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $boardLog))
    [IO.File]::WriteAllText($boardLog, $boardText.ToString(), [Text.UTF8Encoding]::new($false))
    Write-Host "`n日志已保存：$boardLog"
    # 指令回复可能是ps2/计数/标定结果，不能套用“默认禁动启动日志”的通过条件。
    if ($Mode -eq 'Command') {
        # 只接受带reply前缀的完整对应回复。即使周期日志还在输出，也不能冒充指令成功。
        $boardResult = Get-BoardCommandResult -Command $Command -Text $boardText.ToString()
        if (-not $boardResult.Passed) { throw $boardResult.Message }
        Write-Host $boardResult.Message
        exit 0
    }
    $boardCode = Invoke-BoardAnalysis $boardLog
    exit $boardCode
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
