#pragma once
#include "CoreHeader.h"
#include "Common.h"

class CommandList;

class GpuResource
{
    friend class CommandList;
    friend class FrameContext;
    friend struct ResourceStateCache;
public:
    GpuResource() :
        mGpuVirtualAddress(D3D12_VIRTUAL_ADDRESS_NULL),
        mUsageState(D3D12_RESOURCE_STATE_COMMON)
    {}

    GpuResource(ID3D12Resource* pResource, D3D12_RESOURCE_STATES usageState) :
        mGpuVirtualAddress(D3D12_VIRTUAL_ADDRESS_NULL),
        mResource(pResource),
        mUsageState(usageState)
    {}

    virtual ~GpuResource() { Destroy(); }

    virtual void Destroy()
    {
        mResource = nullptr;
        mGpuVirtualAddress = D3D12_VIRTUAL_ADDRESS_NULL;
    }

    ID3D12Resource* GetResource() { return mResource.Get(); }
    const ID3D12Resource* GetResource() const { return mResource.Get(); }

    ID3D12Resource** GetResourceAddressOf() { return mResource.GetAddressOf(); }

    D3D12_GPU_VIRTUAL_ADDRESS GetGpuVirtualAddress() const { return mGpuVirtualAddress; }
protected:
    Microsoft::WRL::ComPtr<ID3D12Resource> mResource;
    D3D12_RESOURCE_STATES mUsageState;
    D3D12_GPU_VIRTUAL_ADDRESS mGpuVirtualAddress;
};
