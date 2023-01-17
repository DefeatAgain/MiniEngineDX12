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

Texture2D<float3> ColorTex : register(t0);

[RootSignature(Present_RootSig)]
float3 main( float4 position : SV_Position ) : SV_Target0
{
    float3 LinearRGB = ColorTex[(int2)position.xy];
#if TONEMAPPING == 1
    LinearRGB = TM_ReinhardSq(LinearRGB);
#endif
    return ApplyDisplayProfile(LinearRGB, DISPLAY_PLANE_FORMAT);
}