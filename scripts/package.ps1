param(
    [Parameter(Mandatory=$true)]
    [string]$Version
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)

$staging = "$root/staging/wlx-listerine"
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Path $staging -Force | Out-Null

try {
    # pluginst registers both .wlx64s into the same defaultdir.
    Copy-Item "$root/config/pluginst.inf" "$staging/pluginst.inf"

    # Plugin DLLs + core DLL.
    Copy-Item "$root/output/wlx-listerine-md.wlx64"           "$staging/"
    Copy-Item "$root/output/wlx-listerine-colorizer.wlx64"    "$staging/"
    Copy-Item "$root/output/wlx-listerine-core.dll"           "$staging/"

    # Sample TOMLs (rename to .toml.sample so users opt in by removing the suffix).
    function Write-Sample($srcPath, $name) {
        $header = @"
# $name configuration
# Rename this file to $name.toml to customize.
# All values shown are the built-in defaults.
# The plugin works without this file — only create it to override specific settings.

"@
        $body = Get-Content $srcPath -Raw
        Set-Content -Path "$staging/$name.toml.sample" -Value ($header + $body) -NoNewline
    }

    Write-Sample "$root/config/wlx-listerine-md.toml"         "wlx-listerine-md"
    Write-Sample "$root/config/wlx-listerine-colorizer.toml"  "wlx-listerine-colorizer"
    Write-Sample "$root/config/wlx-listerine-core.toml"       "wlx-listerine-core"

    # Themes + grammars: shipped once.
    Copy-Item "$root/output/themes"   "$staging/themes"   -Recurse
    Copy-Item "$root/output/grammars" "$staging/grammars" -Recurse

    # Single bundled ZIP.
    $zip = "$root/wlx-listerine-$Version.zip"
    Remove-Item $zip -ErrorAction SilentlyContinue
    Compress-Archive -Path "$staging/*" -DestinationPath $zip
    Write-Host "Created $zip"
} finally {
    if (Test-Path "$root/staging") {
        Remove-Item "$root/staging" -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "Done."
