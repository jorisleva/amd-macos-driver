[CmdletBinding()]
param(
    [ValidateSet('development', 'target')][string]$Role = 'development',
    [string]$OutputPath = '',
    [string]$CardManufacturer = '', [string]$Vbios = '',
    [string]$Monitor = '', [string]$Connector = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputPath) { $OutputPath = Join-Path $repoRoot "reports/local/windows-$Role.json" }
$collectionErrors = [System.Collections.Generic.List[string]]::new()
function Read-Hardware($Class, $Fields) {
    try { @(Get-CimInstance -ClassName $Class | Select-Object -Property $Fields) }
    catch { $collectionErrors.Add("${Class}: $($_.Exception.Message)"); @() }
}
$graphics = @(Read-Hardware 'Win32_VideoController' @('Name', 'PNPDeviceID', 'DriverVersion', 'DriverDate'))
$pci = foreach ($gpu in $graphics) {
    $match = [regex]::Match($gpu.PNPDeviceID, 'VEN_([0-9A-F]{4})&DEV_([0-9A-F]{4})&SUBSYS_([0-9A-F]{4})([0-9A-F]{4})', 'IgnoreCase')
    [ordered]@{
        name = $gpu.Name
        vendor_id = if ($match.Success) { '0x' + $match.Groups[1].Value } else { $null }
        device_id = if ($match.Success) { '0x' + $match.Groups[2].Value } else { $null }
        subsystem_device_id = if ($match.Success) { '0x' + $match.Groups[3].Value } else { $null }
        subsystem_vendor_id = if ($match.Success) { '0x' + $match.Groups[4].Value } else { $null }
        driver_version = $gpu.DriverVersion
        driver_date = $gpu.DriverDate
        # Deliberately omit the instance-specific suffix of PNPDeviceID and all serial numbers.
    }
}
$tools = [ordered]@{}
foreach ($tool in @('cargo', 'rustc', 'cmake', 'glslangValidator', 'spirv-val', 'llvm-dis')) {
    $command = Get-Command $tool -ErrorAction SilentlyContinue
    $tools[$tool] = if ($command) { ((& $command.Source --version 2>&1) | Out-String).Trim() } else { $null }
}
$bootPci = @(Read-Hardware 'Win32_PnPEntity' @('Name', 'PNPClass', 'PNPDeviceID') | Where-Object {
    $_.PNPDeviceID -match '^PCI\\' -and $_.PNPClass -in @('Net', 'USB', 'SCSIAdapter', 'HDC')
} | ForEach-Object {
    [ordered]@{ name = $_.Name; class = $_.PNPClass; pci_hardware_id = ($_.PNPDeviceID -split '\\')[1] }
})
$inventory = [ordered]@{
    schema_version = 2; collected_at_utc = [DateTime]::UtcNow.ToString('o'); role = $Role
    code_revision = (& git -C $repoRoot rev-parse HEAD)
    computer = @(Read-Hardware 'Win32_ComputerSystem' @('Manufacturer', 'Model', 'TotalPhysicalMemory'))
    motherboard = @(Read-Hardware 'Win32_BaseBoard' @('Manufacturer', 'Product', 'Version'))
    bios = @(Read-Hardware 'Win32_BIOS' @('Manufacturer', 'SMBIOSBIOSVersion', 'ReleaseDate'))
    cpu = @(Read-Hardware 'Win32_Processor' @('Name', 'NumberOfCores', 'NumberOfLogicalProcessors'))
    memory = @(Read-Hardware 'Win32_PhysicalMemory' @('Capacity', 'Speed', 'ConfiguredClockSpeed'))
    system = @(Read-Hardware 'Win32_OperatingSystem' @('Caption', 'Version', 'BuildNumber', 'OSArchitecture'))
    graphics = @($pci); tools = $tools
    manual = [ordered]@{ card_manufacturer = $CardManufacturer; vbios = $Vbios; monitor = $Monitor; connector = $Connector }
    collection_errors = $collectionErrors
    target_rx9070xt_observed = @($pci | Where-Object { $_.vendor_id -eq '0x1002' -and $_.name -match 'RX 9070 XT' }).Count -gt 0
    storage = @(Read-Hardware 'Win32_DiskDrive' @('Index', 'Model', 'Size', 'FirmwareRevision', 'InterfaceType', 'MediaType'))
    boot_pci = $bootPci
    # WMI InterfaceType can report SCSI for an NVMe drive; boot_pci identifies the actual controller.
    boot_storage_and_peripherals = 'observed hardware only; installation disk and physical USB socket mapping remain manual'
}
$parent = Split-Path -Parent ([IO.Path]::GetFullPath($OutputPath))
[void](New-Item -ItemType Directory -Path $parent -Force)
$inventory | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Host "Inventory: $OutputPath ($Role)"
if ($collectionErrors.Count) { throw 'Inventory incomplete; see collection_errors in the report.' }
if ($Role -eq 'target' -and -not $inventory.target_rx9070xt_observed) { throw 'Target inventory refused: no AMD RX 9070 XT observed.' }
