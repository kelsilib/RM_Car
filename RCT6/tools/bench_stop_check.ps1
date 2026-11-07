# 架空检查电脑stop。会给指定接口一次短脉冲，再发stop并查询输出，实际停轮由人在旁观察。
param(
    [Parameter(Mandatory=$true)][ValidatePattern('^COM[1-9][0-9]*$')][string]$Port,
    [ValidateRange(1,4)][int]$Channel = 1,
    [ValidateRange(1,100)][int]$Pwm = 50,
    [string]$Log = (Join-Path $PSScriptRoot ('..\reports\bench-stop-' + [Guid]::NewGuid().ToString('N').Substring(0,8) + '.log'))
)
$ErrorActionPreference = 'Stop'
$serial = [IO.Ports.SerialPort]::new($Port,115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
$serial.Handshake = [IO.Ports.Handshake]::None
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.WriteTimeout = 200
$writer = $null
$stream = $null

function Invoke-BenchQuery([string]$Text, [string]$Expected) {
    $serial.DiscardInBuffer()
    $writer.WriteLine('# UTC ' + [DateTime]::UtcNow.ToString('o') + ' TX ' + $Text)
    $serial.Write($Text + "`n")
    $received = [Text.StringBuilder]::new()
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.ElapsedMilliseconds -lt 1000) {
        $chunk = $serial.ReadExisting()
        if ($chunk) { [void]$received.Append($chunk); $writer.Write($chunk) }
        $responses = [regex]::Matches($received.ToString(), '(?m)^reply ([^\r\n]*)\r?\n')
        if ($responses.Count -gt 1) { throw 'Multiple replies received.' }
        if ($responses.Count -eq 1) {
            $payload = $responses[0].Groups[1].Value
            if ($payload -cnotmatch $Expected) { throw ('Unexpected reply: ' + $payload) }
            return $payload
        }
        Start-Sleep -Milliseconds 5
    }
    throw ('Reply timed out: ' + $Text)
}

try {
    $logPath = [IO.Path]::GetFullPath($Log)
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $logPath))
    $stream = [IO.File]::Open($logPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    $writer = [IO.StreamWriter]::new($stream,[Text.UTF8Encoding]::new($false))
    $writer.AutoFlush = $true
    $serial.Open()
    [void](Invoke-BenchQuery 'version' '^version=rm_rct6-(mecanum-20261105-r12-test|drive-20261106-r13-(test|field))$')
    [void](Invoke-BenchQuery 'bench' '^bench built=1 state=1 checks=0x03 ')
    [void](Invoke-BenchQuery 'bench arm' '^OK bench state=2$')
    [void](Invoke-BenchQuery ("bench pulse $Channel $Pwm 200") '^OK bench state=3$')
    Start-Sleep -Milliseconds 60
    $expectedChannels = @(0,0,0,0)
    $expectedChannels[$Channel-1] = $Pwm
    $moving = 'output mode=pwm M1={0} M2={1} M3={2} M4={3}' -f $expectedChannels
    [void](Invoke-BenchQuery 'output' ('^'+[regex]::Escape($moving)+'$'))
    [void](Invoke-BenchQuery 'stop' '^OK stopped; fresh_START_required$')
    [void](Invoke-BenchQuery 'output' '^output mode=pwm M1=0 M2=0 M3=0 M4=0$')
    Write-Host 'PASS stop reply and zero output. Record whether the real wheel stopped.'
    Write-Host ('Log: ' + $logPath)
} finally {
    # 正常结束、超时或检查失败都会再尝试发零输出请求，然后释放串口。
    if ($serial.IsOpen) {
        try {
            $serial.Write("stop`n")
            Start-Sleep -Milliseconds 20
        } finally { $serial.Close() }
    }
    $serial.Dispose()
    if ($writer) { $writer.Dispose() }
    elseif ($stream) { $stream.Dispose() }
}
