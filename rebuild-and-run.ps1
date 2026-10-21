# Checks/installs everything CppDetector needs, rebuilds it and starts it.
#   .\rebuild-and-run.ps1                   check tools + libraries (skips what is installed), build, run
#   .\rebuild-and-run.ps1 -NoRun            only check + build
#   .\rebuild-and-run.ps1 -Clean            wipe the build folder first (libraries come back from the vcpkg cache)
#   .\rebuild-and-run.ps1 -CheckOnly        only report what is installed / missing
#   .\rebuild-and-run.ps1 -Port 8081 -Frames D:\Pj\CppDetector\frames36      options passed to run.ps1
#
# What is checked (and downloaded only when missing):
#   1. Visual Studio C++ build tools (cl.exe)     -> Build Tools 2022 installer (needs admin, UAC prompt)
#   2. CMake >= 3.25                              -> portable CMake into .tools\
#   3. git (only to fetch vcpkg)                  -> portable MinGit into .tools\
#   4. vcpkg                                      -> cloned next to the project drive (or VCPKG_ROOT), bootstrapped
#   5. libraries opencv4, cpp-httplib, nlohmann-json (vcpkg manifest) -> installed into build\vcpkg_installed
param(
    [switch]$NoRun,
    [switch]$Clean,
    [switch]$CheckOnly,
    [int]$Port = 8080,
    [string]$Frames = '',
    [string]$Config = 'Release'
)
. (Join-Path $PSScriptRoot 'scripts\common.ps1')
$started = Get-Date

try {
    # ---- toolchain ---------------------------------------------------------------------------------
    Stop-Detector
    $vs = Ensure-VisualStudio
    $null = Ensure-CMake
    $vcpkgRoot = Ensure-Vcpkg

    # ---- project libraries ---------------------------------------------------------------------------
    Write-Step 'Project libraries (opencv4, cpp-httplib, nlohmann-json)'
    if ($Clean -and (Test-Path $script:BuildDir) -and -not $CheckOnly) {
        Write-Get "cleaning $($script:BuildDir)"
        Remove-Item -Recurse -Force $script:BuildDir
    }
    $missing = Get-MissingLibs
    $stampFile = Get-StampFile
    $configured = Test-ProjectConfigured $vs.Generator
    $stampExists = Test-Path $stampFile
    # a missing stamp with libraries present means "installed by hand earlier": adopt it, do not reinstall
    $manifestChanged = $stampExists -and ((Get-Content $stampFile -ErrorAction SilentlyContinue) -ne (Get-ManifestStamp))
    if (-not $stampExists -and $missing.Count -eq 0 -and $configured -and -not $CheckOnly) { Set-Content -Path $stampFile -Value (Get-ManifestStamp) }

    foreach ($lib in $script:RequiredLibs) {
        if ($missing -contains $lib.Name) { Write-Warn2 "$($lib.Name): not installed" } else { Write-Ok "$($lib.Name): installed" }
    }
    $needConfigure = ($missing.Count -gt 0) -or $manifestChanged -or (-not $configured)
    if ($manifestChanged -and $missing.Count -eq 0) { Write-Warn2 'vcpkg.json changed since the last install' }

    if ($CheckOnly) {
        Write-Host ''
        if ($needConfigure) { Write-Host 'Result: something is missing - run .\rebuild-and-run.ps1 to install it.' -ForegroundColor Yellow }
        else { Write-Host 'Result: everything is installed.' -ForegroundColor Green }
        exit 0
    }

    if ($needConfigure) {
        Write-Get 'installing libraries through vcpkg and configuring (first run builds OpenCV: 10-40 minutes, later runs use the binary cache)'
        $cmakeArgs = @(
            '-S', $script:ProjectDir, '-B', $script:BuildDir,
            '-G', $vs.Generator, '-A', 'x64',
            "-DCMAKE_TOOLCHAIN_FILE=$vcpkgRoot\scripts\buildsystems\vcpkg.cmake",
            "-DVCPKG_TARGET_TRIPLET=$($script:Triplet)"
        )
        Invoke-Native 'cmake' $cmakeArgs 'cmake configure'
        Set-Content -Path $stampFile -Value (Get-ManifestStamp)
        $stillMissing = Get-MissingLibs
        if ($stillMissing.Count -gt 0) { throw "libraries still missing after install: $($stillMissing -join ', ')" }
        Write-Ok 'libraries installed and project configured'
    } else {
        Write-Skip 'all libraries are installed and the project is configured'
    }

    # ---- build ------------------------------------------------------------------------------------------
    Write-Step "Build ($Config)"
    Invoke-Native 'cmake' @('--build', $script:BuildDir, '--config', $Config, '--parallel') 'cmake build'
    $exe = Get-ExePath $Config
    if (-not (Test-Path $exe)) { throw "build finished but $exe was not produced" }
    $elapsed = [int]((Get-Date) - $started).TotalSeconds
    Write-Ok "built $exe ($elapsed s)"
}
catch {
    Write-Host ''
    Write-Host "FAILED: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

if ($NoRun) { exit 0 }

Write-Step 'Run'
& (Join-Path $PSScriptRoot 'run.ps1') -Port $Port -Frames $Frames -Config $Config
exit $LASTEXITCODE
