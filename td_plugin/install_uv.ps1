param(
    [string]$WorkDir = $PSScriptRoot
)

Set-Location $WorkDir

function Emit([string]$msg) {
    Write-Output "INSTALL_STATUS: $msg"
    [Console]::Out.Flush()
}

# ── 1. Locate or install UV ───────────────────────────────────────────────────
$uvExe = $null
foreach ($candidate in @(
    "uv",
    "$env:LOCALAPPDATA\uv\uv.exe",
    "$env:USERPROFILE\.local\bin\uv.exe",
    "$env:USERPROFILE\.cargo\bin\uv.exe"
)) {
    try {
        $ver = & $candidate --version 2>$null
        if ($LASTEXITCODE -eq 0) { $uvExe = $candidate; break }
    } catch {}
}

if (-not $uvExe) {
    Emit "UV not found — installing via irm..."
    try {
        irm https://astral.sh/uv/install.ps1 | iex
    } catch {
        Write-Output "INSTALL_ERROR: UV installer download failed: $_"
        exit 1
    }
    $uvExe = "$env:USERPROFILE\.local\bin\uv.exe"
    if (-not (Test-Path $uvExe)) {
        Write-Output "INSTALL_ERROR: uv.exe not found after install at $uvExe"
        exit 1
    }
}
Emit "UV: $uvExe"

# ── 2. Create virtual environment ─────────────────────────────────────────────
Emit "Creating .venv..."
& $uvExe venv .venv
if ($LASTEXITCODE -ne 0) { Write-Output "INSTALL_ERROR: uv venv failed"; exit 1 }

# ── 3. PyTorch (CUDA 12.8) ────────────────────────────────────────────────────
Emit "Installing PyTorch with CUDA 12.8 (this may take several minutes)..."
& $uvExe pip install --python .venv torch torchvision --index-url https://download.pytorch.org/whl/cu128
if ($LASTEXITCODE -ne 0) { Write-Output "INSTALL_ERROR: torch install failed"; exit 1 }

# ── 4. Project requirements ───────────────────────────────────────────────────
Emit "Installing requirements.txt..."
& $uvExe pip install --python .venv -r requirements.txt
if ($LASTEXITCODE -ne 0) { Write-Output "INSTALL_ERROR: requirements.txt install failed"; exit 1 }

Emit "Installing requirements_lipsync.txt..."
& $uvExe pip install --python .venv -r requirements_lipsync.txt
if ($LASTEXITCODE -ne 0) { Write-Output "INSTALL_ERROR: requirements_lipsync.txt install failed"; exit 1 }

# ── 5. Install FluxRT package ─────────────────────────────────────────────────
Emit "Installing FluxRT package (editable)..."
& $uvExe pip install --python .venv -e .
if ($LASTEXITCODE -ne 0) { Write-Output "INSTALL_ERROR: fluxrt package install failed"; exit 1 }

# ── 6. Pull LFS model weights ─────────────────────────────────────────────────
Emit "Pulling git-lfs model weights (FLUX.2-Klein-4B, RIFE)..."
git lfs pull
if ($LASTEXITCODE -ne 0) { Write-Output "INSTALL_ERROR: git lfs pull failed"; exit 1 }

# ── 7. LivePortrait weights ───────────────────────────────────────────────────
if (Test-Path "LivePortrait") {
    Emit "LivePortrait already present, skipping clone."
} else {
    Emit "Cloning LivePortrait weights from Hugging Face..."
    git clone https://huggingface.co/KwaiVGI/LivePortrait LivePortrait
    if ($LASTEXITCODE -ne 0) { Write-Output "INSTALL_ERROR: LivePortrait clone failed"; exit 1 }
}

# ── Done ──────────────────────────────────────────────────────────────────────
$venvPython = Join-Path $WorkDir ".venv\Scripts\python.exe"
Write-Output "INSTALL_DONE"
Emit "Complete. Set Python Exe to: $venvPython"
