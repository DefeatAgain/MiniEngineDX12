#ifndef __SHADOWUTILITY_HLSLI__
#define __SHADOWUTILITY_HLSLI__

#include "../Constants.hlsli"

#define PCF_AREA 0.001
#define CSM_FLAG_BY_INTERVAL
#define MAX_CSM_SHADOW_DIVIDES 3
#define MAX_NUM_CSM_SHADOW_MAP MAX_CSM_SHADOW_DIVIDES + 1

#ifndef NUM_CSM_DIVIDES
#define NUM_CSM_DIVIDES 0
#endif

#ifndef NUM_CSM_SHADOW_MAP
#define NUM_CSM_SHADOW_MAP NUM_CSM_DIVIDES + 1
#endif

#ifdef CSM_FLAG_BY_INTERVAL
#define BLEND_INTERVAL 0.5
#else
#define BLEND_INTERVAL 0.25
#endif


float GetPCFShadow(Texture2D<float> shadowMap, SamplerComparisonState shadowSampler, float2 shadowCoord, float shadowCoordZ)
{
    float allSamples = 0.0;
    for (uint i = 0; i < 4; i+=4)
    {
        float2 uvSample0 = PoissonDisk16[i] * PCF_AREA + shadowCoord.xy;
        float2 uvSample1 = PoissonDisk16[i + 1] * PCF_AREA + shadowCoord.xy;
        float2 uvSample2 = PoissonDisk16[i + 2] * PCF_AREA + shadowCoord.xy;
        float2 uvSample3 = PoissonDisk16[i + 3] * PCF_AREA + shadowCoord.xy;
        float shadowZ0 = shadowMap.SampleCmpLevelZero(shadowSampler, uvSample0, shadowCoordZ);
        float shadowZ1 = shadowMap.SampleCmpLevelZero(shadowSampler, uvSample1, shadowCoordZ);
        float shadowZ2 = shadowMap.SampleCmpLevelZero(shadowSampler, uvSample2, shadowCoordZ);
        float shadowZ3 = shadowMap.SampleCmpLevelZero(shadowSampler, uvSample2, shadowCoordZ);
        allSamples += shadowZ0 + shadowZ1 + shadowZ2 + shadowZ3;
    }
    allSamples /= 16.0;
    return allSamples;
}

float GetPCFShadow(Texture2DArray<float> shadowMap, SamplerComparisonState shadowSampler, float3 shadowCoord, float shadowCoordZ)
{
    float allSamples = 0.0;
    for (uint i = 0; i < 16; i+=4)
    {
        float3 uvSample0 = float3(PoissonDisk16[i] * PCF_AREA + shadowCoord.xy, shadowCoord.z);
        float3 uvSample1 = float3(PoissonDisk16[i + 1] * PCF_AREA + shadowCoord.xy, shadowCoord.z);
        float3 uvSample2 = float3(PoissonDisk16[i + 2] * PCF_AREA + shadowCoord.xy, shadowCoord.z);
        float3 uvSample3 = float3(PoissonDisk16[i + 3] * PCF_AREA + shadowCoord.xy, shadowCoord.z);
        float shadowZ0 = shadowMap.SampleCmpLevelZero(shadowSampler, uvSample0, shadowCoordZ);
        float shadowZ1 = shadowMap.SampleCmpLevelZero(shadowSampler, uvSample1, shadowCoordZ);
        float shadowZ2 = shadowMap.SampleCmpLevelZero(shadowSampler, uvSample2, shadowCoordZ);
        float shadowZ3 = shadowMap.SampleCmpLevelZero(shadowSampler, uvSample2, shadowCoordZ);
        allSamples += shadowZ0 + shadowZ1 + shadowZ2 + shadowZ3;
    }
    allSamples /= 16.0;
    return allSamples;
}

float GetAdaptiveShadowBias(float3 normalW, float3 lightDir, float shadowBias)
{
    // method 2
    float dirDot = dot(normalW, lightDir);
    return max(shadowBias, shadowBias * min(1.0 / dirDot * dirDot, 1000.0));
    // method 1
    return max(shadowBias, (1.0 - abs(dot(normalW, lightDir))) * 1000.0 * shadowBias);
}

