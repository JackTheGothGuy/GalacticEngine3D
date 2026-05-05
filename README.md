```
 ________  ________  ___       ________  ________ _________  ___  ________     
|\   ____\|\   __  \|\  \     |\   __  \|\   ____\\___   ___\\  \|\   ____\    
\ \  \___|\ \  \|\  \ \  \    \ \  \|\  \ \  \___\|___ \  \_\ \  \ \  \___|    
 \ \  \  __\ \   __  \ \  \    \ \   __  \ \  \       \ \  \ \ \  \ \  \       
  \ \  \|\  \ \  \ \  \ \  \____\ \  \ \  \ \  \____   \ \  \ \ \  \ \  \____  
   \ \_______\ \__\ \__\ \_______\ \__\ \__\ \_______\  \ \__\ \ \__\ \_______\
    \|_______|\|__|\|__|\|_______|\|__|\|__|\|_______|   \|__|  \|__|\|_______|
                                                                               
                                                                               
                                                                               
 _______   ________   ________  ___  ________   _______                        
|\  ___ \ |\   ___  \|\   ____\|\  \|\   ___  \|\  ___ \                       
\ \   __/|\ \  \\ \  \ \  \___|\ \  \ \  \\ \  \ \   __/|                      
 \ \  \_|/_\ \  \\ \  \ \  \  __\ \  \ \  \\ \  \ \  \_|/__                    
  \ \  \_|\ \ \  \\ \  \ \  \|\  \ \  \ \  \\ \  \ \  \_|\ \                   
   \ \_______\ \__\\ \__\ \_______\ \__\ \__\\ \__\ \_______\                  
    \|_______|\|__| \|__|\|_______|\|__|\|__| \|__|\|_______|                  
```

> **RenderX Engine v0.9.4 BETA** — A software/hardware 3D rendering engine written in C/C++

