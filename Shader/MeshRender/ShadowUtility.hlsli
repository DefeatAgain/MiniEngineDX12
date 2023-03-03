#ifndef __SHADOWUTILITY_HLSLI__
#define __SHADOWUTILITY_HLSLI__

float CalcDirectionShadow(Texture2D<float> shadowMap, SamplerState shadowSampler, float3 shadowCoord, float shadowBias)
{
    float shadowZ = shadowMap.Sample(shadowSampler, shadowCoord.xy).r - shadowBias;
#ifdef REVERSED_Z
    if (shadowZ > shadowCoord.z)
#else
    if (shadowZ < shadowCoord.z)
#endif
        return 0.0;
    return 1.0;
}

#endif // __SHADOWUTILITY_HLSLI__
