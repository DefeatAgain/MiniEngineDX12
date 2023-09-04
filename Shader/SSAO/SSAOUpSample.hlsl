#include "SSAOUtils.hlsli"
Texture2D<uint4> GBuffer0 : register(t0);
Texture2D<float> depthTexture : register(t1);

Texture2D<float> lowResAOTex : register(t2);
Texture2D<float3> lowResNormalTex : register(t3);
Texture2D<float> lowResDepthTex : register(t4);

Texture2D<float> hiResAOTex : register(t5);
Texture2D<float3> hiResNormalTex : register(t6);
Texture2D<float> hiResDepthTex : register(t7);

RWTexture2D<float> smoothAOTex : register(u0);

SamplerState aoSampler : register(s0);

#define TILE_DIM 10
groupshared uint gsNormals[TILE_DIM * TILE_DIM];
groupshared float gsDepths[TILE_DIM * TILE_DIM];
groupshared float gsAOs[TILE_DIM * TILE_DIM];

cbuffer CB0 : register(b0)
{
    uint gCurrentLevel;
    float2 invBufferDimension;
}


float BilateraUpSample(float4 lowResAO, float hiResAO, float4 lowResDepth, float hiResDepth, float3 lowResNormal[4], float3 hiResNormal)
{
    const float4 constantWeights = float4(9.0 / 16.0, 3.0 / 16.0, 3.0 / 16.0, 1.0 / 16.0);
    float4 normalWeights = float4( pow((dot(hiResNormal, lowResNormal[0]) + 1.0) * 0.5, 8.0),
                                    pow((dot(hiResNormal, lowResNormal[1]) + 1.0) * 0.5, 8.0),
                                    pow((dot(hiResNormal, lowResNormal[2]) + 1.0) * 0.5, 8.0),
                                    pow((dot(hiResNormal, lowResNormal[3]) + 1.0) * 0.5, 8.0));
    float4 depthWeights = float4( pow(1.0 / (1.0 + abs(hiResDepth - lowResDepth.x)), 16.0),
                                    pow(1.0 / (1.0 + abs(hiResDepth - lowResDepth.y)), 16.0),
                                    pow(1.0 / (1.0 + abs(hiResDepth - lowResDepth.z)), 16.0),
                                    pow(1.0 / (1.0 + abs(hiResDepth - lowResDepth.w)), 16.0));
    float4 allWeights = constantWeights * depthWeights * normalWeights;
    float sumWeight = dot(allWeights, 1.0);
    float res = dot(lowResAO, allWeights) / sumWeight;
    return lerp(res, hiResAO, 0.3);
}

float3 LoadNormal(uint2 DTid)
{
    uint rawData = GBuffer0.Load(uint3(DTid, 0)).r;
    return DecodeNormal(rawData);
}

void StoreLDSData(uint index, float depth, float ao, float3 normal)
{
    gsNormals[index] = EncodeNormal(normal);
    gsDepths[index] = depth;
    gsAOs[index] = ao;
}

void LoadLDSData(uint index, out float depth, out float ao, out float3 normal)
{
    normal = DecodeNormal(gsNormals[index]);
    depth = gsDepths[index];
    ao = gsAOs[index];
}

float2 GetBlurWeights(uint thisIdx, int offset)
{
    float3 normal0, normal1;
    float ao0, ao1;
    float depth0, depth1;

    LoadLDSData(thisIdx, depth0, ao0, normal0);
    LoadLDSData(thisIdx + offset, depth1, ao1, normal1);

    if (abs(depth0 - depth1) < 0.2)
    {
        const float constantWeights = 1.0 / 9.0;
        float noramlWeight = saturate(dot(normal0, normal1));
        float totalWeights = constantWeights * noramlWeight;
        return float2(ao1 * totalWeights , totalWeights);
    }

    return 0.0;
}

void BlurAO(uint thisIdx)
{
    float totalAO = 0.0;
    float totalWeights = 0.0;
    float2 ws[9] = {
        GetBlurWeights(thisIdx, TILE_DIM),
        GetBlurWeights(thisIdx, -TILE_DIM),
        GetBlurWeights(thisIdx, -1),
        GetBlurWeights(thisIdx, 1),
        GetBlurWeights(thisIdx, 0),
        GetBlurWeights(thisIdx, TILE_DIM - 1),
        GetBlurWeights(thisIdx, TILE_DIM + 1),
        GetBlurWeights(thisIdx, -TILE_DIM - 1),
        GetBlurWeights(thisIdx, -TILE_DIM + 1),
    };

    for (uint i = 0; i < 9; i++)
    {
        float2 res = ws[i];
        totalAO += res.x;
        totalWeights += res.y;
    }

    gsAOs[thisIdx] = totalAO / totalWeights;
}


