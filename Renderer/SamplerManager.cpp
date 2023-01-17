#include "SamplerManager.h"
#include "Utils/Hash.h"
#include "Graphics.h"

namespace
{
    std::map<size_t, DescriptorHandle> sSamplerCache;
}


DescriptorHandle SamplerDesc::CreateDescriptor()
{
    DescriptorHandle Handle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    Graphics::gDevice->CreateSampler(this, Handle);
    return Handle;
}

void SamplerDesc::CreateDescriptor(const DescriptorHandle& Handle)
{
    ASSERT(Handle);
    Graphics::gDevice->CreateSampler(this, Handle);
}

void SamplerDesc::DestroyDescriptor(DescriptorHandle& Handle)
{
    Graphics::DeAllocateDescriptor(Handle, 1);
}

DescriptorHandle SamplerManager::CreateDescriptor(SamplerDesc samDesc)
{
    size_t hashValue = Utility::HashState(&samDesc);
    auto iter = sSamplerCache.find(hashValue);
    if (iter != sSamplerCache.end())
    {
        return iter->second;
    }

    DescriptorHandle Handle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    Graphics::gDevice->CreateSampler(&samDesc, Handle);
    return sSamplerCache.emplace(hashValue, Handle).first->second;
}

void SamplerManager::Clear()
{
    for (auto& kv : sSamplerCache)
    {
        Graphics::DeAllocateDescriptor(kv.second, 1);
    }
}
