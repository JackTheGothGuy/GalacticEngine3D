// ============================================================================
//  fbx_engine.cpp  –  OpenGL 3.3 core rendering engine
//  Features:
//    • Assimp model loading (FBX, OBJ, GLTF, …)
//    • Vertex colours: imported OR procedurally generated per-vertex
//    • Diffuse textures / vertex colours / flat colour fallback
//    • Skysphere: procedural blue gradient dome
//    • SUN: HDR billboard in the sky, massive bloom glow
//    • LENS FLARE: screenspace flare elements along sun→center axis
//    • On-screen debug HUD  (F1 to toggle)
//    • PHYSICS: gravity, ground collision (CPU ray-triangle test), jump
//    • Camera: WASD walk/run (Shift), Space=jump, Arrow keys=look
//    • Noclip free-fly toggle  (F8)
//    • Bloom post-process  (F4 toggle, [ / ] threshold, - / + strength)
//    • Wireframe toggle  (F2)
//    • Back-face cull toggle  (F3)
//    • Vertex colour mode toggle  (F5)
//    • Scene switch: F6 = Scene 1 (testmap1), F7 = Scene 2 (testmap2)
//    • Quit  (Escape)
//
//  Default scene: models/testmap1/map.fbx  (override with argv[1])
//  Scene 1: models/testmap1/map.fbx   (F6)
//  Scene 2: models/testmap2/map.fbx   (F7)
//
//  Compile (Linux / macOS):
//    g++ fbx_engine.cpp -o fbx_engine \
//        -lGL -lGLEW -lglfw -lassimp \
//        $(pkg-config --cflags --libs glfw3 glew assimp) \
//        -std=c++17
//
//  Deps:  sudo apt install libglfw3-dev libglew-dev libassimp-dev libglm-dev
//  stb:   wget https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
//  Usage: ./fbx_engine [path/to/model.fbx]
// ============================================================================

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <limits>

namespace fs = std::filesystem;
using Clock  = std::chrono::high_resolution_clock;

// ============================================================================
//  Tiny math
// ============================================================================
struct Vec2 { float x=0,y=0; };
struct Vec3 { float x=0,y=0,z=0; };
struct Vec4 { float x=0,y=0,z=0,w=1; };
struct Mat4 { float m[16]={}; };

