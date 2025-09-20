# Solar System (OpenGL 3.3, GLFW/GLEW/GLM)

A real-time Solar System demo created for **CGD6214 – Interactive Graphics**.  
It renders the Sun and eight planets with textures, moons (Earth→Moon, Jupiter→Europa), Saturn’s ring, orbit lines, and a starfield sky. The scene uses **Blinn–Phong** lighting with an emissive Sun and supports **three camera modes** (Orbit, Free, Focus), **time scaling**, **pause**, **FOV control**, and HUD/FPS instrumentation.

<p align="center">
  <img src="https://i.imgur.com/H2aZIOK.png" alt="Solar System screenshot" width="860">
</p>

---

## ▶️ Demo Video

**Watch:** https://youtu.be/UaMqDelHW7U

<p align="center">
  <a href="https://youtu.be/UaMqDelHW7U">
    <img src="https://img.youtube.com/vi/UaMqDelHW7U/hqdefault.jpg" alt="YouTube demo thumbnail" width="860">
  </a>
</p>

---

## Features

- Textured spheres for the Sun and 8 planets
- **Hierarchy:** Earth→Moon, Jupiter→Europa, Saturn→Ring
- **Cameras:** Orbit, Free (RMB look + WASD/QE), Focus (N/P target + Z/X focus distance)
- **Lighting:** Blinn–Phong (ambient + diffuse + specular) + emissive Sun
- **FX:** Starfield sky (inside-out sphere, depth write off), orbit lines
- **Controls:** Time scale, pause, FOV, stars/orbits toggles, fullscreen
- **HUD/Perf:** HUD circle, FPS in window title and console

---

## Controls

| Action | Keys / Mouse |
|---|---|
| Camera mode | `1` Orbit, `2` Free, `3` Focus |
| Change focus target | `N` next, `P` previous |
| Focus distance (Focus cam) | `Z` / `X` |
| Move (Free cam) | `WASD` + `Q/E` (RMB to look) |
| Zoom or FOV | Mouse wheel |
| Time scale | `[` slower, `]` faster |
| Pause / Resume | `Space` |
| FOV | `-` and `=` |
| Toggles | `H` orbit lines, `B` starfield |
| Fullscreen | `F11` or `Alt+Enter` |
| Quit | `Esc` |

> FPS is displayed in the window title and printed to the console approximately 4× per second.

---

## Build & Run

### Dependencies
- OpenGL 3.3 core profile
- GLFW (window & input)
- GLEW (OpenGL extensions)
- GLM (math)
- `stb_image.h` (textures), `stb_easy_font.h` (simple text)

### CMake + vcpkg (Windows/macOS/Linux)
```bash
# Install deps with vcpkg (example)
vcpkg install glfw3 glew glm

# Configure & build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
