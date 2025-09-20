# Solar System (OpenGL 3.3)

Real-time Solar System demo for CGD6214. Uses GLFW for window/input, GLEW for OpenGL extensions, GLM for math, and stb headers for textures and simple text.  
Features orbit, free, and focus cameras; hierarchical transforms (Earth→Moon, Jupiter→Europa); Blinn–Phong lighting with an emissive Sun; orbit lines; a starfield sky; HUD circle; time scaling and pause; FOV control; and FPS shown in the title bar and console.

<p align="center">
  <img src="media/hero.png" alt="Solar System screenshot" width="800">
</p>

> Put a screenshot at `media/hero.png`. If you prefer another path or filename, update the `<img src="...">` accordingly.

---

## Features

- Textured spheres for Sun and eight planets
- Moon around Earth and Europa around Jupiter
- Saturn ring with alpha-blended texture
- Orbit lines with reusable GL buffers
- Starfield sky sphere drawn inside-out
- 3 camera modes: Orbit, Free (RMB look + WASD/QE), Focus (cycle N/P, Z/X distance)
- Time scale control, pause/resume, and FOV control
- FPS readout in window title and console

## Controls

| Action | Keys |
|-------|------|
| Camera mode | `1` Orbit, `2` Free, `3` Focus |
| Focus target | `N` next, `P` previous |
| Zoom or FOV | Mouse wheel |
| Move (Free cam) | `WASD` + `Q/E` |
| Focus distance (Focus cam) | `Z` / `X` |
| Time scale | `[` slower, `]` faster |
| Pause / Resume | `Space` |
| FOV | `-` and `=` |
| Toggles | `H` orbit lines, `B` stars |
| Fullscreen | `F11` or `Alt+Enter` |
| Quit | `Esc` |

## Build and run

### Dependencies
- OpenGL 3.3 core profile
- GLFW
- GLEW
- GLM
- `stb_image.h` and `stb_easy_font.h` (header-only)

### CMake (recommended)
```bash
# with vcpkg
vcpkg install glfw3 glew glm

cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
