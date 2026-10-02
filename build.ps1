param([switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
$taskVswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (!(Test-Path -LiteralPath $taskVswhere)) { throw 'Install Visual Studio with Desktop development with C++ first.' }
$taskVs = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$taskVs) { throw 'MSVC is missing. Add Desktop development with C++ in Visual Studio Installer.' }
$taskCmake = Join-Path $taskVs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (!(Test-Path -LiteralPath $taskCmake)) {
    $taskCmake = (Get-Command cmake -ErrorAction Stop).Source
}
& $taskCmake -S $taskRoot -B (Join-Path $taskRoot 'build') -A x64
if ($LASTEXITCODE) { throw 'CMake configure failed.' }
& $taskCmake --build (Join-Path $taskRoot 'build') --config Release
if ($LASTEXITCODE) { throw 'Build failed.' }
if (!$SkipTests) {
    $taskCtest = Join-Path (Split-Path $taskCmake) 'ctest.exe'
    & $taskCtest --test-dir (Join-Path $taskRoot 'build') -C Release --output-on-failure
    if ($LASTEXITCODE) { throw 'Native integration tests failed. See %TEMP%\SlackOff-test-*\results.txt.' }
}
$taskRelease = Join-Path $taskRoot 'release'
New-Item -ItemType Directory -Path $taskRelease -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $taskRoot 'build\Release\SlackOff.exe') -Destination $taskRelease
Copy-Item -LiteralPath (Join-Path $taskRoot 'README.md'),(Join-Path $taskRoot 'README_EN.md'),(Join-Path $taskRoot 'VALIDATION.md') -Destination $taskRelease
Write-Host "Built: $taskRelease\SlackOff.exe"
