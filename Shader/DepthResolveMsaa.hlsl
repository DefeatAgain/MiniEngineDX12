#include "CommonRS.hlsli"

#ifdef USE_ARRAY
Texture2DMSArray<float> input : register(t0);
RWTexture2DArray<float> output : register(u0);
#else
Texture2DMS<float> input : register(t0);
RWTexture2D<float> output : register(u0);
#endif


cbuffer Constants : register(b0)
{
    uint gSampleCount;
    uint gArraySize;
}

[RootSignature(Common_RootSig)]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
#ifdef USE_ARRAY
    for (uint i = 0; i < gArraySize; i++)
    {
        float samples = 0.0;
        for (uint j = 0; j < gSampleCount; j++)
            samples += input.Load(int3(DTid.xy, i), j);
        output[int3(DTid.xy, i)] = samples / gSampleCount;
    }
#else
    float samples = 0.0;
    // for (uint i = 0; i < gSampleCount; i++)
    //     samples = min(samples, input.Load(DTid.xy, i));
    // output[DTid.xy] = samples;
    for (uint i = 0; i < gSampleCount; i++)
        samples += input.Load(DTid.xy, i);
    output[DTid.xy] = samples / gSampleCount;
#endif
}