---

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)](.)
[![Version](https://img.shields.io/badge/version-0.9.4--beta-blue)](.)
[![License](https://img.shields.io/badge/license-MIT-yellow)](.)
[![Platform](https://img.shields.io/badge/platform-Win32%20%7C%20Linux-lightgrey)](.)
[![OpenGL](https://img.shields.io/badge/OpenGL-1.4%2B-orange)](.)

---

## :: OVERVIEW ::

**RenderX** is a real-time 3D rendering engine built from the ground up in C and C++. It supports both a pure **software rasterizer** (no GPU required!) and a **hardware-accelerated** path via OpenGL. Originally started as a personal project in a basement in 2002, it has grown into a fairly capable system for rendering static and dynamic 3D scenes.

If you're looking for a clean, fast, hackable engine that doesn't pull in 400MB of dependencies — you're in the right place.

> ⚠️ **This is BETA software.** Some features are incomplete or buggy. Don't use this for anything mission-critical. You have been warned.

---

## :: FEATURES ::

- ✅ Software rasterizer (runs on any hardware from the last 10 years)
- ✅ OpenGL 1.4 hardware renderer path
- ✅ Gouraud and Phong shading
- ✅ Texture mapping with bilinear filtering
- ✅ Directional, point, and spot lights (up to 8 simultaneous)
- ✅ BSP tree scene management
- ✅ .OBJ and .3DS mesh loading
- ✅ Basic skeletal animation
- ✅ Fog effects (linear + exponential)
- ✅ Z-buffering + alpha blending
- ✅ Simple collision detection (AABB)
- ✅ Win32 and Linux (X11) window backends
- 🚧 Stencil shadow volumes *(WIP — works 80% of the time)*
- 🚧 Cube map reflections *(partial implementation)*
- ❌ DirectX backend *(not planned)*

---

## :: SCREENSHOTS ::

> *Screenshots taken on a Pentium 4 2.4GHz with 512MB RAM and a GeForce4 Ti 4200*

```
[ sponza.bmp ]   [ outdoor_scene.bmp ]   [ stress_test.bmp ]
  (see /docs/screenshots/ folder)
```

*Note: The screenshots directory contains .BMP files. Paint works fine for viewing them.*

---

## :: SYSTEM REQUIREMENTS ::

### Minimum (Software Mode)
| Component | Requirement |
|-----------|-------------|
| OS        | Windows 98/2000/XP or Linux (kernel 2.4+) |
| CPU       | Pentium II 400MHz or equivalent |
| RAM       | 64 MB |
| Disk      | 15 MB free |
| Display   | 640x480, 16-bit color |

### Recommended (Hardware Mode)
| Component | Requirement |
|-----------|-------------|
| OS        | Windows XP or Linux (kernel 2.6+) |
| CPU       | Pentium III 800MHz or faster |
| RAM       | 256 MB |
| GPU       | GeForce2 / Radeon 7500 or better |
| Drivers   | OpenGL 1.4 capable drivers |
| Display   | 1024x768, 32-bit color |

> **Note:** Voodoo 1/2 cards are **NOT** supported. Voodoo3 works but has Z-buffer precision issues.

---

## :: BUILDING FROM SOURCE ::

### Windows (MSVC 6.0 / Visual C++ .NET 2003)

Open `renderx.dsw` or `renderx.sln` in your IDE and hit **Build → Build Solution**.

If you don't have the IDE, you can use the batch file:

```bat
cd src
build_win32.bat
```

Output binary will be at `bin\renderx.exe`.

> Requires: `opengl32.lib`, `glu32.lib`, `winmm.lib` — all included in the Windows SDK.

### Linux (GCC 3.x / 4.x)

```bash
tar -xzf renderx-0.9.4.tar.gz
cd renderx-0.9.4
make
```

You might need to install Mesa or proper GL headers first:

```bash
# Debian/Ubuntu
apt-get install libgl1-mesa-dev libglu1-mesa-dev libx11-dev

# Red Hat / Fedora
rpm -i mesa-libGL-devel xorg-x11-devel
```

Run with:

```bash
./renderx demo/sponza.scene
```

---

## :: USAGE ::

### Loading and rendering a scene

```c
#include "renderx.h"

int main(void) {
    RX_Context *ctx = RX_Init(800, 600, RX_MODE_OPENGL);
    if (!ctx) {
        printf("Failed to init RenderX!\n");
        return 1;
    }

    RX_Scene *scene = RX_LoadScene("data/myscene.scene");
    RX_Camera *cam  = RX_CreateCamera(ctx);
    RX_CameraSetPos(cam, 0.0f, 5.0f, -10.0f);

    while (!RX_WindowShouldClose(ctx)) {
        RX_BeginFrame(ctx);
        RX_RenderScene(ctx, scene, cam);
        RX_EndFrame(ctx);
        RX_PollEvents(ctx);
    }

    RX_DestroyScene(scene);
    RX_Shutdown(ctx);
    return 0;
}
```

### Switching to software mode

```c
RX_Context *ctx = RX_Init(640, 480, RX_MODE_SOFTWARE);
```

Software mode is slower but will run anywhere. On a 1GHz CPU you can expect roughly 15–25 FPS for a moderately complex scene at 640x480.

---

## :: FILE FORMATS ::

| Format | Import | Export | Notes |
|--------|--------|--------|-------|
| `.OBJ` | ✅ | ✅ | Most reliable format |
| `.3DS` | ✅ | ❌ | Tested with 3ds Max 4/5 output |
| `.MD2` | ✅ | ❌ | Quake 2 animated models |
| `.BMP` | ✅ | ✅ | Textures only, 24-bit |
| `.TGA` | ✅ | ✅ | Textures, supports alpha channel |
| `.PCX` | ✅ | ❌ | Legacy support, don't use for new stuff |

> **.JPG textures are NOT supported.** I know, I know. It's on the TODO list. Use TGA.

---

## :: CONFIGURATION ::

Edit `renderx.cfg` in the same folder as the executable:

```ini
[Renderer]
mode        = opengl      ; opengl | software
width       = 1024
height      = 768
fullscreen  = 0
vsync       = 1
texfilter   = bilinear    ; nearest | bilinear

[Lighting]
max_lights  = 8
shadows     = 1

[Debug]
show_fps    = 1
wireframe   = 0
log_file    = renderx.log
```

---

## :: KNOWN ISSUES / BUGS ::

- **[ BUG #14 ]** Stencil shadows flicker when camera is inside shadow volume. Workaround: disable shadows in cfg.
- **[ BUG #21 ]** `.3DS` loader crashes on files exported from 3ds Max 7. Use OBJ export instead.
- **[ BUG #27 ]** Software renderer has incorrect alpha sorting when two transparent objects overlap.
- **[ BUG #31 ]** Linux: window resize causes texture corruption until scene is reloaded. Known X11 issue.
- **[ BUG #35 ]** Very large scenes (500k+ triangles) cause integer overflow in BSP builder. Being investigated.

> Found a bug not listed here? Post in the **Issues** tab or email me directly (address in the source code header).

---

## :: BENCHMARKS ::

Tested scene: `bench_outdoor.scene` — 42,000 triangles, 4 dynamic lights, 640x480

| System | Mode | FPS |
|--------|------|-----|
| Pentium 4 2.4GHz + GeForce4 Ti 4200 | OpenGL | 87 |
| Pentium 4 2.4GHz (no GPU) | Software | 18 |
| Athlon XP 2000+ + Radeon 9600 | OpenGL | 104 |
| Pentium III 800MHz + GeForce2 MX | OpenGL | 34 |
| Pentium III 800MHz (no GPU) | Software | 6 |

> All benchmarks done on Windows XP SP2, drivers up to date as of Q1 2004.

---

## :: DIRECTORY STRUCTURE ::

```
renderx/
├── src/
│   ├── core/          <- Engine core, math, memory
│   ├── renderer/      <- OpenGL + software renderers
│   ├── scene/         <- Scene graph, BSP, lights
│   ├── loader/        <- Mesh and texture loaders
│   └── platform/      <- Win32 / Linux backends
├── include/
│   └── renderx.h      <- Public API header
├── demo/              <- Demo scenes and assets
├── docs/
│   ├── API.txt        <- Full API reference (plain text)
│   └── screenshots/   <- .BMP screenshots
├── tools/
│   └── bsp_compiler/  <- Offline BSP tree builder
├── bin/               <- Compiled output goes here
├── renderx.cfg        <- Default config file
├── Makefile
├── renderx.dsw        <- MSVC 6.0 workspace
├── renderx.sln        <- VS .NET 2003 solution
└── README.md          <- You are here
```

---

## :: TODO / ROADMAP ::

Things I want to add before a v1.0 release:

- [ ] Fix stencil shadow volumes properly
- [ ] JPG texture support (using libjpeg)
- [ ] Proper specular highlights per-pixel (currently per-vertex)
- [ ] Particle system
- [ ] Simple scripting (maybe Lua?)
- [ ] Better LOD system
- [ ] Cube map / environment reflections
- [ ] Lightmap baking tool
- [ ] Properly document the BSP compiler

---

## :: CHANGELOG ::

### v0.9.4 (current)
- Added MD2 model loading (Quake 2 animated meshes)
- Fixed major memory leak in texture cache
- Linux: Fixed crash on exit when using software mode
- Improved bilinear filtering performance (~12% faster)
- Added `show_fps` config option

### v0.9.3
- First public release
- OpenGL and software render paths both working
- OBJ and 3DS loading stable
- BSP scene management working

### v0.9.0 – v0.9.2
- Private development versions, not released

---

## :: LICENSE ::

MIT License. Do whatever you want with it. Credit appreciated but not required.

See `LICENSE.txt`.

---

## :: CREDITS ::

Written by **[JackTheGothGuy]**  
Started: late 2025 
This project would not exist without:

- *"3D Game Engine Design"* by David Eberly — absolutely essential book
- The folks on **GameDev.net** forums, especially in the Graphics & GPU subforum
- **Nehe's OpenGL tutorials** — the best resource on the web for learning OpenGL
- **flipCode.com** — RIP, best gamedev site of its era

---

## :: CONTACT ::

Post an **Issue** here on GitHub, or find me on GameDev.net (username: `rxdev`).

> *"It renders triangles. What more do you want?"*

---

*Last updated: 2004 — RenderX Engine Project*
