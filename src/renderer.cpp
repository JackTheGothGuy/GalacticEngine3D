// ============================================================================
//  engine/renderer.cpp  –  OpenGL 3.3 Core Showcase Engine
//  GCN/Wii-era aesthetic meets modern post-processing
//
//  TITLE SCREEN:
//    • Animated logo, Play Demo button, Quit button
//    • Transitions to demo scene
//
//  DEMO SCENE – 8 models in a circle, all rotating:
//    Model 1 : Flat-shaded texture        (mat1_flat.frag)
//    Model 2 : Smooth-shaded texture      (mat2_smooth.frag)
//    Model 3 : Vertex-coloured + smooth   (mat3_vertex_col.frag)
//    Model 4 : Lit texture (HDR bloom)    (mat4_lit_bloom.frag)
//    Model 5 : Normal-mapped texture      (mat5_normal_map.frag)
//    Model 6 : Water sparkle              (mat6_water.frag)
//    Model 7 : Fresnel-lit texture        (mat7_fresnel.frag)
//    Model 8 : PBR metallic/roughness     (mat8_pbr.frag)
//
//  KEY BINDINGS:
//    F1  – Toggle free-look camera (WASD + Arrow look)
//    F3  – Toggle HUD
//    F4  – Toggle bloom
//    F5  – Toggle textures
//    F6  – Toggle vertex colours
//    F7  – Toggle backface culling
//    ESC – Quit / return to title
//
//  Compile via Makefile or manually:
//    g++ engine/renderer.cpp -o TwilightEngine \
//        $(pkg-config --cflags --libs glfw3 glew assimp) \
//        -lGL -lGLEW -lglfw -lassimp -std=c++17 -O2
//
//  Usage: ./TwilightEngine [model1.fbx model2.fbx ... model8.fbx]
//         (Fewer than 8 args → procedural sphere/cube for missing slots.)
// ============================================================================

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// miniaudio — single-header audio engine (place miniaudio.h in include/)
// These defines must come BEFORE the MINIAUDIO_IMPLEMENTATION define.
// Without them only WAV works; MP3/OGG/FLAC are silently unsupported.
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS  // suppress unused backend warnings on MSVC
#undef  MA_ENABLE_ONLY_SPECIFIC_BACKENDS  // actually we want all backends — undo it
#define MA_ENABLE_WASAPI                  // Windows: primary high-quality backend
#define MA_ENABLE_WINMM                   // Windows: fallback
// Built-in codec support (each pulls in the matching dr_* / stb_* header)
#define MA_ENABLE_WAV
#define MA_ENABLE_MP3
#define MA_ENABLE_FLAC
// OGG Vorbis via the bundled stb_vorbis inside miniaudio
#define MA_NO_ENCODING                    // we only decode, never encode
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <functional>

// Platform-specific exe-path headers
#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#elif defined(__linux__)
#  include <unistd.h>        // readlink
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>   // _NSGetExecutablePath
#endif

namespace fs = std::filesystem;
using Clock = std::chrono::high_resolution_clock;

// ============================================================================
//  Shader file loading
// ============================================================================

// ---------------------------------------------------------------------------
//  Shader directory resolution
//
//  Priority order (first hit wins):
//    1. SHADER_DIR env variable      e.g.  SHADER_DIR=C:\te\shaders ./TwilightEngine
//    2. <exe_dir>/shaders/           works when running ./TwilightEngine  (most common)
//    3. <exe_dir>/../shaders/        works from a build/ subdirectory
//    4. cwd/shaders/                 last-resort relative fallback
// ---------------------------------------------------------------------------

static std::string g_shaderDir;

static std::string exeDir(const char* argv0)
{
#if defined(_WIN32)
    wchar_t wbuf[4096] = {};
    DWORD n = GetModuleFileNameW(nullptr, wbuf, (DWORD)(sizeof(wbuf) / sizeof(wchar_t)));
    if (n > 0 && n < (DWORD)(sizeof(wbuf) / sizeof(wchar_t)))
        return fs::path(wbuf).parent_path().string();

#elif defined(__linux__)
    char buf[4096] = {};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        return fs::path(buf).parent_path().string();
    }

#elif defined(__APPLE__)
    char buf[4096] = {};
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0)
        return fs::path(buf).parent_path().string();
#endif

    // Universal fallback: canonicalise argv[0]
    std::error_code ec;
    fs::path p = fs::canonical(argv0, ec);
    if (!ec) return p.parent_path().string();
    return fs::current_path().string();
}

static bool dirHasShaders(const std::string& dir)
{
    // Quick sanity check: at least material.vert must exist
    return fs::exists(fs::path(dir) / "material.vert");
}

static void resolveShaderDir(const char* argv0)
{
    // 1. Environment variable
    const char* env = getenv("SHADER_DIR");
    if (env && dirHasShaders(env)) { g_shaderDir = env; return; }

    std::string exe = exeDir(argv0);
    std::vector<std::string> tried;

    // 2. <exe>/shaders/
    std::string candidate = (fs::path(exe) / "shaders").string();
    tried.push_back(candidate);
    if (dirHasShaders(candidate)) { g_shaderDir = candidate; return; }

    // 3. <exe>/../shaders/
    candidate = (fs::path(exe) / ".." / "shaders").string();
    tried.push_back(candidate);
    if (dirHasShaders(candidate)) { g_shaderDir = candidate; return; }

    // 4. <exe>/../../shaders/  (e.g. exe lives in build/Debug/)
    candidate = (fs::path(exe) / ".." / ".." / "shaders").string();
    tried.push_back(candidate);
    if (dirHasShaders(candidate)) { g_shaderDir = candidate; return; }

    // 5. cwd/shaders/
    candidate = (fs::current_path() / "shaders").string();
    tried.push_back(candidate);
    if (dirHasShaders(candidate)) { g_shaderDir = candidate; return; }

    // Give up — print every path we tried so the user knows exactly what to fix
    fprintf(stderr, "[shader] WARNING: could not locate shaders/ directory.\n");
    fprintf(stderr, "         Looked in:\n");
    for (auto& t : tried)
        fprintf(stderr, "           %s\n", t.c_str());
    fprintf(stderr, "         Fix: set SHADER_DIR=<absolute path to shaders folder>\n"
        "         or copy the shaders/ folder next to the executable.\n");
    g_shaderDir = (fs::path(exe) / "shaders").string(); // best guess
}

// ---------------------------------------------------------------------------

static std::string loadShaderFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) {
        fprintf(stderr, "[shader] Cannot open: %s\n", path.c_str());
        return "";
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static GLuint compileShaderSrc(GLenum type, const char* src, const char* label)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        fprintf(stderr, "[shader] Compile error in %s:\n%s\n", label, log);
    }
    return s;
}

