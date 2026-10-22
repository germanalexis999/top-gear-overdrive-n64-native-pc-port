<#
.SYNOPSIS
Build, check and package the Windows port; optionally publish a GitHub prerelease.
.EXAMPLE
.\tools\release_windows.ps1 -Version v0.1.0-preview.3 -RomPath 'D:\ROMs\mi-copia.z64'
.EXAMPLE
.\tools\release_windows.ps1 -Version v0.1.0-preview.3 -RomPath 'D:\ROMs\mi-copia.z64' -Resume -Publish
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^v\d+\.\d+\.\d+(?:-[a-zA-Z0-9.]+)?$')]
    [string]$Version,
    [Parameter(Mandatory = $true)]
    [string]$RomPath,
    [string]$PythonExecutable = 'python',
    [string]$DependencyRoot = '',
    [ValidateRange(1, 64)]
    [int]$Jobs = 4,
    [switch]$Resume,
    [switch]$Publish
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $projectRoot 'build'
$outputDirectory = [IO.Path]::GetFullPath((Join-Path $projectRoot '../release-packages'))
$archive = Join-Path $outputDirectory "top-gear-overdrive-$Version-windows-x64.zip"
$romSource = (Resolve-Path -LiteralPath $RomPath).Path
if (-not $DependencyRoot) { $DependencyRoot = Join-Path $projectRoot 'tools' }
$dependencyDirectory = [IO.Path]::GetFullPath($DependencyRoot)

function Invoke-Checked([string]$Program, [string[]]$CommandArguments) {
    & $Program @CommandArguments
    if ($LASTEXITCODE -ne 0) { throw "Failed (exit $LASTEXITCODE): $Program" }
}

Push-Location -LiteralPath $projectRoot
try {
    if ($env:OS -ne 'Windows_NT') { throw 'This release workflow requires Windows x64 and MinGW-w64.' }
    Get-Command git -ErrorAction Stop | Out-Null
    $pythonCommand = Get-Command $PythonExecutable -ErrorAction Stop
    $python = $pythonCommand.Source
    Invoke-Checked $python @('-c', 'import sys; assert sys.version_info >= (3,11), "Python 3.11+ required"')
    $changes = & git status --porcelain
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read the source Git checkout.' }
    if ($changes) { throw 'Commit source changes first. The workflow never stages or commits files automatically.' }
    Invoke-Checked $python @('tools/check_public_tree.py')

    if ($Resume -and -not (Test-Path -LiteralPath $archive)) { throw "No package to resume: $archive" }
    if (-not $Resume -and (Test-Path -LiteralPath $archive)) {
        throw "Package already exists. Use -Resume to verify/publish it, or choose a new version: $archive"
    }
    if ($Publish) {
        $remote = (& git remote get-url origin)
        if ($LASTEXITCODE -ne 0 -or ($remote -replace '\.git$', '') -ne
            'https://github.com/germanalexis999/top-gear-overdrive-n64-native-pc-port') {
            throw 'Unexpected publication remote; nothing was pushed.'
        }
        $branch = & git rev-parse --abbrev-ref HEAD
        if ($LASTEXITCODE -ne 0 -or $branch -eq 'HEAD') { throw 'Check out a branch before using -Publish.' }
    }

    # Keep the normalized working copy in ignored build/, preserving the input.
    $normalizedRom = Join-Path $buildDirectory 'release-input/Top Gear Overdrive (USA).z64'
    if ([string]::Equals($romSource, $normalizedRom, [StringComparison]::OrdinalIgnoreCase)) {
        $normalizedRom = $romSource
    } else {
        Write-Host '[1/7] Normalize and verify the local ROM (never included in the ZIP).'
        & (Join-Path $PSScriptRoot 'normalize_n64_rom.ps1') -InputPath $romSource -OutputPath $normalizedRom -Force
    }
    if ((Get-FileHash -LiteralPath $normalizedRom -Algorithm SHA1).Hash.ToLowerInvariant() -ne
        '75ebbe451906ce68b0dd43eef9ee44416452a4f1') { throw 'Unsupported ROM revision: expected USA v0.' }

    if (-not $Resume) {
        foreach ($program in @('cmake', 'ninja', 'g++')) { Get-Command $program -ErrorAction Stop | Out-Null }
        $compilerVersion = & g++ -dumpfullversion
        if ($LASTEXITCODE -ne 0 -or $compilerVersion -ne '16.2.0') {
            throw 'Packaging currently records GCC 16.2.0. Use that toolchain or update and validate package provenance first.'
        }
        Write-Host '[2/7] Prepare pinned dependencies and the recompiler.'
        & (Join-Path $PSScriptRoot 'bootstrap.ps1') -DependencyRoot $dependencyDirectory
        $recompilerDirectory = Join-Path $dependencyDirectory 'N64Recomp'
        $recompilerBuild = Join-Path $recompilerDirectory 'build'
        Invoke-Checked cmake @('-S', $recompilerDirectory, '-B', $recompilerBuild, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release')
        Invoke-Checked cmake @('--build', $recompilerBuild, '--target', 'N64Recomp', '--parallel', "$Jobs")
        Write-Host '[3/7] Regenerate local game C and build the native executable.'
        Invoke-Checked $python @('tools/prepare_recomp.py', '--rom', $normalizedRom,
            '--recompiler', (Join-Path $recompilerBuild 'N64Recomp.exe'))
        Invoke-Checked cmake @('-S', $projectRoot, '-B', $buildDirectory, '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', '-DTGOD_BUILD_GAME=ON',
            "-DTGOD_RUNTIME_DIR=$(Join-Path $dependencyDirectory 'N64ModernRuntime')",
            "-DTGOD_RECOMPILER_DIR=$recompilerDirectory",
            "-DTGOD_SDL_DIR=$(Join-Path $dependencyDirectory 'SDL')")
        Invoke-Checked cmake @('--build', $buildDirectory, '--target', 'tgod_native', 'tgod_regression_checks', 'tgod_gpu_checks', '--parallel', "$Jobs")
        Write-Host '[4/7] Run ROM-independent checks and create the allowlisted package.'
        Invoke-Checked (Join-Path $buildDirectory 'tgod_regression_checks.exe') @()
        & (Join-Path $buildDirectory 'tgod_gpu_checks.exe')
        if ($LASTEXITCODE -eq 77) { Write-Host 'GPU checks skipped: no compatible hardware.' }
        elseif ($LASTEXITCODE -ne 0) { throw 'GPU regression checks failed.' }
        Invoke-Checked $python @('tools/package_windows.py', '--version', $Version, '--output-dir', $outputDirectory)
    }

    Write-Host '[5/7] Audit the ZIP and check its source revision/checksum.'
    Invoke-Checked $python @('tools/package_windows.py', '--audit', $archive)
    Invoke-Checked $python @('tools/publish_release.py', $archive)
    Write-Host '[6/7] Test the extracted ZIP with Windows-only PATH, race, audio and save.'
    Invoke-Checked $python @('tools/test_windows_package.py', $archive, '--rom', $normalizedRom)
    if ($Publish) {
        Write-Host '[7/7] Push this source branch, upload verified assets and publish a prerelease.'
        Invoke-Checked git @('push', 'origin', 'HEAD')
        Invoke-Checked $python @('tools/publish_release.py', $archive, '--publish')
    } else {
        Write-Host '[7/7] Local package ready. Use the same command with -Resume -Publish to upload.'
    }
    Write-Host "ZIP: $archive"
    Write-Host "SHA256: $((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant())"
} finally {
    Pop-Location
}
