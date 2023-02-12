#define ForwardRendererRootSig \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX), " \
    "CBV(b0, visibility = SHADER_VISIBILITY_PIXEL), " \
    "CBV(b1), " \
    "DescriptorTable(SRV(t0, numDescriptors = 5), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(Sampler(s0, numDescriptors = 5), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(SRV(t10, numDescriptors = 5), visibility = SHADER_VISIBILITY_PIXEL)," \
    "StaticSampler(s10, maxAnisotropy = 8, visibility = SHADER_VISIBILITY_PIXEL)," \
    "StaticSampler(s11, visibility = SHADER_VISIBILITY_PIXEL," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "comparisonFunc = COMPARISON_GREATER_EQUAL," \
        "filter = FILTER_MIN_MAG_LINEAR_MIP_POINT)," \
    "StaticSampler(s12, visibility = SHADER_VISIBILITY_PIXEL," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "filter = FILTER_MIN_MAG_LINEAR_MIP_POINT)"
    
// Common (static) samplers
SamplerState defaultSampler : register(s10);
SamplerComparisonState shadowSamplerComparison : register(s11);
SamplerState shadowSamplerPoint : register(s12);
