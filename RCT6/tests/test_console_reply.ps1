# 在电脑上喂入文字，检验真实Command模式使用的判定函数；不需要主板或串口。
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '..\tools\console_reply.ps1')
$script:replyChecks = 0
function Assert-Reply([string]$Name, [string]$Command, [string]$Text, [bool]$Expected) {
    $result = Get-BoardCommandResult -Command $Command -Text $Text
    if ($result.Passed -ne $Expected) { throw "FAIL ${Name}: $($result.Message)" }
    $script:replyChecks++
}

$periodic = "state=2 inhibit=0x07 motor_seen=0 age_ms=500 bad=0 unknown=0 cal=0 console_fault=1`r`n"
$status = "reply state=2 inhibit=0x07 motor_seen=0 age_ms=500 bad=0 unknown=0 cal=0`r`n"
$version = "reply version=rm_rct6-chassis-20261022-r1`r`n"
Assert-Reply 'only periodic status' 'status' $periodic $false
Assert-Reply 'only boot version' 'version' "version=rm_rct6-chassis-20261022-r1`r`n" $false
Assert-Reply 'real status with periodic logs' 'status' ($periodic+$status+$periodic) $true
Assert-Reply 'real version' 'version' $version $true
Assert-Reply 'wrong command response' 'status' $version $false
Assert-Reply 'no reply' 'status' '' $false
Assert-Reply 'truncated reply' 'status' ($status.TrimEnd("`r","`n")) $false
Assert-Reply 'CR without LF is incomplete' 'status' ($status.TrimEnd("`n")) $false
Assert-Reply 'embedded marker is not a reply' 'status' ('noise '+$status) $false
Assert-Reply 'error overrides automatic log' 'status' ($periodic+"reply ERR unknown_command; send_help`n") $false
Assert-Reply 'two replies are ambiguous' 'status' ($status+$status) $false
Assert-Reply 'error after success is ambiguous' 'status' ($status+"reply ERR stale_command`n") $false
Assert-Reply 'case must match firmware' 'STATUS' $status $false
Assert-Reply 'command cannot contain line break' "status`nstop" $status $false
Assert-Reply 'command cannot contain tab' "status`t" $status $false
Assert-Reply 'long command rejected' ('status'+' '*58) $status $false
Assert-Reply 'spaces match firmware tokenization' '  driver   reply  ' "reply driver_reply seen=0 age_ms=0 frame=none`n" $true
Assert-Reply 'unrelated OK is not cal permission' 'cal arm' "reply OK query_queued; inspect_feedback_not_ACK`n" $false
Assert-Reply 'leading zeroes match firmware numbers' 'cal pulse 01 050 0200' "reply OK one_pulse_queued`n" $true
Assert-Reply 'bare legacy OK rejected' 'stop' "OK stopped; fresh_START_required`n" $false
Assert-Reply 'unknown command cannot borrow OK' 'unknown' "reply OK stopped; fresh_START_required`n" $false

$pairs = @(
    @('help','help version status ps2'),
    @('ps2','ps2 valid=1 fwd=0 left=0 yaw=0 start=0 select=0 age_ms=1'),
    @('counts','counts seen=1 M1=0 M2=0 M3=0 M4=0 age_ms=1'),
    @('config','config issues=0xFF speed_unit=0 cal_enabled=0 cal_max_pwm=100 cal_max_ms=200'),
    @('output','output mode=pwm M1=0 M2=0 M3=0 M4=0'),
    @('battery','battery seen=0 millivolts=0 age_ms=1'),
    @('driver read','OK query_queued; inspect_feedback_not_ACK'),
    @('driver voltage','OK query_queued; inspect_feedback_not_ACK'),
    @('stop','OK stopped; fresh_START_required'),
    @('cal cancel','OK stopped; fresh_START_required'),
    @('cal arm','OK cal_ready window_ms=5000'),
    @('cal pulse 1 -50 200','OK one_pulse_queued')
)
foreach ($pair in $pairs) { Assert-Reply ('valid '+$pair[0]) $pair[0] ("reply "+$pair[1]+"`r`n") $true }
Write-Host "PASS console reply checks: $script:replyChecks"