static GLuint buildProgramFromFiles(const std::string& vsPath, const std::string& fsPath)
{
    std::string vsrc = loadShaderFile(vsPath);
    std::string fsrc = loadShaderFile(fsPath);
    if (vsrc.empty() || fsrc.empty()) {
        fprintf(stderr, "[shader] Failed to load %s / %s\n",
            vsPath.c_str(), fsPath.c_str());
        return 0;
    }
    GLuint v = compileShaderSrc(GL_VERTEX_SHADER, vsrc.c_str(), vsPath.c_str());
    GLuint f = compileShaderSrc(GL_FRAGMENT_SHADER, fsrc.c_str(), fsPath.c_str());
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        fprintf(stderr, "[shader] Link error (%s + %s):\n%s\n",
            vsPath.c_str(), fsPath.c_str(), log);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

// Build a program whose shaders live in g_shaderDir
static GLuint buildProgram(const std::string& vertFile, const std::string& fragFile)
{
    return buildProgramFromFiles(
        (fs::path(g_shaderDir) / vertFile).string(),
        (fs::path(g_shaderDir) / fragFile).string());
}

// ============================================================================
//  Math
// ============================================================================
struct Vec2 { float x = 0, y = 0; };
struct Vec3 { float x = 0, y = 0, z = 0; };
struct Vec4 { float x = 0, y = 0, z = 0, w = 1; };
struct Mat4 { float m[16] = {}; };

static Mat4 identity() { Mat4 r; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.f; return r; }
static Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int row = 0;row < 4;++row)
        for (int col = 0;col < 4;++col)
            for (int k = 0;k < 4;++k)
                r.m[col * 4 + row] += a.m[k * 4 + row] * b.m[col * 4 + k];
    return r;
}
static Vec4 mulVec4(const Mat4& m, Vec4 v) {
    return { m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12] * v.w,
             m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13] * v.w,
             m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14] * v.w,
             m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15] * v.w };
}
static Mat4 perspective(float fovY, float aspect, float n, float f) {
    float t = 1.f / tanf(fovY * .5f); Mat4 r;
    r.m[0] = t / aspect; r.m[5] = t;
    r.m[10] = (f + n) / (n - f); r.m[11] = -1.f;
    r.m[14] = 2.f * f * n / (n - f); return r;
}
static Vec3 norm3(Vec3 v) { float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z) + 1e-9f; return { v.x / l,v.y / l,v.z / l }; }
static Vec3 cross3(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y,a.z * b.x - a.x * b.z,a.x * b.y - a.y * b.x }; }
static float dot3(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 add3(Vec3 a, Vec3 b) { return { a.x + b.x,a.y + b.y,a.z + b.z }; }
static Vec3 sub3(Vec3 a, Vec3 b) { return { a.x - b.x,a.y - b.y,a.z - b.z }; }
static Vec3 scale3(Vec3 v, float s) { return { v.x * s,v.y * s,v.z * s }; }
static float len3(Vec3 v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }

static Mat4 lookAt(Vec3 eye, Vec3 cen, Vec3 up) {
    Vec3 f = norm3(sub3(cen, eye));
    Vec3 r = norm3(cross3(f, up));
    Vec3 u = cross3(r, f);
    Mat4 m = identity();
    m.m[0] = r.x; m.m[4] = r.y; m.m[8] = r.z;
    m.m[1] = u.x; m.m[5] = u.y; m.m[9] = u.z;
    m.m[2] = -f.x;m.m[6] = -f.y;m.m[10] = -f.z;
    m.m[12] = -dot3(r, eye); m.m[13] = -dot3(u, eye); m.m[14] = dot3(f, eye);
    return m;
}
static Mat4 viewRotOnly(const Mat4& view) { Mat4 r = view; r.m[12] = r.m[13] = r.m[14] = 0.f; return r; }
static Mat4 scaleMat(float sx, float sy, float sz) { Mat4 m = identity(); m.m[0] = sx; m.m[5] = sy; m.m[10] = sz; return m; }
static Mat4 translateMat(float tx, float ty, float tz) { Mat4 m = identity(); m.m[12] = tx; m.m[13] = ty; m.m[14] = tz; return m; }
static Mat4 rotY(float a) { Mat4 m = identity(); m.m[0] = cosf(a); m.m[8] = sinf(a); m.m[2] = -sinf(a); m.m[10] = cosf(a); return m; }

// ============================================================================
//  Colour helpers
// ============================================================================
static void hsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    h = fmodf(h, 1.f); if (h < 0)h += 1.f;
    int i = (int)(h * 6.f);
    float f = h * 6.f - i, p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    switch (i % 6) {
    case 0:r = v;g = t;b = p;break; case 1:r = q;g = v;b = p;break;
    case 2:r = p;g = v;b = t;break; case 3:r = p;g = q;b = v;break;
    case 4:r = t;g = p;b = v;break; default:r = v;g = p;b = q;break;
    }
}

// ============================================================================
//  Procedural geometry: sphere + cube
// ============================================================================
struct GeoData {
    std::vector<float>    verts; // pos3 nrm3 uv2 col4 tan3 bitan3
    std::vector<unsigned> idxs;
};

static GeoData genSphere(int stacks = 24, int slices = 48) {
    GeoData g;
    const float PI = 3.14159265f;
    for (int i = 0;i <= stacks;++i) {
        float phi = PI * (float)i / (float)stacks;
        float y = cosf(phi), rr = sinf(phi);
        float v = (float)i / (float)stacks;
        for (int j = 0;j <= slices;++j) {
            float th = 2.f * PI * (float)j / (float)slices;
            float x = rr * cosf(th), z = rr * sinf(th);
            float u = (float)j / (float)slices;
            g.verts.insert(g.verts.end(), { x,y,z, x,y,z, u,v,
                1.f,1.f,1.f,1.f,
                -sinf(th),0.f,cosf(th),
                -cosf(phi) * cosf(th),sinf(phi),-cosf(phi) * sinf(th) });
        }
    }
    for (int i = 0;i < stacks;++i)
        for (int j = 0;j < slices;++j) {
            unsigned a = i * (slices + 1) + j;
            unsigned b = a + slices + 1;
            g.idxs.insert(g.idxs.end(), { a,b,a + 1, b,b + 1,a + 1 });
        }
    return g;
}

static GeoData genCube() {
    GeoData g;
    struct Face { Vec3 nrm, tan, bitan; Vec3 corners[4]; };
    Face faces[6];
    faces[0] = { {0,0,1},  {1,0,0}, {0,1,0}, { {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1} } };
    faces[1] = { {0,0,-1}, {-1,0,0},{0,1,0}, { {1,-1,-1},{-1,-1,-1},{-1,1,-1},{1,1,-1} } };
    faces[2] = { {-1,0,0}, {0,0,1}, {0,1,0}, { {-1,-1,-1},{-1,-1,1},{-1,1,1},{-1,1,-1} } };
    faces[3] = { {1,0,0},  {0,0,-1},{0,1,0}, { {1,-1,1},{1,-1,-1},{1,1,-1},{1,1,1} } };
    faces[4] = { {0,1,0},  {1,0,0}, {0,0,-1},{ {-1,1,1},{1,1,1},{1,1,-1},{-1,1,-1} } };
    faces[5] = { {0,-1,0}, {1,0,0}, {0,0,1}, { {-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1} } };
    static const float uvs[4][2] = { {0,0},{1,0},{1,1},{0,1} };
    unsigned base = 0;
    for (auto& face : faces) {
        for (int i = 0;i < 4;++i) {
            auto& c = face.corners[i];
            g.verts.insert(g.verts.end(), {
                c.x,c.y,c.z,
                face.nrm.x,face.nrm.y,face.nrm.z,
                uvs[i][0],uvs[i][1],
                1.f,1.f,1.f,1.f,
                face.tan.x,face.tan.y,face.tan.z,
                face.bitan.x,face.bitan.y,face.bitan.z
                });
        }
        g.idxs.insert(g.idxs.end(), { base,base + 1,base + 2, base,base + 2,base + 3 });
        base += 4;
    }
    return g;
}

// ============================================================================
//  Procedural textures
// ============================================================================
static GLuint makeCheckerTex(int w = 256, int h = 256, int sz = 16,
    unsigned char r1 = 180, unsigned char g1 = 120, unsigned char b1 = 60,
    unsigned char r2 = 80, unsigned char g2 = 50, unsigned char b2 = 25)
{
    std::vector<unsigned char> data(w * h * 4);
    for (int y = 0;y < h;++y) for (int x = 0;x < w;++x) {
        bool c = ((x / sz) + (y / sz)) % 2;
        int i = (y * w + x) * 4;
        data[i] = c ? r1 : r2; data[i + 1] = c ? g1 : g2; data[i + 2] = c ? b1 : b2; data[i + 3] = 255;
    }
    GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return tex;
}

static GLuint makeNormalTex(int w = 256, int h = 256, float bumpScale = 16.f) {
    auto hash = [](int x, int y)->float {
        int n = x + y * 57; n = (n << 13) ^ n;
        return (1.f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.f) * 0.5f + 0.5f;
        };
    auto height = [&](float fx, float fy)->float {
        int ix = (int)fx, iy = (int)fy;
        float tx = fx - ix, ty = fy - iy;
        return hash(ix, iy) * (1 - tx) * (1 - ty) + hash(ix + 1, iy) * tx * (1 - ty)
            + hash(ix, iy + 1) * (1 - tx) * ty + hash(ix + 1, iy + 1) * tx * ty;
        };
    std::vector<unsigned char> data(w * h * 4);
    for (int y = 0;y < h;++y) for (int x = 0;x < w;++x) {
        float fx = (float)x / w * bumpScale, fy = (float)y / h * bumpScale;
        float dx = height(fx + 0.01f, fy) - height(fx - 0.01f, fy);
        float dy = height(fx, fy + 0.01f) - height(fx, fy - 0.01f);
        Vec3 n = norm3({ -dx * 20.f,-dy * 20.f,1.f });
        int i = (y * w + x) * 4;
        data[i] = (unsigned char)((n.x * 0.5f + 0.5f) * 255.f);
        data[i + 1] = (unsigned char)((n.y * 0.5f + 0.5f) * 255.f);
        data[i + 2] = (unsigned char)((n.z * 0.5f + 0.5f) * 255.f);
        data[i + 3] = 255;
    }
    GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return tex;
}

