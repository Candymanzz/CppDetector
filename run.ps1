# Starts CppDetector (no build) and opens the UI. Ctrl+C stops it.
#   .\run.ps1                         frames36 next to the project, http://localhost:8080
#   .\run.ps1 -Port 8081 -Frames D:\Pj\CppDetector\frames36 [-NoBrowser]
param(
    [int]$Port = 8080,
    [string]$Frames = '',
    [string]$Config = 'Release',
    [switch]$NoBrowser
)
. (Join-Path $PSScriptRoot 'scripts\common.ps1')

$exe = Get-ExePath $Config
if (-not (Test-Path $exe)) {
    Write-Host "Executable not found: $exe" -ForegroundColor Red
    Write-Host 'Build it first:  .
ebuild-and-run.ps1   (installs missing tools/libraries and builds)' -ForegroundColor Yellow
    exit 1
}

# refuse to start twice on the same port
$busy = $null
try { $busy = Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue } catch { }
if ($busy) {
    $owner = Get-Process -Id ($busy | Select-Object -First 1).OwningProcess -ErrorAction SilentlyContinue
    Write-Host "Port $Port is already in use by $($owner.ProcessName) (pid $($owner.Id)). Stop it or pass -Port." -ForegroundColor Red
    exit 1
}

if (-not $Frames) { $Frames = Join-Path $script:ProjectDir 'frames36' }
if (-not (Test-Path $Frames)) {
    Write-Host "Frames folder not found: $Frames (pass -Frames <dir with camera_0..9>)" -ForegroundColor Red
    exit 1
}

$url = "http://localhost:$Port/"
Write-Host "CppDetector: $url  frames=$Frames" -ForegroundColor Green
Write-Host 'Ctrl+C to stop.' -ForegroundColor DarkGray
if (-not $NoBrowser) {
    # give the server a moment to start listening, then open the UI
    Start-Job -ScriptBlock { param($u) Start-Sleep -Seconds 2; Start-Process $u } -ArgumentList $url | Out-Null
}
Set-Location $script:ProjectDir
& $exe --frames $Frames --port $Port
exit $LASTEXITCODE
