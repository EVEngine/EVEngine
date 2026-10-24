# Build the native Windows ICD: the prebuilt SwiftShader action supplies x64 only.
param([string]$Destination = "build/arm64-swiftshader-runtime")
$ErrorActionPreference = "Stop"
if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne 'Arm64') {
    throw 'A native Windows ARM64 host is required'
}
$pin = 'cddb5c91b23ff70a1f607ea31610aa0d7f4b5ad5'
$destinationPath = [IO.Path]::GetFullPath((Join-Path $PWD $Destination))
$dll = Join-Path $destinationPath 'vk_swiftshader.dll'
$icd = Join-Path $destinationPath 'vk_swiftshader_icd.json'
if (!(Test-Path $dll) -or !(Test-Path $icd)) {
    $sourcePath = Join-Path $env:RUNNER_TEMP 'eve-swiftshader-source'
    git init $sourcePath
    if ($LASTEXITCODE -ne 0) { throw 'git init failed' }
    git -C $sourcePath fetch --depth 1 https://github.com/google/swiftshader.git $pin
    if ($LASTEXITCODE -ne 0) { throw 'SwiftShader fetch failed' }
    git -C $sourcePath checkout --detach FETCH_HEAD
    if ($LASTEXITCODE -ne 0) { throw 'SwiftShader checkout failed' }
    # The pinned tree vendors the default LLVM backend. Test/example/debugger
    # submodules are unused by the vk_swiftshader target with these options.
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $version = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.ARM64 -property installationVersion
    $major = ([version]$version).Major
    $generators = @{ 17 = 'Visual Studio 17 2022'; 18 = 'Visual Studio 18 2026' }
    if (!$generators.ContainsKey($major)) { throw "Unsupported Visual Studio: $version" }
    $buildPath = Join-Path $sourcePath 'out'
    cmake -S $sourcePath -B $buildPath -G $generators[$major] -A ARM64 `
        -DSWIFTSHADER_BUILD_TESTS=OFF -DSWIFTSHADER_BUILD_PVR=OFF `
        -DSWIFTSHADER_WARNINGS_AS_ERRORS=OFF
    if ($LASTEXITCODE -ne 0) { throw 'SwiftShader configure failed' }
    cmake --build $buildPath --config Release --target vk_swiftshader --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'SwiftShader build failed' }
    New-Item -ItemType Directory -Force $destinationPath | Out-Null
    Copy-Item -LiteralPath (Join-Path $buildPath 'Windows/vk_swiftshader.dll') -Destination $dll
    Copy-Item -LiteralPath (Join-Path $buildPath 'Windows/vk_swiftshader_icd.json') -Destination $icd
}
python3 .github/scripts/verify-arm64-sdk.py $dll
if ($LASTEXITCODE -ne 0) { throw 'ICD is not a native ARM64 DLL' }
# Hosted Windows runners are elevated: Vulkan-Loader intentionally ignores
# VK_ICD_FILENAMES there. Register the ICD on this disposable runner instead.
if ($env:GITHUB_ACTIONS -eq 'true') {
    $driversKey = 'HKLM:\SOFTWARE\Khronos\Vulkan\Drivers'
    New-Item -Path $driversKey -Force | Out-Null
    New-ItemProperty -Path $driversKey -Name $icd -PropertyType DWord -Value 0 -Force | Out-Null
}
"VK_ICD_FILENAMES=$icd" >> $env:GITHUB_ENV
