#include "ForwardRS.hlsli"
#include "../Constants.hlsli"
#include "ShadowUtility.hlsli"
#include "PBRUtility.hlsli"

Texture2D<float4> baseColorTexture          : register(t0);
Texture2D<float3> metallicRoughnessTexture  : register(t1);
Texture2D<float3> emissiveTexture           : register(t2);
Texture2D<float2> normalTexture             : register(t3);

SamplerState baseColorSampler               : register(s0);
SamplerState metallicRoughnessSampler       : register(s1);
SamplerState emissiveSampler                : register(s2);
SamplerState normalSampler                  : register(s3);

cbuffer MaterialConstants : register(b0)
{
    float4 gBaseColorFactor;
    float3 gEmissiveFactor;
    float gNormalTextureScale;
    float2 gMetallicRoughnessFactor;
    uint gMaterialFlags;
}

struct PSIutput
{
    float4 positionSV : SV_Position;
    float3 positionWorld : POSITION;
    float3 normalWorld : NORMAL;
    float4 tangetWorld : TANGENT;
    float2 uv0 : TEXCOORD0;
#ifdef SECOND_UV
    float2 uv1 : TEXCOORD1;
#endif
};

// struct PSOutput
// {
//     uint4 normals : SV_Target0;
//     uint4 baseColorMaterial : SV_Target1;
// };

// MaterialFlag helpers
static const uint BASECOLOR_FLAG = 0x01;
static const uint METALLICROUGHNESS_FLAG = 0x02;
static const uint EMISSIVE_FLAG = 0x04;
static const uint NORMAL_FLAG = 0x08;


uint3 PackData1(float3 normal, float3 bumpNormal, float4 baseColor, float3 occlusionMetallicRoughness)
{
    normal = normal * 0.5 + 0.5;
    bumpNormal = bumpNormal * 0.5 + 0.5;
    uint r0 = (uint(normal.x * 255) & 0xFF) << 24 |
              (uint(normal.y * 255) & 0xFF) << 16 |
              (uint(bumpNormal.x * 255) & 0xFF) << 8 |
              (uint(bumpNormal.y * 255) & 0xFF);
    uint r1 = (uint(baseColor.r * 255) & 0xFF) << 24 |
              (uint(baseColor.g * 255) & 0xFF) << 16 |
              (uint(baseColor.b * 255) & 0xFF) << 8 |
              (uint(baseColor.a * 255) & 0xFF);
    uint r2 = (uint(occlusionMetallicRoughness.g * 255) & 0xFF) << 24 |
              (uint(occlusionMetallicRoughness.b * 255) & 0xFF) << 16 |
              (uint(normal.z * 255) & 0xFF) << 8 | 
              (uint(bumpNormal.z * 255) & 0xFF);

    return uint3(r0, r1, r2);
}


[RootSignature(ForwardRendererRootSig)]
uint4 main(PSIutput psInput) : SV_Target
{
#ifdef ENABLE_ALPHATEST
    float cutoff = f16tof32(gMaterialFlags >> 16);
    if (baseColorTexture.Sample(baseColorSampler, psInput.uv0).a * gBaseColorFactor.a < cutoff)
        discard;
#endif

#ifdef SECOND_UV
    #define UVSET(FLAG) lerp(psInput.uv0, psInput.uv1, gMaterialFlags & FLAG)
#else
    #define UVSET(FLAG) psInput.uv0
#endif
    psInput.normalWorld = normalize(psInput.normalWorld);

    float4 baseColor = gBaseColorFactor * baseColorTexture.Sample(baseColorSampler, UVSET(BASECOLOR_FLAG));
    float3 occlusionMetallicRoughness = metallicRoughnessTexture.Sample(metallicRoughnessSampler, UVSET(METALLICROUGHNESS_FLAG)).rgb;
    float2 metallicRoughness = gMetallicRoughnessFactor * occlusionMetallicRoughness.bg;
    float3 emissive = gEmissiveFactor * emissiveTexture.Sample(emissiveSampler, UVSET(EMISSIVE_FLAG));
    float3 normal = ComputeNormal(normalTexture, normalSampler, UVSET(NORMAL_FLAG), 
        psInput.normalWorld, psInput.tangetWorld, gNormalTextureScale);

    uint3 pack1 = PackData1(psInput.normalWorld, normal, baseColor, float3(occlusionMetallicRoughness.r, metallicRoughness));
    return uint4(pack1, 0);
}
