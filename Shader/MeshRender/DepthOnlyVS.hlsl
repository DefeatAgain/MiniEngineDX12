#include "ForwardRS.hlsli"

cbuffer MeshConstants : register(b0)
{
    float4x4 gWorldMatrix;
    float3x3 gWorldITMatrix;
}

cbuffer GlobalConstants : register(b1)
{
    float4x4 gViewProjMatrix;
}

struct VSInput
{
    float3 position : POSITION;
#ifdef ENABLE_ALPHATEST
    float2 uv0 : TEXCOORD0;
#endif
};

struct VSOutput
{
    float4 positionSV : SV_POSITION;
#ifdef ENABLE_ALPHATEST
    float2 uv0 : TEXCOORD0;
#endif
};

[RootSignature(ForwardRendererRootSig)]
VSOutput main(VSInput vsInput)
{
    VSOutput vsOutput;
    vsOutput.positionSV = mul(gViewProjMatrix, float4(vsInput.position, 1.0));
#ifdef ENABLE_ALPHATEST
    vsOutput.uv0 = vsInput.uv0;
#endif
    return vsOutput;
}

