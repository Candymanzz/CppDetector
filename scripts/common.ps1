# Shared helpers for rebuild-and-run.ps1 / run.ps1 (CppDetector, Windows PowerShell 5.1+).
# Dot-source this file; it only defines functions and settings.

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'      # Invoke-WebRequest is much faster without the progress bar
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$script:ProjectDir = Split-Path -Parent $PSScriptRoot
$script:BuildDir   = Join-Path $script:ProjectDir 'build'
$script:ToolsDir   = Join-Path $script:ProjectDir '.tools'      # portable tools downloaded by the scripts (git-ignored)
$script:Triplet    = 'x64-windows-static-md'
$script:ExeName    = 'cppdetector.exe'

# Pinned versions of the tools that are downloaded when missing.
$script:MinCMake      = [version]'3.25.0'
$script:CMakeVersion  = '3.31.6'
$script:CMakeUrl      = "https://github.com/Kitware/CMake/releases/download/v$($script:CMakeVersion)/cmake-$($script:CMakeVersion)-windows-x86_64.zip"
$script:MinGitUrl     = 'https://github.com/git-for-windows/git/releases/download/v2.47.1.windows.2/MinGit-2.47.1.2-64-bit.zip'
$script:VsBuildToolsUrl = 'https://aka.ms/vs/17/release/vs_buildtools.exe'
$script:VcpkgRepoUrl  = 'https://github.com/microsoft/vcpkg.git'

# Libraries the project needs (vcpkg ports from vcpkg.json) and a header/lib that proves each one is installed.
$script:RequiredLibs = @(
    @{ Name = 'opencv4';        Probe = 'include\opencv4\opencv2\core.hpp';  Alt = 'include\opencv2\core.hpp' },
    @{ Name = 'cpp-httplib';    Probe = 'include\httplib.h' },
    @{ Name = 'nlohmann-json';  Probe = 'include\nlohmann\json.hpp' }
)

function Write-Step([string]$text) { Write-Host ''; Write-Host "==> $text" -ForegroundColor Cyan }
function Write-Ok([string]$text)   { Write-Host "    [ok]   $text" -ForegroundColor Green }
function Write-Skip([string]$text) { Write-Host "    [skip] $text" -ForegroundColor DarkGray }
function Write-Get([string]$text)  { Write-Host "    [get]  $text" -ForegroundColor Yellow }
function Write-Warn2([string]$text){ Write-Host "    [warn] $text" -ForegroundColor Yellow }

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    return (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# Runs a native program, echoes nothing extra, throws on a non-zero exit code.
function Invoke-Native([string]$exe, [string[]]$arguments, [string]$what) {
    & $exe @arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "$what failed (exit code $LASTEXITCODE)" }
}

function Download-File([string]$url, [string]$target) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Write-Get "downloading $url"
    Invoke-WebRequest -Uri $url -OutFile $target -UseBasicParsing
}

function Add-ToPath([string]$dir) {
    if (-not (($env:Path -split ';') -contains $dir)) { $env:Path = "$dir;$env:Path" }
}

# ---------------------------------------------------------------- Visual Studio (C++ compiler)

function Get-VsInstallation {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $json = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json 2>$null
    if (-not $json) { return $null }
    $items = $json | ConvertFrom-Json
    if (-not $items) { return $null }
    $vs = @($items)[0]
    $major = [int](($vs.installationVersion -split '\.')[0])
    # CMake generator names of the Visual Studio versions we know about
    $generators = @{ 16 = 'Visual Studio 16 2019'; 17 = 'Visual Studio 17 2022'; 18 = 'Visual Studio 18 2026' }
    if (-not $generators.ContainsKey($major)) { return $null }
    return [pscustomobject]@{ Path = $vs.installationPath; Version = $vs.installationVersion; Generator = $generators[$major]; Name = $vs.displayName }
}