static Mat4 identity(){
    Mat4 r; r.m[0]=r.m[5]=r.m[10]=r.m[15]=1.f; return r;
}
static Mat4 mul(const Mat4& a,const Mat4& b){
    Mat4 r;
    for(int row=0;row<4;++row)
        for(int col=0;col<4;++col)
            for(int k=0;k<4;++k)
                r.m[col*4+row]+=a.m[k*4+row]*b.m[col*4+k];
    return r;
}
static Vec4 mulVec4(const Mat4& m,Vec4 v){
    return {
        m.m[0]*v.x+m.m[4]*v.y+m.m[8]*v.z +m.m[12]*v.w,
        m.m[1]*v.x+m.m[5]*v.y+m.m[9]*v.z +m.m[13]*v.w,
        m.m[2]*v.x+m.m[6]*v.y+m.m[10]*v.z+m.m[14]*v.w,
        m.m[3]*v.x+m.m[7]*v.y+m.m[11]*v.z+m.m[15]*v.w
    };
}
static Mat4 perspective(float fovY,float aspect,float n,float f){
    float t=1.f/tanf(fovY*.5f); Mat4 r;
    r.m[0]=t/aspect; r.m[5]=t;
    r.m[10]=(f+n)/(n-f); r.m[11]=-1.f;
    r.m[14]=2.f*f*n/(n-f); return r;
}
static Vec3 norm3(Vec3 v){
    float l=sqrtf(v.x*v.x+v.y*v.y+v.z*v.z)+1e-9f;
    return {v.x/l,v.y/l,v.z/l};
}
static Vec3 cross3(Vec3 a,Vec3 b){
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
static float dot3(Vec3 a,Vec3 b){ return a.x*b.x+a.y*b.y+a.z*b.z; }
static Vec3 add3(Vec3 a,Vec3 b){ return {a.x+b.x,a.y+b.y,a.z+b.z}; }
static Vec3 sub3(Vec3 a,Vec3 b){ return {a.x-b.x,a.y-b.y,a.z-b.z}; }
static Vec3 scale3(Vec3 v,float s){ return {v.x*s,v.y*s,v.z*s}; }
static float len3(Vec3 v){ return sqrtf(v.x*v.x+v.y*v.y+v.z*v.z); }

static Mat4 lookAt(Vec3 eye,Vec3 cen,Vec3 up){
    Vec3 f=norm3({cen.x-eye.x,cen.y-eye.y,cen.z-eye.z});
    Vec3 r=norm3(cross3(f,up));
    Vec3 u=cross3(r,f);
    Mat4 m=identity();
    m.m[0]=r.x; m.m[4]=r.y; m.m[8]=r.z;
    m.m[1]=u.x; m.m[5]=u.y; m.m[9]=u.z;
    m.m[2]=-f.x;m.m[6]=-f.y;m.m[10]=-f.z;
    m.m[12]=-dot3(r,eye); m.m[13]=-dot3(u,eye); m.m[14]=dot3(f,eye);
    return m;
}
static Mat4 viewRotOnly(const Mat4& view){
    Mat4 r=view; r.m[12]=r.m[13]=r.m[14]=0.f; return r;
}
static Mat4 scaleMat(float sx,float sy,float sz){
    Mat4 m=identity(); m.m[0]=sx; m.m[5]=sy; m.m[10]=sz; return m;
}

// ============================================================================
//  HSV -> RGB
// ============================================================================
static void hsvToRgb(float h,float s,float v,float& r,float& g,float& b){
    h=fmodf(h,1.f); if(h<0)h+=1.f;
    int i=(int)(h*6.f);
    float f=h*6.f-i,p=v*(1-s),q=v*(1-f*s),t=v*(1-(1-f)*s);
    switch(i%6){
        case 0:r=v;g=t;b=p;break; case 1:r=q;g=v;b=p;break;
        case 2:r=p;g=v;b=t;break; case 3:r=p;g=q;b=v;break;
        case 4:r=t;g=p;b=v;break; default:r=v;g=p;b=q;break;
    }
}

// ============================================================================
//  Procedural vertex colour generation
// ============================================================================
enum class VCMode { NormalRainbow=0, PositionGradient=1, MeshHue=2 };

static void generateVertexColours(const aiMesh* mesh,VCMode mode,float meshHue,
                                   std::vector<float>& rgba){
    unsigned N=mesh->mNumVertices; rgba.resize(N*4);
    float mnX=1e30f,mnY=1e30f,mnZ=1e30f,mxX=-1e30f,mxY=-1e30f,mxZ=-1e30f;
    for(unsigned i=0;i<N;++i){
        float x=mesh->mVertices[i].x,y=mesh->mVertices[i].y,z=mesh->mVertices[i].z;
        mnX=fminf(mnX,x);mxX=fmaxf(mxX,x);
        mnY=fminf(mnY,y);mxY=fmaxf(mxY,y);
        mnZ=fminf(mnZ,z);mxZ=fmaxf(mxZ,z);
    }
    float rngX=mxX-mnX+1e-9f,rngY=mxY-mnY+1e-9f,rngZ=mxZ-mnZ+1e-9f;
    for(unsigned i=0;i<N;++i){
        float r=1,g=1,b=1,a=1;
        if(mode==VCMode::NormalRainbow){
            float nx=(mesh->HasNormals()?mesh->mNormals[i].x:0.f)*0.5f+0.5f;
            float ny=(mesh->HasNormals()?mesh->mNormals[i].y:1.f)*0.5f+0.5f;
            float nz=(mesh->HasNormals()?mesh->mNormals[i].z:0.f)*0.5f+0.5f;
            float hue=fmodf(meshHue+atan2f(nz-0.5f,nx-0.5f)/(2.f*3.14159265f)+0.5f,1.f);
            hsvToRgb(hue,0.6f+0.4f*ny,0.95f,r,g,b);
        } else if(mode==VCMode::PositionGradient){
            float tx=(mesh->mVertices[i].x-mnX)/rngX;
            float ty=(mesh->mVertices[i].y-mnY)/rngY;
            float tz=(mesh->mVertices[i].z-mnZ)/rngZ;
            float hue=fmodf(meshHue+ty*0.65f+tx*0.15f+tz*0.1f,1.f);
            hsvToRgb(hue,0.75f+0.25f*tz,0.75f+0.25f*ty,r,g,b);
        } else {
            float nx=(mesh->HasNormals()?mesh->mNormals[i].x:0.f)*0.5f+0.5f;
            float ny=(mesh->HasNormals()?mesh->mNormals[i].y:1.f)*0.5f+0.5f;
            float nz=(mesh->HasNormals()?mesh->mNormals[i].z:0.f)*0.5f+0.5f;
            hsvToRgb(meshHue,0.7f,0.55f+0.45f*(nx*0.3f+ny*0.5f+nz*0.2f),r,g,b);
        }
        rgba[i*4]=r; rgba[i*4+1]=g; rgba[i*4+2]=b; rgba[i*4+3]=a;
    }
}

// ============================================================================
//  Collision geometry
//  CPU-side triangle soup built once per scene load, pre-scaled by MODEL_SCALE.
// ============================================================================
struct Triangle {
    Vec3 v0,v1,v2;
};
static std::vector<Triangle> g_collisionTris;

// Möller–Trumbore ray-triangle intersection.
// Returns true+t if the ray hits the front face within (0, inf).
static bool rayTriIntersect(Vec3 orig,Vec3 dir,const Triangle& tri,float& t){
    const float EPS=1e-7f;
    Vec3 e1=sub3(tri.v1,tri.v0);
    Vec3 e2=sub3(tri.v2,tri.v0);
    Vec3 h =cross3(dir,e2);
    float a=dot3(e1,h);
    if(a>-EPS&&a<EPS) return false;
    float f=1.f/a;
    Vec3  s=sub3(orig,tri.v0);
    float u=f*dot3(s,h);
    if(u<0.f||u>1.f) return false;
    Vec3  q=cross3(s,e1);
    float v=f*dot3(dir,q);
    if(v<0.f||u+v>1.f) return false;
    t=f*dot3(e2,q);
    return t>EPS;
}

// Cast a downward ray from origin. Returns true and sets hitY to the
// highest surface Y within maxDist below origin.
static bool castRayDown(Vec3 origin, float maxDist, float& hitY){
    Vec3 dir={0.f,-1.f,0.f};
    bool found=false;
    float bestT=maxDist;
    for(const auto& tri : g_collisionTris){
        float t;
        if(rayTriIntersect(origin,dir,tri,t) && t<bestT){
            bestT=t;
            found=true;
        }
    }
    hitY = origin.y - bestT;
    return found;
}

// Build the collision BVH (flat list for now) from an Assimp scene.
static void buildCollisionMesh(const aiScene* scene, float scale){
    g_collisionTris.clear();
    size_t cnt=0;
    for(unsigned mi=0;mi<scene->mNumMeshes;++mi) cnt+=scene->mMeshes[mi]->mNumFaces;
    g_collisionTris.reserve(cnt);
    for(unsigned mi=0;mi<scene->mNumMeshes;++mi){
        const aiMesh* mesh=scene->mMeshes[mi];
        for(unsigned fi=0;fi<mesh->mNumFaces;++fi){
            const aiFace& face=mesh->mFaces[fi];
            if(face.mNumIndices!=3) continue;
            auto vv=[&](unsigned idx)->Vec3{
                return { mesh->mVertices[idx].x*scale,
                         mesh->mVertices[idx].y*scale,
                         mesh->mVertices[idx].z*scale };
            };
            g_collisionTris.push_back({vv(face.mIndices[0]),
                                       vv(face.mIndices[1]),
                                       vv(face.mIndices[2])});
        }
    }
    printf("[physics] collision tris: %zu\n", g_collisionTris.size());
}

// ============================================================================
//  Camera
// ============================================================================
struct Camera {
    Vec3  pos={0,0,5};
    float yaw=-90.f,pitch=0.f;
    float walkSpeed=4.f;      // world units per second while walking
    float runSpeed =10.f;     // world units per second while running (Shift)
    float turnSens =90.f;     // degrees per second via arrow keys
    float scrollSens=0.5f;
    // Legacy free-fly (noclip)
    float moveSens=5.f;
    float sprintMult=3.f;
    // Derived
    Vec3 forward={0,0,-1}, right={1,0,0}, up={0,1,0};
    Vec3 flatForward={0,0,-1}, flatRight={1,0,0}; // XZ-only directions
    void update(){
        float yR=yaw*(3.14159265f/180.f), pR=pitch*(3.14159265f/180.f);
        forward=norm3({cosf(pR)*cosf(yR), sinf(pR), cosf(pR)*sinf(yR)});
        right  =norm3(cross3(forward,{0,1,0}));
        up     =cross3(right,forward);
        flatForward=norm3({cosf(yR),0.f,sinf(yR)});
        flatRight  =norm3(cross3(flatForward,{0,1,0}));
    }
    void clampPitch(){ pitch=fmaxf(-89.f,fminf(89.f,pitch)); }
};

// ============================================================================
//  Physics state
// ============================================================================
struct PhysicsState {
    float velY     = 0.f;
    bool  grounded = false;
    bool  noclip   = false;  // F8: disable gravity/collision for free-fly

    static constexpr float GRAVITY      = -28.f;   // units/s²
    static constexpr float JUMP_VEL     =  10.f;   // initial jump velocity (units/s)
    static constexpr float EYE_HEIGHT   =   1.8f;  // camera above floor
    static constexpr float STEP_UP      =   0.5f;  // step-up tolerance (stairs)
    static constexpr float GROUND_SKIN  =   0.05f; // snap-to-ground tolerance
    static constexpr float TERMINAL_VEL = -40.f;   // max fall speed
};

struct InputState {
    bool w=false,s=false,a=false,d=false;
    bool space=false,ctrl=false,shift=false;
    bool arrowUp=false,arrowDown=false,arrowLeft=false,arrowRight=false;
};

struct Mesh {
    GLuint vao=0,vbo=0,ebo=0,indexCount=0,diffuseTex=0;
    bool hasImportedVertexColor=false,hasProceduralVertexColor=true;
    float flatColor[3]={0.8f,0.8f,0.8f};
};

struct RenderState {
    bool wireframe=false,backfaceCull=true,showHUD=true;
    bool bloom=true,forceVertexCol=false;
    float bloomThresh=0.65f,bloomStrength=1.2f;
    float fps=0.f,frameMs=0.f;
    int drawCalls=0,totalTris=0,meshCount=0,texCount=0;
};

// ============================================================================
//  5x7 bitmap font
// ============================================================================
static void kFont5x7_init(unsigned char f[128][5]){
    auto s=[&](int c,unsigned char a,unsigned char b,unsigned char cc,unsigned char d,unsigned char e){
        f[c][0]=a;f[c][1]=b;f[c][2]=cc;f[c][3]=d;f[c][4]=e;
    };
    s('!',0,0,0x5F,0,0);s('"',0,7,0,7,0);s('#',0x14,0x7F,0x14,0x7F,0x14);
    s('%',0x23,0x13,0x08,0x64,0x62);s('(',0,0x1C,0x22,0x41,0);s(')',0,0x41,0x22,0x1C,0);
    s('+',0x08,0x08,0x3E,0x08,0x08);s(',',0,0x50,0x30,0,0);s('-',0x08,0x08,0x08,0x08,0x08);
    s('.',0,0x60,0x60,0,0);s('/',0x20,0x10,0x08,0x04,0x02);
    s('0',0x3E,0x51,0x49,0x45,0x3E);s('1',0,0x42,0x7F,0x40,0);
    s('2',0x42,0x61,0x51,0x49,0x46);s('3',0x21,0x41,0x45,0x4B,0x31);
    s('4',0x18,0x14,0x12,0x7F,0x10);s('5',0x27,0x45,0x45,0x45,0x39);
    s('6',0x3C,0x4A,0x49,0x49,0x30);s('7',0x01,0x71,0x09,0x05,0x03);
    s('8',0x36,0x49,0x49,0x49,0x36);s('9',0x06,0x49,0x49,0x29,0x1E);
    s(':',0,0x36,0x36,0,0);s('<',0x08,0x14,0x22,0x41,0);
    s('=',0x14,0x14,0x14,0x14,0x14);s('>',0,0x41,0x22,0x14,0x08);
    s('@',0x3E,0x41,0x5D,0x55,0x1E);
    s('A',0x7E,0x09,0x09,0x09,0x7E);s('B',0x7F,0x49,0x49,0x49,0x36);
    s('C',0x3E,0x41,0x41,0x41,0x22);s('D',0x7F,0x41,0x41,0x22,0x1C);
    s('E',0x7F,0x49,0x49,0x49,0x41);s('F',0x7F,0x09,0x09,0x09,0x01);
    s('G',0x3E,0x41,0x49,0x49,0x7A);s('H',0x7F,0x08,0x08,0x08,0x7F);
    s('I',0,0x41,0x7F,0x41,0);s('J',0x20,0x40,0x41,0x3F,0x01);
    s('K',0x7F,0x08,0x14,0x22,0x41);s('L',0x7F,0x40,0x40,0x40,0x40);
    s('M',0x7F,0x02,0x0C,0x02,0x7F);s('N',0x7F,0x04,0x08,0x10,0x7F);
    s('O',0x3E,0x41,0x41,0x41,0x3E);s('P',0x7F,0x09,0x09,0x09,0x06);
    s('Q',0x3E,0x41,0x51,0x21,0x5E);s('R',0x7F,0x09,0x19,0x29,0x46);
    s('S',0x46,0x49,0x49,0x49,0x31);s('T',0x01,0x01,0x7F,0x01,0x01);
    s('U',0x3F,0x40,0x40,0x40,0x3F);s('V',0x1F,0x20,0x40,0x20,0x1F);
    s('W',0x3F,0x40,0x38,0x40,0x3F);s('X',0x63,0x14,0x08,0x14,0x63);
    s('Y',0x07,0x08,0x70,0x08,0x07);s('Z',0x61,0x51,0x49,0x45,0x43);
    s('[',0,0x7F,0x41,0x41,0);s('\\',0x02,0x04,0x08,0x10,0x20);
    s(']',0,0x41,0x41,0x7F,0);s('_',0x40,0x40,0x40,0x40,0x40);
    s('a',0x20,0x54,0x54,0x54,0x78);s('b',0x7F,0x48,0x44,0x44,0x38);
    s('c',0x38,0x44,0x44,0x44,0x20);s('d',0x38,0x44,0x44,0x48,0x7F);
    s('e',0x38,0x54,0x54,0x54,0x18);s('f',0x08,0x7E,0x09,0x01,0x02);
    s('g',0x0C,0x52,0x52,0x52,0x3E);s('h',0x7F,0x08,0x04,0x04,0x78);
    s('i',0,0x44,0x7D,0x40,0);s('j',0x20,0x40,0x44,0x3D,0);
    s('k',0x7F,0x10,0x28,0x44,0);s('l',0,0x41,0x7F,0x40,0);
    s('m',0x7C,0x04,0x18,0x04,0x78);s('n',0x7C,0x08,0x04,0x04,0x78);
    s('o',0x38,0x44,0x44,0x44,0x38);s('p',0x7C,0x14,0x14,0x14,0x08);
    s('q',0x08,0x14,0x14,0x18,0x7C);s('r',0x7C,0x08,0x04,0x04,0x08);
    s('s',0x48,0x54,0x54,0x54,0x20);s('t',0x04,0x3F,0x44,0x40,0x20);
    s('u',0x3C,0x40,0x40,0x20,0x7C);s('v',0x1C,0x20,0x40,0x20,0x1C);
    s('w',0x3C,0x40,0x30,0x40,0x3C);s('x',0x44,0x28,0x10,0x28,0x44);
    s('y',0x0C,0x50,0x50,0x50,0x3C);s('z',0x44,0x64,0x54,0x4C,0x44);
}
static unsigned char kFont5x7[128][5]={};

// ============================================================================
//  Shader sources
// ============================================================================
static const char* hudVS=R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main(){ gl_Position=vec4(aPos,0,1); vUV=aUV; }
)";
static const char* hudFS=R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uFont;
uniform vec4 uColor;
out vec4 FragColor;
void main(){
    float a=texture(uFont,vUV).r;
    if(a<0.1)discard;
    FragColor=vec4(uColor.rgb,uColor.a*a);
}
)";

