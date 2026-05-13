#ifndef ShaderTypes_h
#define ShaderTypes_h

#ifdef __METAL_VERSION__
// In Metal Shading Language: use metal types directly
#define SIMD_FLOAT2 float2
#define SIMD_FLOAT3 float3
#define SIMD_FLOAT4 float4
#define SIMD_FLOAT3X3 float3x3
#define SIMD_FLOAT4X4 float4x4
#define SIMD_INT int
#else
// In C/C++/ObjC/Swift: use simd types
#include <simd/simd.h>
#define SIMD_FLOAT2 simd_float2
#define SIMD_FLOAT3 simd_float3
#define SIMD_FLOAT4 simd_float4
#define SIMD_FLOAT3X3 simd_float3x3
#define SIMD_FLOAT4X4 simd_float4x4
#define SIMD_INT int
#endif

// Vertex layout for tile body mesh (position + normal + color)
struct VertexIn {
    SIMD_FLOAT3 position;
    SIMD_FLOAT3 normal;
    SIMD_FLOAT4 color;      // pre-baked base color (white front / green back)
    SIMD_FLOAT2 texCoord;   // UV for face decal quad (0,0 for body vertices)
};

// Per-frame uniforms
struct Uniforms {
    SIMD_FLOAT4X4 viewMatrix;
    SIMD_FLOAT4X4 projectionMatrix;
    float time;
    float padding[3];
};

// Per-instance data (one per tile)
struct InstanceData {
    SIMD_FLOAT4X4 modelMatrix;
    SIMD_FLOAT3X3 normalMatrix;  // inverse transpose of upper-left 3x3
    SIMD_FLOAT4 tintColor;       // multiplied with vertex color (for highlights)
    SIMD_FLOAT4 atlasUV;         // x,y = top-left UV; z,w = size UV (for face quad)
    SIMD_INT showFace;           // 1 = render face texture, 0 = body only
    SIMD_INT padding2[3];
};

// Buffer indices matching [[buffer(N)]] in shaders
#ifndef __METAL_VERSION__
enum BufferIndex {
    BufferIndexVertices  = 0,
    BufferIndexUniforms  = 1,
    BufferIndexInstances = 2,
};

enum TextureIndex {
    TextureIndexAtlas = 0,
    TextureIndexFelt  = 1,
};
#endif

#endif /* ShaderTypes_h */
