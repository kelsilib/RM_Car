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
Assert-Reply 'driver info rejects unrelated frame reply' 'driver info' "reply driver_reply seen=0 age_ms=0 frame=none`n" $false
Assert-Reply 'driver safety rejects unrelated info' 'driver safety' "reply driver_info fields=0x00`n" $false
Assert-Reply 'arm query cannot borrow state reply' 'arm' $status $false
Assert-Reply 'servo out of range rejected' 'servo 8' "reply servo channel=8 enabled=0`n" $false
Assert-Reply 'servo channel must match' 'servo 1' "reply servo channel=2 enabled=0`n" $false
Assert-Reply 'servo leading zero accepted' 'servo 01' "reply servo channel=1 enabled=0`n" $true

$pairs = @(
    @('help','help version status ps2'),
    @('ps2','ps2 valid=1 fwd=0 left=0 yaw=0 start=0 select=0 age_ms=1'),
    @('arm','arm mode=0 armed=0 channel=1 outputs=0x00 pulse_us=0 issues=0x7F inhibit=0x061 hw=0 io_fault=0'),
    @('servo 1','servo channel=1 enabled=0 ready=0 min_us=0 max_us=0 start_us=0 rate_us_s=0 dir=0 pulse_us=0'),
    @('counts','counts seen=1 M1=0 M2=0 M3=0 M4=0 age_ms=1'),
    @('config','config issues=0xBF speed_unit=1 cal_enabled=0 cal_max_pwm=100 cal_max_ms=200'),
    @('output','output mode=pwm M1=0 M2=0 M3=0 M4=0'),
    @('battery','battery seen=0 millivolts=0 age_ms=1'),
    @('driver read','OK query_queued; inspect_feedback_not_ACK'),
    @('driver voltage','OK query_queued; inspect_feedback_not_ACK'),
    @('driver info','driver_info fields=0x00 age_ms=1 version=none mtype=0 deadzone=0 mline=0 mphase=0 diameter_mm=0.000'),
    @('driver safety','driver_safety spd_unit=mm_s command_loss=hold_last timeout_config=unsupported software_stop_verified=0 diameter_match=0'),
    @('stop','OK stopped; fresh_START_required'),
    @('cal cancel','OK stopped; fresh_START_required'),
    @('cal arm','OK cal_ready window_ms=5000'),
    @('cal pulse 1 -50 200','OK one_pulse_queued')
)
foreach ($pair in $pairs) { Assert-Reply ('valid '+$pair[0]) $pair[0] ("reply "+$pair[1]+"`r`n") $true }
Assert-Reply 'bench status' 'bench' "reply bench built=1 state=0`r`n" $true
Assert-Reply 'bench on' 'bench on' "reply OK bench state=1`r`n" $true
Assert-Reply 'bench off' 'bench off' "reply OK bench state=0`r`n" $true
Assert-Reply 'bench arm' 'bench arm' "reply OK bench state=2`r`n" $true
Assert-Reply 'bench pulse' 'bench pulse 1 -50 200' "reply OK bench state=3`r`n" $true
Assert-Reply 'bench dir' 'bench dir 1 -1' "reply OK bench state=1`r`n" $true
Assert-Reply 'bench drive' 'bench drive' "reply OK bench state=4`r`n" $true
Assert-Reply 'bench response mismatch' 'bench arm' "reply OK bench state=3`r`n" $false
Assert-Reply 'bench rejected' 'bench drive' "reply ERR bench_denied`r`n" $false
Assert-Reply 'bench invalid direction' 'bench dir 1 0' "reply OK bench state=1`r`n" $false
Assert-Reply 'wheel speed feedback' 'wheels' "reply wheels speed_seen=1 age_ms=2 target_mm_s=50,-50,-50,50 actual_mm_s=49,-50,-48,50`r`n" $true
Assert-Reply 'wheel speed subscription' 'wheels read' "reply OK query_queued; inspect_feedback_not_ACK`r`n" $true
Assert-Reply 'wheel feedback mismatch' 'wheels' "reply output mode=pwm M1=0 M2=0 M3=0 M4=0`r`n" $false
Assert-Reply 'shovel pose query' 'shovel' "reply shovel ready=0 scoop_us=0 store_us=0 pulse_us=0`r`n" $true
Assert-Reply 'shovel rejects unrelated servo reply' 'shovel' "reply servo channel=2 enabled=0`r`n" $false
Assert-Reply 'lift pose query' 'poses 1' "reply poses channel=1 ready=0 a_us=0 b_us=0 target_us=0 auto=0`r`n" $true
Assert-Reply 'pose channel must match' 'poses 6' "reply poses channel=1 ready=0`r`n" $false
Assert-Reply 'pose invalid channel' 'poses 8' "reply poses channel=8 ready=0`r`n" $false
Assert-Reply 'single-servo calibration status' 'scal' "reply scal channel=2 ack=0 armed=0`r`n" $true
Assert-Reply 'single-servo calibration arm' 'scal arm' "reply OK scal accepted; inspect_scal_for_applied_pulse`r`n" $true
Assert-Reply 'single-servo calibration step' 'scal step -10' "reply OK scal accepted; inspect_scal_for_applied_pulse`r`n" $true
Assert-Reply 'single-servo calibration result' 'scal result' "reply scal_result channel=2 ready=0`r`n" $true
Assert-Reply 'single-servo rejected step' 'scal step 1000' "reply ERR scal_denied; inspect_scal_and_calibration_steps`r`n" $false
Assert-Reply 'single-servo wrong reply' 'scal result' "reply scal channel=2 ack=1`r`n" $false
Assert-Reply 'S2 manual status' 'shovel' "reply shovel enabled=0 active=0 output=0`r`n" $true
Assert-Reply 'S2 manual pulse setup' 'shovel pulses 1530 1470' "reply OK shovel pulses_set; START_required`r`n" $true
Assert-Reply 'S2 manual rejected pulse' 'shovel pulses 1700 1470' "reply ERR shovel_pulses_denied`r`n" $false
Assert-Reply 'S3 S4 status' 'joints' "reply joints selected=3 active=0 center_required=0 s3_us=0 s4_us=0`r`n" $true
Write-Host "PASS console reply checks: $script:replyChecks"
