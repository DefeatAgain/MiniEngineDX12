//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  James Stanard 
//

#include "ShaderUtility.hlsli"
#include "PresentRS.hlsli"
#include "ToneMappingUtility.hlsli"

#ifndef TONEMAPPING
#define TONEMAPPING 1
#endif

Texture2D<float3> MainBuffer : register(t0);

SamplerState BilinearFilter : register(s0);

cbuffer Constants : register(b0)
{
    float2 UVOffset;
}

float3 SampleColor(float2 uv)
{
    return MainBuffer.SampleLevel(BilinearFilter, uv, 0);
}

float3 ScaleBuffer(float2 uv)
{
    // 看起来像是锐化图像
    return 1.4 * SampleColor(uv) - 0.1 * (
        SampleColor(uv + float2(+UVOffset.x, +UVOffset.y)) +
        SampleColor(uv + float2(+UVOffset.x, -UVOffset.y)) +
        SampleColor(uv + float2(-UVOffset.x, +UVOffset.y)) +
        SampleColor(uv + float2(-UVOffset.x, -UVOffset.y))
        );
}

[RootSignature(Present_RootSig)]
float3 main( float4 position : SV_Position, float2 uv : TexCoord0 ) : SV_Target0
{
    float3 MainColor = ScaleBuffer(uv);
#if TONEMAPPING == 1
    MainColor = TM_ReinhardSq(MainColor);
#endif
    return float4(ApplyDisplayProfile(MainColor, DISPLAY_PLANE_FORMAT), 1.0);
}
