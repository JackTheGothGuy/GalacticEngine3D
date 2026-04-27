// fbx_check.cpp — standalone FBX/model diagnostic tool
// Compile: g++ fbx_check.cpp -o fbx_check -lassimp -std=c++17
// Usage:   ./fbx_check models/testmap2/map.fbx

#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/Logger.hpp>
#include <assimp/DefaultLogger.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// ============================================================================
//  Verbose Assimp logger — prints every internal Assimp message to stderr
// ============================================================================
class VerboseLogger : public Assimp::Logger {
public:
    void OnDebug(const char* msg) override { fprintf(stderr,"  [DBG] %s\n",msg); }
    void OnVerboseDebug(const char* msg) override { fprintf(stderr,"  [VRB] %s\n",msg); }
    void OnInfo (const char* msg) override { fprintf(stderr,"  [INF] %s\n",msg); }
    void OnWarn (const char* msg) override { fprintf(stderr,"  [WRN] %s\n",msg); }
    void OnError(const char* msg) override { fprintf(stderr,"  [ERR] %s\n",msg); }
    bool attachStream(Assimp::LogStream*,unsigned) override { return true; }
    void detachStream(Assimp::LogStream*,unsigned) override {}
};

// ============================================================================
//  Hex-dump first 32 bytes — reveals file magic / encoding
// ============================================================================
static void hexDump(const std::string& path){
    std::ifstream f(path, std::ios::binary);
    if(!f){ fprintf(stderr,"  Cannot open for binary read\n"); return; }
    unsigned char buf[32]={};
    f.read((char*)buf, sizeof(buf));
    size_t got = f.gcount();
    fprintf(stderr,"  First %zu bytes (hex): ", got);
    for(size_t i=0;i<got;++i) fprintf(stderr,"%02X ",buf[i]);
    fprintf(stderr,"\n");
    fprintf(stderr,"  First %zu bytes (ascii): ", got);
    for(size_t i=0;i<got;++i) fprintf(stderr,"%c", (buf[i]>=32&&buf[i]<127)?buf[i]:'.');
    fprintf(stderr,"\n");

    // FBX binary magic: "Kaydara FBX Binary  \0"
    const char* binMagic = "Kaydara FBX Binary";
    bool isBinary = (got >= 18 && memcmp(buf, binMagic, 18)==0);
    // FBX ASCII starts with "; FBX" or "FBX" or similar comment
    bool isAscii = (got >= 3 && (buf[0]==';' || (buf[0]=='F'&&buf[1]=='B'&&buf[2]=='X')));
    if(isBinary)  fprintf(stderr,"  → Detected: FBX BINARY format\n");
    else if(isAscii) fprintf(stderr,"  → Detected: FBX ASCII format\n");
    else          fprintf(stderr,"  → WARNING: Does not look like a standard FBX file!\n");
}

// ============================================================================
//  Try loading with progressively fewer post-process flags
// ============================================================================
static const struct { unsigned int flags; const char* label; } kFlagSets[] = {
    {
        aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs |
        aiProcess_CalcTangentSpace | aiProcess_JoinIdenticalVertices |
        aiProcess_SortByPType | aiProcess_PreTransformVertices |
        aiProcess_RemoveRedundantMaterials | aiProcess_FindDegenerates |
        aiProcess_FindInvalidData,
        "Full flags (same as engine)"
    },
    {
        aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_PreTransformVertices,
        "Minimal flags"
    },
    {
        aiProcess_Triangulate,
        "Triangulate only"
    },
    {
        0,
        "No post-processing at all"
    },
};

