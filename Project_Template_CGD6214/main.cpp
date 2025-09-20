#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>   
#include <cstdio>    

// ===================== FORCE CONSOLE (Windows) =====================
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
static void open_console() {
    if (!GetConsoleWindow()) {
        AllocConsole();
        FILE* fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
    }
}
#ifdef _MSC_VER
#pragma comment(linker, "/SUBSYSTEM:CONSOLE")
#endif
#else
static void open_console() {}
#endif

static void print_controls() {
    std::cout <<
        "Controls:\n"
        "  1 Orbit cam, 2 Free cam (RMB look + WASD/QE), 3 Focus cam (N/P cycle)\n"
        "  Mouse wheel: zoom/FOV   |  H: toggle orbit lines   |  B: toggle stars\n"
        "  [ / ] time speed   |  Space pause/resume   |  F11 or Alt+Enter fullscreen\n"
        "  - / = FOV          |  Z/X focus distance   |  ESC quit\n\n";
}

// ===================== GLOBAL STATE =====================
enum CamMode { ORBIT = 0, FREE = 1, FOCUS = 2 };
CamMode camMode = ORBIT;

bool showOrbits = true, showStars = true, paused = false;
float timeScale = 1.0f;
float fovDeg = 45.0f;
int winW = 1280, winH = 720;

// orbit cam
float camYaw = glm::radians(0.0f);
float camPitch = glm::radians(15.0f);
float camDist = 45.0f;

// free cam
glm::vec3 freePos(0, 10, 60);
float freeYaw = 0.0f, freePitch = 0.0f;

// mouse (shared)
bool rmbDown = false;
double lastX = 0.0, lastY = 0.0;

// focus cam
int focusIndex = 0;
float focusDist = 12.0f;

// fullscreen tracking
bool fullscreen = false;
int savedX = 100, savedY = 100, savedW = 1280, savedH = 720;

// ===================== SHADERS =====================
static const char* vsSrc = R"(#version 330 core
layout (location=0) in vec3 aPos;
layout (location=1) in vec3 aNormal;
layout (location=2) in vec2 aUV;
uniform mat4 model, view, projection;
out vec3 FragPos; out vec3 Normal; out vec2 UV;
void main(){
  FragPos = vec3(model * vec4(aPos,1.0));
  Normal  = mat3(transpose(inverse(model))) * aNormal;
  UV = aUV;
  gl_Position = projection * view * vec4(FragPos,1.0);
})";

static const char* fsSrc = R"(#version 330 core
out vec4 FragColor;
in vec3 FragPos; in vec3 Normal; in vec2 UV;
uniform vec3 lightPos, lightColor, viewPos;
uniform sampler2D albedo;
uniform bool useTexture;
uniform vec3 baseColor, emissive;
uniform float shininess;
uniform float ks;
void main(){
  vec3 color = useTexture ? texture(albedo, UV).rgb : baseColor;
  vec3 N = normalize(Normal);
  vec3 L = normalize(lightPos - FragPos);
  vec3 V = normalize(viewPos - FragPos);
  vec3 H = normalize(L + V);
  float diff = max(dot(N,L),0.0);
  float spec = pow(max(dot(N,H),0.0), max(shininess, 1.0));
  vec3 ambient  = 0.05 * lightColor;
  vec3 diffuse  = diff * lightColor;
  vec3 specular = ks * spec * lightColor;
  vec3 lit = (ambient + diffuse + specular) * color;
  FragColor = vec4(lit + emissive * color, 1.0);
})";

static const char* vsLine = R"(#version 330 core
layout (location=0) in vec3 aPos;
uniform mat4 mvp;
void main(){ gl_Position = mvp * vec4(aPos,1.0); })";

static const char* fsLine = R"(#version 330 core
out vec4 FragColor;
uniform vec3 color;
void main(){ FragColor = vec4(color,1.0); })";

