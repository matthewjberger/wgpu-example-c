# C / SDL2 / WGPU Triangle

A cross-platform C graphics demo using [wgpu-native](https://github.com/gfx-rs/wgpu-native) to render a spinning triangle. This is a port of the [wgpu-example-odin](https://github.com/matthewjberger/wgpu-example-odin) project.

> **Related Projects:**
> - [wgpu-example](https://github.com/matthewjberger/wgpu-example) - Rust version
> - [wgpu-example-odin](https://github.com/matthewjberger/wgpu-example-odin) - Odin version

## Prerequisites

- [just](https://github.com/casey/just) - Command runner
- [clang](https://clang.llvm.org/) - C compiler (LLVM)
- PowerShell (Windows)

## Quickstart

```bash
# Download and setup dependencies (wgpu-native, SDL2, cglm)
just setup

# Build and run
just run
```

<img width="802" height="632" alt="image" src="https://github.com/user-attachments/assets/022278e1-aaa9-43a9-a696-0d82ca399770" />

## Commands

| Command | Description |
|---------|-------------|
| `just setup` | Download wgpu-native, SDL2, and cglm headers/libraries |
| `just build` | Build the triangle executable |
| `just build-debug` | Build with debug symbols |
| `just run` | Build and run the triangle |
| `just clean` | Remove build artifacts |

## Project Structure

```
wgpu-example-c/
├── main.c              # Main source file
├── justfile            # Build commands
├── README.md           # This file
├── include/            # Headers (created by setup)
│   ├── webgpu/         # wgpu-native headers
│   ├── SDL2/           # SDL2 headers
│   └── cglm/           # cglm math library headers
├── lib/                # Libraries (created by setup)
├── SDL2.dll            # SDL2 runtime (created by setup)
└── wgpu_native.dll     # wgpu-native runtime (created by setup)
```

## Dependencies

- [wgpu-native](https://github.com/gfx-rs/wgpu-native) - Native WebGPU implementation
- [SDL2](https://www.libsdl.org/) - Cross-platform windowing and input
- [cglm](https://github.com/recp/cglm) - OpenGL Mathematics for C

## Controls

- **ESC** - Quit the application
- Window can be resized