static GLuint makeWaterSparkle(int w = 256, int h = 256) {
    auto hash = [](int x, int y)->float {
        unsigned n = (unsigned)(x * 2654435761u ^ y * 2246822519u);
        return (float)(n & 0xFFFF) / 65535.f;
        };
    std::vector<unsigned char> data(w * h * 4);
    for (int y = 0;y < h;++y) for (int x = 0;x < w;++x) {
        float v = hash(x, y);
        v = powf(fmaxf(0.f, v - 0.85f) / 0.15f, 2.f);
        unsigned char c = (unsigned char)(fminf(v, 1.f) * 255.f);
        int i = (y * w + x) * 4;
        data[i] = data[i + 1] = data[i + 2] = c; data[i + 3] = 255;
    }
    GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return tex;
}

// ============================================================================
//  Material presets
// ============================================================================
enum class MatPreset {
    FLAT_TEXTURE = 0,
    SMOOTH_TEXTURE,
    VERTEX_SMOOTH,
    LIT_BLOOM,
    NORMAL_MAPPED,
    WATER_SPARKLE,
    FRESNEL,
    PBR
};

static const char* kMatNames[8] = {
    "1: Flat Shaded",
    "2: Smooth Shaded",
    "3: Vertex Colour",
    "4: Lit Bloom",
    "5: Normal Mapped",
    "6: Water Sparkle",
    "7: Fresnel",
    "8: PBR"
};

// ============================================================================
//  GPU Mesh
// ============================================================================
struct GPUMesh {
    GLuint vao = 0, vbo = 0, ebo = 0;
    GLuint indexCount = 0;
    GLuint diffuseTex = 0;
    GLuint normalTex = 0;
    GLuint sparkleTex = 0;
    MatPreset preset = MatPreset::FLAT_TEXTURE;
    float tint[3] = { 1.f,1.f,1.f };
    float metallic = 0.f, roughness = 0.8f;
    float fresnelPow = 3.f;
    std::string label;
};

// Vertex layout: pos3 nrm3 uv2 col4 tan3 bitan3  (18 floats / 72 bytes)
static const int VSTRIDE = 18;

static GPUMesh uploadMesh(const GeoData& geo) {
    GPUMesh m;
    glGenVertexArrays(1, &m.vao); glGenBuffers(1, &m.vbo); glGenBuffers(1, &m.ebo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, geo.verts.size() * 4, geo.verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, geo.idxs.size() * 4, geo.idxs.data(), GL_STATIC_DRAW);
    size_t st = VSTRIDE * 4;
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, (GLsizei)st, (void*)0);        glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, (GLsizei)st, (void*)(3 * 4));    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, (GLsizei)st, (void*)(6 * 4));    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, (GLsizei)st, (void*)(8 * 4));    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, (GLsizei)st, (void*)(12 * 4));   glEnableVertexAttribArray(4);
    glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, (GLsizei)st, (void*)(15 * 4));   glEnableVertexAttribArray(5);
    glBindVertexArray(0);
    m.indexCount = (GLuint)geo.idxs.size();
    return m;
}

static GPUMesh loadModelMesh(const std::string& path) {
    static Assimp::Importer imp;
    const aiScene* sc = imp.ReadFile(path,
        aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs |
        aiProcess_CalcTangentSpace | aiProcess_JoinIdenticalVertices |
        aiProcess_PreTransformVertices);
    if (!sc || !sc->mRootNode) {
        fprintf(stderr, "[load] %s: %s\n", path.c_str(), imp.GetErrorString());
        return {};
    }
    GeoData geo;
    unsigned idxOff = 0;
    for (unsigned mi = 0;mi < sc->mNumMeshes;++mi) {
        const aiMesh* mesh = sc->mMeshes[mi];
        for (unsigned vi = 0;vi < mesh->mNumVertices;++vi) {
            auto& p = mesh->mVertices[vi];
            const aiVector3D n = mesh->HasNormals() ? mesh->mNormals[vi] : aiVector3D(0, 1, 0);
            float u = 0, v = 0;
            if (mesh->HasTextureCoords(0)) { u = mesh->mTextureCoords[0][vi].x; v = mesh->mTextureCoords[0][vi].y; }
            float r = 1, g = 1, b = 1, a = 1;
            if (mesh->HasVertexColors(0)) { r = mesh->mColors[0][vi].r; g = mesh->mColors[0][vi].g; b = mesh->mColors[0][vi].b; a = mesh->mColors[0][vi].a; }
            float tx = 1, ty = 0, tz = 0, bx = 0, by = 1, bz = 0;
            if (mesh->HasTangentsAndBitangents()) { tx = mesh->mTangents[vi].x;ty = mesh->mTangents[vi].y;tz = mesh->mTangents[vi].z; bx = mesh->mBitangents[vi].x;by = mesh->mBitangents[vi].y;bz = mesh->mBitangents[vi].z; }
            geo.verts.insert(geo.verts.end(), { p.x,p.y,p.z, n.x,n.y,n.z, u,v, r,g,b,a, tx,ty,tz, bx,by,bz });
        }
        for (unsigned fi = 0;fi < mesh->mNumFaces;++fi)
            for (unsigned j = 0;j < mesh->mFaces[fi].mNumIndices;++j)
                geo.idxs.push_back(idxOff + mesh->mFaces[fi].mIndices[j]);
        idxOff += (unsigned)mesh->mNumVertices;
    }
    return uploadMesh(geo);
}

// ============================================================================
//  Bloom FBO
// ============================================================================
struct BloomFBO {
    GLuint fbo[2] = { 0,0 }, tex[2] = { 0,0 };
    GLuint sceneFBO = 0, sceneTex = 0, sceneDepth = 0;
    int w = 0, h = 0;

