param(
    [string]$BuildRoot = ""
)

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    $BuildRoot = Join-Path $projectRoot "build/tdlib"
}

$gperfBuild = Join-Path $BuildRoot "gperf"
$zlibBuild = Join-Path $BuildRoot "zlib"
$dependencyPrefix = Join-Path $BuildRoot "deps"
$tdlibBuild = Join-Path $BuildRoot "tdlib"
$tdlibPrefix = Join-Path $BuildRoot "install"
$opensslRoot = Join-Path $projectRoot "external/openssl"
$gperfExecutable = Join-Path $gperfBuild "bin/gperf.exe"
$env:PATH = (Join-Path $opensslRoot "bin") + ";" + $env:PATH

function Invoke-CMake {
    param([string[]]$Arguments)
    & cmake @Arguments
    if ($LASTEXITCODE -ne 0) { throw "CMake failed: $($Arguments -join ' ')" }
}

foreach ($requiredPath in @(
    (Join-Path $projectRoot "external/gperf/src/main.cc"),
    (Join-Path $projectRoot "external/zlib/CMakeLists.txt"),
    (Join-Path $projectRoot "external/tdlib/CMakeLists.txt"),
    (Join-Path $opensslRoot "include/openssl/ssl.h"),
    (Join-Path $opensslRoot "lib/VC/x64/MD/libcrypto.lib"),
    (Join-Path $opensslRoot "lib/VC/x64/MD/libssl.lib"))) {
    if (-not (Test-Path -LiteralPath $requiredPath)) { throw "Missing TDLib prerequisite: $requiredPath" }
}

Invoke-CMake @("-S", (Join-Path $projectRoot "cmake/gperf"), "-B", $gperfBuild, "-G", "Ninja")
Invoke-CMake @("--build", $gperfBuild, "--target", "tggate_gperf_tool", "--parallel")

Invoke-CMake @("-S", (Join-Path $projectRoot "external/zlib"), "-B", $zlibBuild, "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_INSTALL_PREFIX=$dependencyPrefix",
    "-DZLIB_BUILD_TESTING=OFF", "-DZLIB_BUILD_SHARED=OFF", "-DZLIB_BUILD_STATIC=ON")
Invoke-CMake @("--build", $zlibBuild, "--target", "install", "--parallel")

Invoke-CMake @("-S", (Join-Path $projectRoot "external/tdlib"), "-B", $tdlibBuild, "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_INSTALL_PREFIX=$tdlibPrefix",
    "-DBUILD_TESTING=OFF", "-DTD_ENABLE_DOTNET=OFF", "-DTD_ENABLE_JNI=OFF",
    "-DOPENSSL_FOUND=TRUE", "-DOPENSSL_INCLUDE_DIR=$opensslRoot/include",
    "-DOPENSSL_CRYPTO_LIBRARY=$opensslRoot/lib/VC/x64/MD/libcrypto.lib",
    "-DOPENSSL_SSL_LIBRARY=$opensslRoot/lib/VC/x64/MD/libssl.lib",
    "-DZLIB_ROOT=$dependencyPrefix", "-DZLIB_INCLUDE_DIR=$dependencyPrefix/include",
    "-DZLIB_LIBRARY=$dependencyPrefix/lib/libzs.a", "-DGPERF_EXECUTABLE=$gperfExecutable")
Invoke-CMake @("--build", $tdlibBuild, "--target", "install", "--parallel")

Invoke-CMake @("-S", $projectRoot, "-B", (Join-Path $BuildRoot "tggate"), "-G", "Ninja",
    "-DTGGATE_ENABLE_TDLIB=ON", "-DTGGATE_ENABLE_HTTP_HOST=ON", "-DTGGATE_BUILD_UI_MODEL=ON", "-DTGGATE_BUILD_TESTS=ON",
    "-DTGGATE_TDLIB_PREFIX=$tdlibPrefix")
Invoke-CMake @("--build", (Join-Path $BuildRoot "tggate"), "--target", "tggate_desktop", "tggate_tdlib_adapter_smoke", "tggate_tdlib_request_router_tests", "--parallel")

& ctest --test-dir (Join-Path $BuildRoot "tggate") --tests-regex "^(tggate_tdlib_adapter_smoke|tggate_tdlib_request_router_tests)$" --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "TDLib adapter tests failed" }