int main(int argc, char** argv){
    if(argc < 2){
        fprintf(stderr,"Usage: ./fbx_check <path/to/model.fbx>\n");
        return 1;
    }
    std::string path = argv[1];

    fprintf(stderr,"\n========================================\n");
    fprintf(stderr," FBX Diagnostic Tool\n");
    fprintf(stderr,"========================================\n");
    fprintf(stderr,"File: %s\n\n", path.c_str());

    // --- Basic filesystem checks ---
    fprintf(stderr,"--- Filesystem ---\n");
    std::error_code ec;
    if(!fs::exists(path, ec)){
        fprintf(stderr,"  ERROR: File does not exist!\n");
        return 1;
    }
    auto sz = fs::file_size(path, ec);
    fprintf(stderr,"  Exists: YES\n");
    fprintf(stderr,"  Size:   %zu bytes (%.1f KB)\n", (size_t)sz, sz/1024.0);
    if(sz == 0){
        fprintf(stderr,"  ERROR: File is empty!\n");
        return 1;
    }
    if(sz < 64){
        fprintf(stderr,"  WARNING: File is suspiciously small for an FBX.\n");
    }

    // --- File magic ---
    fprintf(stderr,"\n--- File header ---\n");
    hexDump(path);

    // --- Supported formats ---
    fprintf(stderr,"\n--- Assimp supported importers ---\n");
    {
        Assimp::Importer tmp;
        std::string ext = fs::path(path).extension().string();
        for(char& c : ext) c = (char)tolower((unsigned char)c);
        // Check if extension is supported
        if(tmp.IsExtensionSupported(ext))
            fprintf(stderr,"  Extension '%s': SUPPORTED by Assimp\n", ext.c_str());
        else
            fprintf(stderr,"  Extension '%s': NOT supported by this Assimp build!\n", ext.c_str());
    }

    // --- Try loading with verbose logging ---
    fprintf(stderr,"\n--- Assimp load attempts ---\n");

    for(auto& attempt : kFlagSets){
        fprintf(stderr,"\n  Trying: %s\n", attempt.label);

        // Install verbose logger
        Assimp::DefaultLogger::kill();
        VerboseLogger* vlog = new VerboseLogger();
        Assimp::DefaultLogger::set(vlog);
        Assimp::DefaultLogger::get()->setLogSeverity(Assimp::Logger::VERBOSE);

        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path, attempt.flags);

        Assimp::DefaultLogger::kill();

        if(!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)){
            const char* err = importer.GetErrorString();
            fprintf(stderr,"  FAILED: %s\n",
                (err && err[0]) ? err : "(empty error string)");
            continue;
        }

        // Success! Print everything we know about the scene
        fprintf(stderr,"\n  ✓ LOADED SUCCESSFULLY with: %s\n", attempt.label);
        fprintf(stderr,"\n--- Scene info ---\n");
        fprintf(stderr,"  Meshes:     %u\n", scene->mNumMeshes);
        fprintf(stderr,"  Materials:  %u\n", scene->mNumMaterials);
        fprintf(stderr,"  Textures:   %u (embedded)\n", scene->mNumTextures);
        fprintf(stderr,"  Animations: %u\n", scene->mNumAnimations);
        fprintf(stderr,"  Lights:     %u\n", scene->mNumLights);
        fprintf(stderr,"  Cameras:    %u\n", scene->mNumCameras);

        int totalVerts=0, totalFaces=0;
        for(unsigned i=0;i<scene->mNumMeshes;++i){
            const aiMesh* m = scene->mMeshes[i];
            totalVerts += m->mNumVertices;
            totalFaces += m->mNumFaces;
        }
        fprintf(stderr,"  Total verts:  %d\n", totalVerts);
        fprintf(stderr,"  Total faces:  %d\n", totalFaces);

        fprintf(stderr,"\n--- Mesh list ---\n");
        for(unsigned i=0;i<scene->mNumMeshes;++i){
            const aiMesh* m = scene->mMeshes[i];
            fprintf(stderr,"  [%02u] %-32s  verts=%-6u  faces=%-6u  "
                           "normals=%s  uv=%s  vc=%s\n",
                i, m->mName.C_Str(),
                m->mNumVertices, m->mNumFaces,
                m->HasNormals()           ? "Y" : "N",
                m->HasTextureCoords(0)    ? "Y" : "N",
                m->HasVertexColors(0)     ? "Y" : "N");
        }

        fprintf(stderr,"\n--- Material list ---\n");
        for(unsigned i=0;i<scene->mNumMaterials;++i){
            aiMaterial* mat = scene->mMaterials[i];
            aiString name;
            mat->Get(AI_MATKEY_NAME, name);
            fprintf(stderr,"  [%02u] %s\n", i, name.C_Str());
            aiTextureType types[]={
                aiTextureType_DIFFUSE,aiTextureType_BASE_COLOR,
                aiTextureType_NORMALS,aiTextureType_SPECULAR
            };
            const char* typeNames[]={"DIFFUSE","BASE_COLOR","NORMALS","SPECULAR"};
            for(int t=0;t<4;++t){
                for(unsigned s=0;s<mat->GetTextureCount(types[t]);++s){
                    aiString tp; mat->GetTexture(types[t],s,&tp);
                    fprintf(stderr,"       tex[%s]: %s\n", typeNames[t], tp.C_Str());
                }
            }
        }

        // The flags that worked — print the recommended fix for the engine
        if(attempt.flags != kFlagSets[0].flags){
            fprintf(stderr,"\n--- RECOMMENDED FIX ---\n");
            fprintf(stderr,"  The file fails with full flags but loads with: %s\n", attempt.label);
            fprintf(stderr,"  One of these post-process flags is crashing Assimp on your file:\n");
            fprintf(stderr,"    aiProcess_CalcTangentSpace\n");
            fprintf(stderr,"    aiProcess_JoinIdenticalVertices\n");
            fprintf(stderr,"    aiProcess_FindDegenerates\n");
            fprintf(stderr,"    aiProcess_FindInvalidData\n");
            fprintf(stderr,"    aiProcess_RemoveRedundantMaterials\n");
            fprintf(stderr,"  Try removing them one by one in fbx_engine.cpp's importFlags.\n");
        }

        return 0;
    }

    // All attempts failed
    fprintf(stderr,"\n========================================\n");
    fprintf(stderr," ALL LOAD ATTEMPTS FAILED\n");
    fprintf(stderr,"========================================\n");
    fprintf(stderr,"Possible causes:\n");
    fprintf(stderr,"  1. FBX was exported with a newer FBX SDK (2020+) — try re-exporting\n");
    fprintf(stderr,"     from Blender as FBX 7.4 binary or earlier.\n");
    fprintf(stderr,"  2. File is corrupted or truncated.\n");
    fprintf(stderr,"  3. File is actually a different format with a .fbx extension.\n");
    fprintf(stderr,"  4. Try exporting as .obj or .glb instead — both are more reliable.\n");
    fprintf(stderr,"\nAlternative: re-export from your 3D tool as OBJ or GLTF/GLB,\n");
    fprintf(stderr,"then run ./fbx_engine models/testmap2/map.obj\n");

    return 1;
}
