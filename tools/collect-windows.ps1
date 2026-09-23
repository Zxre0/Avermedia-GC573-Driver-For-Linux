# Read-only device/driver inventory; writes only the requested JSON report.
# Does not trace MMIO or DMA and does not change device or driver settings.
param(
    [string]$OutputPath = (Join-Path ([Environment]::GetFolderPath('Desktop')) 'gc573-windows.json')
)
$ErrorActionPreference = 'Stop'
$devices = @(Get-PnpDevice -PresentOnly | Where-Object {
    $_.InstanceId -match '^PCI\\VEN_1461&DEV_0054&SUBSYS_57301461(?:&|\\)'
})
if ($devices.Count -eq 0) {
    throw 'No present GC573 with PCI 1461:0054 / subsystem 1461:5730 found.'
}
$signedDrivers = @(Get-CimInstance Win32_PnPSignedDriver)
$records = @($devices | ForEach-Object {
    $device = $_
    $matchingDrivers = @($signedDrivers | Where-Object {
        $_.DeviceID -eq $device.InstanceId
    } | Select-Object DeviceName, DriverProviderName, DriverVersion, DriverDate,
        InfName, IsSigned)
    [ordered]@{
        instance_id = $device.InstanceId
        name = $device.FriendlyName
        status = $device.Status
        class = $device.Class
        drivers = $matchingDrivers
    }
})
$report = [ordered]@{
    collected_utc = [DateTime]::UtcNow.ToString('o')
    windows_version = [Environment]::OSVersion.VersionString
    devices = $records
    scope = 'PnP and driver inventory only; no firmware version or capture validation'
}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
Write-Output "Saved GC573 inventory to $OutputPath"
