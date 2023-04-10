#ifndef __PBRUTILITY_HLSLI__
#define __PBRUTILITY_HLSLI__
#include "../Constants.hlsli"

float3 SchlickFresnel(float3 R0, float3 wm, float3 wi)
{
	float cosTheta = saturate(dot(wm, wi));
	float f0 = 1.0f - cosTheta;
	return R0 + (1.0f - R0) * (f0 * f0 * f0 * f0 * f0);
}

float3 FresnelSchlickRoughness(float3 R0, float3 wm, float3 wi, float roughness)
{
    float cosTheta = saturate(dot(wm, wi));
	float f0 = 1.0f - cosTheta;
    float shininess = 1.0 - roughness;
    return R0 + (max(float3(shininess, shininess, shininess), f0) - f0) * (f0 * f0 * f0 * f0 * f0);;
}

float3 FresnelSchlickRoughness(float3 R0, float NoV, float roughness)
{
	float f0 = 1.0f - NoV;
    float shininess = 1.0 - roughness;
    return R0 + (max(float3(shininess, shininess, shininess), f0) - f0) * (f0 * f0 * f0 * f0 * f0);;
}   

float DistributionGGX(float3 N, float3 wm, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, wm), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / max(denom, 1e-6);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float a = roughness;
    float k = (a * a) / 2.0;

    float nom = max(NdotV, 0.0);
    float denom = NdotV * (1.0 - k) + k;

    return nom / max(denom, 1e-6);
}

float GeometrySmith(float3 N, float3 wo, float3 wi, float roughness)
{
    // TODO: To calculate Smith G here
    return GeometrySchlickGGX(dot(N, wo), roughness) * GeometrySchlickGGX(dot(N, wi), roughness);
}

float3 ComputeNormal(Texture2D<float2> normalTexture, SamplerState normalSampler, 
    float2 uv, float3 normal, float4 tangent, float normalTextureScale)
{
    // normal = normalize(normal);
    // Construct tangent frame
    float3 T = normalize(tangent.xyz);
    float3 B = normalize(cross(normal, T)) * tangent.w;
    float3x3 tangentFrame = float3x3(T, B, normal);
    
    float2 normalTex = normalTexture.Sample(normalSampler, uv) * 2.0 - 1.0;
    float normalTexZ = sqrt(1.0 - min(dot(normalTex, normalTex), 0.99999));
    normal = float3(normalTex, normalTexZ);

    // glTF spec says to normalize N before and after scaling, but that's excessive
    normal = normalize(normal * float3(normalTextureScale, normalTextureScale, 1));

    // Multiply by transpose (reverse order)
    return mul(normal, tangentFrame);
}

#endif // __PBRUTILITY_HLSLI__
