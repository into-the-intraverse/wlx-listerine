param(
    [Parameter(Mandatory=$true)]
    [string]$Version
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)

# Two ZIPs, both unpacking into TC's `wlx-listerine\` plugin dir:
#  - wlx-listerine-md-<v>.zip carries the shared payload (core DLL, themes,
#    grammars, all three sample TOMLs) plus the md plugin + its pluginst.
#  - wlx-listerine-colorizer-<v>.zip is tiny: pluginst + the colorizer .wlx64.
#
# TC's pluginst.inf `file2=`/`fileN=` keys only mean "additional files to
# copy", not "additional plugins to register". Two pluginst installs is the
# only path to auto-registration of two WLX plugins. Both ZIPs target
# defaultdir=wlx-listerine so disk layout is the unified single folder.

function Write-Sample($srcPath, $stagingDir, $name) {
    $header = @"
# $name configuration
# Rename this file to $name.toml to customize.
# All values shown are the built-in defaults.
# The plugin works without this file — only create it to override specific settings.

"@
    $body = Get-Content $srcPath -Raw
    Set-Content -Path "$stagingDir/$name.toml.sample" -Value ($header + $body) -NoNewline
}

$staging = "$root/staging"
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }

try {
    # ---- md ZIP: full bundle ----
    $mdStage = "$staging/wlx-listerine-md"
    New-Item -ItemType Directory -Path $mdStage -Force | Out-Null

    Copy-Item "$root/config/pluginst-md.inf"               "$mdStage/pluginst.inf"
    Copy-Item "$root/output/wlx-listerine-md.wlx64"        "$mdStage/"
    Copy-Item "$root/output/wlx-listerine-core.dll"        "$mdStage/"
    Write-Sample "$root/config/wlx-listerine-md.toml"      $mdStage "wlx-listerine-md"
    Write-Sample "$root/config/wlx-listerine-colorizer.toml" $mdStage "wlx-listerine-colorizer"
    Write-Sample "$root/config/wlx-listerine-core.toml"    $mdStage "wlx-listerine-core"
    Copy-Item "$root/output/themes"   "$mdStage/themes"   -Recurse
    Copy-Item "$root/output/grammars" "$mdStage/grammars" -Recurse

    $mdZip = "$root/wlx-listerine-md-$Version.zip"
    Remove-Item $mdZip -ErrorAction SilentlyContinue
    Compress-Archive -Path "$mdStage/*" -DestinationPath $mdZip
    Write-Host "Created $mdZip"

    # ---- colorizer ZIP: minimal ----
    $colStage = "$staging/wlx-listerine-colorizer"
    New-Item -ItemType Directory -Path $colStage -Force | Out-Null

    Copy-Item "$root/config/pluginst-colorizer.inf"           "$colStage/pluginst.inf"
    Copy-Item "$root/output/wlx-listerine-colorizer.wlx64"    "$colStage/"

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