float GetCSMShadowByIntervalFlag(Texture2DArray<float> shadowMap, SamplerComparisonState shadowSampler, float3 shadowCoord[NUM_CSM_SHADOW_MAP],  
    float shadowBias, float eyeZ, float nearZ, float farZ, float3 divideZ4)
{
    float4 allDivides = float4(nearZ, divideZ4);
    float4 comparison = eyeZ > allDivides;
    float curIndex = dot(float4(NUM_CSM_SHADOW_MAP > 0,
                              NUM_CSM_SHADOW_MAP > 1, 
                              NUM_CSM_SHADOW_MAP > 2,
                              NUM_CSM_SHADOW_MAP > 3),
                              comparison) - 1;
    curIndex = clamp(0, NUM_CSM_SHADOW_MAP - 1, curIndex);

    float3 currentShadowCoord = shadowCoord[curIndex];
    currentShadowCoord.z = max(0.0, currentShadowCoord.z);
    // float shadowZ = shadowMap.SampleCmpLevelZero(shadowSampler, float3(currentShadowCoord.xy,  curIndex), 
    //     currentShadowCoord.z + shadowBias);
    float shadowZ = GetPCFShadow(shadowMap, shadowSampler, float3(currentShadowCoord.xy,  curIndex), 
        currentShadowCoord.z + shadowBias);

    // code for blend
    // float nextIndex = min(curIndex + 1, NUM_CSM_SHADOW_MAP - 1);
    // float distanceToBlend = (farZ - nearZ) * 0.25;
    // float distanceToCurDivide = eyeZ - allDivides[curIndex];
    // float distanceToNextDivide = allDivides[nextIndex] - eyeZ;
    // float3 nextShadowCoord = shadowCoord[nextIndex];
    // float blendArea = 0.0f;
    // if (distanceToCurDivide < distanceToBlend * 0.5)
    // {
    //     nextIndex = max(0, curIndex - 1);
    //     nextShadowCoord = shadowCoord[nextIndex];
    //     blendArea = distanceToBlend * 0.5 + distanceToCurDivide;
    //     float nextShadowZ = shadowMap.SampleCmpLevelZero(shadowSampler, float3(nextShadowCoord.xy,  nextIndex), 
    //         nextShadowCoord.z + shadowBias);
    //     return lerp(nextShadowZ, shadowZ, blendArea / distanceToBlend);
    // }
    // else if (distanceToNextDivide < distanceToBlend * 0.5)
    // {
    //     blendArea = distanceToNextDivide;
    //     float nextShadowZ = shadowMap.SampleCmpLevelZero(shadowSampler, float3(nextShadowCoord.xy,  nextIndex), 
    //         nextShadowCoord.z + shadowBias);
    //     return lerp(shadowZ, nextShadowZ, blendArea / distanceToBlend);
    // }
    // else
    // {
    //     return shadowZ;
    // }

    float nextIndex = curIndex + 1;
    float nextDivide = nextIndex < NUM_CSM_SHADOW_MAP ? allDivides[nextIndex] : farZ;
    float distanceToNextDivide = nextDivide - eyeZ;
    float distanceToBlend = (nextDivide - allDivides[curIndex]) * BLEND_INTERVAL;
    if (distanceToNextDivide < distanceToBlend)
    {
        float3 nextShadowCoord = shadowCoord[min(nextIndex, NUM_CSM_SHADOW_MAP - 1)];
        nextShadowCoord.z = max(0.0, nextShadowCoord.z);
        // float nextShadowZ = shadowMap.SampleCmpLevelZero(shadowSampler, float3(nextShadowCoord.xy,  nextIndex), 
        //     nextShadowCoord.z + shadowBias);
        float nextShadowZ = GetPCFShadow(shadowMap, shadowSampler, float3(nextShadowCoord.xy,  nextIndex),
            nextShadowCoord.z + shadowBias);
#ifdef REVERSED_Z
        return lerp(nextShadowZ, shadowZ, distanceToNextDivide / distanceToBlend);
#else
        return lerp(shadowZ, nextShadowZ, distanceToNextDivide / distanceToBlend);
#endif
    }
    return shadowZ;
}

float GetCSMShadowByMapEdge(Texture2DArray<float> shadowMap, SamplerComparisonState shadowSampler, float3 shadowCoord[NUM_CSM_SHADOW_MAP],  
    float shadowBias)
{
    int curIndex = 0;
    for (int i = 0; i < NUM_CSM_SHADOW_MAP; i++)
    {
        float3 curShadowCoord = shadowCoord[i];
        if (min(min(curShadowCoord.x, curShadowCoord.y), curShadowCoord.z) > 0.0f &&
            max(max(curShadowCoord.x, curShadowCoord.y), curShadowCoord.z) < 1.0f)
        {
            curIndex = i;
            break;
        }
    }

    float3 curShadowCoord = shadowCoord[curIndex];
    float shadowZ = shadowMap.SampleCmpLevelZero(shadowSampler, float3(curShadowCoord.xy,  curIndex), 
        curShadowCoord.z + shadowBias);

    float distanceToOne = min(1.0 - curShadowCoord.x, 1.0 - curShadowCoord.y);
    float distanceToZero = min(curShadowCoord.x, curShadowCoord.y);
    float distanceToEdge = min(distanceToOne, distanceToZero);
    if (distanceToEdge < BLEND_INTERVAL)
    {
        int nextIndex = min(curIndex + 1, NUM_CSM_SHADOW_MAP - 1);
        float3 nextShadowCoord = shadowCoord[nextIndex];
        float nextShadowZ = shadowMap.SampleCmpLevelZero(shadowSampler, float3(nextShadowCoord.xy,  nextIndex), 
            nextShadowCoord.z + shadowBias);
#ifdef REVERSED_Z
        return lerp(nextShadowZ, shadowZ, distanceToEdge / BLEND_INTERVAL);
#else
        return lerp(shadowZ, nextShadowZ, distanceToEdge / BLEND_INTERVAL);
#endif
    }
    return shadowZ;
}

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

float CalcDirectionShadow(Texture2D<float> shadowMap, SamplerComparisonState shadowSampler, float3 shadowCoord, 
    float shadowBias, float3 normalW, float3 lightDir)
{
    shadowBias = GetAdaptiveShadowBias(normalW, lightDir, shadowBias);

    float shadowZ = shadowMap.SampleCmpLevelZero(shadowSampler, shadowCoord.xy, shadowCoord.z + shadowBias).r;
    return shadowZ;
}

float CalcDirectionShadow(Texture2DArray<float> shadowMap, SamplerComparisonState shadowSampler, float3 shadowCoord[NUM_CSM_SHADOW_MAP],  
    float shadowBias, float3 normalW, float3 lightDir, float eyeZ, float nearZ, float farZ, float3 divideZ4)
{
    shadowBias = GetAdaptiveShadowBias(normalW, lightDir, shadowBias);

#ifdef CSM_FLAG_BY_INTERVAL
    return GetCSMShadowByIntervalFlag(shadowMap, shadowSampler, shadowCoord, shadowBias, eyeZ, nearZ, farZ, divideZ4);
#else
    return GetCSMShadowByMapEdge(shadowMap, shadowSampler, shadowCoord, shadowBias);
#endif
}

#endif // __SHADOWUTILITY_HLSLI__
