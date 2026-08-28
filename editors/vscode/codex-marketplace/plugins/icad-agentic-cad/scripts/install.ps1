param(
    [string]$Version = "latest",
    [switch]$PluginOnly,
    [switch]$DependenciesOnly
)

$ErrorActionPreference = "Stop"
$Repository = "valorisystems/ICAD"
$InstallPlugin = -not $DependenciesOnly
$InstallDependencies = -not $PluginOnly

if ($InstallPlugin -and -not (Get-Command codex -ErrorAction SilentlyContinue)) {
    throw "Codex CLI is required to register the plugin."
}
if ($Version -eq "latest") {
    $Release = Invoke-RestMethod -Headers @{ "User-Agent" = "icad-installer" } `
        -Uri "https://api.github.com/repos/$Repository/releases/latest"
    $Version = $Release.tag_name
}
if (-not $Version) { throw "Could not resolve the ICAD release version." }

$ReleaseUrl = "https://github.com/$Repository/releases/download/$Version"
$DataRoot = Join-Path $env:LOCALAPPDATA "ICAD"
$TemporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("icad-install-" + [guid]::NewGuid())
New-Item -ItemType Directory -Force -Path $TemporaryRoot | Out-Null

function Get-VerifiedAsset([string]$Name) {
    $AssetPath = Join-Path $TemporaryRoot $Name
    $ChecksumPath = "$AssetPath.sha256"
    Invoke-WebRequest -UseBasicParsing -Uri "$ReleaseUrl/$Name" -OutFile $AssetPath
    Invoke-WebRequest -UseBasicParsing -Uri "$ReleaseUrl/$Name.sha256" -OutFile $ChecksumPath
    $Expected = ((Get-Content $ChecksumPath -Raw).Trim() -split "\s+")[0].ToLowerInvariant()
    $Actual = (Get-FileHash -Algorithm SHA256 $AssetPath).Hash.ToLowerInvariant()
    if ($Expected -ne $Actual) { throw "SHA-256 verification failed for $Name" }
    return $AssetPath
}

try {
    if ($InstallPlugin) {
        $PluginArchive = Get-VerifiedAsset "icad-codex-plugin.zip"
        $PluginStaging = Join-Path $TemporaryRoot "plugin"
        Expand-Archive -Path $PluginArchive -DestinationPath $PluginStaging -Force
        $MarketplaceSource = Join-Path $PluginStaging "codex-marketplace"
        $MarketplaceManifest = Join-Path $MarketplaceSource ".agents\plugins\marketplace.json"
        if (-not (Test-Path $MarketplaceManifest)) {
            throw "Plugin release does not contain the ICAD marketplace."
        }
        $MarketplaceRoot = Join-Path $DataRoot "codex-marketplace"
        if (Test-Path $MarketplaceRoot) { Remove-Item -Recurse -Force $MarketplaceRoot }
        New-Item -ItemType Directory -Force -Path $DataRoot | Out-Null
        Move-Item $MarketplaceSource $MarketplaceRoot
        $Marketplaces = (& codex plugin marketplace list | Out-String)
        if ($Marketplaces -match "(?m)^icad\s") {
            & codex plugin marketplace remove icad --json | Out-Null
            if ($LASTEXITCODE -ne 0) { throw "Could not replace the existing ICAD marketplace." }
        }
        & codex plugin marketplace add $MarketplaceRoot --json | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Could not register the ICAD release marketplace." }
        & codex plugin add "icad-agentic-cad@icad" --json | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Could not install the ICAD plugin." }
        Write-Host "Installed ICAD Agentic CAD plugin $Version."
    }

    if ($InstallDependencies) {
        if (-not [Environment]::Is64BitOperatingSystem) {
            throw "ICAD currently requires 64-bit Windows."
        }
        $NativeAsset = "icad-windows-x86_64"
        $NativeArchive = Get-VerifiedAsset "$NativeAsset.zip"
        $NativeStaging = Join-Path $TemporaryRoot "native"
        Expand-Archive -Path $NativeArchive -DestinationPath $NativeStaging -Force
        $NativeRoot = Join-Path $DataRoot "toolchain\$Version\$NativeAsset"
        if (Test-Path $NativeRoot) { Remove-Item -Recurse -Force $NativeRoot }
        New-Item -ItemType Directory -Force -Path (Split-Path $NativeRoot) | Out-Null
        Move-Item $NativeStaging $NativeRoot
        $BinaryDirectory = Join-Path $NativeRoot "stage\bin"
        $Compiler = Join-Path $BinaryDirectory "icad.exe"
        $Viewer = Join-Path $BinaryDirectory "icad-viewer.exe"
        if (-not (Test-Path $Compiler) -or -not (Test-Path $Viewer)) {
            throw "Native release does not contain icad.exe and icad-viewer.exe."
        }
        $CurrentUserPath = [Environment]::GetEnvironmentVariable("Path", "User")
        $Entries = @($CurrentUserPath -split ";" | Where-Object { $_ })
        if ($Entries -notcontains $BinaryDirectory) {
            $UpdatedPath = (($Entries + $BinaryDirectory) -join ";")
            [Environment]::SetEnvironmentVariable("Path", $UpdatedPath, "User")
        }
        $env:Path = "$BinaryDirectory;$env:Path"
        & $Compiler --version
        Write-Host "Installed compiler and viewer under $NativeRoot."
    }

    Write-Host "Start a new Codex conversation before using the updated ICAD plugin."
}
finally {
    if (Test-Path $TemporaryRoot) { Remove-Item -Recurse -Force $TemporaryRoot }
}
