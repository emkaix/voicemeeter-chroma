param (
    [string]$UserDocsPath = ""
)

if ($UserDocsPath -eq "") {
    $UserDocsPath = [Environment]::GetFolderPath(5)
}

function Pause
{
    Write-Host
    Write-Host "Press any key to exit..." -ForegroundColor Cyan
    $null = [Console]::ReadKey($true)
}

# Elevation Check
if (-not ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]"Administrator"))
{
    Write-Host "Script not running as administrator. Requesting elevation..." -ForegroundColor Yellow
    Start-Process powershell "-ExecutionPolicy Bypass -File `"$PSCommandPath`" -UserDocsPath `"$UserDocsPath`"" -Verb RunAs
    exit
}

[Console]::BackgroundColor = "Black"
Clear-Host

# Get current script directory
$scriptDir = $PSScriptRoot

# Required files in script directory
$requiredFiles = @("vmchroma32.dll", "vmchroma64.dll", "addimport32.exe", "addimport64.exe", "vmchroma.yaml")
foreach ($file in $requiredFiles)
{
    $filePath = Join-Path $scriptDir $file
    if (-not (Test-Path $filePath))
    {
        Write-Host "Required file '$file' not found in script directory. Exiting..." -ForegroundColor Red
        Pause
        exit 1
    }
}

Write-Host "--------- VMChroma Patcher ---------" -ForegroundColor Cyan
Write-Host "Make sure Voicemeeter is not running before continuing." -ForegroundColor Cyan
Write-Host "Press any key to start patching..." -ForegroundColor Cyan
$null = [Console]::ReadKey($true)

$vmNames32 = @(
    "voicemeeter.exe",
    "voicemeeterpro.exe",
    "voicemeeter8.exe"
)

$vmNames64 = @(
    "voicemeeter_x64.exe",
    "voicemeeterpro_x64.exe",
    "voicemeeter8x64.exe"
)

$vmNamesExist32 = @()
$vmNamesExist64 = @()
$voicemeeterPath = "C:\Program Files (x86)\VB\Voicemeeter"

foreach ($exe in $vmNames32)
{
    $exePath = Join-Path $voicemeeterPath $exe
    if (Test-Path $exePath)
    {
        $vmNamesExist32 += $exe
        Write-Host "Found Voicemeeter executable: $exe" -ForegroundColor Green
    }
}

foreach ($exe in $vmNames64)
{
    $exePath = Join-Path $voicemeeterPath $exe
    if (Test-Path $exePath)
    {
        $vmNamesExist64 += $exe
        Write-Host "Found Voicemeeter executable: $exe" -ForegroundColor Green
    }
}

if (($vmNamesExist32 + $vmNamesExist64).Count -eq 0)
{
    Write-Host "No Voicemeeter executables found in $voicemeeterPath. Exiting." -ForegroundColor Red
    Pause
    exit 1
}

# Terminate running instances
foreach ($name in $vmNamesExist32 + $vmNamesExist64) {
    Get-Process -Name ($name -replace '\.exe$') -ErrorAction SilentlyContinue | Stop-Process -Force
}

# Copy executables with _vmchroma suffix (i.e voicemeeter8x64_vmchroma.exe)
foreach ($exe in $vmNamesExist32 + $vmNamesExist64)
{
    $exePath = Join-Path $voicemeeterPath $exe
    $modExePath = Join-Path $voicemeeterPath ($exe -replace '\.exe$', '_vmchroma.exe')

    Write-Host
    Write-Host "Duplicate executable for patching: $destPath" -ForegroundColor Cyan
    Copy-Item -Path $exePath -Destination $modExePath -Force

    if (-not (Test-Path $modExePath))
    {
        Write-Host "Failed to create copy of $exe. Exiting..." -ForegroundColor Red
        Pause
        exit 1
    }

    Write-Host "Created duplicate: $modExePath" -ForegroundColor Green
}

# Run addimport.exe for both 64-bit and 32-bit
foreach ($exe in $vmNamesExist32)
{
    Write-Host
    Write-Host "Patching 32bit target: $( $exe -replace '\.exe$', '_vmchroma.exe' )" -ForegroundColor Yellow
    $process = Start-Process -FilePath "$scriptDir\addimport32.exe" -ArgumentList "vmchroma32.dll", "`"$voicemeeterPath\$exe`"", "`"$voicemeeterPath\$( $exe -replace '\.exe$', '_vmchroma.exe' )`"" -Wait -NoNewWindow -PassThru
    if ($process.ExitCode -ne 0)
    {
        Write-Host "addimport32.exe failed for $exe with exit code $($process.ExitCode). Exiting..." -ForegroundColor Red
        Pause
        if ($process.ExitCode) { exit $process.ExitCode } else { exit 1 }
    }
    Write-Host "Successfully patched: $( $exe -replace '\.exe$', '_vmchroma.exe' )" -ForegroundColor Green
}

