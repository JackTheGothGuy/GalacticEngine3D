```
  ________  ________  ___       ________  ________  _________  ___  ________     
|\   ____\|\   __  \|\  \     |\   __  \|\   ____\\___   ___\\  \|\   ____\    
\ \  \___|\ \  \|\  \ \  \    \ \  \|\  \ \  \___\|___ \  \_\ \  \ \  \___|    
 \ \  \  __\ \   __  \ \  \    \ \   __  \ \  \       \ \  \ \ \  \ \  \       
  \ \  \|\  \ \  \ \  \ \  \____\ \  \ \  \ \  \____   \ \  \ \ \  \ \  \____  
   \ \_______\ \__\ \__\ \_______\ \__\ \__\ \_______\  \ \__\ \ \__\ \_______\
    \|_______|\|__|\|__|\|_______|\|__|\|__|\|_______|   \|__|  \|__|\|_______|


 ________  ________  ___       ________  ________  _________  ___  ________     
|\   ____\|\   __  \|\  \     |\   __  \|\   ____\\___   ___\\  \|\   ____\    
\ \  \___|\ \  \|\  \ \  \    \ \  \|\  \ \  \___\|___ \  \_\ \  \ \  \___|    
 \ \  \  __\ \   __  \ \  \    \ \   __  \ \  \       \ \  \ \ \  \ \  \       
  \ \  \|\  \ \  \ \  \ \  \____\ \  \ \  \ \  \____   \ \  \ \ \  \ \  \____  
   \ \_______\ \__\ \__\ \_______\ \__\ \__\ \_______\  \ \__\ \ \__\ \_______\
    \|_______|\|__|\|__|\|_______|\|__|\|__|\|_______|   \|__|  \|__|\|_______|
```

> **GalacticEngine v0.2 Alpha** — An OpenGL 3.3 Core 3D rendering engine written in C++

