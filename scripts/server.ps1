param(
    [ValidateSet('init', 'serve', 'reset-password', 'backup')][string]$Action = 'serve',
    [string]$Database,
    [string]$Username,
    [string]$Engine,
    [string]$ListenHost = '127.0.0.1',
    [int]$Port = 8765,
    [string]$Certificate,
    [string]$PrivateKey,
    [string]$PublicOrigin,
    [string]$BackupOutput
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskPython = Join-Path $taskRoot '.deps/server/Scripts/python.exe'
if (!(Test-Path -LiteralPath $taskPython)) {
    throw '먼저 python -m venv .deps/server 및 .deps/server/Scripts/python.exe -m pip install -r server/requirements-lock.txt 를 실행하세요.'
}
if (!$Database) { $Database = Join-Path $taskRoot 'build-governance/server-data/ppm.sqlite' }
if (!$Engine) {
    foreach ($taskCandidate in @('build-governance/PPM-Automation.exe', 'build/PPM-Automation.exe', 'build/Release/PPM-Automation.exe')) {
        $taskPath = Join-Path $taskRoot $taskCandidate
        if (Test-Path -LiteralPath $taskPath) { $Engine = $taskPath; break }
    }
}
$taskArgs = @('-m', 'ppm_server', '--database', [IO.Path]::GetFullPath($Database), $Action)
switch ($Action) {
    'init' { if (!$Username) { throw '-Username을 지정하세요.' }; $taskArgs += @('--user', $Username) }
    'reset-password' { if (!$Username) { throw '-Username을 지정하세요.' }; $taskArgs += @('--user', $Username) }
    'backup' { if (!$BackupOutput) { throw '-BackupOutput을 지정하세요.' }; $taskArgs += @('--output', [IO.Path]::GetFullPath($BackupOutput)) }
    'serve' {
        if (!$Engine) { throw 'PPM-Automation을 빌드하거나 -Engine 경로를 지정하세요.' }
        $taskArgs += @('--engine', [IO.Path]::GetFullPath($Engine), '--host', $ListenHost, '--port', $Port)
        if ($Certificate) { $taskArgs += @('--certificate', [IO.Path]::GetFullPath($Certificate)) }
        if ($PrivateKey) { $taskArgs += @('--private-key', [IO.Path]::GetFullPath($PrivateKey)) }
        if ($PublicOrigin) { $taskArgs += @('--public-origin', $PublicOrigin) }
    }
}
$taskPreviousPath = $env:PATH
$taskPreviousPythonPath = $env:PYTHONPATH
try {
    $env:PATH = "$(Join-Path $taskRoot '.deps/Qt/6.8.3/mingw_64/bin');$(Join-Path $taskRoot '.deps/Qt/Tools/mingw1310_64/bin');$taskPreviousPath"
    $env:PYTHONPATH = "$(Join-Path $taskRoot 'server');$taskPreviousPythonPath"
    & $taskPython @taskArgs
    if ($LASTEXITCODE -ne 0) { throw "서버 명령이 실패했습니다: $LASTEXITCODE" }
} finally {
    $env:PATH = $taskPreviousPath
    $env:PYTHONPATH = $taskPreviousPythonPath
}
