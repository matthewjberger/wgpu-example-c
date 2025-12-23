set windows-shell := ["powershell.exe", "-NoLogo", "-Command"]

[private]
default:
    @just --list

# Build the triangle executable
build:
    clang -O2 -o triangle.exe main.c \
        -I./include \
        -L./lib \
        -lSDL2 \
        -lwgpu_native \
        -luser32 -lgdi32 -lshell32 -lole32 -loleaut32 -luuid -ladvapi32 -lws2_32 -luserenv -lbcrypt -lntdll -ld3dcompiler -ldxgi -ld3d12

# Build with debug symbols
build-debug:
    clang -g -O0 -o triangle.exe main.c \
        -I./include \
        -L./lib \
        -lSDL2 \
        -lwgpu_native \
        -luser32 -lgdi32 -lshell32 -lole32 -loleaut32 -luuid -ladvapi32 -lws2_32 -luserenv -lbcrypt -lntdll -ld3dcompiler -ldxgi -ld3d12

# Run the triangle example
run: build
    ./triangle.exe

# Clean build artifacts
clean:
    Remove-Item -Force -ErrorAction SilentlyContinue triangle.exe

# Download and setup dependencies (wgpu-native, SDL2, cglm)
setup:
    @echo "Setting up dependencies..."
    New-Item -ItemType Directory -Force -Path include/webgpu | Out-Null
    New-Item -ItemType Directory -Force -Path include/SDL2 | Out-Null
    New-Item -ItemType Directory -Force -Path include/cglm | Out-Null
    New-Item -ItemType Directory -Force -Path lib | Out-Null
    @echo "Downloading wgpu-native v25.0.2.2..."
    Invoke-WebRequest -Uri "https://github.com/gfx-rs/wgpu-native/releases/download/v25.0.2.2/wgpu-windows-x86_64-msvc-release.zip" -OutFile wgpu.zip
    Expand-Archive -Force wgpu.zip -DestinationPath wgpu-temp
    Get-ChildItem -Recurse wgpu-temp -Filter "wgpu_native.dll" | Copy-Item -Destination .
    Get-ChildItem -Recurse wgpu-temp -Filter "wgpu_native.dll.lib" | Copy-Item -Destination lib/wgpu_native.lib
    Get-ChildItem -Recurse wgpu-temp -Filter "webgpu.h" | Copy-Item -Destination include/webgpu/
    Get-ChildItem -Recurse wgpu-temp -Filter "wgpu.h" | Copy-Item -Destination include/webgpu/
    Remove-Item -Recurse -Force wgpu-temp, wgpu.zip
    @echo "Downloading SDL2 2.30.10..."
    Invoke-WebRequest -Uri "https://github.com/libsdl-org/SDL/releases/download/release-2.30.10/SDL2-devel-2.30.10-VC.zip" -OutFile sdl2.zip
    Expand-Archive -Force sdl2.zip -DestinationPath sdl2-temp
    Copy-Item sdl2-temp/SDL2-2.30.10/include/* include/SDL2/
    Copy-Item sdl2-temp/SDL2-2.30.10/lib/x64/SDL2.dll .
    Copy-Item sdl2-temp/SDL2-2.30.10/lib/x64/SDL2.lib lib/
    Remove-Item -Recurse -Force sdl2-temp, sdl2.zip
    @echo "Downloading cglm v0.9.4..."
    Invoke-WebRequest -Uri "https://github.com/recp/cglm/archive/refs/tags/v0.9.4.zip" -OutFile cglm.zip
    Expand-Archive -Force cglm.zip -DestinationPath cglm-temp
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue include/cglm
    Copy-Item -Recurse cglm-temp/cglm-0.9.4/include/cglm include/
    Remove-Item -Recurse -Force cglm-temp, cglm.zip
    @echo "Setup complete!"
