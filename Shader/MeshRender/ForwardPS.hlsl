#include "ForwardRS.hlsli"
#include "../Constants.hlsli"
#include "PBRUtility.hlsli"

Texture2D<float4> baseColorTexture          : register(t0);
Texture2D<float3> metallicRoughnessTexture  : register(t1);
Texture2D<float1> occlusionTexture          : register(t2);
Texture2D<float3> emissiveTexture           : register(t3);
Texture2D<float3> normalTexture             : register(t4);

SamplerState baseColorSampler               : register(s0);
SamplerState metallicRoughnessSampler       : register(s1);
SamplerState occlusionSampler               : register(s2);
SamplerState emissiveSampler                : register(s3);
SamplerState normalSampler                  : register(s4);

TextureCube<float3> radianceIBLTexture      : register(t10);
TextureCube<float3> irradianceIBLTexture    : register(t11);
Texture2D<float2> preComputeBRDFTexture     : register(t12);
Texture2D<float> sunShadowTexture		    : register(t13);

cbuffer MaterialConstants : register(b0)
{
    float4 gBaseColorFactor;
    float3 gEmissiveFactor;
    float gNormalTextureScale;
    float2 gMetallicRoughnessFactor;
    uint gMaterialFlags;
}

cbuffer GlobalConstants : register(b1)
{
    float4x4 gViewProjMatrix;
    float4x4 gShadowFinalMatrix;
    float3 gEyePos;
    float3 gSunDirection;
    float3 gSunIntensity;
    float _pad;
    float gIBLRange;
    float gIBLBias;
}

struct PSIutput
{
    float4 positionSV : SV_Position;
    float3 positionWorld : POSITION;
    float3 normalWorld : NORMAL;
    float4 tangetWorld : TANGENT;
    float2 uv0 : TEXCOORD0;
#ifndef NO_SECOND_UV
    float2 uv1 : TEXCOORD1;
#endif
    float2 shadowCoord : TEXCOORD2;
};

// MaterialFlag helpers
static const uint BASECOLOR_FLAG = 0x0;
static const uint METALLICROUGHNESS_FLAG = 0x01;
static const uint OCCLUSION_FLAG = 0x02;
static const uint EMISSIVE_FLAG = 0x04;
static const uint NORMAL_FLAG = 0x08;


struct SurfaceProperties
{
    float3 N;
    float3 wo;
    float3 diffuse;
    float3 specular;
    float roughness;
    float metallic;
};

struct LightProperties
{
    float3 instensity;
    float3 wi;
    float distanceSqrDiv;
};

float3 GGXMicrofacet(LightProperties light, SurfaceProperties surf)
{
    float3 wm = normalize(light.wi + surf.wo);
    float D = DistributionGGX(surf.N, wm, surf.roughness);
    float G = GeometrySmith(surf.N, surf.wo, light.wi, surf.roughness);
    float3 F = FresnelSchlickRoughness(surf.specular, wm, surf.wo, surf.roughness);

    float3 specAlbedo = F * D * G / max(4.0f * dot(surf.N, surf.wo) * dot(surf.N, light.wi), 1e-6);
    float3 kS = F;
    float3 kD = 1.0 - kS;

    return (surf.diffuse * kD / PI + specAlbedo) * light.instensity * light.distanceSqrDiv;
}

float3 DiffuseIBL(SurfaceProperties surf)
{
    // Assumption:  L = N
    return surf.diffuse * irradianceIBLTexture.Sample(defaultSampler, surf.N);
}

float3 SpecularIBL(SurfaceProperties surf)
{
    float lod = surf.roughness * gIBLRange + gIBLBias;
    float NoV = dot(surf.wo, surf.N);

    float3 prefilterLi = radianceIBLTexture.SampleLevel(defaultSampler, reflect(-surf.wo, surf.N), lod).rgb;
    float2 preComputeBRDF = preComputeBRDFTexture.Sample(defaultSampler, float2(NoV, surf.roughness)).rg;

    float3 F = FresnelSchlickRoughness(surf.specular, NoV, surf.roughness);
    return prefilterLi * (F * preComputeBRDF.r + preComputeBRDF.g);
}


[RootSignature(ForwardRendererRootSig)]
float4 main(PSIutput psInput) : SV_Target
{
#ifdef NO_SECOND_UV
    #define UVSET(FLAG) psInput.uv0
#else
    #define UVSET(FLAG) lerp(psInput.uv0, psInput.uv1, gMaterialFlags & FLAG)
#endif

    float4 baseColor = gBaseColorFactor * baseColorTexture.Sample(baseColorSampler, UVSET(BASECOLOR_FLAG));
    float2 metallicRoughness = gMetallicRoughnessFactor * 
        metallicRoughnessTexture.Sample(metallicRoughnessSampler, UVSET(METALLICROUGHNESS_FLAG)).bg;
    float occlusion = occlusionTexture.Sample(occlusionSampler, UVSET(OCCLUSION_FLAG));
    float3 emissive = gEmissiveFactor * emissiveTexture.Sample(emissiveSampler, UVSET(EMISSIVE_FLAG));
    float3 normal = ComputeNormal(normalTexture, normalSampler, UVSET(NORMAL_FLAG), 
        psInput.normalWorld, psInput.tangetWorld, gNormalTextureScale);

    SurfaceProperties surface;
    surface.N = normal;
    surface.wo = normalize(gEyePos - psInput.positionWorld);
    surface.diffuse = baseColor.rgb * (1 - metallicRoughness.x) * occlusion;
    surface.specular = lerp(kDielectricSpecular, baseColor.rgb, metallicRoughness.x) * occlusion;
    surface.roughness = metallicRoughness.y;

    LightProperties light;
    float3 sunVec = gSunDirection - psInput.positionWorld;
    light.distanceSqrDiv = dot(sunVec, sunVec);
    light.wi = sunVec / sqrt(light.distanceSqrDiv);
    light.instensity = gSunIntensity;

    float3 ambient = DiffuseIBL(surface) + SpecularIBL(surface);
    float3 sunLight = GGXMicrofacet(light, surface);

    return float4(ambient + sunLight, baseColor.a);
}
