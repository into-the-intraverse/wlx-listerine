param(
    [Parameter(Mandatory=$true)]
    [string]$Version
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)

# --- md plugin ---
$md = "$root/staging/wlx-listerine-md"
New-Item -ItemType Directory -Path $md -Force | Out-Null

Copy-Item "$root/config/pluginst-md.inf" "$md/pluginst.inf"
Copy-Item "$root/output/wlx-listerine-md.wlx64" "$md/"

$header = @"
# wlx-listerine-md configuration
# Rename this file to wlx-listerine-md.toml to customize.
# All values shown are the built-in defaults.
# The plugin works without this file — only create it to override specific settings.

"@
$config = Get-Content "$root/config/wlx-listerine-md.toml" -Raw
Set-Content -Path "$md/wlx-listerine-md.toml.sample" -Value ($header + $config) -NoNewline

# md plugin instantiates its own Colorizer for fenced code blocks; ship the
# grammars and themes alongside so a fresh install renders highlighted code
# out of the box.
Copy-Item "$root/output/themes" "$md/themes" -Recurse
Copy-Item "$root/output/grammars" "$md/grammars" -Recurse

$mdZip = "$root/wlx-listerine-md-$Version.zip"
Remove-Item $mdZip -ErrorAction SilentlyContinue
Compress-Archive -Path "$md/*" -DestinationPath $mdZip
Write-Host "Created $mdZip"

# --- colorizer plugin ---
$col = "$root/staging/wlx-listerine-colorizer"
New-Item -ItemType Directory -Path $col -Force | Out-Null

Copy-Item "$root/config/pluginst-colorizer.inf" "$col/pluginst.inf"
Copy-Item "$root/output/wlx-listerine-colorizer.wlx64" "$col/"

$header = @"
# wlx-listerine-colorizer configuration
# Rename this file to wlx-listerine-colorizer.toml to customize.
# All values shown are the built-in defaults.
# The plugin works without this file — only create it to override specific settings.

"@
$config = Get-Content "$root/config/wlx-listerine-colorizer.toml" -Raw
Set-Content -Path "$col/wlx-listerine-colorizer.toml.sample" -Value ($header + $config) -NoNewline

Copy-Item "$root/output/themes" "$col/themes" -Recurse
Copy-Item "$root/output/grammars" "$col/grammars" -Recurse

$colZip = "$root/wlx-listerine-colorizer-$Version.zip"
Remove-Item $colZip -ErrorAction SilentlyContinue
Compress-Archive -Path "$col/*" -DestinationPath $colZip
Write-Host "Created $colZip"

# --- cleanup ---
Remove-Item "$root/staging" -Recurse -Force
Write-Host "Done."
