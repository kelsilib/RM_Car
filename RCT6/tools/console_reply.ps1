# 只检查已经收到的文字，不打开串口。board.ps1和PC自测共同使用这份判断。
function Get-BoardCommandResult {
    param([string]$Command, [string]$Text)

    # 与固件一致：只接受ASCII，一行最多63字节，单词之间可以有多个空格。
    if (-not $Command -or $Command.Length -gt 63 -or $Command -match '[^\x20-\x7E]') {
        return [pscustomobject]@{ Passed=$false; Message='指令格式不合法。' }
    }
    $normalized = ($Command.Trim(' ') -split ' +' ) -join ' '
    $expected = switch -CaseSensitive -Regex ($normalized) {
        '^help$' { '^help version '; break }
        '^version$' { '^version=[A-Za-z0-9_-]+$'; break }
        '^status$' { '^state=[0-9]+ inhibit=0x[0-9A-Fa-f]+ '; break }
        '^ps2$' { '^ps2 valid='; break }
        '^counts$' { '^counts seen='; break }
        '^config$' { '^config issues='; break }
        '^output$' { '^output mode='; break }
        '^battery$' { '^battery seen='; break }
        '^driver reply$' { '^driver_reply seen='; break }
        '^driver (read|voltage)$' { '^OK query_queued; inspect_feedback_not_ACK$'; break }
        '^(stop|cal cancel)$' { '^OK stopped; fresh_START_required$'; break }
        '^cal arm$' { '^OK cal_ready window_ms=5000$'; break }
        '^cal pulse 0*[1-4] -?[0-9]+ [0-9]+$' { '^OK one_pulse_queued$'; break }
        default { $null }
    }
    # 必须从行首到换行都收齐。周期state日志、开机version和半截回复不能算成功。
    $responses = [regex]::Matches($Text, '(?m)^reply ([^\r\n]*)\r?\n')
    if ($responses.Count -ne 1) {
        return [pscustomobject]@{ Passed=$false; Message='需要恰好一条完整的reply回复；自动日志不算回复，请核对固件版本和串口。' }
    }
    $payload = $responses[0].Groups[1].Value
    if ($payload -cmatch '^ERR ') {
        return [pscustomobject]@{ Passed=$false; Message=('主控拒绝了指令：' + $payload) }
    }
    if (-not $expected -or $payload -cnotmatch $expected) {
        return [pscustomobject]@{ Passed=$false; Message='收到的reply与本次指令不对应，不能判定成功。' }
    }
    return [pscustomobject]@{ Passed=$true; Message='主控已回复；这不等于驱动已执行，更不等于实物验收通过。' }
}
