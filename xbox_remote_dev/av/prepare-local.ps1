param(
    [Parameter(Mandatory=$true)][string]$PcIp,
    [string]$SourceDirectory = (Join-Path $env:USERPROFILE 'Downloads\360Stream(1)\USB')
)
$ErrorActionPreference = 'Stop'
$parsed = [System.Net.IPAddress]::Parse($PcIp)
if ($parsed.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) { throw 'IPv4 required' }
if (-not (Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -eq $PcIp })) { throw 'PC IP is not assigned to this computer' }
$source = Join-Path $SourceDirectory 'Xbox360Stream.xex'
$expectedHash = 'f9ca633356554a0d5f8c2fd5a027ca075d5ac6db91dae6fc5b3c4527c59374fb'
$actualHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHash -ne $expectedHash) { throw 'Capture XEX differs from audited binary; review it before preparing files' }
$destination = Join-Path $env:LOCALAPPDATA ('XboxRemoteDev\av-staging\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $destination | Out-Null
Copy-Item -LiteralPath $source -Destination (Join-Path $destination 'Xbox360Stream.xex')
[System.IO.File]::WriteAllText((Join-Path $destination 'PERFORMER-AV.ini'), "PC_IP=$PcIp`r`n", [System.Text.Encoding]::ASCII)
@{ original_source = $source; original_binary_sha256 = $actualHash; pc_ip = $PcIp;
   source_available = $false; installed_on_console = $false; hardware_validated = $false } |
    ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $destination 'provenance.json')
Write-Output "Prepared personal local test files: $destination"
Write-Output 'No upload, module load, launch.ini edit or firewall change was performed.'
