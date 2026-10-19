$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$testBinary = Join-Path ([System.IO.Path]::GetTempPath()) (
    'kronos-ext2-host-test-' + [guid]::NewGuid().ToString('N') + '.exe'
)
$compiler = Get-Command gcc -ErrorAction Stop

try {
    & $compiler.Source `
        -std=gnu11 -Wall -Wextra -Werror -fno-builtin `
        "-I$(Join-Path $repoRoot 'src')" `
        (Join-Path $PSScriptRoot 'ext2_host_test.c') `
        (Join-Path $repoRoot 'src/filesystem/ext2.c') `
        (Join-Path $repoRoot 'src/stdlib/string.c') `
        -o $testBinary

    if ($LASTEXITCODE -ne 0) {
        throw "Host test compilation failed with exit code $LASTEXITCODE."
    }

    & $testBinary
    if ($LASTEXITCODE -ne 0) {
        throw "Host tests failed with exit code $LASTEXITCODE."
    }
}
finally {
    if (Test-Path -LiteralPath $testBinary) {
        Remove-Item -LiteralPath $testBinary
    }
}