    void init(int ww, int hh) {
        w = ww; h = hh;
        glGenFramebuffers(1, &sceneFBO); glGenTextures(1, &sceneTex);
        glBindTexture(GL_TEXTURE_2D, sceneTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenRenderbuffers(1, &sceneDepth);
        glBindRenderbuffer(GL_RENDERBUFFER, sceneDepth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneTex, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sceneDepth);
        int bw = (ww / 2 > 1 ? ww / 2 : 1), bh = (hh / 2 > 1 ? hh / 2 : 1);
        glGenFramebuffers(2, fbo); glGenTextures(2, tex);
        for (int i = 0;i < 2;++i) {
            glBindTexture(GL_TEXTURE_2D, tex[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, bw, bh, 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[i], 0);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    void resize(int ww, int hh) { if (ww == w && hh == h)return; destroy(); init(ww, hh); }
    void destroy() {
        if (sceneFBO)  glDeleteFramebuffers(1, &sceneFBO);
        if (sceneTex)  glDeleteTextures(1, &sceneTex);
        if (sceneDepth)glDeleteRenderbuffers(1, &sceneDepth);
        glDeleteFramebuffers(2, fbo); glDeleteTextures(2, tex);
        sceneFBO = sceneTex = sceneDepth = fbo[0] = fbo[1] = tex[0] = tex[1] = 0; w = h = 0;
    }
};

// ============================================================================
//  5x7 bitmap font
// ============================================================================
static unsigned char kFont5x7[128][5] = {};
static void kFont5x7_init() {
    auto s = [](int c, unsigned char a, unsigned char b, unsigned char cc, unsigned char d, unsigned char e) {
        kFont5x7[c][0] = a;kFont5x7[c][1] = b;kFont5x7[c][2] = cc;kFont5x7[c][3] = d;kFont5x7[c][4] = e;
        };
    s('!', 0, 0, 0x5F, 0, 0);s('"', 0, 7, 0, 7, 0);s('%', 0x23, 0x13, 0x08, 0x64, 0x62);
    s('(', 0, 0x1C, 0x22, 0x41, 0);s(')', 0, 0x41, 0x22, 0x1C, 0);s('+', 0x08, 0x08, 0x3E, 0x08, 0x08);
    s(',', 0, 0x50, 0x30, 0, 0);s('-', 0x08, 0x08, 0x08, 0x08, 0x08);s('.', 0, 0x60, 0x60, 0, 0);
    s('/', 0x20, 0x10, 0x08, 0x04, 0x02);
    s('0', 0x3E, 0x51, 0x49, 0x45, 0x3E);s('1', 0, 0x42, 0x7F, 0x40, 0);
    s('2', 0x42, 0x61, 0x51, 0x49, 0x46);s('3', 0x21, 0x41, 0x45, 0x4B, 0x31);
    s('4', 0x18, 0x14, 0x12, 0x7F, 0x10);s('5', 0x27, 0x45, 0x45, 0x45, 0x39);
    s('6', 0x3C, 0x4A, 0x49, 0x49, 0x30);s('7', 0x01, 0x71, 0x09, 0x05, 0x03);
    s('8', 0x36, 0x49, 0x49, 0x49, 0x36);s('9', 0x06, 0x49, 0x49, 0x29, 0x1E);
    s(':', 0, 0x36, 0x36, 0, 0);s('<', 0x08, 0x14, 0x22, 0x41, 0);s('>', 0, 0x41, 0x22, 0x14, 0x08);
    s('A', 0x7E, 0x09, 0x09, 0x09, 0x7E);s('B', 0x7F, 0x49, 0x49, 0x49, 0x36);
    s('C', 0x3E, 0x41, 0x41, 0x41, 0x22);s('D', 0x7F, 0x41, 0x41, 0x22, 0x1C);
    s('E', 0x7F, 0x49, 0x49, 0x49, 0x41);s('F', 0x7F, 0x09, 0x09, 0x09, 0x01);
    s('G', 0x3E, 0x41, 0x49, 0x49, 0x7A);s('H', 0x7F, 0x08, 0x08, 0x08, 0x7F);
    s('I', 0, 0x41, 0x7F, 0x41, 0);s('J', 0x20, 0x40, 0x41, 0x3F, 0x01);
    s('K', 0x7F, 0x08, 0x14, 0x22, 0x41);s('L', 0x7F, 0x40, 0x40, 0x40, 0x40);
    s('M', 0x7F, 0x02, 0x0C, 0x02, 0x7F);s('N', 0x7F, 0x04, 0x08, 0x10, 0x7F);
    s('O', 0x3E, 0x41, 0x41, 0x41, 0x3E);s('P', 0x7F, 0x09, 0x09, 0x09, 0x06);
    s('Q', 0x3E, 0x41, 0x51, 0x21, 0x5E);s('R', 0x7F, 0x09, 0x19, 0x29, 0x46);
    s('S', 0x46, 0x49, 0x49, 0x49, 0x31);s('T', 0x01, 0x01, 0x7F, 0x01, 0x01);
    s('U', 0x3F, 0x40, 0x40, 0x40, 0x3F);s('V', 0x1F, 0x20, 0x40, 0x20, 0x1F);
    s('W', 0x3F, 0x40, 0x38, 0x40, 0x3F);s('X', 0x63, 0x14, 0x08, 0x14, 0x63);
    s('Y', 0x07, 0x08, 0x70, 0x08, 0x07);s('Z', 0x61, 0x51, 0x49, 0x45, 0x43);
    s('[', 0, 0x7F, 0x41, 0x41, 0);s('\\', 0x02, 0x04, 0x08, 0x10, 0x20);
    s(']', 0, 0x41, 0x41, 0x7F, 0);s('_', 0x40, 0x40, 0x40, 0x40, 0x40);
    s('a', 0x20, 0x54, 0x54, 0x54, 0x78);s('b', 0x7F, 0x48, 0x44, 0x44, 0x38);
    s('c', 0x38, 0x44, 0x44, 0x44, 0x20);s('d', 0x38, 0x44, 0x44, 0x48, 0x7F);
    s('e', 0x38, 0x54, 0x54, 0x54, 0x18);s('f', 0x08, 0x7E, 0x09, 0x01, 0x02);
    s('g', 0x0C, 0x52, 0x52, 0x52, 0x3E);s('h', 0x7F, 0x08, 0x04, 0x04, 0x78);
    s('i', 0, 0x44, 0x7D, 0x40, 0);s('j', 0x20, 0x40, 0x44, 0x3D, 0);
    s('k', 0x7F, 0x10, 0x28, 0x44, 0);s('l', 0, 0x41, 0x7F, 0x40, 0);
    s('m', 0x7C, 0x04, 0x18, 0x04, 0x78);s('n', 0x7C, 0x08, 0x04, 0x04, 0x78);
    s('o', 0x38, 0x44, 0x44, 0x44, 0x38);s('p', 0x7C, 0x14, 0x14, 0x14, 0x08);
    s('q', 0x08, 0x14, 0x14, 0x18, 0x7C);s('r', 0x7C, 0x08, 0x04, 0x04, 0x08);
    s('s', 0x48, 0x54, 0x54, 0x54, 0x20);s('t', 0x04, 0x3F, 0x44, 0x40, 0x20);
    s('u', 0x3C, 0x40, 0x40, 0x20, 0x7C);s('v', 0x1C, 0x20, 0x40, 0x20, 0x1C);
    s('w', 0x3C, 0x40, 0x30, 0x40, 0x3C);s('x', 0x44, 0x28, 0x10, 0x28, 0x44);
    s('y', 0x0C, 0x50, 0x50, 0x50, 0x3C);s('z', 0x44, 0x64, 0x54, 0x4C, 0x44);
    s(' ', 0, 0, 0, 0, 0);
}

// ============================================================================
//  HUD Renderer
// ============================================================================
struct HUDRenderer {
    GLuint prog = 0, vao = 0, vbo = 0, fontTex = 0;
    GLint locFont = 0, locColor = 0;
    static constexpr int GW = 6, GH = 8, AW = 128 * 6, AH = 8;

    void init() {
        prog = buildProgram("hud.vert", "hud.frag");
        locFont = glGetUniformLocation(prog, "uFont");
        locColor = glGetUniformLocation(prog, "uColor");
        std::vector<unsigned char> atlas(AW * AH * 4, 0);
        for (int c = 0;c < 128;++c) {
            int ox = c * GW;
            for (int col = 0;col < 5;++col) {
                unsigned char bits = kFont5x7[c][col];
                for (int row = 0;row < 7;++row)
                    if (bits & (1 << row)) {
                        int px = (row * AW + (ox + col)) * 4;
                        atlas[px] = atlas[px + 1] = atlas[px + 2] = atlas[px + 3] = 255;
                    }
            }
        }
        glGenTextures(1, &fontTex); glBindTexture(GL_TEXTURE_2D, fontTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, AW, AH, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
        glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0);  glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8);  glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    void drawString(const char* str, float px, float py, int ww, int wh,
        float r = 1, float g = 1, float b = 1, float a = 1, float sc = 2.f) {
        std::vector<float> v; v.reserve(strlen(str) * 24);
        float cx = px, cy = py;
        for (const char* p = str;*p;++p) {
            int c = (unsigned char)*p; if (c < 0 || c >= 128)c = ' ';
            if (c == '\n') { cx = px;cy += GH * sc + 2;continue; }
            float u0 = (float)(c * GW) / (float)AW, u1 = (float)(c * GW + GW - 1) / (float)AW;
            float v0 = 0.f, v1 = (float)(GH - 1) / (float)AH;
            float x0 = 2.f * cx / (float)ww - 1.f, x1 = 2.f * (cx + GW * sc) / (float)ww - 1.f;
            float y1b = 1.f - 2.f * cy / (float)wh, y0b = 1.f - 2.f * (cy + GH * sc) / (float)wh;
            v.insert(v.end(), { x0,y1b,u0,v0, x1,y1b,u1,v0, x0,y0b,u0,v1,
                               x1,y1b,u1,v0, x1,y0b,u1,v1, x0,y0b,u0,v1 });
            cx += GW * sc;
        }
        if (v.empty())return;
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, v.size() * 4, v.data(), GL_DYNAMIC_DRAW);
        glUseProgram(prog);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, fontTex);
        glUniform1i(locFont, 0); glUniform4f(locColor, r, g, b, a);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v.size() / 4));
        glBindVertexArray(0);
    }
    void destroy() {
        glDeleteProgram(prog); glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo); glDeleteTextures(1, &fontTex);
    }
};

static std::string sfmt(const char* f, ...) {
    char buf[512]; va_list a; va_start(a, f); vsnprintf(buf, sizeof(buf), f, a); va_end(a); return buf;
}

// ============================================================================
//  Skysphere
// ============================================================================
struct Skysphere {
    GLuint vao = 0, vbo = 0, ebo = 0, prog = 0;
    GLuint indexCount = 0;
    GLint locVP = 0, locRadius = 0;

