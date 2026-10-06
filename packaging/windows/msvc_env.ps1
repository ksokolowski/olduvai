# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Load the MSVC developer environment into GITHUB_ENV for a CI job's later
# steps.  Both CI systems call it: gitea's self-hosted box (VS BuildTools, one
# side-by-side toolset per matrix row) and GitHub's runner (VS Enterprise).
#   pwsh packaging/windows/msvc_env.ps1 [toolset]     e.g. 14.44; none = newest
#
# vswhere finds Visual Studio by component, never by path: VS 2026 installs
# under ...\Microsoft Visual Studio\18\..., the major version, not the year.
param([string]$Toolset = '')
$ErrorActionPreference = 'Stop'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere not found - is VS installed?" }
$root = & $vswhere -latest -products * `
          -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
          -property installationPath
if (-not $root) { throw "vswhere reports no VC.Tools instance" }
$vcvars = Join-Path $root 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat missing under $root" }
Write-Host "VS at: $root  (toolset: $(if ($Toolset) { $Toolset } else { 'newest' }))"

# Each `run:` step gets a fresh shell, so the environment is exported once.
# -vcvars_ver selects a side-by-side toolset; without it vcvars picks the
# newest.
$ver = if ($Toolset) { "-vcvars_ver=$Toolset" } else { '' }
cmd /c "call `"$vcvars`" $ver >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^(?<k>[^=]+)=(?<v>.*)$') {
        $k = $matches['k']
        # Never overwrite the runner's own bookkeeping, and never export
        # PSModulePath: this runs under pwsh (PowerShell 7), so the cmd child
        # inherits PS7's module path, and exporting it breaks any later
        # `powershell` (5.1) call, which then reports core cmdlets such as
        # Get-FileHash as "not recognized".
        if ($k -notmatch '^(GITHUB_|RUNNER_|ACTIONS_)' -and
            $k -notin @('PSModulePath', 'PSExecutionPolicyPreference')) {
            "$k=$($matches['v'])" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
        }
    }
}
