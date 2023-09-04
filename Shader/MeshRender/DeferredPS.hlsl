#include "DeferredRS.hlsli"
#include "../Constants.hlsli"
#include "ShadowUtility.hlsli"
#include "PBRUtility.hlsli"

Texture2D<uint4> GBuffer0		            : register(t0);
Texture2D<float> depthBuffer	            : register(t1);
Texture2D<float> aoTexture	                : register(t2);

TextureCube<float3> radianceIBLTexture      : register(t10);
TextureCube<float3> irradianceIBLTexture    : register(t11);
Texture2D<float2> preComputeBRDFTexture     : register(t12);
#if NUM_CSM_SHADOW_MAP > 1
Texture2DArray<float> sunShadowTexture		: register(t18);
#else
Texture2D<float> sunShadowTexture		    : register(t18);
#endif

cbuffer GlobalConstants : register(b0)
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

struct RenderData
{
    float3 positionWorld;
#if NUM_CSM_SHADOW_MAP > 1
    float3 shadowCoord[NUM_CSM_SHADOW_MAP];
#else
    float3 shadowCoord;
#endif
    float3 worldNormal;
    float3 bumpNormal;
    float4 baseColor;
    float metallic;
    float roughness;
    float screenDepth;
    float viewDepth;
};


float LinearizeDepth(float z, float n, float f)
{
    return 1.0 / ((1.0 - f / n) / f * z + n);
}

RenderData GetRenderData(float3 positionSCN, float2 screenUV)
{
    uint3 positionSV = uint3(positionSCN.xy, 0);
    uint4 rawData = GBuffer0.Load(positionSV);
    const float muitpler255 = 1.0 / 255.0;
    const float muitpler1023 = 1.0 / 1023.0;
    
    float4 d0, d1, d2, d3;
    d0.x = float((rawData.r >> 22) & 0x3FF) * muitpler1023;
    d0.y = float((rawData.r >> 12) & 0x3FF) * muitpler1023;
    d0.z = float((rawData.r >> 2) & 0x3FF) * muitpler1023;
    // float d03 = float((rawData.r) & 0xFF) * muitpler;

    d1.x = float((rawData.g >> 22) & 0x3FF) * muitpler1023;
    d1.y = float((rawData.g >> 12) & 0x3FF) * muitpler1023;
    d1.z = float((rawData.g >> 2) & 0x3FF) * muitpler1023;
    // float d13 = float((rawData.g) & 0xFF) * muitpler;

    d2.x = float((rawData.b >> 24) & 0xFF) * muitpler255;
    d2.y = float((rawData.b >> 16) & 0xFF) * muitpler255;
    d2.z = float((rawData.b >> 8) & 0xFF) * muitpler255;
    d2.w = float((rawData.b) & 0xFF) * muitpler255;

    d3.x = float((rawData.a >> 24) & 0xFF) * muitpler255;
    d3.y = float((rawData.a >> 16) & 0xFF) * muitpler255;
    // float d32 = float((rawData.a >> 8) & 0xFF) * muitpler255;
    // float d33 = float((rawData.a) & 0xFF) * muitpler255;

    RenderData data;
    data.worldNormal = normalize(2.0 * d0.xyz - 1.0);
    data.bumpNormal = normalize(2.0 * d1.xyz - 1.0);
    data.baseColor = d2;
    data.metallic = d3.x;
    data.roughness = d3.y;

    data.screenDepth = depthBuffer.Load(positionSV);
    screenUV.y = 1.0 - screenUV.y;
    screenUV = 2.0 * screenUV - 1.0f; // convert to NDC
    float3 positionNDC = float3(screenUV, data.screenDepth);
    float4 positionWorldW = mul(gInvViewProjMatrix, float4(positionNDC, 1.0));
    positionWorldW /= positionWorldW.w;
    data.positionWorld = positionWorldW.xyz;
#if NUM_CSM_SHADOW_MAP > 1
    for (uint i = 0; i < NUM_CSM_SHADOW_MAP; i++)
         data.shadowCoord[i] = mul(gShadowFinalMatrix[i], positionWorldW).xyz;
#else
    data.shadowCoord = mul(gShadowFinalMatrix[0], positionWorldW).xyz;
#endif

#ifdef REVERSED_Z
    data.viewDepth = LinearizeDepth(1.0 - data.screenDepth, gNearZ, gFarZ);
#else
    data.viewDepth = LinearizeDepth(data.screenDepth, gNearZ, gFarZ);
#endif 

    return data;
}

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

    float NoL = max(dot(light.wi, surf.N), 0.0);

    return (surf.diffuse * kD / PI + specAlbedo) * light.instensity * NoL * light.distanceSqrDiv;
}

float3 CalcIBL(LightProperties light, SurfaceProperties surf)
{
    float3 diffuse = surf.diffuse * irradianceIBLTexture.Sample(defaultSampler, surf.N);

    float lod = surf.roughness * gIBLRange;
    float NoV = dot(surf.wo, surf.N);

    float3 prefilterLi = radianceIBLTexture.SampleLevel(defaultSampler, reflect(-surf.wo, surf.N), lod);
    float2 preComputeBRDF = preComputeBRDFTexture.Sample(pointSampler, float2(NoV, surf.roughness));

    float3 F = FresnelSchlickRoughness(surf.specular, NoV, surf.roughness);
    // return  prefilterLi * (F * preComputeBRDF.r + preComputeBRDF.g);

    return diffuse * (1.0 - F) * PI_DIV + prefilterLi * (F * preComputeBRDF.r + preComputeBRDF.g) * float(NoV > 0.0f);
}

[RootSignature(DeferredRendererRootSig)]
float4 main( float4 positionSV : SV_Position, float2 uv : TexCoord0 ) : SV_Target
{
    RenderData renderData = GetRenderData(positionSV.xyz, uv);

    // avoid render sky box
    if (renderData.screenDepth == 0.0)
        return float4(0.0, 0.0, 0.0, 1.0);
    // return renderData.baseColor;
    // return float4(renderData.worldNormal, 1.0);

    SurfaceProperties surface;
    surface.N = renderData.bumpNormal;
    surface.wo = normalize(gEyePos - renderData.positionWorld);
    surface.diffuse = renderData.baseColor.rgb * (1 - renderData.metallic);
    surface.specular = lerp(kDielectricSpecular, renderData.baseColor.rgb, renderData.metallic.x);
    surface.roughness = renderData.roughness;

    LightProperties light;
    light.distanceSqrDiv = 1.0;
    light.wi = gSunDirection;
    light.instensity = gSunIntensity;

    float3 ambient = CalcIBL(light, surface) * aoTexture.Sample(defaultSampler, uv);
    float3 sunLight = GGXMicrofacet(light, surface);
#if NUM_CSM_SHADOW_MAP > 1
    float visiblity = CalcDirectionShadow(sunShadowTexture, shadowSamplerComparison, renderData.shadowCoord, gShadowBias, 
        renderData.worldNormal, gSunDirection, renderData.viewDepth, gNearZ, gFarZ, gCSMDivides);
#else
    float visiblity = CalcDirectionShadow(sunShadowTexture, shadowSamplerComparison, renderData.shadowCoord, gShadowBias,
        renderData.worldNormal, gSunDirection);
#endif
    float3 Li = ambient + sunLight * visiblity;
    return float4(Li, 1.0);
}