function Ensure-VisualStudio {
    Write-Step 'Visual Studio C++ build tools'
    $vs = Get-VsInstallation
    if ($vs) {
        Write-Skip "$($vs.Name) $($vs.Version) with the C++ toolset is installed"
        return $vs
    }
    Write-Get 'Visual Studio Build Tools 2022 (C++ workload) not found - installing (about 3-6 GB, several minutes)'
    $installer = Join-Path $script:ToolsDir 'vs_buildtools.exe'
    Download-File $script:VsBuildToolsUrl $installer
    $vsArgs = @('--quiet', '--wait', '--norestart', '--nocache',
                '--add', 'Microsoft.VisualStudio.Workload.VCTools', '--includeRecommended')
    if (Test-IsAdmin) {
        $p = Start-Process -FilePath $installer -ArgumentList $vsArgs -Wait -PassThru
    } else {
        Write-Warn2 'administrator rights are required: accept the UAC prompt'
        $p = Start-Process -FilePath $installer -ArgumentList $vsArgs -Wait -PassThru -Verb RunAs
    }
    # 3010 = success, reboot recommended
    if ($p.ExitCode -ne 0 -and $p.ExitCode -ne 3010) { throw "Visual Studio Build Tools installer failed (exit code $($p.ExitCode))" }
    $vs = Get-VsInstallation
    if (-not $vs) { throw 'Visual Studio Build Tools were installed but the C++ toolset was not detected; reboot and run again' }
    Write-Ok "installed $($vs.Name) $($vs.Version)"
    return $vs
}

# ---------------------------------------------------------------- CMake

function Get-CMakeVersion([string]$cmakeExe) {
    try {
        $line = (& $cmakeExe --version 2>$null | Select-Object -First 1)
        if ($line -match '(\d+\.\d+\.\d+)') { return [version]$Matches[1] }
    } catch { }
    return $null
}

function Ensure-CMake {
    Write-Step 'CMake'
    $candidates = @()
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { $candidates += $cmd.Source }
    $candidates += (Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe')
    $candidates += (Join-Path $script:ToolsDir "cmake-$($script:CMakeVersion)-windows-x86_64\bin\cmake.exe")
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) {
            $v = Get-CMakeVersion $c
            if ($v -and $v -ge $script:MinCMake) {
                Add-ToPath (Split-Path -Parent $c)
                Write-Skip "cmake $v ($c)"
                return $c
            }
        }
    }
    Write-Get "cmake >= $($script:MinCMake) not found - downloading portable cmake $($script:CMakeVersion)"
    $zip = Join-Path $script:ToolsDir "cmake-$($script:CMakeVersion).zip"
    Download-File $script:CMakeUrl $zip
    Expand-Archive -Path $zip -DestinationPath $script:ToolsDir -Force
    Remove-Item $zip -Force
    $exe = Join-Path $script:ToolsDir "cmake-$($script:CMakeVersion)-windows-x86_64\bin\cmake.exe"
    if (-not (Test-Path $exe)) { throw 'portable cmake was not extracted as expected' }
    Add-ToPath (Split-Path -Parent $exe)
    Write-Ok "cmake $(Get-CMakeVersion $exe) (portable, $($script:ToolsDir))"
    return $exe
}

# ---------------------------------------------------------------- git (needed only to fetch vcpkg)

function Ensure-Git {
    Write-Step 'git'
    $cmd = Get-Command git -ErrorAction SilentlyContinue
    if ($cmd) { Write-Skip "git ($($cmd.Source))"; return }
    $portable = Join-Path $script:ToolsDir 'mingit\cmd\git.exe'
    if (Test-Path $portable) {
        Add-ToPath (Split-Path -Parent $portable)
        Write-Skip 'portable MinGit'
        return
    }
    Write-Get 'git not found - downloading portable MinGit'
    $zip = Join-Path $script:ToolsDir 'mingit.zip'
    Download-File $script:MinGitUrl $zip
    Expand-Archive -Path $zip -DestinationPath (Join-Path $script:ToolsDir 'mingit') -Force
    Remove-Item $zip -Force
    if (-not (Test-Path $portable)) { throw 'MinGit was not extracted as expected' }
    Add-ToPath (Split-Path -Parent $portable)
    Write-Ok 'portable MinGit ready'
}

# ---------------------------------------------------------------- vcpkg

function Test-VcpkgRoot([string]$dir) {
    return $dir -and (Test-Path (Join-Path $dir 'scripts\buildsystems\vcpkg.cmake'))
}