static const char* vertSrc=R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in vec4 aColor;
uniform mat4 uMVP,uModel;
out vec3 vNormal; out vec2 vUV; out vec4 vColor; out vec3 vWorldPos;
void main(){
    gl_Position=uMVP*vec4(aPos,1);
    vNormal=mat3(uModel)*aNormal; vUV=aUV; vColor=aColor;
    vWorldPos=(uModel*vec4(aPos,1)).xyz;
}
)";
static const char* fragSrc=R"(
#version 330 core
in vec3 vNormal; in vec2 vUV; in vec4 vColor; in vec3 vWorldPos;
uniform sampler2D uDiffuse;
uniform bool uHasTexture,uHasVertexColor,uForceVertexCol;
uniform vec3 uFlatColor,uLightDir;
out vec4 FragColor;
void main(){
    vec4 base;
    if(uHasTexture&&!uForceVertexCol){ base=texture(uDiffuse,vUV); if(uHasVertexColor)base*=vColor; }
    else if(uHasVertexColor){ base=vColor; }
    else { base=vec4(uFlatColor,1); }
    if(base.a<0.01)discard;
    vec3 N=normalize(vNormal);
    float d=max(dot(N,uLightDir),0.0);
    FragColor=vec4(base.rgb*(0.35+0.65*d),base.a);
}
)";

static const char* fsTriVS=R"(
#version 330 core
out vec2 vUV;
void main(){
    vec2 p=vec2((gl_VertexID&1)*4.0-1.0,(gl_VertexID&2)*2.0-1.0);
    vUV=p*0.5+0.5; gl_Position=vec4(p,0,1);
}
)";

static const char* brightFS=R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uScene;
uniform float uThreshold;
out vec4 FragColor;
void main(){
    vec3 c=texture(uScene,vUV).rgb;
    float luma=dot(c,vec3(0.2126,0.7152,0.0722));
    float knee=0.1;
    float remap=smoothstep(uThreshold-knee,uThreshold+knee,luma);
    FragColor=vec4(c*remap,1);
}
)";

static const char* blurFS=R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
uniform vec2 uDir;
out vec4 FragColor;
void main(){
    const float w[5]=float[](0.227027,0.194595,0.121622,0.054054,0.016216);
    vec3 r=texture(uTex,vUV).rgb*w[0];
    for(int i=1;i<5;++i){
        r+=texture(uTex,vUV+uDir*float(i)).rgb*w[i];
        r+=texture(uTex,vUV-uDir*float(i)).rgb*w[i];
    }
    FragColor=vec4(r,1);
}
)";

static const char* compositeFS=R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uScene,uBloom;
uniform float uStrength;
out vec4 FragColor;
vec3 reinhardBloom(vec3 x){ return x/(x+vec3(1)); }
void main(){
    vec3 scene=clamp(texture(uScene,vUV).rgb,0.0,1.0);
    vec3 bloom=reinhardBloom(texture(uBloom,vUV).rgb*uStrength);
    float bloomLuma=dot(bloom,vec3(0.2126,0.7152,0.0722));
    vec3 col=scene+vec3(bloomLuma);
    col=pow(clamp(col,0.0,1.0),vec3(1.0/2.2));
    FragColor=vec4(col,1);
}
)";

static const char* skyVS=R"(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uVP; uniform float uRadius;
out vec3 vDir;
void main(){ vDir=aPos; vec4 clip=uVP*vec4(aPos*uRadius,1); gl_Position=clip.xyww; }
)";
static const char* skyFS=R"(
#version 330 core
in vec3 vDir;
out vec4 FragColor;
void main(){
    float t=clamp(vDir.y,0.0,1.0);
    vec3 col=mix(vec3(0.54,0.81,0.94),vec3(0.05,0.10,0.35),t);
    FragColor=vec4(col,1);
}
)";

static const char* sunVS=R"(
#version 330 core
layout(location=0) in vec2 aPos;
uniform vec2  uSunNDC;
uniform float uSunSize;
uniform float uAspect;
out vec2 vUV;
void main(){
    vUV=aPos;
    vec2 pos=uSunNDC+aPos*vec2(uSunSize/uAspect,uSunSize);
    gl_Position=vec4(pos,0.9999,1.0);
}
)";
static const char* sunFS=R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
void main(){
    float d=length(vUV);
    if(d>1.0) discard;
    float core  =smoothstep(0.18,0.0,d);
    float corona=smoothstep(1.0,0.0,d)*0.65;
    vec3 coreCol  =vec3(9.0, 8.5, 6.5);
    vec3 coronaCol=vec3(4.0, 2.0, 0.4);
    vec3 col=coronaCol*corona + coreCol*core;
    float alpha=clamp(corona+core,0.0,1.0);
    if(alpha<0.001) discard;
    FragColor=vec4(col,alpha);
}
)";

static const char* flareVS=R"(
#version 330 core
layout(location=0) in vec2 aPos;
uniform vec2  uCenter;
uniform float uRadius;
uniform float uAspect;
out vec2 vUV;
void main(){
    vUV=aPos;
    vec2 pos=uCenter+aPos*vec2(uRadius/uAspect,uRadius);
    gl_Position=vec4(pos,0,1);
}
)";
static const char* flareFS=R"(
#version 330 core
in vec2 vUV;
uniform vec4 uColor;
uniform int  uType;
out vec4 FragColor;
float hexDist(vec2 p){
    p=abs(p);
    float c=dot(p,normalize(vec2(1.0,1.732)));
    c=max(c,p.x);
    return c;
}
void main(){
    float d=length(vUV);
    float alpha=0.0;
    if(uType==0){
        alpha=pow(smoothstep(1.0,0.0,d),1.4);
    } else if(uType==1){
        float ring=abs(d-0.65);
        alpha=smoothstep(0.18,0.0,ring)*0.9;
    } else if(uType==2){
        float hd=hexDist(vUV);
        float rim=abs(hd-0.75);
        alpha=smoothstep(0.09,0.0,rim)*0.85;
        alpha+=smoothstep(0.85,0.0,hd)*0.07;
    } else {
        float streak=smoothstep(0.06,0.0,abs(vUV.y));
        streak*=smoothstep(1.0,0.0,abs(vUV.x));
        alpha=streak*0.75;
    }
    if(alpha<0.002) discard;
    FragColor=vec4(uColor.rgb,uColor.a*alpha);
}
)";

// ============================================================================
//  BloomFBO
// ============================================================================
struct BloomFBO {
    GLuint fbo[2]={0,0},tex[2]={0,0};
    GLuint sceneFBO=0,sceneTex=0,sceneDepth=0;
    int w=0,h=0;
    void init(int ww,int hh){
        w=ww; h=hh;
        glGenFramebuffers(1,&sceneFBO); glGenTextures(1,&sceneTex);
        glBindTexture(GL_TEXTURE_2D,sceneTex);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,w,h,0,GL_RGBA,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glGenRenderbuffers(1,&sceneDepth);
        glBindRenderbuffer(GL_RENDERBUFFER,sceneDepth);
        glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,w,h);
        glBindFramebuffer(GL_FRAMEBUFFER,sceneFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,sceneTex,0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,sceneDepth);
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        int bw=(int)fmaxf(1,ww/2),bh=(int)fmaxf(1,hh/2);
        glGenFramebuffers(2,fbo); glGenTextures(2,tex);
        for(int i=0;i<2;++i){
            glBindTexture(GL_TEXTURE_2D,tex[i]);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,bw,bh,0,GL_RGBA,GL_FLOAT,nullptr);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glBindFramebuffer(GL_FRAMEBUFFER,fbo[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex[i],0);
        }
        glBindFramebuffer(GL_FRAMEBUFFER,0);
    }
    void resize(int ww,int hh){ if(ww==w&&hh==h)return; destroy(); init(ww,hh); }
    void destroy(){
        if(sceneFBO) glDeleteFramebuffers(1,&sceneFBO);
        if(sceneTex) glDeleteTextures(1,&sceneTex);
        if(sceneDepth) glDeleteRenderbuffers(1,&sceneDepth);
        glDeleteFramebuffers(2,fbo); glDeleteTextures(2,tex);
        sceneFBO=sceneTex=sceneDepth=fbo[0]=fbo[1]=tex[0]=tex[1]=0; w=h=0;
    }
};

// ============================================================================
//  Globals
// ============================================================================
static Camera      g_cam;
static InputState  g_input;
static RenderState g_rs;
static PhysicsState g_phys;

static Vec3 g_sunDir={0.35f,0.80f,-0.45f};
static float g_flareVis=0.f;

static bool g_sceneSwitch = false;
static int  g_pendingScene = -1;
static int  g_currentScene = 1;

