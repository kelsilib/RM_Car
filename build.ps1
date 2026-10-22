<#
.SYNOPSIS
    Build the RM_Car firmware with the toolchain bundled in STM32CubeIDE.

.EXAMPLE
    .\build.ps1
    .\build.ps1 -Clean -Jobs 4
#>
param(
    [switch]$Clean,
    [int]$Jobs = 8
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$idePlugins = 'D:\stm32\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins'
if (-not (Test-Path $idePlugins)) {
    $idePlugins = 'C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins'
}

function Find-Tool([string]$name, [string]$pattern) {
    $hit = Get-ChildItem -Path $idePlugins -Directory -Filter $pattern -ErrorAction SilentlyContinue |
        ForEach-Object { Get-ChildItem -Path $_.FullName -Recurse -Filter $name -ErrorAction SilentlyContinue } |
        Select-Object -First 1
    return $hit
}

$gcc  = Find-Tool 'arm-none-eabi-gcc.exe' 'com.st.stm32cube.ide.mcu.externaltools.gnu-tools-*'
$make = Find-Tool 'make.exe'              'com.st.stm32cube.ide.mcu.externaltools.make.*'

if (-not $gcc -or -not $make) {
    Write-Error "Toolchain not found. Install STM32CubeIDE or put arm-none-eabi-gcc in PATH."
}

$env:PATH = "$($gcc.Directory.FullName);$($make.Directory.FullName);$env:PATH"

Write-Host "GCC : $($gcc.FullName)"
Write-Host "MAKE: $($make.FullName)"

if ($Clean) {
    if (Test-Path build) { Remove-Item -Recurse -Force build }
}

& $make.FullName -j $Jobs
$code = $LASTEXITCODE

if ($code -eq 0) {
    Write-Host ""
    Write-Host "BUILD OK" -ForegroundColor Green
    Get-ChildItem build -Filter 'rm_car.*' | Select-Object Name, Length | Format-Table -AutoSize
} else {
    Write-Host "BUILD FAILED, exit code $code" -ForegroundColor Red
}

exit $code
