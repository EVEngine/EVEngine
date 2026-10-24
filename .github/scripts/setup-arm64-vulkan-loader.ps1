# The SDK supplies import libraries; hosted machines also need a native loader.
param([string]$Destination = "build/arm64-vulkan-loader")
$ErrorActionPreference = "Stop"
$destinationPath = [IO.Path]::GetFullPath((Join-Path $PWD $Destination))
$loader = Join-Path $destinationPath 'vulkan-1.dll'
if (!(Test-Path $loader)) {
    $sourcePath = Join-Path $env:RUNNER_TEMP 'eve-vulkan-loader-source'
    git init $sourcePath
    if ($LASTEXITCODE -ne 0) { throw 'Loader git init failed' }
    # Khronos Vulkan-Loader v1.4.304; UPDATE_DEPS uses its pinned headers.
    git -C $sourcePath fetch --depth 1 https://github.com/KhronosGroup/Vulkan-Loader.git 804a7a4efd865532646e54ad98807c0f318da413
    if ($LASTEXITCODE -ne 0) { throw 'Loader fetch failed' }
    git -C $sourcePath checkout --detach FETCH_HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Loader checkout failed' }
    $buildPath = Join-Path $sourcePath 'out'
    & cmake/with-msvc.cmd --arm64 cmake -S $sourcePath -B $buildPath -G Ninja `
        -DCMAKE_BUILD_TYPE=Release -DUPDATE_DEPS=ON -DBUILD_TESTS=OFF
    if ($LASTEXITCODE -ne 0) { throw 'Loader configure failed' }
    & cmake/with-msvc.cmd --arm64 cmake --build $buildPath --target vulkan --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Loader build failed' }
    New-Item -ItemType Directory -Force $destinationPath | Out-Null
    Copy-Item -LiteralPath (Join-Path $buildPath 'loader/vulkan-1.dll') -Destination $loader
    Copy-Item -LiteralPath (Join-Path $sourcePath 'LICENSE.txt') -Destination $destinationPath
}
python3 .github/scripts/verify-arm64-sdk.py $loader
if ($LASTEXITCODE -ne 0) { throw 'Loader is not a native ARM64 DLL' }
"EVE_VULKAN_LOADER=$loader" >> $env:GITHUB_ENV
$destinationPath >> $env:GITHUB_PATH
