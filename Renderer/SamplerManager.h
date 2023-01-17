#pragma once
#include "CoreHeader.h"
#include "Common.h"
#include "DescriptorHandle.h"
#include "Color.h"


class SamplerDesc : public D3D12_SAMPLER_DESC
{
public:
    // These defaults match the default values for HLSL-defined root
    // signature static samplers.  So not overriding them here means
    // you can safely not define them in HLSL.
    SamplerDesc()
    {
        Filter = D3D12_FILTER_ANISOTROPIC;
        AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        MipLODBias = 0.0f;
        MaxAnisotropy = 16;
        ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        BorderColor[0] = 1.0f;
        BorderColor[1] = 1.0f;
        BorderColor[2] = 1.0f;
        BorderColor[3] = 1.0f;
        MinLOD = 0.0f;
        MaxLOD = D3D12_FLOAT32_MAX;
    }

    void SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE addressMode)
    {
        AddressU = addressMode;
        AddressV = addressMode;
        AddressW = addressMode;
    }

    void SetBorderColor(Color border)
    {
        BorderColor[0] = border.R();
        BorderColor[1] = border.G();
        BorderColor[2] = border.B();
        BorderColor[3] = border.A();
    }

    // Allocate new descriptor as needed; no cached
    DescriptorHandle CreateDescriptor();

    // Create descriptor in place (no deduplication).  Handle must be preallocated
    void CreateDescriptor(const DescriptorHandle& handle);

    // must destroy manually if create DescriptorHandle.
    void DestroyDescriptor(DescriptorHandle& handle);
};


class SamplerManager : public Singleton<SamplerManager>
{
    USE_SINGLETON;
private:
    SamplerManager() {}
public:
    ~SamplerManager() { Clear(); }

    DescriptorHandle CreateDescriptor(SamplerDesc samDesc);

    // all DescriptorHandle invalid
    void Clear();
};

#define GET_SAM(samDesc) SamplerManager::GetInstance()->CreateDescriptor(samDesc)
