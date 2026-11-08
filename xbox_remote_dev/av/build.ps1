param([switch]$Test)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'build\av'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$csc = 'C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe'
$common = @((Join-Path $PSScriptRoot 'Hy37.cs'), (Join-Path $PSScriptRoot 'Receiver.cs'), (Join-Path $PSScriptRoot 'AudioOutput.cs'))
$refs = @('/r:System.Drawing.dll', '/r:System.Windows.Forms.dll', '/r:System.Web.Extensions.dll')
$exe = Join-Path $out 'RemoteAV.exe'
& $csc /nologo /warn:4 /optimize+ /target:winexe /platform:x86 @refs "/out:$exe" @common (Join-Path $PSScriptRoot 'Program.cs')
if ($LASTEXITCODE -ne 0) { throw 'RemoteAV build failed' }
if ($Test) {
    $tests = Join-Path $out 'RemoteAV.Tests.exe'
    & $csc /nologo /warn:4 /optimize+ /target:exe /platform:x86 @refs "/out:$tests" @common (Join-Path $PSScriptRoot 'Tests.cs')
    if ($LASTEXITCODE -ne 0) { throw 'RemoteAV tests build failed' }
    & $tests
    if ($LASTEXITCODE -ne 0) { throw 'RemoteAV tests failed' }
}
$commit = (git -C $PSScriptRoot rev-parse HEAD).Trim()
$sources = @{}
$dirty = [bool](git -C $PSScriptRoot status --porcelain -- .)
Get-ChildItem -LiteralPath $PSScriptRoot -File | Where-Object { $_.Extension -in '.cs','.ps1','.csproj' } | ForEach-Object {
    $sources[$_.Name] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $tracked = git -C $PSScriptRoot ls-files -- $_.Name
    if (-not $tracked) { $dirty = $true }
}
@{ source_commit = $commit; source_hashes = $sources; hardware_tested = $false;
   host_tests_run = $Test.IsPresent; validation_scope = 'host simulation only';
   source_tree_dirty = $dirty;
   file = 'RemoteAV.exe'; bytes = (Get-Item -LiteralPath $exe).Length;
   sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant();
   third_party_capture_xex_included = $false } |
    ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $out 'manifest.json')
Get-Content -LiteralPath (Join-Path $out 'manifest.json')