[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
#ifdef NO_BLUR
    uint2 DTid_xy = DTid.xy / 2;
    float3 lowResNormals[4];
    lowResNormals[0] = lowResNormalTex.Load(uint3(DTid_xy, 0));
    lowResNormals[1] = lowResNormalTex.Load(uint3(DTid_xy + uint2(1, 0), 0));
    lowResNormals[2] = lowResNormalTex.Load(uint3(DTid_xy + uint2(0, 1), 0));
    lowResNormals[3] = lowResNormalTex.Load(uint3(DTid_xy + uint2(1, 1), 0));

    float4 lowResDepth = 0.0;
    lowResDepth.x = lowResDepthTex.Load(uint3(DTid_xy, 0));
    lowResDepth.y = lowResDepthTex.Load(uint3(DTid_xy + uint2(1, 0), 0));
    lowResDepth.z = lowResDepthTex.Load(uint3(DTid_xy + uint2(0, 1), 0));
    lowResDepth.w = lowResDepthTex.Load(uint3(DTid_xy + uint2(1, 1), 0));

    float4 lowResAO = 0.0;
    lowResAO.x = lowResAOTex.Load(uint3(DTid_xy, 0));
    lowResAO.y = lowResAOTex.Load(uint3(DTid_xy + uint2(1, 0), 0));
    lowResAO.z = lowResAOTex.Load(uint3(DTid_xy + uint2(0, 1), 0));
    lowResAO.w = lowResAOTex.Load(uint3(DTid_xy + uint2(1, 1), 0));

    float3 hiResNormals;
    float hiResDepth;
    float hiResAO;
    if (gCurrentLevel == 1)
    {
        hiResNormals = LoadNormal(DTid.xy);
        hiResDepth = depthTexture.Load(uint3(DTid.xy, 0));
        hiResAO = 1.0;
    }
    else
    {
        hiResNormals = hiResNormalTex.Load(uint3(DTid.xy, 0));
        hiResDepth = hiResDepthTex.Load(uint3(DTid.xy, 0));
        hiResAO = hiResAOTex.Load(uint3(DTid.xy, 0));
    }

    float ao = BilateraUpSample(lowResAO, hiResAO, lowResDepth, hiResDepth, lowResNormals, hiResNormals);

    smoothAOTex[DTid.xy] = ao;
#else
    if (GI < 37)
    {
        float2 sampleCenter = (float2(DTid.xy + GTid.xy) - 0.5) * invBufferDimension;

        float4 lowResNormalsR = lowResNormalTex.GatherRed(aoSampler, sampleCenter);
        float4 lowResNormalsG = lowResNormalTex.GatherGreen(aoSampler, sampleCenter);
        float4 lowResNormalsB = lowResNormalTex.GatherBlue(aoSampler, sampleCenter);
        float3 lowResNormals[4];
        lowResNormals[0] = float3(lowResNormalsR.x, lowResNormalsG.x, lowResNormalsB.x);
        lowResNormals[1] = float3(lowResNormalsR.y, lowResNormalsG.y, lowResNormalsB.y);
        lowResNormals[2] = float3(lowResNormalsR.z, lowResNormalsG.z, lowResNormalsB.z);
        lowResNormals[3] = float3(lowResNormalsR.w, lowResNormalsG.w, lowResNormalsB.w);

        float4 lowResDepth = lowResDepthTex.Gather(aoSampler, sampleCenter);

        float4 lowResAO = lowResAOTex.Gather(aoSampler, sampleCenter);

        uint gsIdx = GTid.x * 2 + GTid.y * 2 * TILE_DIM;
        StoreLDSData(gsIdx, lowResDepth.x, lowResAO.x, lowResNormals[0]);
        StoreLDSData(gsIdx + 1, lowResDepth.y, lowResAO.y, lowResNormals[1]);
        StoreLDSData(gsIdx + TILE_DIM, lowResDepth.z, lowResAO.z, lowResNormals[2]);
        StoreLDSData(gsIdx + TILE_DIM + 1, lowResDepth.w, lowResAO.w, lowResNormals[3]);
    }

    GroupMemoryBarrierWithGroupSync();

    uint thisIdx = GTid.x + GTid.y * TILE_DIM + TILE_DIM + 1; // 第一个要检测的像素
    BlurAO(thisIdx);

    GroupMemoryBarrierWithGroupSync();

    // smoothAOTex[DTid.xy / 2] = gsAOs[thisIdx];

    float4 lowResAO, lowResDepth;
    float3 lowResNormals[4];
    LoadLDSData(thisIdx, lowResDepth.x, lowResAO.x, lowResNormals[0]);
    LoadLDSData(thisIdx + 1, lowResDepth.y, lowResAO.y, lowResNormals[1]);
    LoadLDSData(thisIdx + TILE_DIM, lowResDepth.z, lowResAO.z, lowResNormals[2]);
    LoadLDSData(thisIdx + TILE_DIM + 1, lowResDepth.w, lowResAO.w, lowResNormals[3]);

    float3 hiResNormals;
    float hiResDepth;
    float hiResAO;
    if (gCurrentLevel == 1)
    {
        hiResNormals = LoadNormal(DTid.xy);
        hiResDepth = depthTexture.Load(uint3(DTid.xy, 0));
    }
    else
    {
        hiResNormals = hiResNormalTex.Load(uint3(DTid.xy, 0));
        hiResDepth = hiResDepthTex.Load(uint3(DTid.xy, 0));
    }
    hiResAO = hiResAOTex.Load(uint3(DTid.xy, 0));

    float ao = BilateraUpSample(lowResAO, hiResAO, lowResDepth, hiResDepth, lowResNormals, hiResNormals);

    smoothAOTex[DTid.xy] = ao;
#endif
}
