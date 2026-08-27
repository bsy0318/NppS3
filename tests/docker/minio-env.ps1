# NppS3 — point the integration runner at the local MinIO container.
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   . .\tests\docker\minio-env.ps1        # dot-source to set NPPS3_TEST_* here
#   .\build\Release\npps3_integration.exe
#
# These are the local development credentials from docker-compose.yml, not
# real storage credentials. Nothing here is written to disk.

$env:NPPS3_TEST_ENDPOINT          = 'http://127.0.0.1:9000'
$env:NPPS3_TEST_ACCESS_KEY_ID     = 'npps3local'
$env:NPPS3_TEST_SECRET_ACCESS_KEY = 'npps3localsecret'
$env:NPPS3_TEST_BUCKET            = 'npps3-test'
$env:NPPS3_TEST_REGION            = 'us-east-1'

Write-Host "NPPS3_TEST_* set for local MinIO at $env:NPPS3_TEST_ENDPOINT (bucket $env:NPPS3_TEST_BUCKET)"