// ============================================================================
//  GLFW callbacks
// ============================================================================
static void scrollCB(GLFWwindow*,double,double dy){
    g_cam.moveSens=fmaxf(0.5f,g_cam.moveSens+(float)dy*g_cam.scrollSens);
}
static void keyCB(GLFWwindow* win,int key,int,int action,int){
    bool p=(action!=GLFW_RELEASE);
    if(key==GLFW_KEY_ESCAPE&&p) glfwSetWindowShouldClose(win,true);
    if(key==GLFW_KEY_F1&&p) g_rs.showHUD=!g_rs.showHUD;
    if(key==GLFW_KEY_F2&&p) g_rs.wireframe=!g_rs.wireframe;
    if(key==GLFW_KEY_F3&&p) g_rs.backfaceCull=!g_rs.backfaceCull;
    if(key==GLFW_KEY_F4&&p) g_rs.bloom=!g_rs.bloom;
    if(key==GLFW_KEY_F5&&p) g_rs.forceVertexCol=!g_rs.forceVertexCol;
    if(key==GLFW_KEY_F8&&p){
        g_phys.noclip=!g_phys.noclip;
        g_phys.velY=0.f;
        printf("[physics] noclip %s\n",g_phys.noclip?"ON":"OFF");
    }
    if(key==GLFW_KEY_F6&&p&&g_currentScene!=1){ g_pendingScene=1; g_sceneSwitch=true; }
    if(key==GLFW_KEY_F7&&p&&g_currentScene!=2){ g_pendingScene=2; g_sceneSwitch=true; }
    if(key==GLFW_KEY_LEFT_BRACKET&&p)  g_rs.bloomThresh=fmaxf(0.f,g_rs.bloomThresh-0.05f);
    if(key==GLFW_KEY_RIGHT_BRACKET&&p) g_rs.bloomThresh=fminf(2.f,g_rs.bloomThresh+0.05f);
    if(key==GLFW_KEY_MINUS&&p)  g_rs.bloomStrength=fmaxf(0.f,g_rs.bloomStrength-0.1f);
    if(key==GLFW_KEY_EQUAL&&p)  g_rs.bloomStrength=fminf(5.f,g_rs.bloomStrength+0.1f);
    if(key==GLFW_KEY_W) g_input.w=p; if(key==GLFW_KEY_S) g_input.s=p;
    if(key==GLFW_KEY_A) g_input.a=p; if(key==GLFW_KEY_D) g_input.d=p;
    if(key==GLFW_KEY_SPACE) g_input.space=p;
    if(key==GLFW_KEY_LEFT_CONTROL||key==GLFW_KEY_RIGHT_CONTROL) g_input.ctrl=p;
    if(key==GLFW_KEY_LEFT_SHIFT||key==GLFW_KEY_RIGHT_SHIFT) g_input.shift=p;
    if(key==GLFW_KEY_UP)    g_input.arrowUp=p;
    if(key==GLFW_KEY_DOWN)  g_input.arrowDown=p;
    if(key==GLFW_KEY_LEFT)  g_input.arrowLeft=p;
    if(key==GLFW_KEY_RIGHT) g_input.arrowRight=p;
}

// ============================================================================
//  Shader helpers
// ============================================================================
static GLuint compileShader(GLenum type,const char* src){
    GLuint s=glCreateShader(type);
    glShaderSource(s,1,&src,nullptr); glCompileShader(s);
    GLint ok; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){ char log[512]; glGetShaderInfoLog(s,512,nullptr,log);
             fprintf(stderr,"Shader error:\n%s\n",log); }
    return s;
}
static GLuint buildProgram(const char* vs,const char* fs){
    GLuint v=compileShader(GL_VERTEX_SHADER,vs);
    GLuint f=compileShader(GL_FRAGMENT_SHADER,fs);
    GLuint p=glCreateProgram();
    glAttachShader(p,v); glAttachShader(p,f); glLinkProgram(p);
    GLint ok; glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){ char log[512]; glGetProgramInfoLog(p,512,nullptr,log);
             fprintf(stderr,"Link error:\n%s\n",log); }
    glDeleteShader(v); glDeleteShader(f); return p;
}

// ============================================================================
//  Skysphere
// ============================================================================
struct Skysphere {
    GLuint vao=0,vbo=0,ebo=0,prog=0; GLuint indexCount=0;
    GLint locVP=0,locRadius=0;
    void init(int stacks=32,int slices=64){
        prog=buildProgram(skyVS,skyFS);
        locVP=glGetUniformLocation(prog,"uVP");
        locRadius=glGetUniformLocation(prog,"uRadius");
        std::vector<float> verts; std::vector<unsigned> idxs;
        const float PI=3.14159265f;
        for(int i=0;i<=stacks;++i){
            float phi=PI*(float)i/(float)stacks,y=cosf(phi),sp=sinf(phi);
            for(int j=0;j<=slices;++j){
                float th=2.f*PI*(float)j/(float)slices;
                verts.push_back(sp*cosf(th)); verts.push_back(y); verts.push_back(sp*sinf(th));
            }
        }
        for(int i=0;i<stacks;++i) for(int j=0;j<slices;++j){
            unsigned a=i*(slices+1)+j,b=a+slices+1;
            idxs.push_back(a);idxs.push_back(b);idxs.push_back(a+1);
            idxs.push_back(b);idxs.push_back(b+1);idxs.push_back(a+1);
        }
        indexCount=(GLuint)idxs.size();
        glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo); glGenBuffers(1,&ebo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glBufferData(GL_ARRAY_BUFFER,verts.size()*4,verts.data(),GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,idxs.size()*4,idxs.data(),GL_STATIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,12,(void*)0);
        glEnableVertexAttribArray(0); glBindVertexArray(0);
    }
    void draw(const Mat4& viewRot,const Mat4& proj,float radius){
        Mat4 vp=mul(proj,viewRot);
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND);
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        glEnable(GL_CULL_FACE); glCullFace(GL_FRONT);
        glUseProgram(prog);
        glUniformMatrix4fv(locVP,1,GL_FALSE,vp.m);
        glUniform1f(locRadius,radius);
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES,indexCount,GL_UNSIGNED_INT,nullptr);
        glBindVertexArray(0); glDepthMask(GL_TRUE); glCullFace(GL_BACK);
    }
    void destroy(){
        glDeleteVertexArrays(1,&vao); glDeleteBuffers(1,&vbo);
        glDeleteBuffers(1,&ebo); glDeleteProgram(prog);
    }
};

// ============================================================================
//  Sun billboard
// ============================================================================
struct Sun {
    GLuint vao=0,vbo=0,prog=0;
    GLint locNDC=0,locSize=0,locAspect=0;
    void init(){
        prog=buildProgram(sunVS,sunFS);
        locNDC   =glGetUniformLocation(prog,"uSunNDC");
        locSize  =glGetUniformLocation(prog,"uSunSize");
        locAspect=glGetUniformLocation(prog,"uAspect");
        float q[]={-1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1};
        glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo);
        glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glBufferData(GL_ARRAY_BUFFER,sizeof(q),q,GL_STATIC_DRAW);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,8,(void*)0);
        glEnableVertexAttribArray(0); glBindVertexArray(0);
    }
    Vec2 draw(const Mat4& proj,const Mat4& viewRot,Vec3 sunDir,
              float ndcSize,int ww,int wh,bool& sunOnScreen){
        Mat4 vp=mul(proj,viewRot);
        Vec4 clip=mulVec4(vp,{sunDir.x,sunDir.y,sunDir.z,1.f});
        sunOnScreen=false;
        Vec2 ndc={0,0};
        if(clip.w>0.0001f){
            ndc={clip.x/clip.w,clip.y/clip.w};
            if(clip.z/clip.w<1.f &&
               ndc.x>-1.6f&&ndc.x<1.6f&&ndc.y>-1.6f&&ndc.y<1.6f)
                sunOnScreen=true;
        }
        if(!sunOnScreen) return ndc;
        float aspect=(float)ww/(float)wh;
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND); glBlendFunc(GL_ONE,GL_ONE);
        glDisable(GL_DEPTH_TEST);
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        glDisable(GL_CULL_FACE);
        glUseProgram(prog);
        glUniform2f(locNDC,ndc.x,ndc.y);
        glUniform1f(locSize,ndcSize);
        glUniform1f(locAspect,aspect);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES,0,6);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
        return ndc;
    }
    void destroy(){
        glDeleteVertexArrays(1,&vao); glDeleteBuffers(1,&vbo); glDeleteProgram(prog);
    }
};

// ============================================================================
//  Lens flare
// ============================================================================
struct FlareElem { float offset,radius,r,g,b,a; int type; };
static const FlareElem kFlares[]={
    {  0.08f,  0.055f, 1.0f,  0.92f, 0.45f, 0.60f,  1 },
    {  0.18f,  0.17f,  0.15f, 0.55f, 1.0f,  0.38f,  0 },
    {  0.30f,  0.08f,  1.0f,  0.50f, 0.10f, 0.50f,  2 },
    {  0.44f,  0.12f,  0.75f, 0.15f, 1.0f,  0.32f,  1 },
    {  0.58f,  0.20f,  0.10f, 1.0f,  0.45f, 0.28f,  0 },
    {  0.70f,  0.065f, 1.0f,  0.82f, 0.18f, 0.55f,  2 },
    {  0.84f,  0.28f,  0.25f, 0.50f, 1.0f,  0.20f,  0 },
    {  1.00f,  0.045f, 1.0f,  0.35f, 0.35f, 0.65f,  1 },
    {  1.18f,  0.14f,  0.95f, 0.95f, 1.0f,  0.24f,  2 },
    {  1.38f,  0.075f, 0.35f, 1.0f,  0.75f, 0.42f,  0 },
    {  0.00f,  1.60f,  1.0f,  0.88f, 0.55f, 0.10f,  3 },
};
static const int kFlareCount=(int)(sizeof(kFlares)/sizeof(kFlares[0]));

struct LensFlare {
    GLuint vao=0,vbo=0,prog=0;
    GLint locCenter=0,locRadius=0,locAspect=0,locColor=0,locType=0;
    void init(){
        prog=buildProgram(flareVS,flareFS);
        locCenter=glGetUniformLocation(prog,"uCenter");
        locRadius=glGetUniformLocation(prog,"uRadius");
        locAspect=glGetUniformLocation(prog,"uAspect");
        locColor =glGetUniformLocation(prog,"uColor");
        locType  =glGetUniformLocation(prog,"uType");
        float q[]={-1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1};
        glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo);
        glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glBufferData(GL_ARRAY_BUFFER,sizeof(q),q,GL_STATIC_DRAW);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,8,(void*)0);
        glEnableVertexAttribArray(0); glBindVertexArray(0);
    }
    void draw(Vec2 sunNDC,float visibility,int ww,int wh){
        if(visibility<0.002f) return;
        float aspect=(float)ww/(float)wh;
        Vec2 toC={-sunNDC.x,-sunNDC.y};
        glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE);
        glUseProgram(prog);
        glUniform1f(locAspect,aspect);
        glBindVertexArray(vao);
        for(int i=0;i<kFlareCount;++i){
            const FlareElem& e=kFlares[i];
            Vec2 cen={sunNDC.x+toC.x*e.offset,sunNDC.y+toC.y*e.offset};
            float edgeFade=fmaxf(0.f,1.f-sqrtf(cen.x*cen.x+cen.y*cen.y)*0.5f);
            float alpha=e.a*visibility*edgeFade;
            glUniform2f(locCenter,cen.x,cen.y);
            glUniform1f(locRadius,e.radius);
            glUniform4f(locColor,e.r,e.g,e.b,alpha);
            glUniform1i(locType,e.type);
            glDrawArrays(GL_TRIANGLES,0,6);
        }
        glBindVertexArray(0); glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
    }
    void destroy(){
        glDeleteVertexArrays(1,&vao); glDeleteBuffers(1,&vbo); glDeleteProgram(prog);
    }
};

