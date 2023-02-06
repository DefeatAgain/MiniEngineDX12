#include "ForwardRS.hlsli"

cbuffer SkyboxConstants : register(b0)
{
    float mipMapLevel;
};

TextureCube<float3> radianceIBLTexture      : register(t10);

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 viewDir : TEXCOORD3;
};

[RootSignature(ForwardRendererRootSig)]
float4 main(VSOutput vsOutput) : SV_Target0
{
    return float4(radianceIBLTexture.SampleLevel(defaultSampler, vsOutput.viewDir, mipMapLevel), 1);
}