// ===================== GL HELPERS =====================
static GLuint makeShader(GLenum t, const char* s) {
    GLuint sh = glCreateShader(t); glShaderSource(sh, 1, &s, nullptr); glCompileShader(sh);
    GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(sh, 1024, nullptr, log); std::cerr << "Shader: " << log << "\n"; }
    return sh;
}
static GLuint makeProgram(const char* vsrc, const char* fsrc) {
    GLuint p = glCreateProgram(); GLuint v = makeShader(GL_VERTEX_SHADER, vsrc), f = makeShader(GL_FRAGMENT_SHADER, fsrc);
    glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(p, 1024, nullptr, log); std::cerr << "Link: " << log << "\n"; }
    glDeleteShader(v); glDeleteShader(f); return p;
}
static GLuint loadTexture2D(const char* path, bool flipY = true) {
    stbi_set_flip_vertically_on_load(flipY);
    int w, h, ch; unsigned char* data = stbi_load(path, &w, &h, &ch, 0);
    if (!data) { std::cerr << "Texture failed: " << path << " (" << stbi_failure_reason() << ")\n"; return 0; }
    GLenum fmt = ch == 1 ? GL_RED : ch == 3 ? GL_RGB : GL_RGBA;
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    stbi_image_free(data); return t;
}

// ===================== MESHES =====================
struct Mesh { GLuint VAO = 0, VBO = 0, EBO = 0; int indexCount = 0; };
struct Vtx { glm::vec3 p; glm::vec3 n; glm::vec2 uv; };

static Mesh buildSphere(int stacks, int slices, float r) {
    std::vector<Vtx> v; std::vector<unsigned int> idx;
    for (int i = 0; i <= stacks; ++i) {
        float fv = (float)i / stacks, phi = fv * glm::pi<float>();
        float y = cosf(phi), rr = sinf(phi);
        for (int j = 0; j <= slices; ++j) {
            float fu = (float)j / slices, th = fu * glm::two_pi<float>();
            float x = rr * cosf(th), z = rr * sinf(th);
            glm::vec3 n = glm::normalize(glm::vec3(x, y, z));
            v.push_back({ r * glm::vec3(x,y,z), n, glm::vec2(fu,1.0f - fv) });
        }
    }
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            int r1 = i * (slices + 1), r2 = (i + 1) * (slices + 1);
            idx.push_back(r1 + j); idx.push_back(r2 + j); idx.push_back(r2 + j + 1);
            idx.push_back(r1 + j); idx.push_back(r2 + j + 1); idx.push_back(r1 + j + 1);
        }
    }
    Mesh m; m.indexCount = (int)idx.size();
    glGenVertexArrays(1, &m.VAO); glGenBuffers(1, &m.VBO); glGenBuffers(1, &m.EBO);
    glBindVertexArray(m.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vtx), v.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)offsetof(Vtx, n)); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)offsetof(Vtx, uv)); glEnableVertexAttribArray(2);
    glBindVertexArray(0); return m;
}

static Mesh buildRing(int segments, float innerR, float outerR) {
    std::vector<Vtx> v; std::vector<unsigned int> idx;
    for (int i = 0; i <= segments; ++i) {
        float u = (float)i / segments, th = u * glm::two_pi<float>(), c = cosf(th), s = sinf(th);
        v.push_back({ glm::vec3(outerR * c,0,outerR * s),glm::vec3(0,1,0),glm::vec2(u,1) });
        v.push_back({ glm::vec3(innerR * c,0,innerR * s),glm::vec3(0,1,0),glm::vec2(u,0) });
        if (i < segments) {
            int b = i * 2; idx.push_back(b); idx.push_back(b + 1); idx.push_back(b + 2);
            idx.push_back(b + 1); idx.push_back(b + 3); idx.push_back(b + 2);
        }
    }
    Mesh m; m.indexCount = (int)idx.size();
    glGenVertexArrays(1, &m.VAO); glGenBuffers(1, &m.VBO); glGenBuffers(1, &m.EBO);
    glBindVertexArray(m.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vtx), v.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)offsetof(Vtx, n)); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)offsetof(Vtx, uv)); glEnableVertexAttribArray(2);
    glBindVertexArray(0); return m;
}

