# Deletes every object under the integration-test namespace in the test
# bucket. Only touches npps3-integration-test/ so unrelated data is safe.
# Credentials come from the project-local .env; values are never printed.
param([string]$ProjectDir = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))

$s3cli = Join-Path $ProjectDir "build\Release\npps3_s3cli.exe"
if (-not (Test-Path $s3cli)) { "build npps3_s3cli first"; exit 1 }

$vals = @{}
Get-Content (Join-Path $ProjectDir ".env") | ForEach-Object {
    if ($_ -match '^\s*([A-Za-z0-9_]+)\s*=\s*(.*)$') {
        $vals[$Matches[1]] = $Matches[2].Trim().Trim('"').Trim("'")
    }
}
$bucket = $vals['NPPS3_TEST_BUCKET']
$prefix = "npps3-integration-test/"

Push-Location $ProjectDir
$keys = & $s3cli list $bucket $prefix
Pop-Location
$keys = $keys | Where-Object { $_ -and $_.StartsWith($prefix) }
"objects under test prefix: $($keys.Count)"

$deleted = 0
foreach ($k in $keys) {
    Push-Location $ProjectDir
    & $s3cli del $bucket $k | Out-Null
    if ($LASTEXITCODE -eq 0) { $deleted++ }
    Pop-Location
}
"deleted: $deleted"
