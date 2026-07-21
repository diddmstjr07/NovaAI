param(
    # "window", "internet" and "ai" mirror build.sh; on Windows the QEMU run
    # already opens a GUI window with networking, so they all behave like "run".
    [ValidateSet("run", "window", "internet", "ai", "install")]
    [string]$Action = "run"
)

if ($Action -in @("window", "internet", "ai")) {
    $Action = "run"
}

$ErrorActionPreference = "Stop"

function Find-Qemu {
    $command = Get-Command qemu-system-x86_64.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidates = @(
        (Join-Path $env:ProgramFiles "qemu\qemu-system-x86_64.exe"),
        (Join-Path $env:ProgramFiles "QEMU\qemu-system-x86_64.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs\qemu\qemu-system-x86_64.exe")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }
    return $null
}

function Install-Qemu {
    $winget = Get-Command winget.exe -ErrorAction SilentlyContinue
    if (-not $winget) {
        throw "winget is unavailable. Install QEMU for Windows from https://www.qemu.org/download/ and run this file again."
    }

    Write-Host "QEMU is not installed. Installing native QEMU for Windows..."
    & winget.exe install --exact --id SoftwareFreedomConservancy.QEMU `
        --accept-source-agreements --accept-package-agreements
    if ($LASTEXITCODE -ne 0) {
        throw "QEMU installation failed with exit code $LASTEXITCODE."
    }
}

$qemu = Find-Qemu
if (-not $qemu) {
    Install-Qemu
    $qemu = Find-Qemu
}

if (-not $qemu) {
    throw "QEMU was installed but qemu-system-x86_64.exe could not be found. Restart PowerShell and try again."
}

if ($Action -eq "install") {
    Write-Host "Native Windows QEMU is ready: $qemu"
    exit 0
}

$image = Join-Path $PSScriptRoot "build\novaos.img"
if (-not (Test-Path -LiteralPath $image)) {
    throw "Boot image not found: $image"
}

# --- Networking (stage 0) ---------------------------------------------------
# QEMU user-mode networking. Guest 10.0.2.15/24, gateway 10.0.2.2, DNS 10.0.2.3.
# No host privileges required; ICMP is limited, so probe connectivity with TCP.
$pcap = Join-Path $PSScriptRoot "net.pcap"

Write-Host "Starting NovaOS directly on native Windows QEMU..."
Write-Host "Press Ctrl+Alt+G to release captured keyboard and mouse input."
Write-Host "Packet capture: $pcap"

$bridgeProcess = $null
$bridgeScript = Join-Path $PSScriptRoot "tools\ai_bridge.py"
$pythonLauncher = Get-Command py.exe -ErrorAction SilentlyContinue
$pythonArguments = @()
if ($pythonLauncher) {
    $pythonArguments = @("-3", "`"$bridgeScript`"")
} else {
    $pythonLauncher = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($pythonLauncher) {
        $pythonArguments = @("`"$bridgeScript`"")
    }
}
if ($pythonLauncher -and (Test-Path -LiteralPath $bridgeScript)) {
    $bridgeProcess = Start-Process -FilePath $pythonLauncher.Source `
        -ArgumentList $pythonArguments -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 500
    if ($bridgeProcess.HasExited) {
        Write-Warning "Nova internet bridge could not start. Port 7780 may already be in use."
        $bridgeProcess = $null
    } else {
        Write-Host "Nova internet bridge started on TCP 7780."
    }
} else {
    Write-Warning "Native Python 3 was not found. NovaOS will boot, but Nova Browser will be offline."
    Write-Host "Install Python 3 for Windows; WSL is not required."
}

$qemuArgs = @(
    "-name"; "NovaOS C Desktop"
    "-cpu"; "qemu64"
    "-m"; "1024M"
    "-vga"; "std"
    "-display"; "gtk,zoom-to-fit=on"
    "-full-screen"
    "-drive"; "file=$image,format=raw,if=ide"
    "-boot"; "order=c"
    "-netdev"; "user,id=n0"
    "-device"; "e1000,netdev=n0"
    "-object"; "filter-dump,id=f0,netdev=n0,file=$pcap"
    "-serial"; "stdio"
    "-monitor"; "none"
    "-no-reboot"
)

Write-Host "Trying Windows Hypervisor Platform acceleration (WHPX)..."
try {
    & $qemu -machine "pc,accel=whpx" @qemuArgs
    $qemuExit = $LASTEXITCODE
    if ($qemuExit -ne 0) {
        Write-Warning "WHPX is unavailable. Falling back to software emulation (TCG)."
        Write-Host "Run enable_acceleration.bat as administrator and restart Windows to reduce lag."
        & $qemu -machine "pc,accel=tcg,thread=multi" @qemuArgs
        $qemuExit = $LASTEXITCODE
    }
} finally {
    if ($bridgeProcess -and -not $bridgeProcess.HasExited) {
        Stop-Process -Id $bridgeProcess.Id -Force -ErrorAction SilentlyContinue
    }
}

exit $qemuExit