// ============================================================================
//  Texture loader
// ============================================================================
static std::unordered_map<std::string,GLuint> g_texCache;
static GLuint loadTexture(const std::string& path){
    auto it=g_texCache.find(path);
    if(it!=g_texCache.end()) return it->second;
    int w,h,ch; stbi_set_flip_vertically_on_load(true);
    unsigned char* data=stbi_load(path.c_str(),&w,&h,&ch,4);
    if(!data){ fprintf(stderr,"[tex] cannot load: %s\n",path.c_str()); return 0; }
    GLuint tex; glGenTextures(1,&tex);
    glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    stbi_image_free(data);
    g_texCache[path]=tex;
    printf("[tex] loaded %s (%dx%d)\n",path.c_str(),w,h);
    return tex;
}

// ============================================================================
//  Model loading helpers
// ============================================================================
static Mesh processMesh(const aiMesh* mesh,const aiScene* scene,
                        const std::string& modelDir,
                        unsigned meshIndex,unsigned totalMeshes)
{
    const int STRIDE=12;
    std::vector<float> verts; verts.reserve(mesh->mNumVertices*STRIDE);
    bool hasVC=mesh->HasVertexColors(0);
    float meshHue=(totalMeshes>1)?(float)meshIndex/(float)totalMeshes:0.f;
    float flat[3]={0.7f,0.7f,0.7f};
    { float h=fmodf(meshIndex*137.508f,360.f)/360.f; hsvToRgb(h,0.6f,0.9f,flat[0],flat[1],flat[2]); }

    std::vector<float> vc;
    if(hasVC){
        vc.resize(mesh->mNumVertices*4);
        for(unsigned i=0;i<mesh->mNumVertices;++i){
            vc[i*4  ]=mesh->mColors[0][i].r; vc[i*4+1]=mesh->mColors[0][i].g;
            vc[i*4+2]=mesh->mColors[0][i].b; vc[i*4+3]=mesh->mColors[0][i].a;
        }
    } else {
        generateVertexColours(mesh,VCMode::PositionGradient,meshHue,vc);
    }

    for(unsigned i=0;i<mesh->mNumVertices;++i){
        float px=mesh->mVertices[i].x;
        float py=mesh->mVertices[i].y;
        float pz=mesh->mVertices[i].z;
        verts.push_back(px); verts.push_back(py); verts.push_back(pz);
        if(mesh->HasNormals()){
            verts.push_back(mesh->mNormals[i].x);
            verts.push_back(mesh->mNormals[i].y);
            verts.push_back(mesh->mNormals[i].z);
        } else { verts.push_back(0);verts.push_back(1);verts.push_back(0); }
        if(mesh->HasTextureCoords(0)){
            verts.push_back(mesh->mTextureCoords[0][i].x);
            verts.push_back(mesh->mTextureCoords[0][i].y);
        } else { verts.push_back(0);verts.push_back(0); }
        verts.push_back(vc[i*4]); verts.push_back(vc[i*4+1]);
        verts.push_back(vc[i*4+2]); verts.push_back(vc[i*4+3]);
    }

    std::vector<unsigned> idxs; idxs.reserve(mesh->mNumFaces*3);
    for(unsigned i=0;i<mesh->mNumFaces;++i)
        for(unsigned j=0;j<mesh->mFaces[i].mNumIndices;++j)
            idxs.push_back(mesh->mFaces[i].mIndices[j]);

    Mesh m;
    m.hasImportedVertexColor=hasVC; m.hasProceduralVertexColor=true;
    m.flatColor[0]=flat[0]; m.flatColor[1]=flat[1]; m.flatColor[2]=flat[2];
    m.indexCount=(GLuint)idxs.size();

    glGenVertexArrays(1,&m.vao); glGenBuffers(1,&m.vbo); glGenBuffers(1,&m.ebo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER,m.vbo);
    glBufferData(GL_ARRAY_BUFFER,verts.size()*4,verts.data(),GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,idxs.size()*4,idxs.data(),GL_STATIC_DRAW);
    size_t st=STRIDE*4;
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,(GLsizei)st,(void*)0);        glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,(GLsizei)st,(void*)(3*4));    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,(GLsizei)st,(void*)(6*4));    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,(GLsizei)st,(void*)(8*4));    glEnableVertexAttribArray(3);
    glBindVertexArray(0);

    if(mesh->mMaterialIndex<scene->mNumMaterials){
        aiMaterial* mat=scene->mMaterials[mesh->mMaterialIndex];
        auto ciEq=[](const std::string& a,const std::string& b)->bool{
            if(a.size()!=b.size())return false;
            for(size_t i=0;i<a.size();++i)
                if(tolower((unsigned char)a[i])!=tolower((unsigned char)b[i]))return false;
            return true;
        };
        auto scanDir=[&](const std::string& dir,const std::string& fname)->std::string{
            std::error_code ec;
            for(auto& e:fs::directory_iterator(dir,ec))
                if(e.is_regular_file(ec)&&ciEq(e.path().filename().string(),fname))
                    return e.path().string();
            return "";
        };
        auto resolveTexture=[&](const std::string& rawPath)->GLuint{
            if(!rawPath.empty()&&rawPath[0]=='*'){
                int idx=std::stoi(rawPath.substr(1));
                const aiTexture* emb=scene->mTextures[idx];
                int w2,h2,ch2; unsigned char* data=nullptr;
                if(emb->mHeight==0)
                    data=stbi_load_from_memory((unsigned char*)emb->pcData,emb->mWidth,&w2,&h2,&ch2,4);
                else{
                    w2=emb->mWidth;h2=emb->mHeight;data=new unsigned char[w2*h2*4];
                    for(int pp=0;pp<w2*h2;++pp){
                        auto& px=emb->pcData[pp];
                        data[pp*4]=px.r;data[pp*4+1]=px.g;data[pp*4+2]=px.b;data[pp*4+3]=px.a;
                    }
                }
                if(!data)return 0;
                GLuint tex; glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex);
                glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w2,h2,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
                glGenerateMipmap(GL_TEXTURE_2D);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
                if(emb->mHeight==0)stbi_image_free(data); else delete[]data;
                return tex;
            }
            std::string tp=rawPath;
            for(char& c:tp)if(c=='\\')c='/';
            if(tp.size()>=2&&tp[1]==':')tp=tp.substr(2);
            while(tp.size()>3&&tp.substr(0,3)=="../")tp=tp.substr(3);
            while(tp.size()>2&&tp.substr(0,2)=="./") tp=tp.substr(2);
            std::string fname=fs::path(tp).filename().string();
            if(fname.empty())return 0;
            std::vector<std::string> variants={fname,fname+".png"};
            std::vector<std::string> dirs={modelDir,modelDir+"/textures",modelDir+"/Textures",
                modelDir+"/texture",modelDir+"/maps",modelDir+"/..",
                modelDir+"/../textures",modelDir+"/../Textures"};
            for(auto& fv:variants){
                std::vector<std::string> exact={tp,modelDir+"/"+tp,modelDir+"/"+fv};
                for(auto& d:dirs)exact.push_back(d+"/"+fv);
                for(auto& cand:exact){std::error_code ec2;if(fs::exists(cand,ec2))return loadTexture(cand);}
            }
            for(auto& fv:variants)
                for(auto& d:dirs){auto f2=scanDir(d,fv);if(!f2.empty())return loadTexture(f2);}
            return 0;
        };
        aiTextureType tryTypes[]={aiTextureType_DIFFUSE,aiTextureType_BASE_COLOR,
            aiTextureType_SPECULAR,aiTextureType_AMBIENT,aiTextureType_EMISSIVE,
            aiTextureType_NORMALS,aiTextureType_DIFFUSE_ROUGHNESS,
            aiTextureType_METALNESS,aiTextureType_UNKNOWN};
        for(auto tt:tryTypes){
            for(unsigned slot=0;slot<mat->GetTextureCount(tt);++slot){
                aiString tp2; if(mat->GetTexture(tt,slot,&tp2)!=AI_SUCCESS)continue;
                std::string tps=tp2.C_Str(); if(tps.empty())continue;
                GLuint tex=resolveTexture(tps); if(tex){m.diffuseTex=tex; goto texDone;}
            }
        }
        texDone:;
    }
    return m;
}

static unsigned countMeshes(const aiNode* n){
    unsigned c=n->mNumMeshes;
    for(unsigned i=0;i<n->mNumChildren;++i)c+=countMeshes(n->mChildren[i]);
    return c;
}
static void processNode(const aiNode* node,const aiScene* scene,const std::string& dir,
                        std::vector<Mesh>& meshes,unsigned total,unsigned& idx){
    for(unsigned i=0;i<node->mNumMeshes;++i)
        meshes.push_back(processMesh(scene->mMeshes[node->mMeshes[i]],scene,dir,idx++,total));
    for(unsigned i=0;i<node->mNumChildren;++i)
        processNode(node->mChildren[i],scene,dir,meshes,total,idx);
}

static void boundingSphere(const aiScene* scene,Vec3& centre,float& radius,float modelScale=1.f){
    float mn[3]={1e30f,1e30f,1e30f},mx[3]={-1e30f,-1e30f,-1e30f};
    for(unsigned mi=0;mi<scene->mNumMeshes;++mi){
        const aiMesh* mesh=scene->mMeshes[mi];
        for(unsigned v=0;v<mesh->mNumVertices;++v){
            float x=mesh->mVertices[v].x*modelScale;
            float y=mesh->mVertices[v].y*modelScale;
            float z=mesh->mVertices[v].z*modelScale;
            if(x<mn[0])mn[0]=x;if(x>mx[0])mx[0]=x;
            if(y<mn[1])mn[1]=y;if(y>mx[1])mx[1]=y;
            if(z<mn[2])mn[2]=z;if(z>mx[2])mx[2]=z;
        }
    }
    centre={(mn[0]+mx[0])*.5f,(mn[1]+mx[1])*.5f,(mn[2]+mx[2])*.5f};
    float dx=mx[0]-mn[0],dy=mx[1]-mn[1],dz=mx[2]-mn[2];
    radius=sqrtf(dx*dx+dy*dy+dz*dz)*.5f;
}

