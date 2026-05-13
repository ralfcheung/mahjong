#include <metal_stdlib>
using namespace metal;

#include "ShaderTypes.h"

// ---------- Tile Body Shader ----------
// Replicates normalShade() from TileRenderer.cpp exactly

struct TileBodyVertexOut {
    float4 position [[position]];
    float3 worldNormal;
    float4 color;
};

vertex TileBodyVertexOut tileBodyVertex(
    const device VertexIn* vertices [[buffer(0)]],
    const device Uniforms& uniforms [[buffer(1)]],
    const device InstanceData* instances [[buffer(2)]],
    uint vid [[vertex_id]],
    uint iid [[instance_id]])
{
    VertexIn vin = vertices[vid];
    InstanceData inst = instances[iid];

    float4 worldPos = inst.modelMatrix * float4(vin.position, 1.0);
    float3 worldNormal = inst.normalMatrix * vin.normal;

    TileBodyVertexOut out;
    out.position = uniforms.projectionMatrix * uniforms.viewMatrix * worldPos;
    out.worldNormal = worldNormal;
    out.color = vin.color * inst.tintColor;
    return out;
}

fragment float4 tileBodyFragment(TileBodyVertexOut in [[stage_in]]) {
    float3 n = normalize(in.worldNormal);

    // Exact normalShade() from C++
    float shade = (n.y > 0 ? n.y * 1.18 : -n.y * 0.55)
                + (n.z > 0 ? n.z * 1.05 : -n.z * 0.70)
                + (n.x > 0 ? n.x * 0.88 : -n.x * 0.82);

    float3 color = in.color.rgb * shade;
    return float4(color, in.color.a);
}

// ---------- Textured Quad Shader (tile face decals) ----------

struct TexturedVertexOut {
    float4 position [[position]];
    float2 texCoord;
    float3 worldNormal;
};

vertex TexturedVertexOut texturedQuadVertex(
    const device VertexIn* vertices [[buffer(0)]],
    const device Uniforms& uniforms [[buffer(1)]],
    const device InstanceData* instances [[buffer(2)]],
    uint vid [[vertex_id]],
    uint iid [[instance_id]])
{
    VertexIn vin = vertices[vid];
    InstanceData inst = instances[iid];

    float4 worldPos = inst.modelMatrix * float4(vin.position, 1.0);

    // Map vertex texCoord (0..1) into atlas sub-rect
    float2 uv = inst.atlasUV.xy + vin.texCoord * inst.atlasUV.zw;

    TexturedVertexOut out;
    out.position = uniforms.projectionMatrix * uniforms.viewMatrix * worldPos;
    out.texCoord = uv;
    out.worldNormal = inst.normalMatrix * vin.normal;
    return out;
}

fragment float4 texturedQuadFragment(
    TexturedVertexOut in [[stage_in]],
    texture2d<float> atlas [[texture(0)]],
    sampler smp [[sampler(0)]])
{
    float4 texColor = atlas.sample(smp, in.texCoord);
    if (texColor.a < 0.01) discard_fragment();
    return texColor;
}

// ---------- Shadow Quad Shader ----------

struct ShadowVertexOut {
    float4 position [[position]];
    float4 color;
};

vertex ShadowVertexOut shadowVertex(
    const device VertexIn* vertices [[buffer(0)]],
    const device Uniforms& uniforms [[buffer(1)]],
    const device InstanceData* instances [[buffer(2)]],
    uint vid [[vertex_id]],
    uint iid [[instance_id]])
{
    VertexIn vin = vertices[vid];
    InstanceData inst = instances[iid];

    float4 worldPos = inst.modelMatrix * float4(vin.position, 1.0);

    ShadowVertexOut out;
    out.position = uniforms.projectionMatrix * uniforms.viewMatrix * worldPos;
    out.color = float4(0.0, 0.0, 0.0, 0.176); // alpha ~45/255
    return out;
}

fragment float4 shadowFragment(ShadowVertexOut in [[stage_in]]) {
    return in.color;
}

// ---------- Wireframe overlay (debug) ----------
// Same as tile body but outputs solid color lines

fragment float4 wireframeFragment(TileBodyVertexOut in [[stage_in]]) {
    return float4(1.0, 1.0, 0.0, 1.0); // yellow wireframe
}

// ---------- Normal visualization ----------

struct NormalVisVertexOut {
    float4 position [[position]];
    float4 color;
};

vertex NormalVisVertexOut normalVisVertex(
    const device VertexIn* vertices [[buffer(0)]],
    const device Uniforms& uniforms [[buffer(1)]],
    const device InstanceData* instances [[buffer(2)]],
    uint vid [[vertex_id]],
    uint iid [[instance_id]])
{
    VertexIn vin = vertices[vid];
    InstanceData inst = instances[iid];

    float4 worldPos = inst.modelMatrix * float4(vin.position, 1.0);
    float3 worldNormal = normalize(inst.normalMatrix * vin.normal);

    // Even vertices = base position, odd vertices = tip of normal line
    // We'll handle this in the renderer by generating line geometry

    NormalVisVertexOut out;
    out.position = uniforms.projectionMatrix * uniforms.viewMatrix * worldPos;
    // Color-code normal direction: R=+X, G=+Y, B=+Z
    out.color = float4(
        abs(worldNormal.x),
        abs(worldNormal.y),
        abs(worldNormal.z),
        1.0
    );
    return out;
}

fragment float4 normalVisFragment(NormalVisVertexOut in [[stage_in]]) {
    return in.color;
}

// ---------- Felt surface shader ----------

struct FeltVertexOut {
    float4 position [[position]];
    float2 texCoord;
};

vertex FeltVertexOut feltVertex(
    const device VertexIn* vertices [[buffer(0)]],
    const device Uniforms& uniforms [[buffer(1)]],
    uint vid [[vertex_id]])
{
    VertexIn vin = vertices[vid];
    float4 worldPos = float4(vin.position, 1.0);

    FeltVertexOut out;
    out.position = uniforms.projectionMatrix * uniforms.viewMatrix * worldPos;
    out.texCoord = vin.texCoord;
    return out;
}

fragment float4 feltFragment(
    FeltVertexOut in [[stage_in]],
    texture2d<float> felt [[texture(1)]],
    sampler smp [[sampler(0)]])
{
    return felt.sample(smp, in.texCoord);
}