function Find-VcpkgRoot {
    if (Test-VcpkgRoot $env:VCPKG_ROOT) { return $env:VCPKG_ROOT }
    $candidates = @('D:\vcpkg', 'C:\vcpkg', (Join-Path $env:USERPROFILE 'vcpkg'))
    $drive = Split-Path -Qualifier $script:ProjectDir
    if ($drive) { $candidates = @("$drive\vcpkg") + $candidates }
    foreach ($c in $candidates) { if (Test-VcpkgRoot $c) { return $c } }
    return $null
}

function Ensure-Vcpkg {
    Write-Step 'vcpkg (package manager for OpenCV, cpp-httplib, nlohmann-json)'
    $root = Find-VcpkgRoot
    if (-not $root) {
        Ensure-Git
        # install next to the project, on the same drive (OpenCV needs a few GB of build space)
        $drive = Split-Path -Qualifier $script:ProjectDir
        $root = if ($drive) { "$drive\vcpkg" } else { Join-Path $env:USERPROFILE 'vcpkg' }
        Write-Get "vcpkg not found - cloning into $root"
        Invoke-Native 'git' @('clone', '--depth', '1', $script:VcpkgRepoUrl, $root) 'git clone vcpkg'
    } else {
        Write-Skip "vcpkg found at $root"
    }
    $exe = Join-Path $root 'vcpkg.exe'
    if (-not (Test-Path $exe)) {
        Write-Get 'bootstrapping vcpkg'
        Invoke-Native (Join-Path $root 'bootstrap-vcpkg.bat') @('-disableMetrics') 'bootstrap-vcpkg'
    } else {
        Write-Skip 'vcpkg.exe is built'
    }
    $env:VCPKG_ROOT = $root
    $env:VCPKG_DISABLE_METRICS = '1'
    return $root
}

# ---------------------------------------------------------------- project libraries

function Get-InstalledDir { return Join-Path $script:BuildDir "vcpkg_installed\$($script:Triplet)" }

# Returns the names of the required libraries that are NOT installed for this project yet.
function Get-MissingLibs {
    $dir = Get-InstalledDir
    $missing = @()
    foreach ($lib in $script:RequiredLibs) {
        $ok = Test-Path (Join-Path $dir $lib.Probe)
        if (-not $ok -and $lib.Alt) { $ok = Test-Path (Join-Path $dir $lib.Alt) }
        if (-not $ok) { $missing += $lib.Name }
    }
    return $missing
}

# vcpkg.json changes (new/removed library) must trigger a re-install even if the old libraries are present.
function Get-ManifestStamp {
    # plain .NET instead of Get-FileHash: works even when the PowerShell module path is broken
    $bytes = [IO.File]::ReadAllBytes((Join-Path $script:ProjectDir "vcpkg.json"))
    $sha = [Security.Cryptography.SHA256]::Create()
    return (($sha.ComputeHash($bytes) | ForEach-Object { $_.ToString("x2") }) -join "")
}
function Get-StampFile { return Join-Path $script:BuildDir '.vcpkg-manifest.sha256' }

function Test-ProjectConfigured([string]$generator) {
    $cache = Join-Path $script:BuildDir 'CMakeCache.txt'
    if (-not (Test-Path $cache)) { return $false }
    $line = Select-String -Path $cache -Pattern '^CMAKE_GENERATOR:INTERNAL=(.*)$' | Select-Object -First 1
    return $line -and ($line.Matches[0].Groups[1].Value -eq $generator)
}

function Stop-Detector {
    # only the executable of THIS build is stopped (copies started from elsewhere are left alone)
    $target = Get-ExePath
    $procs = @(Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($script:ExeName)) -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and (($_.Path) -eq $target -or ($_.Path) -eq (Get-ExePath "Debug")) })
    foreach ($p in $procs) {
        Write-Warn2 "stopping running $($script:ExeName) (pid $($p.Id)) - the exe must be free to rebuild"
        Stop-Process -Id $p.Id -Force
    }
    if ($procs.Count -gt 0) { Start-Sleep -Milliseconds 600 }
}

function Get-ExePath([string]$config = 'Release') { return Join-Path $script:BuildDir "$config\$($script:ExeName)" }