// ============================================================================
//  HUD renderer
// ============================================================================
struct HUDRenderer {
    GLuint prog=0,vao=0,vbo=0,fontTex=0;
    GLint locFont=0,locColor=0;
    static constexpr int GW=6,GH=8,AW=128*6,AH=8;
    void init(){
        prog=buildProgram(hudVS,hudFS);
        locFont=glGetUniformLocation(prog,"uFont");
        locColor=glGetUniformLocation(prog,"uColor");
        std::vector<unsigned char> atlas(AW*AH*4,0);
        for(int c=0;c<128;++c){
            int ox=c*GW;
            for(int col=0;col<5;++col){
                unsigned char bits=kFont5x7[c][col];
                for(int row=0;row<7;++row)
                    if(bits&(1<<row)){
                        int px=(row*AW+(ox+col))*4;
                        atlas[px]=atlas[px+1]=atlas[px+2]=atlas[px+3]=255;
                    }
            }
        }
        glGenTextures(1,&fontTex); glBindTexture(GL_TEXTURE_2D,fontTex);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,AW,AH,0,GL_RGBA,GL_UNSIGNED_BYTE,atlas.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo);
        glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,16,(void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,16,(void*)8); glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }
    void drawString(const char* str,float px,float py,int ww,int wh,
                    float r=1,float g=1,float b=1,float a=1,float sc=2.f){
        std::vector<float> v; v.reserve(strlen(str)*24);
        float cx=px,cy=py;
        for(const char* p=str;*p;++p){
            int c=(unsigned char)*p; if(c<0||c>=128)c=' ';
            if(c=='\n'){cx=px;cy+=GH*sc+2;continue;}
            float u0=(float)(c*GW)/(float)AW,u1=(float)(c*GW+GW-1)/(float)AW;
            float v0=0.f,v1=(float)(GH-1)/(float)AH;
            float x0=2.f*cx/(float)ww-1.f,x1=2.f*(cx+GW*sc)/(float)ww-1.f;
            float y1=1.f-2.f*cy/(float)wh,y0=1.f-2.f*(cy+GH*sc)/(float)wh;
            v.insert(v.end(),{x0,y1,u0,v0, x1,y1,u1,v0, x0,y0,u0,v1,
                               x1,y1,u1,v0, x1,y0,u1,v1, x0,y0,u0,v1});
            cx+=GW*sc;
        }
        if(v.empty())return;
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glBufferData(GL_ARRAY_BUFFER,v.size()*4,v.data(),GL_DYNAMIC_DRAW);
        glUseProgram(prog);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,fontTex);
        glUniform1i(locFont,0); glUniform4f(locColor,r,g,b,a);
        glDrawArrays(GL_TRIANGLES,0,(GLsizei)(v.size()/4));
        glBindVertexArray(0);
    }
    void destroy(){
        glDeleteProgram(prog); glDeleteVertexArrays(1,&vao);
        glDeleteBuffers(1,&vbo); glDeleteTextures(1,&fontTex);
    }
};

static std::string fmt(const char* f,...){
    char buf[256]; va_list a; va_start(a,f); vsnprintf(buf,sizeof(buf),f,a); va_end(a); return buf;
}

// ============================================================================
//  Scene loading
// ============================================================================
static bool loadScene(const std::string& modelPath,
                      std::vector<Mesh>& meshes,
                      Vec3& outCentre, float& outRadius,
                      std::string& outModelDir, std::string& outModelName)
{
    {
        std::error_code ec;
        fprintf(stderr,"[load] cwd: %s\n", fs::current_path(ec).string().c_str());
    }
    fprintf(stderr,"[load] attempting: %s\n", modelPath.c_str());
    {
        std::error_code ec;
        if(!fs::exists(modelPath,ec)){
            fprintf(stderr,"[load] ERROR: file not found: %s\n",modelPath.c_str());
            for(const char* probe:{"models","./models","../models"}){
                if(fs::exists(probe,ec)){
                    fprintf(stderr,"[load] Contents of %s/:\n",probe);
                    for(auto& entry:fs::recursive_directory_iterator(probe,ec))
                        fprintf(stderr,"  %s\n",entry.path().string().c_str());
                    break;
                }
            }
            return false;
        }
    }
    for(auto& m:meshes){
        if(m.vao) glDeleteVertexArrays(1,&m.vao);
        if(m.vbo) glDeleteBuffers(1,&m.vbo);
        if(m.ebo) glDeleteBuffers(1,&m.ebo);
    }
    meshes.clear();

    unsigned int importFlags =
        aiProcess_Triangulate            |
        aiProcess_GenNormals             |
        aiProcess_FlipUVs                |
        aiProcess_CalcTangentSpace       |
        aiProcess_JoinIdenticalVertices  |
        aiProcess_SortByPType            |
        aiProcess_PreTransformVertices   |
        aiProcess_RemoveRedundantMaterials|
        aiProcess_FindDegenerates        |
        aiProcess_FindInvalidData;

    Assimp::Importer importer;
    fprintf(stderr,"[load] %s\n",modelPath.c_str());
    const aiScene* scene=importer.ReadFile(modelPath,importFlags);
    if(!scene||!scene->mRootNode||scene->mFlags&AI_SCENE_FLAGS_INCOMPLETE){
        const char* err=importer.GetErrorString();
        fprintf(stderr,"[load] Assimp error: %s\n",(err&&err[0])?err:"(empty)");
        return false;
    }
    fprintf(stderr,"[load] OK — meshes=%u  materials=%u\n",
            scene->mNumMeshes,scene->mNumMaterials);

    outModelDir=fs::path(modelPath).parent_path().string();
    if(outModelDir.empty()) outModelDir=".";
    outModelName=fs::path(modelPath).filename().string();

    const float MODEL_SCALE=25.f;
    boundingSphere(scene,outCentre,outRadius,MODEL_SCALE);
    if(outRadius<1e-5f) outRadius=1.f;

    unsigned total=countMeshes(scene->mRootNode),midx=0;
    processNode(scene->mRootNode,scene,outModelDir,meshes,total,midx);
    printf("[load] GPU meshes: %zu\n",meshes.size());

    // Build CPU-side collision mesh (triangles scaled by MODEL_SCALE)
    buildCollisionMesh(scene,MODEL_SCALE);
    return true;
}

// ============================================================================
//  Physics update — called every frame
// ============================================================================
static void physicsUpdate(float dt, float sceneRadius){
    if(g_phys.noclip){
        // Free-fly (original behaviour)
        float speed=g_cam.moveSens*dt*(g_input.shift?g_cam.sprintMult:1.f);
        if(g_input.w)   g_cam.pos=add3(g_cam.pos,scale3(g_cam.forward, speed));
        if(g_input.s)   g_cam.pos=add3(g_cam.pos,scale3(g_cam.forward,-speed));
        if(g_input.a)   g_cam.pos=add3(g_cam.pos,scale3(g_cam.right,  -speed));
        if(g_input.d)   g_cam.pos=add3(g_cam.pos,scale3(g_cam.right,   speed));
        if(g_input.space) g_cam.pos=add3(g_cam.pos,{0, speed,0});
        if(g_input.ctrl)  g_cam.pos=add3(g_cam.pos,{0,-speed,0});
        g_phys.velY=0.f; g_phys.grounded=false;
        return;
    }

    // ---- Horizontal movement (XZ only) ----
    float hSpeed=(g_input.shift ? g_cam.runSpeed : g_cam.walkSpeed) * dt;
    Vec3 moveDir={0,0,0};
    if(g_input.w) moveDir=add3(moveDir, g_cam.flatForward);
    if(g_input.s) moveDir=add3(moveDir, scale3(g_cam.flatForward,-1.f));
    if(g_input.d) moveDir=add3(moveDir, g_cam.flatRight);
    if(g_input.a) moveDir=add3(moveDir, scale3(g_cam.flatRight,-1.f));
    float mdLen=len3(moveDir);
    if(mdLen>1e-5f){
        moveDir=scale3(moveDir,1.f/mdLen); // normalise diagonals
        g_cam.pos.x+=moveDir.x*hSpeed;
        g_cam.pos.z+=moveDir.z*hSpeed;
    }

    // ---- Jump (edge-triggered: only fires on the frame Space is first pressed) ----
    static bool prevSpace=false;
    bool jumpEdge=(g_input.space && !prevSpace);
    prevSpace=g_input.space;
    if(jumpEdge && g_phys.grounded){
        g_phys.velY=PhysicsState::JUMP_VEL;
        g_phys.grounded=false;
    }

    // ---- Gravity ----
    if(!g_phys.grounded){
        g_phys.velY+=PhysicsState::GRAVITY*dt;
        if(g_phys.velY<PhysicsState::TERMINAL_VEL)
            g_phys.velY=PhysicsState::TERMINAL_VEL;
    }
    g_cam.pos.y+=g_phys.velY*dt;

    // ---- Ground collision (downward ray from slightly above feet) ----
    const float eyeH  =PhysicsState::EYE_HEIGHT;
    const float stepUp=PhysicsState::STEP_UP;
    const float skin  =PhysicsState::GROUND_SKIN;

    // Raise the ray origin by stepUp so small ledges are stepped over
    Vec3 rayOrig={g_cam.pos.x, g_cam.pos.y - eyeH + stepUp, g_cam.pos.z};
    float hitY;
    bool hit=castRayDown(rayOrig, sceneRadius*4.f + stepUp, hitY);

    if(hit){
        float feetY=g_cam.pos.y-eyeH;
        if(feetY<=hitY+skin){
            g_cam.pos.y=hitY+eyeH;           // snap feet onto surface
            if(g_phys.velY<0.f) g_phys.velY=0.f;
            g_phys.grounded=true;
        } else {
            g_phys.grounded=false;
        }
    } else {
        g_phys.grounded=false;
        // Respawn if fallen far below the world
        if(g_cam.pos.y < -sceneRadius*10.f){
            fprintf(stderr,"[physics] fell out of world, respawning\n");
            g_cam.pos.y=sceneRadius*2.f;
            g_phys.velY=0.f;
        }
    }
}

