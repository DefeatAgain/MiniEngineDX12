#pragma once
#include <cstdint>
#include "Math/VectorMath.h"

#define MAX_CSM_DIVIDES 3


__declspec(align(256)) 
struct ModelConstants
{
    Math::Matrix4 World;         // Object to world
    Math::Matrix3 WorldIT;       // Object normal to world normal
};


struct PBRMaterialConstants
{
    float baseColorFactor[4]; // default=[1,1,1,1]
    float emissiveFactor[3]; // default=[0,0,0]
    float normalTextureScale; // default=1
    float metallicFactor; // default=1
    float roughnessFactor; // default=1
    union
    {
        uint32_t flags;
        struct
        {
            // UV0 or UV1 for each texture
            uint32_t baseColorUV : 1;
            uint32_t metallicRoughnessUV : 1;
            uint32_t emissiveUV : 1;
            uint32_t normalUV : 1;

            // Three special modes
            uint32_t twoSided : 1;
            uint32_t alphaTest : 1;
            uint32_t alphaBlend : 1;

            uint32_t _pad : 9;

            uint32_t alphaRef : 16; // half float
        };
    };
};


struct GlobalConstants
{
    Math::Matrix4 ViewProjMatrix;
    Math::Matrix4 InvViewProjMatrix;
    Math::Matrix4 SunShadowMatrix[MAX_CSM_DIVIDES + 1];
    Math::Vector3 CSMDivides;
    Math::Vector3 CameraPos;
    Math::Vector3 SunDirection;
    Math::Vector3 SunIntensity;
    float IBLRange;
    float ShadowBias;
    float gNearZ;
    float gFarZ;
};