    void init(int stacks = 24, int slices = 48) {
        prog = buildProgram("sky.vert", "sky.frag");
        locVP = glGetUniformLocation(prog, "uVP");
        locRadius = glGetUniformLocation(prog, "uRadius");
        std::vector<float> verts; std::vector<unsigned> idxs;
        const float PI = 3.14159265f;
        for (int i = 0;i <= stacks;++i) {
            float phi = PI * (float)i / (float)stacks, y = cosf(phi), sp = sinf(phi);
            for (int j = 0;j <= slices;++j) {
                float th = 2.f * PI * (float)j / (float)slices;
                verts.push_back(sp * cosf(th)); verts.push_back(y); verts.push_back(sp * sinf(th));
            }
        }
        for (int i = 0;i < stacks;++i) for (int j = 0;j < slices;++j) {
            unsigned a = i * (slices + 1) + j, b = a + slices + 1;
            idxs.push_back(a);idxs.push_back(b);idxs.push_back(a + 1);
            idxs.push_back(b);idxs.push_back(b + 1);idxs.push_back(a + 1);
        }
        indexCount = (GLuint)idxs.size();
        glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); glGenBuffers(1, &ebo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * 4, verts.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idxs.size() * 4, idxs.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void*)0);
        glEnableVertexAttribArray(0); glBindVertexArray(0);
    }

    void draw(const Mat4& viewRot, const Mat4& proj, float radius) {
        Mat4 vp = mul(proj, viewRot);
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glEnable(GL_CULL_FACE); glCullFace(GL_FRONT);
        glUseProgram(prog);
        glUniformMatrix4fv(locVP, 1, GL_FALSE, vp.m);
        glUniform1f(locRadius, radius);
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0); glDepthMask(GL_TRUE); glCullFace(GL_BACK);
    }

    void destroy() {
        glDeleteVertexArrays(1, &vao); glDeleteBuffers(1, &vbo);
        glDeleteBuffers(1, &ebo);      glDeleteProgram(prog);
    }
};

// ============================================================================
//  Audio System  (miniaudio streaming engine)
// ============================================================================
struct AudioSystem {
    ma_engine engine{};
    ma_sound  music{};
    bool      engineReady = false;
    bool      soundReady = false;

    bool init() {
        ma_engine_config cfg = ma_engine_config_init();
        ma_result r = ma_engine_init(&cfg, &engine);
        if (r != MA_SUCCESS) {
            fprintf(stderr, "[audio] ma_engine_init failed: %s\n",
                ma_result_description(r));
            return false;
        }
        fprintf(stdout, "[audio] Engine ready. Output device: %s\n",
            ma_engine_get_device(&engine)->playback.name);
        engineReady = true;
        return true;
    }

    // path – absolute path to WAV / MP3 / OGG / FLAC
    // vol  – linear volume (1.0 = 100%)
    bool playMusic(const std::string& path, float vol = 0.7f) {
        if (!engineReady) return false;
        stopMusic();

        // MA_SOUND_FLAG_STREAM = don't load whole file into RAM (good for music)
        // MA_SOUND_FLAG_NO_SPATIALIZATION = 2-D stereo, no 3-D attenuation
        ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
        ma_result r = ma_sound_init_from_file(
            &engine, path.c_str(), flags, nullptr, nullptr, &music);

        if (r != MA_SUCCESS) {
            fprintf(stderr, "[audio] Cannot open: %s\n"
                "        Error: %s (code %d)\n"
                "        Make sure the file exists and the format is\n"
                "        WAV, MP3, FLAC, or OGG Vorbis.\n",
                path.c_str(), ma_result_description(r), (int)r);
            return false;
        }

        ma_sound_set_looping(&music, MA_TRUE);
        ma_sound_set_volume(&music, vol);
        r = ma_sound_start(&music);
        if (r != MA_SUCCESS) {
            fprintf(stderr, "[audio] ma_sound_start failed: %s\n",
                ma_result_description(r));
            ma_sound_uninit(&music);
            return false;
        }

        soundReady = true;
        fprintf(stdout, "[audio] Playing (looping, vol=%.0f%%): %s\n",
            vol * 100.f, path.c_str());
        return true;
    }

    void setVolume(float vol) {
        if (soundReady) ma_sound_set_volume(&music, vol);
    }

    void stopMusic() {
        if (soundReady) { ma_sound_uninit(&music); soundReady = false; }
    }

    void destroy() {
        stopMusic();
        if (engineReady) { ma_engine_uninit(&engine); engineReady = false; }
    }
};

// ============================================================================
//  Title screen image
// ============================================================================
// Loads an image (PNG/JPG/BMP/TGA) and uploads it as an RGBA GL texture.
// Falls back gracefully if the file is missing (texture stays 0).
struct TitleImage {
    GLuint tex = 0;
    GLuint vao = 0;   // fullscreen quad
    GLuint vbo = 0;
    GLuint prog = 0;   // title_image shader (fullscreen_tri.vert + title_image.frag)
    bool   hasImage = false;

    void init() {
        prog = buildProgram("fullscreen_tri.vert", "title_image.frag");

        // Fullscreen triangle VAO (same trick as post-process passes)
        glGenVertexArrays(1, &vao);
    }

    // Load an image file from disk.  Call after init().
    bool load(const std::string& path) {
        if (tex) { glDeleteTextures(1, &tex); tex = 0; hasImage = false; }

        int w, h, ch;
        stbi_set_flip_vertically_on_load(1); // OpenGL UV origin is bottom-left
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (!data) {
            fprintf(stderr, "[image] Cannot load title image: %s\n", path.c_str());
            return false;
        }

        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        stbi_image_free(data);

        fprintf(stdout, "[image] Title image loaded: %s  (%dx%d)\n", path.c_str(), w, h);
        hasImage = true;
        return true;
    }

    // Draw fullscreen, blended over whatever is already in the framebuffer.
    // alpha – overall opacity (0=invisible, 1=fully opaque)
    void draw(float alpha) {
        if (!hasImage || !prog || alpha <= 0.f) return;
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_CULL_FACE);

        glUseProgram(prog);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1i(glGetUniformLocation(prog, "uImage"), 0);
        glUniform1f(glGetUniformLocation(prog, "uAlpha"), alpha);

        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3); // fullscreen triangle, no VBO needed
        glBindVertexArray(0);
        glDisable(GL_BLEND);
    }

    void destroy() {
        if (tex) glDeleteTextures(1, &tex);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (prog) glDeleteProgram(prog);
        tex = vao = prog = 0;
    }
};

// ============================================================================
//  Global state
// ============================================================================
enum class AppState { TITLE, DEMO };
static AppState g_state = AppState::TITLE;

struct Camera {
    Vec3  pos = { 0,3,10 };
    float yaw = 180.f, pitch = -10.f;
    Vec3  forward = { 0,0,-1 }, right = { 1,0,0 };
    bool  freeLook = false;
    void update() {
        float yr = yaw * (3.14159265f / 180.f), pr = pitch * (3.14159265f / 180.f);
        forward = norm3({ cosf(pr) * cosf(yr), sinf(pr), cosf(pr) * sinf(yr) });
        right = norm3(cross3(forward, { 0,1,0 }));
    }
    void clampPitch() { pitch = fmaxf(-89.f, fminf(89.f, pitch)); }
};

struct InputState {
    bool w = false, s = false, a = false, d = false;
    bool shift = false;
    bool arrowUp = false, arrowDown = false, arrowLeft = false, arrowRight = false;
};

struct RenderToggles {
    bool  bloom = true;
    bool  showTex = true;
    bool  showVC = false;
    bool  backface = true;
    bool  showHUD = true;
    float bloomThresh = 0.6f;
    float bloomStr = 1.2f;
};

static Camera        g_cam;
static InputState    g_input;
static RenderToggles g_rt;
static float g_time = 0.f;
static float g_fps = 60.f;

// Title screen state
static int   g_hoverBtn = -1;
static float g_titleAlpha = 1.f;
static float g_fadeTimer = 0.f;
static bool  g_fadingOut = false;

static double g_mouseX = 0, g_mouseY = 0;
static int    g_ww = 1280, g_wh = 800;

