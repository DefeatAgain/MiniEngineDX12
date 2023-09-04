#include "SSAOUtils.hlsli"

Texture2D<uint4> GBuffer0 : register(t0);
Texture2D<float> depthTexture : register(t1);

Texture2D<float> depthDownTexture : register(t2);
RWTexture2D<float> aoResult : register(u0);

SamplerState DepthSampler : register(s0);

cbuffer CB0 : register(b0)
{
    float4 gSampleThinkesss[2];
    float4 gSampleWeights[2];
    float2 invBufferDimension;
}

#define TILE_DIM 16

groupshared float gsDepthSamples[TILE_DIM * TILE_DIM];


float3 LoadNormal(uint2 DTid)
{
    uint rawData = GBuffer0.Load(uint3(DTid, 0)).r;
    return DecodeNormal(rawData);
}

float TestSamplePair(float thinkness, float thisDepth, uint base, int offset )
{
    float halfThinkness = thinkness * 0.5;
#ifdef REVERSED_Z
    float d1 = thisDepth - gsDepthSamples[base + offset];
    float d2 = thisDepth - gsDepthSamples[base - offset];
    float occlusion1 = abs(d1) > 0.0005 && abs(d1) < 0.1 ? saturate(d1 / halfThinkness) : 1.0;
    float occlusion2 = abs(d2) > 0.0005 && abs(d1) < 0.1 ? saturate(d2 / halfThinkness) : 1.0;
#else
    float d1 = gsDepthSamples[base + offset] - thisDepth;
    float d2 = gsDepthSamples[base - offset] - thisDepth;
    float occlusion1 = saturate(d1 / halfThinkness);
    float occlusion2 = saturate(d1 / halfThinkness);
#endif
    return (occlusion1 + occlusion2) * 0.5;
}


[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID)
{
    float2 sampleCenter = (float2(DTid.xy + GTid.xy) - 3.0 - 0.5) * invBufferDimension;
    
    float4 depthsTex = depthDownTexture.Gather(DepthSampler, sampleCenter);

    uint gsIdx = GTid.x * 2 + GTid.y * 2 * TILE_DIM; // x, y分别跳过两个像素
    gsDepthSamples[gsIdx] = depthsTex.x;
    gsDepthSamples[gsIdx + 1] = depthsTex.y;
    gsDepthSamples[gsIdx + TILE_DIM] = depthsTex.z;
    gsDepthSamples[gsIdx + TILE_DIM + 1] = depthsTex.w;

    GroupMemoryBarrierWithGroupSync();

    uint thisIdx = GTid.x + GTid.y * TILE_DIM + TILE_DIM * 4 + 4; // 第一个要检测的像素
    float thisDepth = gsDepthSamples[thisIdx];
    float ao = 0.0;

    // (1, 0)
    ao += gSampleWeights[0].x * TestSamplePair(gSampleThinkesss[0].x, thisDepth, thisIdx, 2);
    ao += gSampleWeights[0].x * TestSamplePair(gSampleThinkesss[0].x, thisDepth, thisIdx, TILE_DIM * 2);
    // (2, 0)
    ao += gSampleWeights[0].y * TestSamplePair(gSampleThinkesss[0].y, thisDepth, thisIdx, 4);
    ao += gSampleWeights[0].y * TestSamplePair(gSampleThinkesss[0].y, thisDepth, thisIdx, TILE_DIM * 4);
    // (1, 1) (2, 2) (3, 3)
    ao += gSampleWeights[0].z * TestSamplePair(gSampleThinkesss[0].z, thisDepth, thisIdx, 1 * TILE_DIM + 1);
    ao += gSampleWeights[0].z * TestSamplePair(gSampleThinkesss[0].z, thisDepth, thisIdx, 1 * TILE_DIM - 1);
    ao += gSampleWeights[1].x * TestSamplePair(gSampleThinkesss[1].x, thisDepth, thisIdx, 2 * TILE_DIM + 2);
    ao += gSampleWeights[1].x * TestSamplePair(gSampleThinkesss[1].x, thisDepth, thisIdx, 2 * TILE_DIM - 2);
    ao += gSampleWeights[1].z * TestSamplePair(gSampleThinkesss[1].z, thisDepth, thisIdx, 3 * TILE_DIM + 3);
    ao += gSampleWeights[1].z * TestSamplePair(gSampleThinkesss[1].z, thisDepth, thisIdx, 3 * TILE_DIM - 3);
    // (1, 3)
    ao += gSampleWeights[0].w * TestSamplePair(gSampleThinkesss[0].w, thisDepth, thisIdx, 3 * TILE_DIM + 1);
    ao += gSampleWeights[0].w * TestSamplePair(gSampleThinkesss[0].w, thisDepth, thisIdx, 3 * TILE_DIM - 1);
    // (3, 1)
    ao += gSampleWeights[0].w * TestSamplePair(gSampleThinkesss[0].w, thisDepth, thisIdx, 1 * TILE_DIM + 3);
    ao += gSampleWeights[0].w * TestSamplePair(gSampleThinkesss[0].w, thisDepth, thisIdx, 1 * TILE_DIM - 3);
    // (2, 4)
    ao += gSampleWeights[1].y * TestSamplePair(gSampleThinkesss[1].y, thisDepth, thisIdx, 4 * TILE_DIM + 2);
    ao += gSampleWeights[1].y * TestSamplePair(gSampleThinkesss[1].y, thisDepth, thisIdx, 4 * TILE_DIM - 2);
    // (4, 2)
    ao += gSampleWeights[1].y * TestSamplePair(gSampleThinkesss[1].y, thisDepth, thisIdx, 2 * TILE_DIM + 4);
    ao += gSampleWeights[1].y * TestSamplePair(gSampleThinkesss[1].y, thisDepth, thisIdx, 2 * TILE_DIM - 4);

    aoResult[DTid.xy] = ao;
}
