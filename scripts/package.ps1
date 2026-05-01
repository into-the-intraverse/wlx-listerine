param(
    [Parameter(Mandatory=$true)]
    [string]$Version
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)

# Two self-contained ZIPs, both targeting TC's `wlx-listerine\` plugin dir.
# Each carries the full shared payload (core DLL, themes, grammars, all
# sample TOMLs) plus its own pluginst.inf and .wlx64. TC's pluginst only
# auto-registers a single WLX plugin per install, so two installs is the
# only path to having both registered. When the second ZIP unpacks on top
# of the first, TC offers to overwrite the shared files (identical content).

function Write-Sample($srcPath, $stagingDir, $name) {
    $header = @"
# $name configuration
# Rename this file to $name.toml to customize.
# All values shown are the built-in defaults.
# The plugin works without this file - only create it to override specific settings.

"@
    $body = Get-Content $srcPath -Raw
    Set-Content -Path "$stagingDir/$name.toml.sample" -Value ($header + $body) -NoNewline
}

function Add-SharedPayload($stagingDir) {
    Copy-Item "$root/output/wlx-listerine-core.dll"     "$stagingDir/"
    Write-Sample "$root/config/wlx-listerine-core.toml" $stagingDir "wlx-listerine-core"
    Copy-Item "$root/output/themes"   "$stagingDir/themes"   -Recurse
    Copy-Item "$root/output/grammars" "$stagingDir/grammars" -Recurse
}

$staging = "$root/staging"
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }

try {
    # ---- md ZIP ----
    $mdStage = "$staging/wlx-listerine-md"
    New-Item -ItemType Directory -Path $mdStage -Force | Out-Null

    Copy-Item "$root/config/pluginst-md.inf"        "$mdStage/pluginst.inf"
    Copy-Item "$root/output/wlx-listerine-md.wlx64" "$mdStage/"
    Write-Sample "$root/config/wlx-listerine-md.toml" $mdStage "wlx-listerine-md"
    Add-SharedPayload $mdStage

    $mdZip = "$root/wlx-listerine-md-$Version.zip"
    Remove-Item $mdZip -ErrorAction SilentlyContinue
    Compress-Archive -Path "$mdStage/*" -DestinationPath $mdZip
    Write-Host "Created $mdZip"

    # ---- colorizer ZIP ----
    $colStage = "$staging/wlx-listerine-colorizer"
    New-Item -ItemType Directory -Path $colStage -Force | Out-Null

    Copy-Item "$root/config/pluginst-colorizer.inf"        "$colStage/pluginst.inf"
    Copy-Item "$root/output/wlx-listerine-colorizer.wlx64" "$colStage/"
    Write-Sample "$root/config/wlx-listerine-colorizer.toml" $colStage "wlx-listerine-colorizer"
    Add-SharedPayload $colStage

    $colZip = "$root/wlx-listerine-colorizer-$Version.zip"
    Remove-Item $colZip -ErrorAction SilentlyContinue
    Compress-Archive -Path "$colStage/*" -DestinationPath $colZip
    Write-Host "Created $colZip"
} finally {
    if (Test-Path $staging) {
        Remove-Item $staging -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "Done."
