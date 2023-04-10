#include "ForwardRS.hlsli"
#include "ShadowUtility.hlsli"

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
    float3 normal : NORMAL;
    float4 tanget : TANGENT;
    float2 uv0 : TEXCOORD0;
#ifdef SECOND_UV
    float2 uv1 : TEXCOORD1;
#endif
};

struct VSOutput
{
    float4 positionSV : SV_Position;
    float3 positionWorld : POSITION;
    float3 normalWorld : NORMAL;
    float4 tangetWorld : TANGENT;
    float2 uv0 : TEXCOORD0;
#ifdef SECOND_UV
    float2 uv1 : TEXCOORD1;
#endif
};


[RootSignature(ForwardRendererRootSig)]
VSOutput main(VSInput vsInput)
{
    VSOutput vsOutput;
    vsOutput.positionWorld = mul(gWorldMatrix, float4(vsInput.position, 1.0)).xyz;
    vsOutput.positionSV = mul(gViewProjMatrix, float4(vsOutput.positionWorld, 1.0));
    vsOutput.normalWorld = mul(gWorldITMatrix, vsInput.normal * 2.0 - 1.0);
    vsOutput.tangetWorld = float4(mul(gWorldITMatrix, vsInput.tanget.xyz * 2.0 - 1.0), vsInput.tanget.w);
    vsOutput.uv0 = vsInput.uv0;
#ifdef SECOND_UV
    vsOutput.uv1 = vsInput.uv1;
#endif
    return vsOutput;
}