// ============================================================================
//  GLFW callbacks
// ============================================================================
static void keyCB(GLFWwindow* win, int key, int, int action, int) {
    bool p = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_ESCAPE && p) {
        if (g_state == AppState::DEMO) { g_state = AppState::TITLE; g_titleAlpha = 1.f; g_fadingOut = false; }
        else glfwSetWindowShouldClose(win, true);
    }
    if (g_state == AppState::DEMO) {
        if (key == GLFW_KEY_F1 && p) g_cam.freeLook = !g_cam.freeLook;
        if (key == GLFW_KEY_F3 && p) g_rt.showHUD = !g_rt.showHUD;
        if (key == GLFW_KEY_F4 && p) g_rt.bloom = !g_rt.bloom;
        if (key == GLFW_KEY_F5 && p) g_rt.showTex = !g_rt.showTex;
        if (key == GLFW_KEY_F6 && p) g_rt.showVC = !g_rt.showVC;
        if (key == GLFW_KEY_F7 && p) g_rt.backface = !g_rt.backface;
    }
    if (key == GLFW_KEY_W)g_input.w = p; if (key == GLFW_KEY_S)g_input.s = p;
    if (key == GLFW_KEY_A)g_input.a = p; if (key == GLFW_KEY_D)g_input.d = p;
    if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT)g_input.shift = p;
    if (key == GLFW_KEY_UP)   g_input.arrowUp = p;
    if (key == GLFW_KEY_DOWN) g_input.arrowDown = p;
    if (key == GLFW_KEY_LEFT) g_input.arrowLeft = p;
    if (key == GLFW_KEY_RIGHT)g_input.arrowRight = p;
}
static void cursorCB(GLFWwindow*, double x, double y) { g_mouseX = x; g_mouseY = y; }
static void mouseCB(GLFWwindow* win, int btn, int action, int) {
    if (btn == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && g_state == AppState::TITLE) {
        if (g_hoverBtn == 0 && !g_fadingOut) { g_fadingOut = true; g_fadeTimer = 0.f; }
        if (g_hoverBtn == 1) glfwSetWindowShouldClose(win, true);
    }
}

// ============================================================================
//  Button helpers
// ============================================================================
struct Button { float cx, cy, hw, hh; };
static bool btnHit(const Button& b, double mx, double my, int ww, int wh) {
    float nx = (float)(mx / ww) * 2.f - 1.f;
    float ny = 1.f - (float)(my / wh) * 2.f;
    return fabsf(nx - b.cx) < b.hw && fabsf(ny - b.cy) < b.hh;
}

// Quad VBO (0..1 range, remapped in shader via uRect)
static GLuint g_quadVAO = 0, g_quadVBO = 0;
static void initQuad() {
    float verts[] = { 0,0, 1,0, 1,1, 0,0, 1,1, 0,1 };
    glGenVertexArrays(1, &g_quadVAO); glGenBuffers(1, &g_quadVBO);
    glBindVertexArray(g_quadVAO); glBindBuffer(GL_ARRAY_BUFFER, g_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, (void*)0); glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}
