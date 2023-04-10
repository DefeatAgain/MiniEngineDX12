#include "ForwardRS.hlsli"
#include "../Constants.hlsli"
#include "PBRUtility.hlsli"
#include "ShadowUtility.hlsli"

Texture2D<float4> baseColorTexture          : register(t0);
Texture2D<float3> metallicRoughnessTexture  : register(t1);
Texture2D<float3> emissiveTexture           : register(t2);
Texture2D<float2> normalTexture             : register(t3);

SamplerState baseColorSampler               : register(s0);
SamplerState metallicRoughnessSampler       : register(s1);
SamplerState emissiveSampler                : register(s2);
SamplerState normalSampler                  : register(s3);

TextureCube<float3> radianceIBLTexture      : register(t10);
TextureCube<float3> irradianceIBLTexture    : register(t11);
Texture2D<float2> preComputeBRDFTexture     : register(t12);
#if NUM_CSM_SHADOW_MAP > 1
Texture2DArray<float> sunShadowTexture		: register(t18);
#else
Texture2D<float> sunShadowTexture		    : register(t18);
#endif

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
    float4x4 gInvViewProjMatrix;
    float4x4 gShadowFinalMatrix[MAX_CSM_SHADOW_DIVIDES + 1];
    float3 gCSMDivides;
    float3 gEyePos;
    float3 gSunDirection;
    float3 gSunIntensity;
    float _pad;
    float gIBLRange;
    float gShadowBias;
    float gNearZ;
    float gFarZ;
}

struct PSIutput
{
    float4 positionSV : SV_Position;
    float3 positionWorld : POSITION;
    float3 normalWorld : NORMAL;
    float4 tangetWorld : TANGENT;
    float2 uv0 : TEXCOORD0;

#if NUM_CSM_SHADOW_MAP > 1
    float3 shadowCoord[NUM_CSM_SHADOW_MAP] : POSITION1;
#else
    float3 shadowCoord : POSITION1;
#endif

#ifdef SECOND_UV
    float2 uv1 : TEXCOORD1;
#endif
};

// MaterialFlag helpers
static const uint BASECOLOR_FLAG = 0x01;
static const uint METALLICROUGHNESS_FLAG = 0x02;
static const uint EMISSIVE_FLAG = 0x04;
static const uint NORMAL_FLAG = 0x08;


struct SurfaceProperties
{
    float3 N;
    float3 wo;
    float3 diffuse;
    float3 specular;
    float roughness;
};

struct LightProperties
{
    float3 instensity;
    float3 wi;
    float3 wm;
    float distanceSqrDiv;
};

float3 GGXMicrofacet(LightProperties light, SurfaceProperties surf)
{
    float D = DistributionGGX(surf.N, light.wm, surf.roughness);
    float G = GeometrySmith(surf.N, surf.wo, light.wi, surf.roughness);
    float3 F = FresnelSchlickRoughness(surf.specular, light.wm, surf.wo, surf.roughness);

    float3 specAlbedo = F * D * G / max(4.0f * dot(surf.N, surf.wo) * dot(surf.N, light.wi), 1e-6);
    float3 kS = F;
    float3 kD = 1.0 - kS;

    float NoL = max(dot(light.wi, surf.N), 0.0);

    return (surf.diffuse * kD / PI + specAlbedo) * light.instensity * NoL * light.distanceSqrDiv;
}

float3 CalcIBL(LightProperties light, SurfaceProperties surf)
{
    float3 diffuse = surf.diffuse * irradianceIBLTexture.Sample(baseColorSampler, surf.N);

    float lod = surf.roughness * gIBLRange;
    float NoV = dot(surf.wo, surf.N);

    float3 prefilterLi = radianceIBLTexture.SampleLevel(defaultSampler, reflect(-surf.wo, surf.N), lod);
    float2 preComputeBRDF = preComputeBRDFTexture.Sample(pointSampler, float2(NoV, surf.roughness));

    // float3 F = FresnelSchlickRoughness(surf.specular, light.wm, light.wi, surf.roughness);
    float3 F = FresnelSchlickRoughness(surf.specular, NoV, surf.roughness);
    // float3 F = SchlickFresnel(surf.specular, light.wm, surf.wo);

    return diffuse * (1.0 - F) * PI_DIV + prefilterLi * (F * preComputeBRDF.r + preComputeBRDF.g) * (NoV > 0.0f);
}


[RootSignature(ForwardRendererRootSig)]
float4 main(PSIutput psInput) : SV_Target
{
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

    SurfaceProperties surface;
    surface.N = normal;
    surface.wo = normalize(gEyePos - psInput.positionWorld);
    surface.diffuse = baseColor.rgb * (1 - metallicRoughness.x);
    surface.specular = lerp(kDielectricSpecular, baseColor.rgb, metallicRoughness.x);
    surface.roughness = metallicRoughness.y;

    LightProperties light;
    light.distanceSqrDiv = 1.0;
    light.wi = gSunDirection;
    light.wm = normalize(light.wi + surface.wo);
    light.instensity = gSunIntensity;

    float3 ambient = CalcIBL(light, surface);
    float3 sunLight = GGXMicrofacet(light, surface);
#if NUM_CSM_SHADOW_MAP > 1
    float visiblity = CalcDirectionShadow(sunShadowTexture, shadowSamplerComparison, psInput.shadowCoord, gShadowBias, 
        psInput.normalWorld, gSunDirection, psInput.positionSV.w, gNearZ, gFarZ, gCSMDivides);
#else
     float visiblity = CalcDirectionShadow(sunShadowTexture, shadowSamplerComparison, psInput.shadowCoord, gShadowBias,
        psInput.normalWorld, gSunDirection);
#endif
    float3 Li = ambient + sunLight * visiblity;
    return float4(Li, baseColor.a);
}
