#include "ForwardRS.hlsli"

cbuffer SkyboxConstants : register(b0)
{
    float4x4 gProjInverse;
    float3x3 gViewInverse;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 viewDir : TEXCOORD;
};

[RootSignature(ForwardRendererRootSig)]
VSOutput main(uint VertID : SV_VertexID)
{
    float2 screenUV = float2(uint2(VertID, VertID << 1) & 2);
    float4 projectedPos = float4(lerp(float2(-1, 1), float2(1, -1), screenUV), 0, 1); // NDC Space
    float4 posViewSpace = mul(gProjInverse, projectedPos); // View Space

    VSOutput vsOutput;
    vsOutput.position = projectedPos;
    vsOutput.viewDir = mul(gViewInverse, posViewSpace.xyz / posViewSpace.w); // World Space

    return vsOutput;
}