static Mesh buildOrbitLine(int segments, float r) {
    std::vector<glm::vec3> p; std::vector<unsigned int> idx;
    for (int i = 0; i < segments; ++i) {
        float u = (float)i / segments, th = u * glm::two_pi<float>();
        p.push_back(glm::vec3(r * cosf(th), 0, r * sinf(th)));
        idx.push_back(i); idx.push_back((i + 1) % segments);
    }
    Mesh m; m.indexCount = (int)idx.size();
    glGenVertexArrays(1, &m.VAO); glGenBuffers(1, &m.VBO); glGenBuffers(1, &m.EBO);
    glBindVertexArray(m.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
    glBufferData(GL_ARRAY_BUFFER, p.size() * sizeof(glm::vec3), p.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0); glEnableVertexAttribArray(0);
    glBindVertexArray(0); return m;
}

// ===================== PLANET =====================
struct Planet {
    Mesh mesh; GLuint tex = 0;
    float orbitRadius = 0, orbitSpeed = 0, spinSpeed = 0;
    float orbitAngle = 0, spinAngle = 0;
};

// ===================== CAMERA HELPERS =====================
static glm::vec3 orbitCamPos() {
    float cp = cosf(camPitch), sp = sinf(camPitch), sy = sinf(camYaw), cy = cosf(camYaw);
    return { camDist * cp * sy, camDist * sp, camDist * cp * cy };
}
static void toggle_fullscreen(GLFWwindow* w) {
    if (!fullscreen) {
        glfwGetWindowPos(w, &savedX, &savedY);
        glfwGetWindowSize(w, &savedW, &savedH);
        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        const GLFWvidmode* vm = glfwGetVideoMode(mon);
        glfwSetWindowMonitor(w, mon, 0, 0, vm->width, vm->height, vm->refreshRate);
        fullscreen = true;
    }
    else {
        glfwSetWindowMonitor(w, nullptr, savedX, savedY, savedW, savedH, 0);
        fullscreen = false;
    }
}

// ===================== INPUT CALLBACKS =====================
static void scroll_cb(GLFWwindow*, double /*xoff*/, double yoff) {
    if (camMode == FREE) {                          // FOV in FREE camera
        fovDeg = glm::clamp(fovDeg - (float)yoff, 20.0f, 90.0f);
        return;
    }
    if (camMode == FOCUS) {                         // Focus distance in FOCUS camera
        focusDist = glm::clamp(focusDist - (float)yoff * 2.0f, 3.0f, 400.0f);
        return;
    }
    camDist = glm::clamp(camDist - (float)yoff * 2.0f, 5.0f, 400.0f); // Orbit distance in ORBIT camera
}
static void mouse_btn_cb(GLFWwindow* w, int button, int action, int /*mods*/) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) { rmbDown = true; glfwGetCursorPos(w, &lastX, &lastY); }
        else rmbDown = false;
    }
}
static void cursor_cb(GLFWwindow*, double x, double y) {
    if (!rmbDown) return;
    float dx = float(x - lastX), dy = float(y - lastY); lastX = x; lastY = y;
    if (camMode != FREE) {
        camYaw += dx * 0.005f; camPitch -= dy * 0.005f;
        camPitch = glm::clamp(camPitch, glm::radians(-89.0f), glm::radians(89.0f));
    }
    else {
        freeYaw += dx * 0.002f; freePitch -= dy * 0.002f;
        freePitch = glm::clamp(freePitch, glm::radians(-85.0f), glm::radians(85.0f));
    }
}
static void key_cb(GLFWwindow* w, int key, int /*sc*/, int action, int mods) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    switch (key) {
    case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, true); break;
    case GLFW_KEY_F11: toggle_fullscreen(w); break;
    case GLFW_KEY_ENTER: if (mods & GLFW_MOD_ALT) toggle_fullscreen(w); break;

    case GLFW_KEY_1: camMode = ORBIT; break;
    case GLFW_KEY_2: camMode = FREE;  break;
    case GLFW_KEY_3: camMode = FOCUS; break;
    case GLFW_KEY_N: focusIndex = (focusIndex + 1) % 9; break;
    case GLFW_KEY_P: focusIndex = (focusIndex + 8) % 9; break;

    case GLFW_KEY_H: showOrbits = !showOrbits; std::cout << "Orbit lines: " << (showOrbits ? "ON" : "OFF") << "\n"; break;
    case GLFW_KEY_B: showStars = !showStars;  std::cout << "Stars: " << (showStars ? "ON" : "OFF") << "\n"; break;

    case GLFW_KEY_LEFT_BRACKET:  timeScale = std::max(0.0f, timeScale - 0.25f); std::cout << "timeScale=" << timeScale << "\n"; break;
    case GLFW_KEY_RIGHT_BRACKET: timeScale += 0.25f; std::cout << "timeScale=" << timeScale << "\n"; break;
    case GLFW_KEY_MINUS:  fovDeg = glm::clamp(fovDeg - 1.0f, 20.0f, 90.0f); break;
    case GLFW_KEY_EQUAL:  fovDeg = glm::clamp(fovDeg + 1.0f, 20.0f, 90.0f); break;

    case GLFW_KEY_Z: if (camMode == FOCUS) focusDist = std::max(3.0f, focusDist - 2.0f); break;
    case GLFW_KEY_X: if (camMode == FOCUS) focusDist = std::min(400.0f, focusDist + 2.0f); break;
    }
}

