$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'build\windows'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$csc = 'C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe'
$exe = Join-Path $out 'RemoteDev.exe'
& $csc /nologo /target:winexe /r:System.Windows.Forms.dll /r:System.Drawing.dll "/out:$exe" (Join-Path $PSScriptRoot 'Program.cs') (Join-Path $PSScriptRoot 'XbdmClient.cs')
if ($LASTEXITCODE -ne 0) { throw 'C# build failed' }
$hash = Get-FileHash (Join-Path $out 'RemoteDev.exe') -Algorithm SHA256
$commit = git rev-parse HEAD
@{ source_commit = $commit.Trim(); hardware_tested = $false; file = 'RemoteDev.exe'; sha256 = $hash.Hash.ToLowerInvariant() } |
    ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $out 'manifest.json')
Get-Content (Join-Path $out 'manifest.json')
