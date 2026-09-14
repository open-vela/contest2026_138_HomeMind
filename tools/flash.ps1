[CmdletBinding()]
param(
    [string]$Port,
    [string]$Firmware,
    [ValidateRange(9600, 921600)]
    [int]$Baud = 460800,
    [string]$Python = "python",
    [switch]$ListPorts,
    [switch]$ProbeOnly,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-HomeMindPorts {
    $portNames = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
    $pnpText = (& pnputil /enum-devices /connected /class Ports 2>$null | Out-String)

    $devicesByPort = @{}
    $devicePattern = "(?ms)^Instance ID:\s*(?<id>[^\r\n]+).*?^Device Description:\s*(?<description>[^\r\n]*\((?<port>COM\d+)\)).*?(?=^Instance ID:|\z)"
    foreach ($match in [regex]::Matches($pnpText, $devicePattern)) {
        $devicesByPort[$match.Groups["port"].Value.ToUpperInvariant()] = [pscustomobject]@{
            Description = $match.Groups["description"].Value.Trim()
            InstanceId = $match.Groups["id"].Value.Trim()
        }
    }

    foreach ($name in $portNames) {
        $device = $devicesByPort[$name.ToUpperInvariant()]
        $instanceId = if ($device) { $device.InstanceId } else { "Unknown" }
        $description = if ($device) { $device.Description } else { "Unknown device" }

        [pscustomobject]@{
            Port = $name
            Description = $description
            InstanceId = $instanceId
            IsLegacyPort = $instanceId -like "ACPI\PNP0501*"
            IsEspressif = $instanceId -like "USB\VID_303A*"
        }
    }
}

function Resolve-EsptoolCommand {
    foreach ($commandName in @("esptool.py", "esptool")) {
        $command = Get-Command $commandName -ErrorAction SilentlyContinue
        if ($command) {
            return [pscustomobject]@{ Executable = $command.Source; Prefix = @() }
        }
    }

    $pythonCandidate = $Python
    $projectPython = Join-Path $PSScriptRoot "..\.venv-tools\Scripts\python.exe"
    if ($Python -eq "python" -and (Test-Path -LiteralPath $projectPython)) {
        $pythonCandidate = (Resolve-Path -LiteralPath $projectPython).Path
    }

    $pythonCommand = Get-Command $pythonCandidate -ErrorAction SilentlyContinue
    if (-not $pythonCommand) {
        throw "Python or esptool was not found. Install esptool with '$Python -m pip install esptool', or specify Python with -Python."
    }

    & $pythonCommand.Source -m esptool version *> $null
    if ($LASTEXITCODE -ne 0) {
        throw "esptool is not installed for this Python. Run '$Python -m pip install esptool'."
    }

    return [pscustomobject]@{ Executable = $pythonCommand.Source; Prefix = @("-m", "esptool") }
}

function Invoke-Esptool {
    param(
        [Parameter(Mandatory)]$Tool,
        [Parameter(Mandatory)][string[]]$Arguments
    )

    $allArguments = @($Tool.Prefix) + $Arguments
    & $Tool.Executable @allArguments
    if ($LASTEXITCODE -ne 0) {
        throw "esptool failed with exit code $LASTEXITCODE."
    }
}

$ports = @(Get-HomeMindPorts)
if ($ListPorts) {
    if ($ports.Count -eq 0) {
        Write-Host "No serial ports detected."
    } else {
        $ports | Format-Table Port, Description, InstanceId, IsEspressif, IsLegacyPort -AutoSize
    }
    if (-not $Port) { exit 0 }
}

if (-not $Port) {
    throw "Specify a port explicitly, for example: .\tools\flash.ps1 -Port COM5 -ProbeOnly"
}

$Port = $Port.ToUpperInvariant()
$selectedPort = $ports | Where-Object { $_.Port -eq $Port } | Select-Object -First 1
if (-not $selectedPort) {
    throw "Serial port $Port is not present. Reconnect the board and run '.\tools\flash.ps1 -ListPorts'."
}
if ($selectedPort.IsLegacyPort -and -not $Force) {
    throw "$Port is a legacy motherboard port ($($selectedPort.InstanceId)), not a detected ESP32 USB port. Refusing to continue."
}

$tool = Resolve-EsptoolCommand
Write-Host "Target port: $Port ($($selectedPort.Description))"
Write-Host "Reading ESP32-S3 chip information..."
Invoke-Esptool -Tool $tool -Arguments @("--chip", "esp32s3", "--port", $Port, "chip-id")

if ($ProbeOnly) {
    Write-Host "Chip probe succeeded; flash was not modified."
    exit 0
}
if (-not $Firmware) {
    throw "Chip probe succeeded, but no firmware was specified. Pass nuttx.bin with -Firmware, or use -ProbeOnly."
}

$firmwarePath = (Resolve-Path -LiteralPath $Firmware).Path
$firmwareInfo = Get-Item -LiteralPath $firmwarePath
if ($firmwareInfo.Length -eq 0) {
    throw "Firmware file is empty: $firmwarePath"
}
$firmwareHash = (Get-FileHash -LiteralPath $firmwarePath -Algorithm SHA256).Hash

Write-Host "Firmware path: $firmwarePath"
Write-Host "Firmware size: $($firmwareInfo.Length) bytes"
Write-Host "Firmware SHA256: $firmwareHash"
Write-Host "Flashing address 0x0 at $Baud baud..."

Invoke-Esptool -Tool $tool -Arguments @(
    "--chip", "esp32s3",
    "--port", $Port,
    "--baud", "$Baud",
    "--before", "default-reset",
    "--after", "hard-reset",
    "write-flash", "0x0", $firmwarePath
)

Write-Host "Firmware flash completed."
