# PowerShell sample
param(
    [string]$Name = "World",
    [int]$Count = 3
)

function Get-Greeting {
    param([string]$Who)
    return "Hello, $Who!"
}

$items = @(1, 2, 3) | ForEach-Object { $_ * 2 }

for ($i = 0; $i -lt $Count; $i++) {
    Write-Host (Get-Greeting -Who $Name)
}