---

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)](.)
[![Version](https://img.shields.io/badge/version-0.2--alpha-blue)](.)
[![License](https://img.shields.io/badge/license-MIT-yellow)](.)
[![Platform](https://img.shields.io/badge/platform-Win32%20%7C%20Linux-lightgrey)](.)
[![OpenGL](https://img.shields.io/badge/OpenGL-3.3%20Core-orange)](.)

---

## :: OVERVIEW ::

**GalacticEngine** is a real-time 3D rendering engine built in C++ targeting OpenGL 3.3 Core Profile. It features a GCN/Wii-era aesthetic with modern post-processing, showcasing 8 distinct material presets arranged in an interactive demo scene. Started as a personal showcase project, it has grown into a capable engine for demonstrating real-time shading techniques.

If you're looking for a clean, hackable engine with a retro game aesthetic and modern rendering techniques — you're in the right place.

> ⚠️ **This is Alpha software.** Some features are incomplete or buggy. Don't use this for anything mission-critical. You have been warned.

---

## :: FEATURES ::

- ✅ OpenGL 3.3 Core Profile renderer
- ✅ 8 material presets (Flat, Smooth, Vertex Colour, Lit Bloom, Normal Mapped, Water, Fresnel, PBR)
- ✅ HDR bloom post-processing (brightness threshold + Gaussian blur + ACES tonemap)
- ✅ GCN/Wii-era aesthetic (cel-shaded flat mode, Twilight Princess × MGS colour palettes)
- ✅ Normal mapping (tangent-space, procedural normal texture)
- ✅ PBR approximation (GGX distribution, Smith geometry, Schlick Fresnel)
- ✅ Water shader (dual-layer additive scrolling sparkle, Fresnel rim)
- ✅ Fresnel shader with animated colour pulse
- ✅ Procedural geometry (sphere + cube fallback meshes)
- ✅ Procedural textures (checker, noise, normal map, water sparkle, brushed metal)
- ✅ FBX/OBJ/etc. mesh loading via Assimp (up to 8 external models)
- ✅ Animated title screen with star field and button UI
- ✅ Free-look camera + orbit camera modes
- ✅ Animated skysphere (GCN-era purple-blue to amber gradient)
- ✅ Bitmap HUD with FPS counter and toggle indicators
- ✅ Win32 and Linux window backends via GLFW
- 🚧 Walk physics / collision *(partially stubbed)*
- 🚧 Free-fly noclip mode *(WIP)*
- ❌ DirectX backend *(not planned)*

---

## :: SCREENSHOTS ::

> *Screenshots taken in the demo scene — 8 models in a circle, all rotating, bloom enabled*

```
[ title_screen.png ]   [ demo_scene.png ]   [ pbr_closeup.png ]
  (see /docs/screenshots/ folder)
```

---

## :: SYSTEM REQUIREMENTS ::

### Minimum
| Component | Requirement |
|-----------|-------------|
| OS        | Windows 10 or Linux (kernel 4.x+) |
| CPU       | Any dual-core from the last 10 years |
| RAM       | 256 MB |
| GPU       | Any GPU with OpenGL 3.3 Core support |
| Disk      | 50 MB free |
| Display   | 1280x800 |

### Recommended
| Component | Requirement |
|-----------|-------------|
| OS        | Windows 10/11 or modern Linux distro |
| CPU       | Quad-core 2GHz+ |
| RAM       | 512 MB |
| GPU       | GTX 460 / Radeon HD 5850 or better |
| Drivers   | Up-to-date OpenGL 3.3 capable drivers |
| Display   | 1920x1080 |

---

## :: BUILDING FROM SOURCE ::

### Dependencies

```
GLFW3, GLEW, Assimp, GLM, stb_image.h
```

Place `stb_image.h` in your project root:
```bash
wget https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
```

### Linux (GCC / Clang)

```bash
sudo apt install libglfw3-dev libglew-dev libassimp-dev libglm-dev
make
```

Or manually:
```bash
g++ engine/renderer.cpp -o GalacticEngine \
    -lGL -lGLEW -lglfw -lassimp \
    $(pkg-config --cflags --libs glfw3 glew assimp) \
    -std=c++17 -O2
```

### Windows (Visual Studio + vcpkg)

```powershell
git clone https://github.com/microsoft/vcpkg
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg install glfw3 glew assimp glm --triplet x64-windows
```

Then open CMake GUI:
- Source: your project folder
- Build: `project/build`
- Set `CMAKE_TOOLCHAIN_FILE` to `C:/vcpkg/scripts/buildsystems/vcpkg.cmake`
- Configure → Generate → Open Project → Build Release x64

Output binary: `build/Release/GalacticEngine.exe`

---

## :: USAGE ::

### Running the demo

```bash
# Default (procedural meshes for all 8 slots)
./GalacticEngine

# With external FBX/OBJ models (up to 8)
./GalacticEngine model1.fbx model2.obj model3.fbx
```

If fewer than 8 models are supplied, procedural sphere/cube meshes fill the remaining slots automatically.

---

## :: KEY BINDINGS ::

| Key | Action |
|-----|--------|
| `F1` | Toggle free-look camera (WASD + Arrow look) |
| `F3` | Toggle HUD |
| `F4` | Toggle bloom |
| `F5` | Toggle textures |
| `F6` | Toggle vertex colours |
| `F7` | Toggle backface culling |
| `WASD` | Move camera |
| `Arrow Keys` | Look / orbit |
| `Shift` | Move faster |
| `ESC` | Quit / return to title screen |

---

## :: MATERIAL PRESETS ::

| Slot | Name | Description |
|------|------|-------------|
| 1 | Flat Shaded | Quantised 4-band cel-shading, GCN feel |
| 2 | Smooth Shaded | Blinn-Phong with smooth normals |
| 3 | Vertex Colour | Per-vertex colour blend with diffuse |
| 4 | Lit Bloom | High specular + pulsing emissive rim, HDR bloom pop |
| 5 | Normal Mapped | Tangent-space normal mapping |
| 6 | Water Sparkle | Dual-layer additive scrolling sparkle + Fresnel |
| 7 | Fresnel | Schlick Fresnel with animated colour shift |
| 8 | PBR | GGX/Smith/Schlick metallic-roughness approximation |

---

## :: FILE FORMATS ::

| Format | Import | Notes |
|--------|--------|-------|
| `.FBX` | ✅ | Primary format, via Assimp |
| `.OBJ` | ✅ | Reliable fallback |
| `.DAE` | ✅ | Collada, via Assimp |
| `.3DS` | ✅ | Via Assimp |
| `.MD5` | ✅ | Via Assimp |
| `.PNG/.JPG/.TGA/.BMP` | ✅ | Textures via stb_image |

> If no model is supplied for a slot, a procedural sphere or cube is generated automatically.

---

## :: CONFIGURATION ::

Render toggles are controlled at runtime via F-keys. No config file yet — planned for v0.3.

Bloom parameters are hardcoded in the source:
```cpp
g_rt.bloomThresh = 0.6f;   // brightness threshold
g_rt.bloomStr    = 1.2f;   // bloom blend strength
```

---

## :: KNOWN ISSUES / BUGS ::

- **[ BUG #1 ]** Normal matrix uses model matrix directly — incorrect for non-uniform scale. Workaround: use uniform scale only.
- **[ BUG #2 ]** Water shader alpha sorting incorrect when two transparent objects overlap.
- **[ BUG #3 ]** Linux: window resize may cause bloom FBO to briefly flicker before resize completes.
- **[ BUG #4 ]** Very large FBX files with multiple mesh nodes may have misaligned tangents after Assimp merge.

> Found a bug not listed here? Post in the **Issues** tab.

---

## :: DIRECTORY STRUCTURE ::

```
GalacticEngine/
├── engine/
│   └── renderer.cpp       <- Everything: shaders, meshes, main loop
├── include/               <- External headers (stb_image.h goes here)
├── docs/
│   └── screenshots/       <- Screenshots
├── bin/                   <- Compiled output
├── Makefile
└── README.md              <- You are here
```

---

## :: TODO / ROADMAP ::

### v0.3
- [ ] Walk physics + AABB collision
- [ ] Config file (resolution, bloom settings)
- [ ] Skybox cubemap support
- [ ] Shadow mapping (PCF soft shadows)

### v0.4
- [ ] Particle system
- [ ] Point light support (currently directional only)
- [ ] Animated models (skeletal, via Assimp)

### v1.0
- [ ] Scene file format
- [ ] Lua scripting
- [ ] Lightmap baking
- [ ] Proper LOD system

---

## :: CHANGELOG ::

### v0.2 Alpha (current)
- Added 8 material presets with full shader suite
- HDR bloom post-processing (threshold + 5-pass Gaussian + ACES tonemap)
- Animated title screen with star field, hover buttons, fade transition
- Procedural textures (checker, noise, normal map, water sparkle, brushed metal)
- Free-look and orbit camera modes
- GCN/Wii-era skysphere gradient
- Bitmap HUD renderer (custom 5×7 font)
- FBX/OBJ/etc. loading via Assimp with procedural fallback meshes

### v0.1 Alpha
- Initial project setup
- Basic OpenGL 3.3 Core context via GLFW/GLEW
- Single flat-shaded mesh rendering

---

## :: LICENSE ::

MIT License. Do whatever you want with it. Credit appreciated but not required.  
See `LICENSE.txt`.

---

## :: CREDITS ::

Written by **JackTheGothGuy**  
Started: 2025

This project would not exist without:
- *"3D Game Engine Design"* by David Eberly
- **LearnOpenGL.com** — the best modern OpenGL resource on the web
- **Nehe's OpenGL tutorials** — a classic
- The **Assimp** and **GLFW** open source projects
- **stb_image** by Sean Barrett

---

## :: CONTACT ::

Post an **Issue** here on GitHub.

> *"Made With Love, And Agony"*

---

*Last updated: 2026 — GalacticEngine Project*
