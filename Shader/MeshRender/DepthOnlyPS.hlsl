#include "ForwardRS.hlsli"

struct PSInput
{
    float4 positionSV : SV_Position;
#ifdef ENABLE_ALPHATEST 
    float2 uv : TEXCOORD0;
#endif
};

Texture2D<float4> baseColorTexture          : register(t0);
SamplerState baseColorSampler               : register(s0);

cbuffer MaterialConstants : register(b0)
{
    float4 gBaseColorFactor;
    float3 gEmissiveFactor;
    float gNormalTextureScale;
    float2 gMetallicRoughnessFactor;
    uint gMaterialFlags;
}

[RootSignature(ForwardRendererRootSig)]
void main(PSInput psInput)
{
#ifdef ENABLE_ALPHATEST
    float cutoff = f16tof32(gMaterialFlags >> 16);
    if (baseColorTexture.Sample(baseColorSampler, psInput.uv).a * gBaseColorFactor.a < cutoff)
        discard;
#endif
}