// ============================================================================
//  Main
// ============================================================================
int main(int argc,char** argv){
    kFont5x7_init(kFont5x7);

    static const std::string kScenePaths[2]={
        "models/testmap1/map.fbx",
        "models/testmap2/map.fbx"
    };

    std::string startPath=(argc>=2)?argv[1]:kScenePaths[0];
    g_currentScene=1;
    g_sunDir=norm3(g_sunDir);

    if(!glfwInit()){ fprintf(stderr,"GLFW init failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES,4);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GL_TRUE);
#endif

    GLFWwindow* window=glfwCreateWindow(1280,800,"FBX Engine",nullptr,nullptr);
    if(!window){ glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window); glfwSwapInterval(1);
    glfwSetInputMode(window,GLFW_CURSOR,GLFW_CURSOR_NORMAL);
    glfwSetScrollCallback(window,scrollCB);
    glfwSetKeyCallback(window,keyCB);

    glewExperimental=GL_TRUE;
    if(glewInit()!=GLEW_OK){ fprintf(stderr,"GLEW init failed\n"); return 1; }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_MULTISAMPLE);

    std::vector<Mesh> meshes;
    Vec3 centre; float radius;
    std::string modelDir,modelName;

    if(!loadScene(startPath,meshes,centre,radius,modelDir,modelName)){
        fprintf(stderr,"[load] Fatal: could not load '%s'\n",startPath.c_str());
        glfwTerminate(); return 1;
    }

    const float MODEL_SCALE=25.f;

    // Place camera above scene, slightly in front
    g_cam.pos={centre.x, centre.y + radius*0.5f, centre.z + radius*2.f};
    g_cam.yaw=-90.f; g_cam.pitch=0.f;
    g_cam.walkSpeed  = radius*0.06f;
    g_cam.runSpeed   = radius*0.18f;
    g_cam.moveSens   = radius*0.3f;
    g_cam.scrollSens = radius*0.05f;
    g_cam.update();

    g_phys.velY=0.f; g_phys.grounded=false; g_phys.noclip=false;

    g_rs.meshCount=(int)meshes.size(); g_rs.texCount=(int)g_texCache.size();
    int totalTris=0; for(auto& m:meshes)totalTris+=(int)(m.indexCount/3);
    g_rs.totalTris=totalTris;

    auto updateTitle=[&](){
        std::string t="FBX Engine – Scene "+std::to_string(g_currentScene)+" – "+modelName;
        glfwSetWindowTitle(window,t.c_str());
    };
    updateTitle();

    GLuint prog3D=buildProgram(vertSrc,fragSrc);
    GLint locMVP    =glGetUniformLocation(prog3D,"uMVP");
    GLint locModel  =glGetUniformLocation(prog3D,"uModel");
    GLint locHasTex =glGetUniformLocation(prog3D,"uHasTexture");
    GLint locHasVC  =glGetUniformLocation(prog3D,"uHasVertexColor");
    GLint locForceVC=glGetUniformLocation(prog3D,"uForceVertexCol");
    GLint locFlat   =glGetUniformLocation(prog3D,"uFlatColor");
    GLint locLight  =glGetUniformLocation(prog3D,"uLightDir");
    GLint locDiff   =glGetUniformLocation(prog3D,"uDiffuse");

    GLuint progBright   =buildProgram(fsTriVS,brightFS);
    GLuint progBlur     =buildProgram(fsTriVS,blurFS);
    GLuint progComposite=buildProgram(fsTriVS,compositeFS);
    GLint locBScene =glGetUniformLocation(progBright,"uScene");
    GLint locBThresh=glGetUniformLocation(progBright,"uThreshold");
    GLint locBlurTex=glGetUniformLocation(progBlur,"uTex");
    GLint locBlurDir=glGetUniformLocation(progBlur,"uDir");
    GLint locCScene =glGetUniformLocation(progComposite,"uScene");
    GLint locCBloom =glGetUniformLocation(progComposite,"uBloom");
    GLint locCStr   =glGetUniformLocation(progComposite,"uStrength");

    GLuint dummyVAO; glGenVertexArrays(1,&dummyVAO);
    BloomFBO bloom; int lastW=0,lastH=0;
    HUDRenderer hud; hud.init();
    Skysphere sky; sky.init();
    float skyRadius=radius*200.f;
    Sun sun; sun.init();
    LensFlare flare; flare.init();

    GLuint occQuery=0; glGenQueries(1,&occQuery);
    bool occQueryPending=false;
    GLuint occResult=1;

    Mat4 modelMat=scaleMat(MODEL_SCALE,MODEL_SCALE,MODEL_SCALE);
    auto tPrev=Clock::now();
    float frameAccum=0.f; int frameCount=0;

    while(!glfwWindowShouldClose(window)){
        glfwPollEvents();

        // ---- Scene switch ----
        if(g_sceneSwitch){
            g_sceneSwitch=false;
            int target=g_pendingScene;
            std::string newPath=(target>=1&&target<=2)?kScenePaths[target-1]:kScenePaths[0];
            if(occQueryPending){
                GLuint dummy; glGetQueryObjectuiv(occQuery,GL_QUERY_RESULT,&dummy);
                occQueryPending=false;
            }
            if(loadScene(newPath,meshes,centre,radius,modelDir,modelName)){
                g_currentScene=target;
                g_cam.pos={centre.x,centre.y+radius*0.5f,centre.z+radius*2.f};
                g_cam.yaw=-90.f; g_cam.pitch=0.f;
                g_cam.walkSpeed  = radius*0.06f;
                g_cam.runSpeed   = radius*0.18f;
                g_cam.moveSens   = radius*0.3f;
                g_cam.scrollSens = radius*0.05f;
                g_cam.update();
                modelMat=scaleMat(MODEL_SCALE,MODEL_SCALE,MODEL_SCALE);
                skyRadius=radius*200.f;
                g_rs.meshCount=(int)meshes.size();
                g_rs.texCount=(int)g_texCache.size();
                totalTris=0; for(auto& m:meshes)totalTris+=(int)(m.indexCount/3);
                g_rs.totalTris=totalTris;
                g_flareVis=0.f;
                g_phys.velY=0.f; g_phys.grounded=false;
                updateTitle();
            } else {
                fprintf(stderr,"[scene] Failed to load scene %d\n",target);
            }
        }

        auto tNow=Clock::now();
        float dt=std::chrono::duration<float>(tNow-tPrev).count();
        tPrev=tNow; dt=fminf(dt,0.1f);

        // Update look direction first
        float lspd=g_cam.turnSens*dt*(g_input.shift?2.f:1.f);
        if(g_input.arrowLeft)  g_cam.yaw  -=lspd;
        if(g_input.arrowRight) g_cam.yaw  +=lspd;
        if(g_input.arrowUp)    g_cam.pitch+=lspd;
        if(g_input.arrowDown)  g_cam.pitch-=lspd;
        g_cam.clampPitch();
        g_cam.update();

        // Physics + movement
        physicsUpdate(dt,radius);

        frameAccum+=dt; ++frameCount;
        if(frameAccum>=0.25f){
            g_rs.fps=frameCount/frameAccum;
            g_rs.frameMs=frameAccum/frameCount*1000.f;
            frameAccum=0; frameCount=0;
        }

        int ww,wh; glfwGetFramebufferSize(window,&ww,&wh);
        if(ww==0||wh==0) continue;
        if(ww!=lastW||wh!=lastH){ bloom.resize(ww,wh); lastW=ww; lastH=wh; }

        Mat4 proj=perspective(0.7854f,(float)ww/(float)wh,radius*0.0001f,radius*500.f);
        Mat4 view=lookAt(g_cam.pos,add3(g_cam.pos,g_cam.forward),{0,1,0});
        Mat4 mvp =mul(proj,mul(view,modelMat));
        Mat4 viewRot=viewRotOnly(view);

        if(occQueryPending){
            GLint available=0;
            glGetQueryObjectiv(occQuery,GL_QUERY_RESULT_AVAILABLE,&available);
            if(available){
                glGetQueryObjectuiv(occQuery,GL_QUERY_RESULT,&occResult);
                occQueryPending=false;
            }
        }

        Mat4 vp=mul(proj,viewRot);
        Vec4 sunClip=mulVec4(vp,{g_sunDir.x,g_sunDir.y,g_sunDir.z,1.f});
        bool sunOnScreen=false;
        Vec2 sunNDC={0,0};
        if(sunClip.w>0.0001f){
            sunNDC={sunClip.x/sunClip.w,sunClip.y/sunClip.w};
            if(sunClip.z/sunClip.w<1.f&&sunNDC.x>-1.6f&&sunNDC.x<1.6f&&
               sunNDC.y>-1.6f&&sunNDC.y<1.6f) sunOnScreen=true;
        }

        float target2=(sunOnScreen&&occResult>0)?1.f:0.f;
        g_flareVis+=fminf(1.f,dt*5.f)*(target2-g_flareVis);

        if(g_rs.bloom) glBindFramebuffer(GL_FRAMEBUFFER,bloom.sceneFBO);
        else            glBindFramebuffer(GL_FRAMEBUFFER,0);
        glViewport(0,0,ww,wh);
        glClearColor(0.54f,0.81f,0.94f,1.f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        if(g_rs.backfaceCull)glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        glPolygonMode(GL_FRONT_AND_BACK,g_rs.wireframe?GL_LINE:GL_FILL);

        // 1. Sky
        sky.draw(viewRot,proj,skyRadius);

        // 2. Sun billboard
        if(sunOnScreen){
            float aspect=(float)ww/(float)wh;
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND); glBlendFunc(GL_ONE,GL_ONE);
            glDisable(GL_DEPTH_TEST);
            glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
            glDisable(GL_CULL_FACE);
            glUseProgram(sun.prog);
            glUniform2f(sun.locNDC,sunNDC.x,sunNDC.y);
            glUniform1f(sun.locSize,0.12f);
            glUniform1f(sun.locAspect,aspect);
            glBindVertexArray(sun.vao);
            glDrawArrays(GL_TRIANGLES,0,6);
            glBindVertexArray(0);
            glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
        }

        // 3. Occlusion query
        if(sunOnScreen&&!occQueryPending){
            glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
            glDepthMask(GL_FALSE); glDepthFunc(GL_LEQUAL);
            float wx=(sunNDC.x*0.5f+0.5f)*(float)ww;
            float wy=(sunNDC.y*0.5f+0.5f)*(float)wh;
            glEnable(GL_SCISSOR_TEST);
            glScissor((int)(wx-2),(int)(wy-2),5,5);
            glBeginQuery(GL_ANY_SAMPLES_PASSED,occQuery);
            float aspect=(float)ww/(float)wh;
            glUseProgram(sun.prog);
            glUniform2f(sun.locNDC,sunNDC.x,sunNDC.y);
            glUniform1f(sun.locSize,0.003f);
            glUniform1f(sun.locAspect,aspect);
            glBindVertexArray(sun.vao);
            glDrawArrays(GL_TRIANGLES,0,6);
            glBindVertexArray(0);
            glEndQuery(GL_ANY_SAMPLES_PASSED);
            glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
            glDepthMask(GL_TRUE); glDepthFunc(GL_LESS);
            glDisable(GL_SCISSOR_TEST);
            occQueryPending=true;
        }

        // 4. Model
        if(g_rs.backfaceCull)glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glPolygonMode(GL_FRONT_AND_BACK,g_rs.wireframe?GL_LINE:GL_FILL);
        glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); glDisable(GL_BLEND);

        glUseProgram(prog3D);
        glUniformMatrix4fv(locMVP,1,GL_FALSE,mvp.m);
        glUniformMatrix4fv(locModel,1,GL_FALSE,modelMat.m);
        float ld[3]={g_sunDir.x,g_sunDir.y,g_sunDir.z};
        glUniform3fv(locLight,1,ld);
        glUniform1i(locForceVC,g_rs.forceVertexCol?1:0);

        int draws=0;
        glDepthMask(GL_TRUE); glDisable(GL_BLEND);
        for(auto& mesh:meshes){
            bool hasTex=(mesh.diffuseTex!=0);
            glUniform1i(locHasTex,hasTex?1:0);
            glUniform1i(locHasVC,mesh.hasProceduralVertexColor?1:0);
            glUniform3fv(locFlat,1,mesh.flatColor);
            if(hasTex){glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,mesh.diffuseTex);glUniform1i(locDiff,0);}
            glBindVertexArray(mesh.vao);
            glDrawElements(GL_TRIANGLES,mesh.indexCount,GL_UNSIGNED_INT,nullptr);
            glBindVertexArray(0); ++draws;
        }
        glDepthMask(GL_FALSE); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        for(auto& mesh:meshes){
            if(!mesh.diffuseTex)continue;
            glUniform1i(locHasTex,1);
            glUniform1i(locHasVC,mesh.hasProceduralVertexColor?1:0);
            glUniform3fv(locFlat,1,mesh.flatColor);
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,mesh.diffuseTex);glUniform1i(locDiff,0);
            glBindVertexArray(mesh.vao);
            glDrawElements(GL_TRIANGLES,mesh.indexCount,GL_UNSIGNED_INT,nullptr);
            glBindVertexArray(0);
        }
        glDepthMask(GL_TRUE);
        g_rs.drawCalls=draws;

        // 5. Bloom
        if(g_rs.bloom){
            glDisable(GL_DEPTH_TEST);
            glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
            glBindVertexArray(dummyVAO);
            int bw=(int)fmaxf(1,ww/2),bh=(int)fmaxf(1,wh/2);

            glBindFramebuffer(GL_FRAMEBUFFER,bloom.fbo[0]);
            glViewport(0,0,bw,bh); glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(progBright);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,bloom.sceneTex);
            glUniform1i(locBScene,0); glUniform1f(locBThresh,g_rs.bloomThresh);
            glDrawArrays(GL_TRIANGLES,0,3);

            int src=0,dst=1;
            glUseProgram(progBlur);
            const int BLUR_PASSES=5;
            for(int i=0;i<BLUR_PASSES;++i){
                glBindFramebuffer(GL_FRAMEBUFFER,bloom.fbo[dst]);
                glViewport(0,0,bw,bh); glClear(GL_COLOR_BUFFER_BIT);
                glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,bloom.tex[src]);
                glUniform1i(locBlurTex,0); glUniform2f(locBlurDir,1.f/(float)bw,0.f);
                glDrawArrays(GL_TRIANGLES,0,3); std::swap(src,dst);

                glBindFramebuffer(GL_FRAMEBUFFER,bloom.fbo[dst]);
                glViewport(0,0,bw,bh); glClear(GL_COLOR_BUFFER_BIT);
                glBindTexture(GL_TEXTURE_2D,bloom.tex[src]);
                glUniform2f(locBlurDir,0.f,1.f/(float)bh);
                glDrawArrays(GL_TRIANGLES,0,3); std::swap(src,dst);
            }

            glBindFramebuffer(GL_FRAMEBUFFER,0);
            glViewport(0,0,ww,wh); glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(progComposite);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,bloom.sceneTex);
            glUniform1i(locCScene,0);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,bloom.tex[src]);
            glUniform1i(locCBloom,1); glUniform1f(locCStr,g_rs.bloomStrength);
            glDrawArrays(GL_TRIANGLES,0,3);
            glBindVertexArray(0); glEnable(GL_DEPTH_TEST);
        }

        // 6. Lens flare
        if(sunOnScreen){
            glBindFramebuffer(GL_FRAMEBUFFER,0);
            flare.draw(sunNDC,g_flareVis,ww,wh);
        }

        // 7. HUD
        if(g_rs.showHUD){
            const float S=2.f,LH=HUDRenderer::GH*S+3.f;
            float x=10.f,y=10.f;
            hud.drawString(modelName.c_str(),x,y,ww,wh,1,1,0.6f,1,S+1);
            y+=(HUDRenderer::GH*(S+1)+5.f);
            hud.drawString("----------------------------",x,y,ww,wh,0.5f,0.5f,0.5f,0.8f,S); y+=LH;
            auto l=[&](const std::string& ss,float r2=0.6f,float g2=1.f,float b2=0.6f){
                hud.drawString(ss.c_str(),x,y,ww,wh,r2,g2,b2,1,S);y+=LH;};
            l(fmt("FPS:       %.1f  (%.2f ms)",g_rs.fps,g_rs.frameMs));
            l(fmt("Meshes:    %d",g_rs.meshCount));
            l(fmt("Triangles: %d",g_rs.totalTris));
            l(fmt("Textures:  %d",g_rs.texCount));
            l(fmt("Draws:     %d",g_rs.drawCalls));
            l(fmt("CollTris:  %zu",g_collisionTris.size()),0.8f,0.8f,0.4f);
            l(fmt("Scene:     %d",g_currentScene),1.0f,0.9f,0.3f);
            y+=3.f;
            l(fmt("Pos: %.1f %.1f %.1f",g_cam.pos.x,g_cam.pos.y,g_cam.pos.z),0.5f,1.f,1.f);
            l(fmt("Yaw: %.1f  Pitch: %.1f",g_cam.yaw,g_cam.pitch),0.5f,1.f,1.f);
            l(fmt("VelY: %+.2f  Grnd: %s",g_phys.velY,g_phys.grounded?"YES":"NO"),0.5f,1.f,1.f);
            l(fmt("Walk:%.1f Run:%.1f",g_cam.walkSpeed,g_cam.runSpeed),0.5f,1.f,1.f);
            y+=3.f;
            auto flag=[&](const char* name,bool on){
                float rv=on?1.f:0.4f,gv=on?1.f:0.4f,bv=on?0.3f:0.4f;
                hud.drawString(fmt("%s: %s",name,on?"ON ":"OFF").c_str(),x,y,ww,wh,rv,gv,bv,1,S);y+=LH;};
            flag("Noclip    [F8]",g_phys.noclip);
            flag("Wireframe [F2]",g_rs.wireframe);
            flag("Backface  [F3]",g_rs.backfaceCull);
            flag("Bloom     [F4]",g_rs.bloom);
            flag("VtxColOnly[F5]",g_rs.forceVertexCol);
            if(g_rs.bloom){
                l(fmt("  Thresh  [ ]: %.2f",g_rs.bloomThresh),1.f,0.8f,0.4f);
                l(fmt("  Strength-/+: %.1f",g_rs.bloomStrength),1.f,0.8f,0.4f);
            }
            y+=3.f;
            hud.drawString("----------------------------",x,y,ww,wh,0.5f,0.5f,0.5f,0.8f,S);y+=LH;
            if(!g_phys.noclip){
                hud.drawString("Walk:WASD  Run:Shift+WASD",x,y,ww,wh,0.55f,0.55f,0.55f,0.9f,S);y+=LH;
                hud.drawString("Jump:Space",x,y,ww,wh,0.55f,0.55f,0.55f,0.9f,S);y+=LH;
            } else {
                hud.drawString("Fly:WASD+Space/Ctrl+Shift",x,y,ww,wh,0.55f,0.55f,0.55f,0.9f,S);y+=LH;
            }
            hud.drawString("Look:Arrow keys",x,y,ww,wh,0.55f,0.55f,0.55f,0.9f,S);y+=LH;
            hud.drawString("Noclip:F8  HUD:F1",x,y,ww,wh,0.55f,0.55f,0.55f,0.9f,S);y+=LH;
            hud.drawString("Wire:F2 Cull:F3 Bloom:F4",x,y,ww,wh,0.55f,0.55f,0.55f,0.9f,S);y+=LH;
            hud.drawString("VtxCol:F5  Scene1/2:F6/F7",x,y,ww,wh,1.0f,0.9f,0.3f,0.9f,S);
            glEnable(GL_DEPTH_TEST);
        }

        glfwSwapBuffers(window);
    }

    // Cleanup
    for(auto& m:meshes){
        glDeleteVertexArrays(1,&m.vao); glDeleteBuffers(1,&m.vbo); glDeleteBuffers(1,&m.ebo);
        if(m.diffuseTex)glDeleteTextures(1,&m.diffuseTex);
    }
    for(auto& [_,tex]:g_texCache)glDeleteTextures(1,&tex);
    glDeleteProgram(prog3D); glDeleteProgram(progBright);
    glDeleteProgram(progBlur); glDeleteProgram(progComposite);
    glDeleteVertexArrays(1,&dummyVAO);
    glDeleteQueries(1,&occQuery);
    bloom.destroy(); hud.destroy(); sky.destroy(); sun.destroy(); flare.destroy();
    glfwTerminate();
    return 0;
}