#pragma once

#define ForwardRendererRootSig \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX), " \
    "CBV(b0, visibility = SHADER_VISIBILITY_PIXEL), " \
    "CBV(b1), " \
    "DescriptorTable(SRV(t0, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(SRV(t1, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(SRV(t2, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(SRV(t3, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(SRV(t4, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(Sampler(s0, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(Sampler(s1, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(Sampler(s2, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(Sampler(s3, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(Sampler(s4, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(SRV(t10, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(SRV(t11, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(SRV(t12, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(SRV(t13, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL)," \
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
