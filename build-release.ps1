#Requires -Version 5.1
[CmdletBinding()]
param([string]$FluentUISdkDirectory, [switch]$RunTests)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$buildRoot = Join-Path $sourceRoot 'build\x64'
$packageRoot = Join-Path $sourceRoot 'package'
$versionMatch = [regex]::Match([IO.File]::ReadAllText((Join-Path $sourceRoot 'CMakeLists.txt')), 'project\(MouseTest VERSION (\d+\.\d+\.\d+\.\d+)')
if (-not $versionMatch.Success) { throw '找不到项目版本号。' }
$expectedVersion = $versionMatch.Groups[1].Value
$history = [IO.File]::ReadAllText((Join-Path $sourceRoot 'docs\MouseTestHistory.txt'))
if (-not $history.StartsWith("[$expectedVersion]")) { throw '请先更新版本历史。' }
$configure = @('-S', $sourceRoot, '-B', $buildRoot, '-G', 'Visual Studio 18 2026', '-A', 'x64')
if ($FluentUISdkDirectory) { $configure += "-DFTC_FLUENTUI_SDK_DIR=$FluentUISdkDirectory" }
& cmake.exe @configure
if ($LASTEXITCODE -ne 0) { throw 'CMake 配置失败。' }
& cmake.exe --build $buildRoot --config Release --target MouseTest --parallel
if ($LASTEXITCODE -ne 0) { throw '正式 EXE 编译失败。' }
if ($RunTests) {
    & cmake.exe --build $buildRoot --config Release --target MouseTestConfigTests MouseTestLogicTests MouseTestWindowTests --parallel
    if ($LASTEXITCODE -ne 0) { throw '定向验证程序编译失败。' }
    & ctest.exe --test-dir $buildRoot -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw '鼠标测试定向验证失败。' }
}
$built = Join-Path $buildRoot 'Release\MouseTest.exe'
$info = (Get-Item -LiteralPath $built).VersionInfo
if ($info.FileVersion -ne $expectedVersion -or $info.ProductVersion -ne $expectedVersion -or
    $info.CompanyName -cne 'sixunited' -or $info.ProductName -cne 'MouseTest' -or
    $info.OriginalFilename -cne 'MouseTest.exe' -or $info.InternalName -cne 'MouseTest' -or
    $info.FileDescription -cne 'Standalone Mouse and TouchPad Test Tool' -or
    $info.LegalCopyright -cne 'Copyright (c) 2026 sixunited. Author: sclliang') {
    throw 'EXE 属性不符合项目约定。'
}
[IO.Directory]::CreateDirectory($packageRoot) | Out-Null
Copy-Item -LiteralPath $built -Destination (Join-Path $packageRoot 'MouseTest.exe')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'docs\MouseTestHistory.txt') -Destination $packageRoot
Copy-Item -LiteralPath (Join-Path $sourceRoot 'README.md') -Destination (Join-Path $packageRoot '使用说明.txt')
# Never overwrite the operator's language or grid settings.
$configDestination = Join-Path $packageRoot 'config.ini'
if (-not (Test-Path -LiteralPath $configDestination)) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot 'config.ini') -Destination $configDestination
}
Write-Host "正式程序：$(Join-Path $packageRoot 'MouseTest.exe')"
Write-Host "版本：$expectedVersion；运行时在程序目录追加 MouseTest.log。"