// ===================== MAIN =====================
int main() {
    open_console();
    print_controls();

    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* win = glfwCreateWindow(winW, winH, "Solar System", nullptr, nullptr);
    if (!win) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(win);

    // --- Important for core profile + GLEW ---
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { std::cerr << "GLEW init failed\n"; return -1; }
    glGetError(); // swallow benign error from GLEW in core profile

    glfwSetScrollCallback(win, scroll_cb);
    glfwSetMouseButtonCallback(win, mouse_btn_cb);
    glfwSetCursorPosCallback(win, cursor_cb);
    glfwSetKeyCallback(win, key_cb);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLuint prog = makeProgram(vsSrc, fsSrc);
    GLuint lineProg = makeProgram(vsLine, fsLine);

    // uniforms
    GLint uModel = glGetUniformLocation(prog, "model");
    GLint uView = glGetUniformLocation(prog, "view");
    GLint uProj = glGetUniformLocation(prog, "projection");
    GLint uLightPos = glGetUniformLocation(prog, "lightPos");
    GLint uLightColor = glGetUniformLocation(prog, "lightColor");
    GLint uViewPos = glGetUniformLocation(prog, "viewPos");
    GLint uUseTex = glGetUniformLocation(prog, "useTexture");
    GLint uBase = glGetUniformLocation(prog, "baseColor");
    GLint uEmis = glGetUniformLocation(prog, "emissive");
    GLint uSh = glGetUniformLocation(prog, "shininess");
    GLint uKs = glGetUniformLocation(prog, "ks");
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "albedo"), 0);

    // geometry
    Mesh sunMesh = buildSphere(48, 96, 2.8f);
    Mesh earthMesh = buildSphere(40, 80, 1.0f);
    Mesh smallMesh = buildSphere(32, 64, 0.6f);
    Mesh tinyMesh = buildSphere(28, 56, 0.35f);
    Mesh bigMesh = buildSphere(48, 96, 2.0f);
    Mesh ringMesh = buildRing(256, 1.8f, 3.2f);
    Mesh skyMesh = buildSphere(24, 48, 300.0f);
    Mesh hudCircle = buildOrbitLine(128, 1.0f); // unit circle; scaled in 2D

    std::vector<Mesh> orbitLines = {
        buildOrbitLine(256, 6.0f),  buildOrbitLine(256, 9.0f),
        buildOrbitLine(256,12.0f), buildOrbitLine(256,15.0f),
        buildOrbitLine(256,20.0f), buildOrbitLine(256,26.0f),
        buildOrbitLine(256,32.0f), buildOrbitLine(256,38.0f)
    };

    // textures (put images in ./textures/)
    GLuint texSun = loadTexture2D("textures/sun.jpg");
    GLuint texMercury = loadTexture2D("textures/mercury.jpg");
    GLuint texVenus = loadTexture2D("textures/venus.jpg");
    GLuint texEarth = loadTexture2D("textures/earth_day.jpg");
    GLuint texMoon = loadTexture2D("textures/moon.jpg");
    GLuint texMars = loadTexture2D("textures/mars.jpg");
    GLuint texJupiter = loadTexture2D("textures/jupiter.jpg");
    GLuint texSaturn = loadTexture2D("textures/saturn.jpg");
    GLuint texRing = loadTexture2D("textures/saturnRing.png");
    GLuint texUranus = loadTexture2D("textures/uranus.jpg");
    GLuint texNeptune = loadTexture2D("textures/neptune.jpg");
    GLuint texStars = loadTexture2D("textures/stars.jpg");

    // planets
    Planet sun{ sunMesh,  texSun,     0,  0, 10 };
    Planet mercury{ tinyMesh, texMercury, 6, 48,  6 };
    Planet venus{ smallMesh,texVenus,   9, 35, -2 };
    Planet earth{ earthMesh,texEarth,  12, 30, 50 };
    Planet moon{ tinyMesh, texMoon,    2, 80, 20 };
    Planet mars{ smallMesh,texMars,   15, 24, 40 };
    Planet jupiter{ bigMesh,  texJupiter,20, 13, 80 };
    Planet saturn{ bigMesh,  texSaturn, 26, 10, 70 };
    Planet uranus{ buildSphere(44,88,1.3f),  texUranus, 32, 7, 50 };
    Planet neptune{ buildSphere(44,88,1.25f), texNeptune,38, 5, 40 };

    // Second moon: Europa around Jupiter
    Planet europa{ tinyMesh, texMoon /*swap if you have europa texture*/, 3.0f, 90.0f, 15.0f };

    float last = (float)glfwGetTime();
    bool prevSpace = false;

    // ===== FPS state =====
    double fpsAccum = 0.0;
    int    fpsFrames = 0;
    double fpsValue = 0.0;     // refreshed every 0.5s

    // Make console pretty numbers
    std::cout.setf(std::ios::fixed);
    std::cout << std::setprecision(1);

    while (!glfwWindowShouldClose(win)) {
        float now = (float)glfwGetTime();
        float dt = now - last; last = now;

        // ===== FPS accumulate & print to CMD =====
        fpsAccum += dt;
        fpsFrames += 1;
        if (fpsAccum >= 0.5) {                     // print twice per second
            fpsValue = fpsFrames / fpsAccum;
            fpsAccum = 0.0;
            fpsFrames = 0;

            // Update window title as a fallback
            char title[128];
            std::snprintf(title, sizeof(title), "Solar System  |  FPS: %.1f", fpsValue);
            glfwSetWindowTitle(win, title);

            // Print one-line live readout in console (overwrites same line)
            std::cout << "\rFPS: " << fpsValue
                << " | Mode: " << (camMode == ORBIT ? "Orbit" : camMode == FREE ? "Free" : "Focus")
                << " | FocusDist: " << focusDist
                << " | FOV: " << fovDeg
                << "          " << std::flush;
        }

        // spacebar pause (edge-detected)
        int sp = glfwGetKey(win, GLFW_KEY_SPACE);
        if (sp == GLFW_PRESS && !prevSpace) {
            paused = !paused;
            std::cout << (paused ? "\nPaused\n" : "\nRunning\n");
        }
        prevSpace = (sp == GLFW_PRESS);

        // keyboard nudge for orbit cam
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS && camMode != FREE) camYaw -= 0.04f;
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS && camMode != FREE) camYaw += 0.04f;
        if (glfwGetKey(win, GLFW_KEY_Q) == GLFW_PRESS && camMode != FREE) camPitch += 0.03f;
        if (glfwGetKey(win, GLFW_KEY_E) == GLFW_PRESS && camMode != FREE) camPitch -= 0.03f;

        float adv = paused ? 0.0f : (dt * timeScale);

        // animate
        sun.spinAngle += sun.spinSpeed * adv;
        mercury.orbitAngle += mercury.orbitSpeed * adv; mercury.spinAngle += mercury.spinSpeed * adv;
        venus.orbitAngle += venus.orbitSpeed * adv;   venus.spinAngle += venus.spinSpeed * adv;
        earth.orbitAngle += earth.orbitSpeed * adv;   earth.spinAngle += earth.spinSpeed * adv;
        moon.orbitAngle += moon.orbitSpeed * adv;    moon.spinAngle += moon.spinSpeed * adv;
        mars.orbitAngle += mars.orbitSpeed * adv;    mars.spinAngle += mars.spinSpeed * adv;
        jupiter.orbitAngle += jupiter.orbitSpeed * adv; jupiter.spinAngle += jupiter.spinSpeed * adv;
        saturn.orbitAngle += saturn.orbitSpeed * adv;  saturn.spinAngle += saturn.spinSpeed * adv;
        uranus.orbitAngle += uranus.orbitSpeed * adv;  uranus.spinAngle += uranus.spinSpeed * adv;
        neptune.orbitAngle += neptune.orbitSpeed * adv; neptune.spinAngle += neptune.spinSpeed * adv;
        europa.orbitAngle += europa.orbitSpeed * adv;  europa.spinAngle += europa.spinSpeed * adv;

        // camera build
        glm::vec3 eye, target(0, 0, 0), up(0, 1, 0);
        if (camMode == ORBIT) {
            eye = orbitCamPos();
        }
        else if (camMode == FOCUS) {
            Planet* arr[9] = { &sun,&mercury,&venus,&earth,&mars,&jupiter,&saturn,&uranus,&neptune };
            Planet* f = arr[std::max(0, std::min(8, focusIndex))];
            glm::vec3 p(f->orbitRadius * cosf(glm::radians(f->orbitAngle)),
                0.0f,
                f->orbitRadius * sinf(glm::radians(f->orbitAngle)));
            target = p;
            float cp = cosf(camPitch), spv = sinf(camPitch), sy = sinf(camYaw), cy = cosf(camYaw);
            glm::vec3 offset(focusDist * cp * sy, focusDist * spv, focusDist * cp * cy);
            eye = p + offset;
        }
        else { // FREE
            const float move = (rmbDown ? 25.0f : 8.0f) * dt;
            glm::vec3 fwd(sinf(freeYaw), 0, -cosf(freeYaw));
            glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
            if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) freePos += fwd * move;
            if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) freePos -= fwd * move;
            if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) freePos -= right * move;
            if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) freePos += right * move;
            if (glfwGetKey(win, GLFW_KEY_Q) == GLFW_PRESS) freePos.y += move;
            if (glfwGetKey(win, GLFW_KEY_E) == GLFW_PRESS) freePos.y -= move;
            glm::vec3 dir(cosf(freePitch) * sinf(freeYaw), sinf(freePitch), -cosf(freePitch) * cosf(freeYaw));
            eye = freePos; target = freePos + dir;
        }

        camPitch = glm::clamp(camPitch, glm::radians(-89.0f), glm::radians(89.0f));
        camDist = glm::clamp(camDist, 5.0f, 400.0f);

        glm::mat4 view = glm::lookAt(eye, target, up);
        glm::mat4 proj = glm::perspective(glm::radians(fovDeg), (float)winW / winH, 0.1f, 1000.0f);

        glViewport(0, 0, winW, winH);
        glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // starfield sky (inside-out)
        if (showStars) {
            glDepthMask(GL_FALSE);
            glCullFace(GL_FRONT);
            glm::mat4 M = glm::translate(glm::mat4(1), eye);
            glUseProgram(prog);
            glUniformMatrix4fv(uView, 1, GL_FALSE, glm::value_ptr(glm::mat4(1)));
            glUniformMatrix4fv(uProj, 1, GL_FALSE, glm::value_ptr(proj));
            glUniformMatrix4fv(uModel, 1, GL_FALSE, glm::value_ptr(M));
            glUniform3fv(uLightPos, 1, glm::value_ptr(glm::vec3(0)));
            glUniform3fv(uLightColor, 1, glm::value_ptr(glm::vec3(1)));
            glUniform3fv(uViewPos, 1, glm::value_ptr(eye));
            glUniform1i(uUseTex, GL_TRUE);
            glUniform3f(uBase, 1, 1, 1);
            glUniform3f(uEmis, 1, 1, 1);
            glUniform1f(uSh, 32.0f);
            glUniform1f(uKs, 0.0f);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texStars);
            glBindVertexArray(skyMesh.VAO);
            glDrawElements(GL_TRIANGLES, skyMesh.indexCount, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
            glCullFace(GL_BACK);
            glDepthMask(GL_TRUE);
        }

        // main shader
        glUseProgram(prog);
        glUniformMatrix4fv(uView, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(uProj, 1, GL_FALSE, glm::value_ptr(proj));
        glUniform3fv(uLightPos, 1, glm::value_ptr(glm::vec3(0, 0, 0)));
        glUniform3fv(uLightColor, 1, glm::value_ptr(glm::vec3(7, 7, 7)));
        glUniform3fv(uViewPos, 1, glm::value_ptr(eye));

        auto setMaterial = [&](float sh, float ks) {
            glUniform1f(uSh, sh);
            glUniform1f(uKs, ks);
            };

        // Sun (emissive)
        glm::mat4 Msun = glm::rotate(glm::mat4(1), glm::radians(sun.spinAngle), glm::vec3(0, 1, 0));
        glUniformMatrix4fv(uModel, 1, GL_FALSE, glm::value_ptr(Msun));
        glUniform1i(uUseTex, GL_TRUE);
        glUniform3f(uBase, 1.0f, 0.8f, 0.2f);
        glUniform3f(uEmis, 2.2f, 2.2f, 2.2f);
        setMaterial(16.0f, 0.0f);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texSun);
        glBindVertexArray(sun.mesh.VAO);
        glDrawElements(GL_TRIANGLES, sun.mesh.indexCount, GL_UNSIGNED_INT, 0);

        auto drawPlanet = [&](Planet& p, float shin, float ks, bool useTex = true) {
            glm::mat4 T = glm::rotate(glm::mat4(1), glm::radians(p.orbitAngle), glm::vec3(0, 1, 0));
            T = glm::translate(T, glm::vec3(p.orbitRadius, 0, 0));
            T = glm::rotate(T, glm::radians(p.spinAngle), glm::vec3(0, 1, 0));
            glUniformMatrix4fv(uModel, 1, GL_FALSE, glm::value_ptr(T));
            glUniform1i(uUseTex, useTex ? GL_TRUE : GL_FALSE);
            glUniform3f(uBase, 1, 1, 1);
            glUniform3f(uEmis, 0, 0, 0);
            setMaterial(shin, ks);
            glBindTexture(GL_TEXTURE_2D, p.tex);
            glBindVertexArray(p.mesh.VAO);
            glDrawElements(GL_TRIANGLES, p.mesh.indexCount, GL_UNSIGNED_INT, 0);
            };

        // Planets
        drawPlanet(mercury, 64.0f, 0.35f);
        drawPlanet(venus, 64.0f, 0.35f);
        drawPlanet(earth, 64.0f, 0.40f);

        // Moon around Earth
        glm::mat4 Me = glm::rotate(glm::mat4(1), glm::radians(earth.orbitAngle), glm::vec3(0, 1, 0));
        Me = glm::translate(Me, glm::vec3(earth.orbitRadius, 0, 0));
        glm::mat4 Mm = glm::rotate(Me, glm::radians(moon.orbitAngle), glm::vec3(0, 1, 0));
        Mm = glm::translate(Mm, glm::vec3(moon.orbitRadius, 0, 0));
        Mm = glm::rotate(Mm, glm::radians(moon.spinAngle), glm::vec3(0, 1, 0));
        glUniformMatrix4fv(uModel, 1, GL_FALSE, glm::value_ptr(Mm));
        glUniform1i(uUseTex, GL_TRUE);
        glUniform3f(uBase, 1, 1, 1);
        glUniform3f(uEmis, 0, 0, 0);
        setMaterial(16.0f, 0.20f);
        glBindTexture(GL_TEXTURE_2D, texMoon);
        glBindVertexArray(moon.mesh.VAO);
        glDrawElements(GL_TRIANGLES, moon.mesh.indexCount, GL_UNSIGNED_INT, 0);

        drawPlanet(mars, 64.0f, 0.35f);
        drawPlanet(jupiter, 32.0f, 0.25f);

        // Europa around Jupiter
        glm::mat4 Mj = glm::rotate(glm::mat4(1), glm::radians(jupiter.orbitAngle), glm::vec3(0, 1, 0));
        Mj = glm::translate(Mj, glm::vec3(jupiter.orbitRadius, 0, 0));
        glm::mat4 Meur = glm::rotate(Mj, glm::radians(europa.orbitAngle), glm::vec3(0, 1, 0));
        Meur = glm::translate(Meur, glm::vec3(europa.orbitRadius, 0, 0));
        Meur = glm::rotate(Meur, glm::radians(europa.spinAngle), glm::vec3(0, 1, 0));
        glUniformMatrix4fv(uModel, 1, GL_FALSE, glm::value_ptr(Meur));
        glUniform1i(uUseTex, GL_TRUE);
        glUniform3f(uBase, 1, 1, 1);
        glUniform3f(uEmis, 0, 0, 0);
        setMaterial(16.0f, 0.20f);
        glBindTexture(GL_TEXTURE_2D, texMoon);
        glBindVertexArray(europa.mesh.VAO);
        glDrawElements(GL_TRIANGLES, europa.mesh.indexCount, GL_UNSIGNED_INT, 0);

        drawPlanet(saturn, 32.0f, 0.25f);

        // Saturn ring
        glm::mat4 Ms = glm::rotate(glm::mat4(1), glm::radians(saturn.orbitAngle), glm::vec3(0, 1, 0));
        Ms = glm::translate(Ms, glm::vec3(saturn.orbitRadius, 0, 0));
        Ms = glm::rotate(Ms, glm::radians(27.0f), glm::vec3(1, 0, 0));
        glUniformMatrix4fv(uModel, 1, GL_FALSE, glm::value_ptr(Ms));
        glUniform1i(uUseTex, GL_TRUE);
        glUniform3f(uBase, 1, 1, 1);
        glUniform3f(uEmis, 0, 0, 0);
        setMaterial(8.0f, 0.05f);
        glBindTexture(GL_TEXTURE_2D, texRing);
        glBindVertexArray(ringMesh.VAO);
        glDrawElements(GL_TRIANGLES, ringMesh.indexCount, GL_UNSIGNED_INT, 0);

        drawPlanet(uranus, 32.0f, 0.25f);
        drawPlanet(neptune, 32.0f, 0.25f);

        // orbit lines
        if (showOrbits) {
            glUseProgram(lineProg);
            GLint uMVP = glGetUniformLocation(lineProg, "mvp");
            GLint uCol = glGetUniformLocation(lineProg, "color");
            glm::mat4 VP = proj * view;
            glm::vec3 col(0.35f, 0.36f, 0.45f);
            for (const Mesh& L : orbitLines) {
                glUniformMatrix4fv(uMVP, 1, GL_FALSE, glm::value_ptr(VP));
                glUniform3fv(uCol, 1, glm::value_ptr(col));
                glBindVertexArray(L.VAO);
                glDrawElements(GL_LINES, L.indexCount, GL_UNSIGNED_INT, 0);
            }
        }

        // ===== HUD: 2D Circle (top-left) =====
        {
            glUseProgram(lineProg);
            GLint uMVP = glGetUniformLocation(lineProg, "mvp");
            GLint uCol = glGetUniformLocation(lineProg, "color");
            glm::mat4 Ortho = glm::ortho(0.0f, float(winW), 0.0f, float(winH));
            glm::vec2 center = { 100.0f, winH - 100.0f };
            float pxR = 80.0f;
            glm::mat4 M2D = glm::translate(glm::mat4(1), glm::vec3(center, 0));
            M2D = glm::scale(M2D, glm::vec3(pxR, pxR, 1));
            glm::mat4 MVP2D = Ortho * M2D;
            glUniformMatrix4fv(uMVP, 1, GL_FALSE, glm::value_ptr(MVP2D));
            glUniform3f(uCol, 0.9f, 0.9f, 0.9f);
            glBindVertexArray(hudCircle.VAO);
            glDrawElements(GL_LINES, hudCircle.indexCount, GL_UNSIGNED_INT, 0);
        }

        glfwSwapBuffers(win);
        glfwPollEvents();

        int w, h; glfwGetFramebufferSize(win, &w, &h);
        winW = w; winH = h;
    }

    std::cout << "\n"; // finish the last inline FPS line with a newline
    glfwTerminate();
    return 0;
}