static void drawButton(GLuint prog, const Button& b,
    float r, float g, float bv, float a, float hover) {
    float x = b.cx - b.hw, y = b.cy - b.hh, w = b.hw * 2.f, h = b.hh * 2.f;
    glUseProgram(prog);
    glUniform4f(glGetUniformLocation(prog, "uRect"), x, y, w, h);
    glUniform4f(glGetUniformLocation(prog, "uColor"), r, g, bv, a);
    glUniform1f(glGetUniformLocation(prog, "uHover"), hover);
    glDisable(GL_DEPTH_TEST); glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glDisable(GL_CULL_FACE);
    glBindVertexArray(g_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

// ============================================================================
//  main
// ============================================================================
int main(int argc, char** argv)
{
    resolveShaderDir(argv[0]);
    fprintf(stdout, "[shader] Using shader directory: %s\n", g_shaderDir.c_str());

    kFont5x7_init();

    if (!glfwInit()) { fprintf(stderr, "GLFW init failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* win = glfwCreateWindow(g_ww, g_wh,
        "Twilight Engine v0.2 Alpha — Material Showcase", nullptr, nullptr);
    if (!win) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win); glfwSwapInterval(1);
    glfwSetKeyCallback(win, keyCB);
    glfwSetCursorPosCallback(win, cursorCB);
    glfwSetMouseButtonCallback(win, mouseCB);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { fprintf(stderr, "GLEW init failed\n"); return 1; }
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);

    // ------------------------------------------------------------------
    //  Build all shader programs (loaded from shaders/ directory)
    // ------------------------------------------------------------------
    GLuint progTitle = buildProgram("title.vert", "title.frag");
    GLuint progBtn = buildProgram("button.vert", "button.frag");
    GLuint progBright = buildProgram("fullscreen_tri.vert", "bloom_bright.frag");
    GLuint progBlur = buildProgram("fullscreen_tri.vert", "bloom_blur.frag");
    GLuint progComposite = buildProgram("fullscreen_tri.vert", "bloom_composite.frag");

    // Each material uses the shared vertex shader + its own fragment shader
    const char* matFragFiles[8] = {
        "mat1_flat.frag",
        "mat2_smooth.frag",
        "mat3_vertex_col.frag",
        "mat4_lit_bloom.frag",
        "mat5_normal_map.frag",
        "mat6_water.frag",
        "mat7_fresnel.frag",
        "mat8_pbr.frag",
    };
    GLuint progMat[8];
    for (int i = 0;i < 8;++i)
        progMat[i] = buildProgram("material.vert", matFragFiles[i]);

    initQuad();

    // ------------------------------------------------------------------
    //  Procedural textures
    // ------------------------------------------------------------------
    GLuint diffTex[8] = {
        makeCheckerTex(256,256,16, 160,80, 30,  60,30,10),
        makeCheckerTex(256,256,16, 100,140,80,  40,70,30),
        makeCheckerTex(256,256,16, 200,160,80,  80,60,20),
        makeCheckerTex(256,256,8,  80, 120,180, 20,40,80),
        makeCheckerTex(256,256,12, 140,100,60,  60,40,20),
        makeCheckerTex(256,256,32, 30, 80, 140, 10,40,90),
        makeCheckerTex(256,256,16, 120,60, 140, 50,20,70),
        makeCheckerTex(256,256,8,  180,160,140, 70,60,50),
    };
    GLuint normalTex = makeNormalTex(256, 256, 12.f);
    GLuint sparkleTex = makeWaterSparkle(256, 256);

    float vcTints[8][3];
    for (int i = 0;i < 8;++i)
        hsvToRgb((float)i / 8.f, 0.7f, 1.f, vcTints[i][0], vcTints[i][1], vcTints[i][2]);

    // ------------------------------------------------------------------
    //  Meshes
    // ------------------------------------------------------------------
    GPUMesh meshes[8];
    GeoData shapes[2] = { genSphere(), genCube() };
    for (int i = 0;i < 8;++i) {
        if (i + 1 < argc) {
            meshes[i] = loadModelMesh(argv[i + 1]);
            if (!meshes[i].vao) meshes[i] = uploadMesh(shapes[i % 2]);
        }
        else {
            meshes[i] = uploadMesh(shapes[i % 2]);
        }
        meshes[i].preset = (MatPreset)i;
        meshes[i].diffuseTex = diffTex[i];
        meshes[i].normalTex = normalTex;
        meshes[i].sparkleTex = sparkleTex;
        meshes[i].tint[0] = vcTints[i][0];
        meshes[i].tint[1] = vcTints[i][1];
        meshes[i].tint[2] = vcTints[i][2];
        meshes[i].label = kMatNames[i];
        if (i == 7) { meshes[i].metallic = 0.85f; meshes[i].roughness = 0.25f; }
        else if (i == 3) { meshes[i].metallic = 0.1f;  meshes[i].roughness = 0.3f; }
        else { meshes[i].metallic = 0.0f;  meshes[i].roughness = 0.7f; }
    }

    // ------------------------------------------------------------------
    //  Systems
    // ------------------------------------------------------------------
    Skysphere sky; sky.init();
    BloomFBO  bloom; bloom.init(g_ww, g_wh);
    HUDRenderer hud; hud.init();

    // ------------------------------------------------------------------
    //  Audio
    //  Place your music file as  assets/music.ogg  (or .mp3 / .wav)
    //  next to the executable.  Supports any format miniaudio handles.
    // ------------------------------------------------------------------
    AudioSystem audio;
    audio.init();
    {
        // Asset directory lives beside the shaders/ folder
        fs::path assetDir = fs::path(g_shaderDir).parent_path() / "assets";
        // Try common filenames in order
        const char* candidates[] = {
            "music.ogg", "music.mp3", "music.wav", "music.flac",
            "title.ogg", "title.mp3", "title.wav"
        };
        bool musicFound = false;
        for (auto* name : candidates) {
            fs::path p = assetDir / name;
            if (fs::exists(p)) {
                musicFound = audio.playMusic(p.string(), 0.7f);
                if (musicFound) {
                    fprintf(stdout, "[audio] Playing: %s\n", p.string().c_str());
                    break;
                }
            }
        }
        if (!musicFound)
            fprintf(stdout, "[audio] No music found. Place a music file in %s\n",
                assetDir.string().c_str());
    }

    // ------------------------------------------------------------------
    //  Title screen image
    //  Place your image as  assets/title.png  (or .jpg / .bmp / .tga)
    //  next to the executable.
    // ------------------------------------------------------------------
    TitleImage titleImg;
    titleImg.init();
    {
        fs::path assetDir = fs::path(g_shaderDir).parent_path() / "assets";
        const char* imgCandidates[] = {
            "title.png", "title.jpg", "title.jpeg", "title.bmp", "title.tga"
        };
        for (auto* name : imgCandidates) {
            fs::path p = assetDir / name;
            if (fs::exists(p)) {
                titleImg.load(p.string());
                break;
            }
        }
        if (!titleImg.hasImage)
            fprintf(stdout, "[image] No title image found in %s\n"
                "        Place title.png there to show a custom background.\n",
                assetDir.string().c_str());
    }

    int lastW = g_ww, lastH = g_wh;

    // Dummy VAOs
    GLuint dummyVAO; glGenVertexArrays(1, &dummyVAO);

    // Title fullscreen triangle
    GLuint titleVAO; glGenVertexArrays(1, &titleVAO);
    {
        float tv[] = { -1,-1, 3,-1, -1,3 };
        GLuint tvbo; glGenBuffers(1, &tvbo);
        glBindVertexArray(titleVAO);
        glBindBuffer(GL_ARRAY_BUFFER, tvbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(tv), tv, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
    }

    Vec3 lightDir = norm3({ 0.6f,1.f,0.4f });
    Vec3 lightCol = { 1.2f,1.0f,0.85f };

    auto tPrev = Clock::now();
    float fpsAccum = 0.f; int fpsCount = 0;

    g_cam.pos = { 0.f,0.f,200.f };
    g_cam.yaw = 180.f; g_cam.pitch = 0.f;
    g_cam.update();

    // ==================================================================
    //  Main loop
    // ==================================================================
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        glfwGetFramebufferSize(win, &g_ww, &g_wh);
        if (g_ww == 0 || g_wh == 0) continue;
        if (g_ww != lastW || g_wh != lastH) { bloom.resize(g_ww, g_wh); lastW = g_ww; lastH = g_wh; }

        auto tNow = Clock::now();
        float dt = std::chrono::duration<float>(tNow - tPrev).count();
        tPrev = tNow; dt = fminf(dt, 0.1f);
        g_time += dt;

        fpsAccum += dt; ++fpsCount;
        if (fpsAccum >= 0.3f) { g_fps = fpsCount / fpsAccum; fpsAccum = 0; fpsCount = 0; }
        glViewport(0, 0, g_ww, g_wh);

        // --------------------------------------------------------------
        //  TITLE SCREEN
        // --------------------------------------------------------------
        if (g_state == AppState::TITLE) {
            if (g_fadingOut) {
                g_fadeTimer += dt;
                g_titleAlpha = fmaxf(0.f, 1.f - g_fadeTimer * 2.f);
                if (g_fadeTimer >= 0.6f) { g_state = AppState::DEMO; g_titleAlpha = 1.f; g_fadingOut = false; }
            }

            Button btnPlay = { 0.f, -0.08f, 0.22f, 0.06f };
            Button btnQuit = { 0.f, -0.22f, 0.22f, 0.06f };
            g_hoverBtn = -1;
            if (btnHit(btnPlay, g_mouseX, g_mouseY, g_ww, g_wh)) g_hoverBtn = 0;
            if (btnHit(btnQuit, g_mouseX, g_mouseY, g_ww, g_wh)) g_hoverBtn = 1;

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glDisable(GL_BLEND);

            float alpha = g_titleAlpha;

            // background 
            titleImg.draw(alpha);

            drawButton(progBtn, btnPlay, 0.08f, 0.18f, 0.35f, alpha, g_hoverBtn == 0 ? 1.f : 0.f);
            drawButton(progBtn, btnQuit, 0.12f, 0.06f, 0.06f, alpha, g_hoverBtn == 1 ? 1.f : 0.f);

            float scl = 3.f;
            hud.drawString("TwilightENGINE v0.2 Alpha",
                (g_ww / 2.f) - 220.f, (g_wh / 2.f) - 180.f, g_ww, g_wh, 0.9f, 0.75f, 0.3f, alpha, scl + 1);
            hud.drawString("MATERIAL SHOWCASE",
                (g_ww / 2.f) - 200.f, (g_wh / 2.f) - 130.f, g_ww, g_wh, 0.6f, 0.5f, 0.8f, alpha * 0.9f, scl - 1.f);
            hud.drawString("Made By Jack",
                (g_ww / 2.f) - 220.f, (g_wh / 2.f) - 30.f, g_ww, g_wh, 0.6f, 0.7f, 0.5f, alpha * 0.7f, 2.f);

            float playScreenY = (1.f - (btnPlay.cy + btnPlay.hh)) * 0.5f * g_wh;
            float quitScreenY = (1.f - (btnQuit.cy + btnQuit.hh)) * 0.5f * g_wh;
            float playLabelY = playScreenY + (btnPlay.hh * g_wh - HUDRenderer::GH * 2.5f) * 0.5f;
            float quitLabelY = quitScreenY + (btnQuit.hh * g_wh - HUDRenderer::GH * 2.5f) * 0.5f;
            hud.drawString("PLAY", g_ww / 2.f - 54.f, playLabelY, g_ww, g_wh, 0.95f, 0.9f, 0.6f, alpha, 2.5f);
            hud.drawString("QUIT", g_ww / 2.f - 24.f, quitLabelY, g_ww, g_wh, 0.8f, 0.4f, 0.4f, alpha, 2.5f);
            hud.drawString("ESC: Return to Title  |  F1: Free Camera  |  F3: HUD  |  F4: Bloom  |  F5: Textures  |  F6: Vertex Col  |  F7: Backface",
                20.f, (float)(g_wh - 24), g_ww, g_wh, 0.4f, 0.4f, 0.4f, alpha * 0.8f, 1.5f);

            glEnable(GL_DEPTH_TEST);
            glfwSwapBuffers(win);
            continue;
        }

        // --------------------------------------------------------------
        //  DEMO SCENE
        // --------------------------------------------------------------
        float spd = (g_input.shift ? 20.f : 8.f) * dt;
        float lspd = 70.f * dt * (g_input.shift ? 2.f : 1.f);

        if (g_cam.freeLook) {
            if (g_input.arrowLeft) g_cam.yaw -= lspd;
            if (g_input.arrowRight)g_cam.yaw += lspd;
            if (g_input.arrowUp)   g_cam.pitch += lspd;
            if (g_input.arrowDown) g_cam.pitch -= lspd;
            g_cam.clampPitch(); g_cam.update();
            if (g_input.w)g_cam.pos = add3(g_cam.pos, scale3(g_cam.forward, spd));
            if (g_input.s)g_cam.pos = add3(g_cam.pos, scale3(g_cam.forward, -spd));
            if (g_input.a)g_cam.pos = add3(g_cam.pos, scale3(g_cam.right, -spd));
            if (g_input.d)g_cam.pos = add3(g_cam.pos, scale3(g_cam.right, spd));
        }
        else {
            if (g_input.arrowLeft) g_cam.yaw -= lspd;
            if (g_input.arrowRight)g_cam.yaw += lspd;
            if (g_input.arrowUp)   g_cam.pitch += lspd * 0.5f;
            if (g_input.arrowDown) g_cam.pitch -= lspd * 0.5f;
            g_cam.clampPitch(); g_cam.update();
            if (g_input.w) { float d = len3(g_cam.pos); d = fmaxf(5.f, d - spd * 10.f);   g_cam.pos = scale3(norm3(g_cam.pos), d); }
            if (g_input.s) { float d = len3(g_cam.pos); d = fminf(500.f, d + spd * 10.f); g_cam.pos = scale3(norm3(g_cam.pos), d); }
        }

        float aspect = (float)g_ww / (float)g_wh;
        Mat4 proj = perspective(0.7854f, aspect, 0.1f, 5000.f);
        Mat4 view = lookAt(g_cam.pos, { 0,0,0 }, { 0,1,0 });

        GLuint targetFBO = g_rt.bloom ? bloom.sceneFBO : 0;
        glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
        glViewport(0, 0, g_ww, g_wh);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        if (g_rt.backface)glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        // Skysphere
        sky.draw(viewRotOnly(view), proj, 2000.f);

        // 8 models in a circle
        const float CIRCLE_R = 80.f, MODEL_S = 12.f, PI = 3.14159265f;
        for (int i = 0;i < 8;++i) {
            float angle = 2.f * PI * (float)i / 8.f;
            float mx = cosf(angle) * CIRCLE_R, mz = sinf(angle) * CIRCLE_R;
            float faceAngle = atan2f(mz, mx) + PI;
            float spinAngle = g_time * 0.4f + (float)i * 0.7f;

            Mat4 T = translateMat(mx, 0.f, mz);
            Mat4 Ry = rotY(faceAngle + spinAngle);
            Mat4 S = scaleMat(MODEL_S, MODEL_S, MODEL_S);
            Mat4 model = mul(T, mul(Ry, S));
            Mat4 mvp = mul(proj, mul(view, model));

            GPUMesh& m = meshes[i];
            GLuint  p = progMat[i];
            glUseProgram(p);

            // Uniforms common to all material shaders
            glUniformMatrix4fv(glGetUniformLocation(p, "uMVP"), 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv(glGetUniformLocation(p, "uModel"), 1, GL_FALSE, model.m);
            glUniformMatrix4fv(glGetUniformLocation(p, "uNormalMat"), 1, GL_FALSE, model.m);
            glUniform3f(glGetUniformLocation(p, "uLightDir"), lightDir.x, lightDir.y, lightDir.z);
            glUniform3f(glGetUniformLocation(p, "uCamPos"), g_cam.pos.x, g_cam.pos.y, g_cam.pos.z);
            glUniform3f(glGetUniformLocation(p, "uLightCol"), lightCol.x, lightCol.y, lightCol.z);
            glUniform3f(glGetUniformLocation(p, "uTint"), m.tint[0], m.tint[1], m.tint[2]);
            glUniform1f(glGetUniformLocation(p, "uTime"), g_time);
            glUniform1i(glGetUniformLocation(p, "uShowTex"), g_rt.showTex ? 1 : 0);
            glUniform1i(glGetUniformLocation(p, "uShowVC"), g_rt.showVC ? 1 : 0);
            glUniform1f(glGetUniformLocation(p, "uMetallic"), m.metallic);
            glUniform1f(glGetUniformLocation(p, "uRoughness"), m.roughness);
            glUniform1f(glGetUniformLocation(p, "uFresnelPow"), m.fresnelPow);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m.diffuseTex);
            glUniform1i(glGetUniformLocation(p, "uDiffuse"), 0);

            if (i == 4) { // normal map
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, m.normalTex);
                glUniform1i(glGetUniformLocation(p, "uNormalMap"), 1);
            }
            if (i == 5) { // water sparkle
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, m.sparkleTex);
                glUniform1i(glGetUniformLocation(p, "uSparkle"), 1);
                glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }

            glBindVertexArray(m.vao);
            glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, nullptr);
            glBindVertexArray(0);
            if (i == 5) glDisable(GL_BLEND);
        }

        // ---- Bloom post-process ----
        if (g_rt.bloom) {
            glDisable(GL_DEPTH_TEST);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glBindVertexArray(dummyVAO);
            int bw = (g_ww / 2 > 1 ? g_ww / 2 : 1), bh = (g_wh / 2 > 1 ? g_wh / 2 : 1);

            // Bright-pass
            glBindFramebuffer(GL_FRAMEBUFFER, bloom.fbo[0]);
            glViewport(0, 0, bw, bh); glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(progBright);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, bloom.sceneTex);
            glUniform1i(glGetUniformLocation(progBright, "uScene"), 0);
            glUniform1f(glGetUniformLocation(progBright, "uThreshold"), g_rt.bloomThresh);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            // Ping-pong blur (5 iterations)
            int src = 0, dst = 1;
            glUseProgram(progBlur);
            for (int i = 0;i < 5;++i) {
                glBindFramebuffer(GL_FRAMEBUFFER, bloom.fbo[dst]);
                glViewport(0, 0, bw, bh); glClear(GL_COLOR_BUFFER_BIT);
                glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, bloom.tex[src]);
                glUniform1i(glGetUniformLocation(progBlur, "uTex"), 0);
                glUniform2f(glGetUniformLocation(progBlur, "uDir"), 1.f / bw, 0.f);
                glDrawArrays(GL_TRIANGLES, 0, 3); std::swap(src, dst);

                glBindFramebuffer(GL_FRAMEBUFFER, bloom.fbo[dst]);
                glViewport(0, 0, bw, bh); glClear(GL_COLOR_BUFFER_BIT);
                glBindTexture(GL_TEXTURE_2D, bloom.tex[src]);
                glUniform2f(glGetUniformLocation(progBlur, "uDir"), 0.f, 1.f / bh);
                glDrawArrays(GL_TRIANGLES, 0, 3); std::swap(src, dst);
            }

            // Composite + tonemap
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, g_ww, g_wh); glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(progComposite);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, bloom.sceneTex);
            glUniform1i(glGetUniformLocation(progComposite, "uScene"), 0);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, bloom.tex[src]);
            glUniform1i(glGetUniformLocation(progComposite, "uBloom"), 1);
            glUniform1f(glGetUniformLocation(progComposite, "uStrength"), g_rt.bloomStr);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0); glEnable(GL_DEPTH_TEST);
        }

        // ---- HUD overlay ----
        if (g_rt.showHUD) {
            const float S = 2.f, LH = HUDRenderer::GH * S + 3.f;
            float x = 10.f, y = 10.f;
            hud.drawString("Twilight Engine v0.2", x, y, g_ww, g_wh, 0.9f, 0.75f, 0.3f, 1.f, S + 0.5f); y += LH + 4.f;
            hud.drawString(sfmt("FPS: %.1f", g_fps).c_str(), x, y, g_ww, g_wh, 0.6f, 1.f, 0.6f, 1.f, S); y += LH;
            hud.drawString(sfmt("Cam: %.0f,%.0f,%.0f", g_cam.pos.x, g_cam.pos.y, g_cam.pos.z).c_str(),
                x, y, g_ww, g_wh, 0.5f, 1.f, 1.f, 1.f, S); y += LH; y += 4.f;
            auto fl = [&](const char* n, bool on, float rc = 0.6f, float gc = 1.f, float bc = 0.6f) {
                hud.drawString(sfmt("%s: %s", n, on ? "ON " : "OFF").c_str(),
                    x, y, g_ww, g_wh, on ? rc : 0.4f, on ? gc : 0.4f, on ? bc : 0.4f, 1.f, S); y += LH;
                };
            fl("F1 Free-Camera", g_cam.freeLook);
            fl("F4 Bloom", g_rt.bloom);
            fl("F5 Textures", g_rt.showTex, 1.f, 0.9f, 0.5f);
            fl("F6 Vertex Col", g_rt.showVC, 0.5f, 0.8f, 1.f);
            fl("F7 Backface", g_rt.backface);
            y += 6.f;
            hud.drawString("MODELS:", x, y, g_ww, g_wh, 0.7f, 0.7f, 0.7f, 0.9f, 1.8f); y += LH;
            for (int i = 0;i < 8;++i) {
                float hr, hg, hb; hsvToRgb((float)i / 8.f, 0.7f, 1.f, hr, hg, hb);
                hud.drawString(kMatNames[i], x + 10, y, g_ww, g_wh, hr, hg, hb, 0.9f, 1.8f); y += LH - 2.f;
            }
            y += 4.f;
            hud.drawString("WASD: move  Arrows: look  F3: HUD", x, y, g_ww, g_wh, 0.5f, 0.5f, 0.5f, 0.8f, 1.8f); y += LH;
            hud.drawString("ESC: title screen", x, y, g_ww, g_wh, 0.5f, 0.5f, 0.5f, 0.8f, 1.8f);
        }

        glfwSwapBuffers(win);
    }

    // Cleanup
    for (int i = 0;i < 8;++i) {
        glDeleteVertexArrays(1, &meshes[i].vao);
        glDeleteBuffers(1, &meshes[i].vbo);
        glDeleteBuffers(1, &meshes[i].ebo);
        glDeleteTextures(1, &diffTex[i]);
    }
    glDeleteTextures(1, &normalTex);
    glDeleteTextures(1, &sparkleTex);
    for (int i = 0;i < 8;++i) glDeleteProgram(progMat[i]);
    glDeleteProgram(progTitle);    glDeleteProgram(progBtn);
    glDeleteProgram(progBright);   glDeleteProgram(progBlur);
    glDeleteProgram(progComposite);
    glDeleteVertexArrays(1, &g_quadVAO); glDeleteBuffers(1, &g_quadVBO);
    glDeleteVertexArrays(1, &titleVAO);
    glDeleteVertexArrays(1, &dummyVAO);
    bloom.destroy(); hud.destroy(); sky.destroy();
    titleImg.destroy(); audio.destroy();
    glfwTerminate();
    return 0;
}