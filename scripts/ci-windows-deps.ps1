$ErrorActionPreference = 'Stop'

# Builds the static zlib/libarchive pair used by the Windows MinGW CI job.
$deps = Join-Path $env:RUNNER_TEMP 'auracore-deps'
$src = Join-Path $env:RUNNER_TEMP 'auracore-deps-src'
New-Item -ItemType Directory -Force -Path $deps, $src | Out-Null
Set-Location $src

Invoke-WebRequest -Uri 'https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.xz' -OutFile 'zlib.tar.xz'
tar -xf zlib.tar.xz
cmake -S zlib-1.3.1 -B zlib-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF "-DCMAKE_INSTALL_PREFIX=$deps"
cmake --build zlib-build
cmake --install zlib-build

Invoke-WebRequest -Uri 'https://github.com/libarchive/libarchive/releases/download/v3.7.7/libarchive-3.7.7.tar.xz' -OutFile 'libarchive.tar.xz'
tar -xf libarchive.tar.xz
cmake -S libarchive-3.7.7 -B libarchive-build -G Ninja `
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF "-DCMAKE_INSTALL_PREFIX=$deps" `
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 `
    -DENABLE_OPENSSL=OFF -DENABLE_ZSTD=OFF -DENABLE_LZMA=OFF -DENABLE_BZip2=OFF `
    -DENABLE_LIBXML2=OFF -DENABLE_EXPAT=OFF -DENABLE_CNG=OFF -DENABLE_NETTLE=OFF `
    -DENABLE_TAR=OFF -DENABLE_CPIO=OFF -DENABLE_CAT=OFF -DENABLE_TEST=OFF
cmake --build libarchive-build
cmake --install libarchive-build

Write-Output "Installed static dependencies to $deps"