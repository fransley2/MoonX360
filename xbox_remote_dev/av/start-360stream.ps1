param(
    [switch]$Setup,
    [string]$ViewerPath = (Join-Path (Split-Path -Parent $PSScriptRoot) '360Stream\PC\PC-PERFORMER-AV-v38.py')
)
$ErrorActionPreference = 'Stop'
$viewer = (Resolve-Path -LiteralPath $ViewerPath).Path
$supportedHashes = @('7be4cf4c559b698480d5192591cf97463e89e88dbb1d6efe02bcef41a937a48b',
                     'f5530813079b892217a1c39469d1ac4a8ca562964fbcd1bf66b6954f6be669ab')
$viewerHash = (Get-FileHash -LiteralPath $viewer -Algorithm SHA256).Hash.ToLowerInvariant()
if ($viewerHash -notin $supportedHashes) {
    throw 'Viewer differs from the audited file; inspect it before executing'
}
$occupied = @(Get-NetTCPConnection -LocalPort 36000 -State Listen -ErrorAction SilentlyContinue)
if ($occupied.Count) { throw ('TCP 36000 is already occupied by PID(s): ' + (($occupied.OwningProcess | Select-Object -Unique) -join ', ')) }
$runtime = Join-Path $env:LOCALAPPDATA 'XboxRemoteDev\360stream-viewer'
$venv = Join-Path $runtime 'venv'
$python = Join-Path $venv 'Scripts\python.exe'
if ($Setup) {
    if (-not (Test-Path -LiteralPath $python)) {
        & py -3.12 -m venv $venv
        if ($LASTEXITCODE -ne 0) { throw 'Python 3.12 venv creation failed' }
    }
    & $python -m pip install --disable-pip-version-check --only-binary=:all: pillow sounddevice
    if ($LASTEXITCODE -ne 0) { throw 'Viewer dependencies failed to install' }
    & $python -m pip freeze | Set-Content -LiteralPath (Join-Path $runtime 'requirements-installed.txt') -Encoding ASCII
}
if (-not (Test-Path -LiteralPath $python)) { throw 'Run this launcher once with -Setup to create the isolated Python 3.12 environment' }
& $python -B -c 'import sys,tkinter,PIL,sounddevice; print(sys.version.split()[0], PIL.__version__, sounddevice.__version__)'
if ($LASTEXITCODE -ne 0) { throw 'Missing viewer dependency; run with -Setup' }
$logs = Join-Path $runtime ('runs\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $logs -Force | Out-Null
$stdout = Join-Path $logs 'stdout.txt'
$stderr = Join-Path $logs 'stderr.txt'
$process = Start-Process -FilePath $python -ArgumentList @('-B','-u',('"' + $viewer + '"')) -WorkingDirectory $logs -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
$listeners = @()
for ($attempt=0; $attempt -lt 20; $attempt++) {
    Start-Sleep -Milliseconds 250
    $process.Refresh()
    if ($process.HasExited) { throw ('Viewer exited; inspect ' + $stderr) }
    # Windows venv's python.exe can forward execution to a child interpreter.
    $owners = @($process.Id) + @(Get-CimInstance Win32_Process -Filter ('ParentProcessId = ' + $process.Id) | Select-Object -ExpandProperty ProcessId)
    $listeners = @(Get-NetTCPConnection -LocalPort 36000 -State Listen -ErrorAction SilentlyContinue | Where-Object { $_.OwningProcess -in $owners })
    if ($listeners.Count) { break }
}
# Do not connect a probe client: this original receiver accepts one connection.
$result = @{ viewer_sha256=$viewerHash; python=$python; process_id=$process.Id;
    listener_verified=[bool]$listeners.Count; listener_process_ids=@($listeners.OwningProcess); logs=$logs;
    xbox_module_state='not queried by this launcher'; hardware_capture_verified=$false;
    dependency_versions=@(Get-Content -LiteralPath (Join-Path $runtime 'requirements-installed.txt') -ErrorAction SilentlyContinue | ForEach-Object { [string]$_ }) }
$result | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $logs 'startup.json') -Encoding UTF8
$result | ConvertTo-Json -Depth 3
if (-not $listeners.Count) { throw ('Viewer is running but TCP listener was not observed. Check ' + $stderr) }
