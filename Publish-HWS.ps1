[CmdletBinding()]
param(
    [string]$InstallDirectory = (Split-Path $PSScriptRoot -Parent),
    [string]$WxWidgetsRoot = (Join-Path $PSScriptRoot '..\BuildDependencies\wxWidgets'),
    [string]$FbxSdkRoot = (Join-Path $PSScriptRoot '..\BuildDependencies\FBX'),
    [string]$RuntimeDirectory = (Join-Path $PSScriptRoot '..\BuildDependencies\wxWidgets\Runtime'),
    [switch]$StageOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$development = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'Build'))
$destination = [IO.Path]::GetFullPath($InstallDirectory).TrimEnd('\')
if ($destination -eq [IO.Path]::GetPathRoot($destination).TrimEnd('\') -or
    $destination.StartsWith($PSScriptRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
    ($PSScriptRoot.StartsWith($destination + '\', [StringComparison]::OrdinalIgnoreCase) -and
     $destination -ne [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent)).TrimEnd('\'))) {
    throw 'Choose a runtime directory separate from the development source and build directories.'
}
$stage = Join-Path $development 'Stage'
$rollback = Join-Path $development 'Rollback'
$output = Join-Path $development 'Release'
$objects = Join-Path $development 'Obj'
New-Item -ItemType Directory -Path $development -Force | Out-Null
# Prevent two publishers from sharing compiler output or transaction files.
$buildLock = [IO.File]::Open((Join-Path $development 'publish.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
$installLock = $null
function Assert-NotRunning {
    foreach ($process in Get-Process) {
        $path = $null
        try { $path = $process.Path } catch { }
        if ($path -and [IO.Path]::GetDirectoryName($path) -eq $destination) {
            throw "Close the application before publishing: $path (PID $($process.Id))."
        }
    }
}
function Reset-TransactionDirectory([string]$path) {
    $full = [IO.Path]::GetFullPath($path)
    if ($full -ne $stage -and $full -ne $rollback) { throw 'Unexpected cleanup path.' }
    if (Test-Path -LiteralPath $full) {
        if ((Get-Item -LiteralPath $full).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing a linked transaction directory.' }
        Remove-Item -LiteralPath $full -Recurse -Force
    }
    New-Item -ItemType Directory -Path $full | Out-Null
}
try {
    if (-not $StageOnly) { Assert-NotRunning }
    # Retain rollback files after an interrupted transaction for explicit recovery.
    if (Test-Path -LiteralPath (Join-Path $rollback 'pending.json')) {
        throw "An interrupted publish needs recovery. Inspect $rollback before publishing again."
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'Visual Studio C++ build tools were not found.' }
    $msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
    $toolset = Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
    $dumpbin = Join-Path $toolset.FullName 'bin\Hostx64\x64\dumpbin.exe'
    New-Item -ItemType Directory -Path $output,$objects -Force | Out-Null
    & $msbuild (Join-Path $PSScriptRoot 'Hades-Workshop.vcxproj') /t:Build /m /nologo /v:minimal `
        /p:Configuration=Release /p:Platform=x64 "/p:WxWidgetsRoot=$WxWidgetsRoot" "/p:FbxSdkRoot=$FbxSdkRoot" `
        "/p:OutDir=$output\" "/p:IntDir=$objects\" /p:TargetName=HadesWorkshop `
        /fl "/flp:logfile=$development\publish-build.log;verbosity=normal"
    if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE). The installation was not changed." }
    $exe = Join-Path $output 'HadesWorkshop.exe'
    if (-not (Test-Path -LiteralPath $exe)) { throw 'Successful build did not produce HadesWorkshop.exe.' }
    Reset-TransactionDirectory $stage
    Copy-Item -LiteralPath $exe -Destination $stage
    # Follow the PE import graph, including indirect wxWidgets dependencies.
    $queue = [Collections.Generic.Queue[string]]::new()
    $queue.Enqueue($exe)
    $queue.Enqueue((Join-Path $RuntimeDirectory 'libfbxsdk.dll'))
    $seen = @{}
    while ($queue.Count) {
        $file = $queue.Dequeue()
        if ($seen.ContainsKey($file)) { continue }
        $seen[$file] = $true
        $headers = & $dumpbin /headers $file
        if ($LASTEXITCODE -ne 0 -or -not ($headers -match '8664 machine')) { throw "Not a valid x64 binary: $file" }
        if ($file.EndsWith('.dll')) { Copy-Item -LiteralPath $file -Destination $stage }
        $imports = & $dumpbin /dependents $file
        if ($LASTEXITCODE -ne 0) { throw "Cannot inspect imports: $file" }
        foreach ($line in $imports) {
            if ($line -match '^\s+([\w.-]+\.dll)\s*$') {
                $name = $Matches[1]
                $dependency = Join-Path $RuntimeDirectory $name
                if (Test-Path -LiteralPath $dependency) { $queue.Enqueue($dependency) }
                elseif ($name -notlike 'api-ms-*' -and -not (Test-Path -LiteralPath (Join-Path $env:windir "System32\$name"))) {
                    throw "Missing runtime dependency: $name"
                }
            }
        }
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'share\ScriptAPI.txt') -Destination $stage
    if ($StageOnly) { Write-Host "Build verified and staged at $stage"; return }
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    $installLock = [IO.File]::Open((Join-Path $destination '.publish.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    Assert-NotRunning
    Reset-TransactionDirectory $rollback
    $changes = @()
    foreach ($file in Get-ChildItem -LiteralPath $stage -File | Sort-Object @{Expression={$_.Extension -eq '.exe'}},Name) {
        $target = Join-Path $destination $file.Name
        # User-configurable opcode definitions, like configuration and workspace data, survive updates.
        if ($file.Name -eq 'ScriptAPI.txt' -and (Test-Path -LiteralPath $target)) { continue }
        $existed = Test-Path -LiteralPath $target
        if ($existed -and (Get-FileHash -LiteralPath $target).Hash -eq (Get-FileHash -LiteralPath $file.FullName).Hash) { continue }
        if ($existed) {
            $probe = [IO.File]::Open($target, 'Open', 'ReadWrite', 'None')
            $probe.Dispose()
            Copy-Item -LiteralPath $target -Destination $rollback
        }
        $changes += [pscustomobject]@{ Name=$file.Name; Existed=$existed }
    }
    $changes | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $rollback 'pending.json')
    $applied = [Collections.Generic.List[object]]::new()
    try {
        foreach ($change in $changes) {
            Assert-NotRunning
            $target = Join-Path $destination $change.Name
            $incoming = Join-Path $destination ($change.Name + '.incoming')
            Copy-Item -LiteralPath (Join-Path $stage $change.Name) -Destination $incoming
            if ((Get-FileHash -LiteralPath $incoming).Hash -ne (Get-FileHash -LiteralPath (Join-Path $stage $change.Name)).Hash) { throw 'Staged copy failed verification.' }
            # PowerShell converts $null to an empty string for this string parameter.
            # File.Replace requires a real null when no additional backup is requested.
            if ($change.Existed) { [IO.File]::Replace($incoming, $target, [NullString]::Value) }
            else { [IO.File]::Move($incoming, $target) }
            $applied.Add($change)
        }
    } catch {
        $failure = $_
        for ($i = $applied.Count - 1; $i -ge 0; $i--) {
            $change = $applied[$i]
            $target = Join-Path $destination $change.Name
            if ($change.Existed) { Copy-Item -LiteralPath (Join-Path $rollback $change.Name) -Destination $target -Force }
            else { Remove-Item -LiteralPath $target }
        }
        throw $failure
    }
    Reset-TransactionDirectory $rollback
    Reset-TransactionDirectory $stage
    Write-Host "Published $destination\HadesWorkshop.exe. Configuration and workspaces preserved."
} finally {
    if ($installLock) {
        $installLock.Dispose()
        Remove-Item -LiteralPath (Join-Path $destination '.publish.lock') -ErrorAction SilentlyContinue
    }
    $buildLock.Dispose()
}
