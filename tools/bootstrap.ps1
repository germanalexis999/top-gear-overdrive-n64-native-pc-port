[CmdletBinding()]
param([string]$DependencyRoot = '')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $DependencyRoot) { $DependencyRoot = Join-Path $projectRoot 'tools' }
$dependencyDirectory = [IO.Path]::GetFullPath($DependencyRoot)
$manifest = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'dependencies.lock.json') | ConvertFrom-Json

function Invoke-GitChecked([string[]]$Arguments) {
    & git @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Git failed: $($Arguments -join ' ')" }
}

function Apply-PortPatch($Entry) {
    $checkout = Join-Path $dependencyDirectory $Entry.path
    $revision = (& git -C $checkout rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $revision -ne $Entry.commit) {
        throw "Unexpected dependency revision in $checkout. Expected $($Entry.commit); existing files were preserved."
    }
    if (-not $Entry.patch) { Write-Host "Pinned dependency: $($Entry.path)"; return }
    $patchFile = Join-Path $projectRoot ('patches/' + $Entry.patch)
    & git -C $checkout apply --reverse --check $patchFile 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Already patched: $($Entry.path)"
        return
    }
    Invoke-GitChecked @('-C', $checkout, 'apply', '--check', $patchFile)
    Invoke-GitChecked @('-C', $checkout, 'apply', $patchFile)
    Write-Host "Patched: $($Entry.path)"
}

foreach ($entry in $manifest.repositories) {
    $checkout = Join-Path $dependencyDirectory $entry.path
    if (-not (Test-Path -LiteralPath (Join-Path $checkout '.git'))) {
        if (Test-Path -LiteralPath $checkout) {
            throw "Dependency directory exists without Git metadata: $checkout"
        }
        if ($entry.PSObject.Properties['tag']) {
            Invoke-GitChecked @('clone', '--depth', '1', '--branch', $entry.tag, $entry.url, $checkout)
        } else {
            Invoke-GitChecked @('clone', '--no-checkout', $entry.url, $checkout)
        }
        Invoke-GitChecked @('-C', $checkout, 'checkout', '--detach', $entry.commit)
        Invoke-GitChecked @('-C', $checkout, 'submodule', 'update', '--init', '--recursive')
    }
    Apply-PortPatch $entry
}
foreach ($entry in $manifest.nestedPatches) { Apply-PortPatch $entry }
Write-Host 'Dependencies are pinned and patched. Their source trees remain local and ignored by Git.'
