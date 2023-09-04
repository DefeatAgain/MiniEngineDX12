#include "SSAOUtils.hlsli"

Texture2D<uint4> GBuffer0 : register(t0);
Texture2D<float> depthTexture : register(t1);

RWTexture2D<float3> normalDownTexture1 : register(u0);
RWTexture2D<float3> normalDownTexture2 : register(u1);
RWTexture2D<float3> normalDownTexture3 : register(u2);
RWTexture2D<float3> normalDownTexture4 : register(u3);
RWTexture2D<float> depthDownTexture1 : register(u4);
RWTexture2D<float> depthDownTexture2 : register(u5);
RWTexture2D<float> depthDownTexture3 : register(u6);
RWTexture2D<float> depthDownTexture4 : register(u7);

groupshared uint gsNormal[64];
groupshared float gsDepth[64];

#define THRESHOLD_DOWNSAMPLE_DIS 0.5

void StoreLDSData(uint index, float depth, float3 normal)
{
    gsNormal[index] = EncodeNormal(normal);
    gsDepth[index] = depth;
}

void LoadLDSData(uint index, out float depth, out float3 normal)
{
    normal = DecodeNormal(gsNormal[index]);
    depth = gsDepth[index];
}

uint4 SortPixel(float4 depths)
{
    uint index[4] = {0, 1, 2, 3};
    float depthsArr[4] = {depths.x, depths.y, depths.z, depths.w};
    for (int i = 1; i < 4; i++)
    {
        float key = depthsArr[i];
        uint indexKey = index[i];
        int j = i - 1;
        for (; j >= 0; j--)
        {
            if (key < depthsArr[j])
            {
                depthsArr[j + 1] = depthsArr[j];
                index[j + 1] = index[j];
            }
        }
        depthsArr[j + 1] = key;
        index[j + 1] = indexKey;
    }
    return uint4(index[0], index[1], index[2], index[3]);
}

float3 LoadNormal(uint2 DTid)
{
    uint rawData = GBuffer0.Load(uint3(DTid, 0)).r;
    return DecodeNormal(rawData);
}

void GetDepthAndNormal(float4 depth, float3 normals[4], out float3 downSampleNormal, out float downSampleDepth)
{
    uint4 sortedPixel = SortPixel(depth);

    if (depth[sortedPixel.w] - depth[sortedPixel.x] < THRESHOLD_DOWNSAMPLE_DIS)
    {
        downSampleNormal = (normals[sortedPixel.y] + normals[sortedPixel.z]) * 0.5;
        downSampleDepth = (depth[sortedPixel.y] + depth[sortedPixel.z]) * 0.5;
    }
    else
    {
        downSampleNormal = normals[sortedPixel.y];
        downSampleDepth = depth[sortedPixel.y];
    }
}


[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint GI : SV_GroupIndex)
{
    uint2 DTid_xy = DTid.xy * 2;
    float3 normals[4];
    normals[0] = LoadNormal(DTid_xy);
    normals[1] = LoadNormal(DTid_xy + uint2(1, 0));
    normals[2] = LoadNormal(DTid_xy + uint2(0, 1));
    normals[3] = LoadNormal(DTid_xy + uint2(1, 1));

    float4 depth = float4(0.0, 0.0, 0.0, 0.0);
    depth.x = depthTexture.Load(uint3(DTid_xy, 0));
    depth.y = depthTexture.Load(uint3(DTid_xy + uint2(1, 0), 0));
    depth.z = depthTexture.Load(uint3(DTid_xy + uint2(0, 1), 0));
    depth.w = depthTexture.Load(uint3(DTid_xy + uint2(1, 1), 0));

    float3 downSampleNormal;
    float downSampleDepth;
    
    GetDepthAndNormal(depth, normals, downSampleNormal, downSampleDepth);
    depthDownTexture1[DTid.xy] = downSampleDepth;
    normalDownTexture1[DTid.xy] = downSampleNormal * 0.5 + 0.5;

    StoreLDSData(GI, downSampleDepth, downSampleNormal);

    GroupMemoryBarrierWithGroupSync();

    // 0x9 -> 001001
    if ((GI & 0x9) == 0)
    {
        LoadLDSData(GI, depth.x, normals[0]);
        LoadLDSData(GI + 1, depth.y, normals[1]);
        LoadLDSData(GI + 8, depth.z, normals[2]);
        LoadLDSData(GI + 9, depth.w, normals[3]);

        GetDepthAndNormal(depth, normals, downSampleNormal, downSampleDepth);
        depthDownTexture2[DTid.xy / 2] = downSampleDepth;
        normalDownTexture2[DTid.xy / 2] = downSampleNormal * 0.5 + 0.5;

        StoreLDSData(GI, downSampleDepth, downSampleNormal);
    }

    GroupMemoryBarrierWithGroupSync();

    // 0x1B -> 011011
    if ((GI & 0x1B) == 0)
    {
        LoadLDSData(GI, depth.x, normals[0]);
        LoadLDSData(GI + 2, depth.y, normals[1]);
        LoadLDSData(GI + 16, depth.z, normals[2]);
        LoadLDSData(GI + 18, depth.w, normals[3]);

        GetDepthAndNormal(depth, normals, downSampleNormal, downSampleDepth);
        depthDownTexture3[DTid.xy / 4] = downSampleDepth;
        normalDownTexture3[DTid.xy / 4] = downSampleNormal * 0.5 + 0.5;

        StoreLDSData(GI, downSampleDepth, downSampleNormal);
    }

    GroupMemoryBarrierWithGroupSync();

    if (GI == 0)
    {
        LoadLDSData(GI, depth.x, normals[0]);
        LoadLDSData(GI + 4, depth.y, normals[1]);
        LoadLDSData(GI + 32, depth.z, normals[2]);
        LoadLDSData(GI + 34, depth.w, normals[3]);

        GetDepthAndNormal(depth, normals, downSampleNormal, downSampleDepth);
        depthDownTexture4[DTid.xy / 8] = downSampleDepth;
        normalDownTexture4[DTid.xy / 8] = downSampleNormal * 0.5 + 0.5;
    }
}