foreach ($exe in $vmNamesExist64)
{
    Write-Host
    Write-Host "Patching 64bit target: $( $exe -replace '\.exe$', '_vmchroma.exe' )" -ForegroundColor Yellow
    $process = Start-Process -FilePath "$scriptDir\addimport64.exe" -ArgumentList "vmchroma64.dll", "`"$voicemeeterPath\$exe`"", "`"$voicemeeterPath\$( $exe -replace '\.exe$', '_vmchroma.exe' )`"" -Wait -NoNewWindow -PassThru
    if ($process.ExitCode -ne 0)
    {
        Write-Host "addimport64.exe failed for $exe with exit code $($process.ExitCode). Exiting..." -ForegroundColor Red
        Pause
        if ($process.ExitCode) { exit $process.ExitCode } else { exit 1 }
    }
    Write-Host "Successfully patched: $( $exe -replace '\.exe$', '_vmchroma.exe' )" -ForegroundColor Green
}

# Copy DLLs to Voicemeeter folder
foreach ($dll in @("vmchroma32.dll", "vmchroma64.dll"))
{
    Write-Host
    Write-Host "Copy $dll to Voicemeeter folder..." -ForegroundColor Cyan
    $source = Join-Path $scriptDir $dll
    $dest = Join-Path $voicemeeterPath $dll
    try
    {
        Copy-Item -Path $source -Destination $dest -Force
    }
    catch
    {
        Write-Host "Failed to copy $dll to $voicemeeterPath. Error: $( $_.Exception.Message )" -ForegroundColor Red
        Pause
        exit 1
    }

    if (-not (Test-Path $dest))
    {
        Write-Host "Failed to copy $dll to Voicemeeter folder. Exiting..." -ForegroundColor Red
        Pause
        exit 1
    }
    Write-Host "Copied $dll to $voicemeeterPath" -ForegroundColor Green
}

# Copy vmchroma.yaml to Documents/Voicemeeter

$vmchromaConfigSource = Join-Path $scriptDir "vmchroma.yaml"
$documentsPath = Join-Path $UserDocsPath "Voicemeeter"
$vmchromaConfigDest = Join-Path $documentsPath "vmchroma.yaml"

Write-Host
Write-Host "Copy vmchroma.yaml to $documentsPath folder..." -ForegroundColor Cyan

if (-not (Test-Path $documentsPath)) {
    New-Item -ItemType Directory -Path $documentsPath -Force | Out-Null
}

try
{
    Copy-Item -Path $vmchromaConfigSource -Destination $vmchromaConfigDest -Force
}
catch
{
    Write-Host "Failed to copy $vmchromaConfigSource to $documentsPath. Error: $( $_.Exception.Message )" -ForegroundColor Red
    Pause
    exit 1
}

if (-not (Test-Path $vmchromaConfigDest))
{
    Write-Host "Failed to copy $vmchromaConfigSource to $documentsPath. Exiting..." -ForegroundColor Red
    Pause
    exit 1
}
Write-Host "Copied vmchroma.yaml to $documentsPath" -ForegroundColor Green


# Create Start Menu items
Write-Host
Write-Host "Creating Start Menu shortcuts..." -ForegroundColor Cyan
$startMenuPath = "$env:ProgramData\Microsoft\Windows\Start Menu\Programs\VB Audio\VoiceMeeter"
if (-not (Test-Path $startMenuPath)) {
    New-Item -ItemType Directory -Path $startMenuPath -Force | Out-Null
}
$shell = New-Object -ComObject WScript.Shell
foreach ($exe in $vmNamesExist32 + $vmNamesExist64) {
    $patchedExe = $exe -replace '\.exe$', '_vmchroma.exe'
    $targetPath = Join-Path $voicemeeterPath $patchedExe

    if ($exe -eq "voicemeeter.exe") {
        $shortcutPath = Join-Path $startMenuPath "Voicemeeter VMChroma.lnk"
    }
    elseif ($exe -eq "voicemeeter_x64.exe") {
        $shortcutPath = Join-Path $startMenuPath "Voicemeeter x64 VMChroma.lnk"
    }
    elseif ($exe -eq "voicemeeterpro.exe") {
        $shortcutPath = Join-Path $startMenuPath "Voicemeeter Banana VMChroma.lnk"
    }
    elseif ($exe -eq "voicemeeterpro_x64.exe") {
        $shortcutPath = Join-Path $startMenuPath "Voicemeeter Banana x64 VMChroma.lnk"
    }
    elseif ($exe -eq "voicemeeter8.exe") {
        $shortcutPath = Join-Path $startMenuPath "Voicemeeter Potato VMChroma.lnk"
    }
    elseif ($exe -eq "voicemeeter8x64.exe") {
        $shortcutPath = Join-Path $startMenuPath "Voicemeeter Potato x64 VMChroma.lnk"
    }

    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = $targetPath
    $shortcut.WorkingDirectory = $voicemeeterPath
    $shortcut.WindowStyle = 1
    $shortcut.Description = "VMChroma"
    $shortcut.Save()

    Write-Host "Created shortcut: $shortcutPath" -ForegroundColor Green
}

Write-Host
Write-Host "Voicemeeter patching complete!" -ForegroundColor Green
Pause
exit 0
