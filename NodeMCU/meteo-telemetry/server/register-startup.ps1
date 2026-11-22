#Requires -RunAsAdministrator
param(
    [Parameter(Mandatory)]
    [string]$PythonPath,
    [string]$ConfigPath = (Join-Path $PSScriptRoot 'config.json'),
    [System.Net.IPAddress]$RouterAddress
)

$ErrorActionPreference = 'Stop'
$python = (Resolve-Path -LiteralPath $PythonPath).ProviderPath
$config = (Resolve-Path -LiteralPath $ConfigPath).ProviderPath
$entry = Join-Path $PSScriptRoot 'server.py'
$appSettings = Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
$dataPath = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $config) $appSettings.database))
if ((Split-Path -Parent $dataPath) -ne (Join-Path $PSScriptRoot 'data')) {
    throw 'For automatic startup, use the application server/data directory for SQLite.'
}
[IO.Directory]::CreateDirectory((Split-Path -Parent $dataPath)) | Out-Null
foreach ($directory in @($PSScriptRoot, (Split-Path -Parent $python))) {
    & icacls $directory /grant '*S-1-5-19:(OI)(CI)RX' | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Cannot grant read access for LOCAL SERVICE: $directory" }
}
& icacls (Split-Path -Parent $dataPath) /grant '*S-1-5-19:(OI)(CI)M' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Cannot grant database write access for LOCAL SERVICE' }

$firewallName = 'MeteoTelemetry-OpenWrt'
if ($RouterAddress -and -not (Get-NetFirewallRule -Name $firewallName -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -Name $firewallName -DisplayName 'Meteo Telemetry from OpenWrt' `
        -Direction Inbound -Action Allow -Protocol TCP -LocalPort $appSettings.port `
        -RemoteAddress $RouterAddress.IPAddressToString -LocalAddress $appSettings.listen `
        -Program $python | Out-Null
}

$arguments = '"{0}" --config "{1}"' -f $entry, $config
$action = New-ScheduledTaskAction -Execute $python -Argument $arguments -WorkingDirectory $PSScriptRoot
$trigger = New-ScheduledTaskTrigger -AtStartup
$principal = New-ScheduledTaskPrincipal -UserId 'S-1-5-19' -LogonType ServiceAccount
$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -MultipleInstances IgnoreNew -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1)
Register-ScheduledTask -TaskName 'Meteo Telemetry' -Action $action -Trigger $trigger `
    -Principal $principal -Settings $settings -Description 'Receives and graphs meteorological readings'
