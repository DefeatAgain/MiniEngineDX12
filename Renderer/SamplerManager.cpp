#include "SamplerManager.h"
#include "Utils/Hash.h"
#include "Graphics.h"

namespace
{
    std::map<size_t, DescriptorHandle> sSamplerCache;
}


DescriptorHandle SamplerDesc::CreateDescriptor()
{
    DescriptorHandle Handle = ALLOC_DESCRIPTOR1(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
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
    DEALLOC_DESCRIPTOR(Handle, 1);
}

DescriptorHandle SamplerManager::GetOrCreateDescriptor(SamplerDesc samDesc)
{
    size_t hashValue = Utility::HashState(&samDesc);
    auto iter = sSamplerCache.find(hashValue);
    if (iter != sSamplerCache.end())
    {
        return iter->second;
    }

    DescriptorHandle Handle = ALLOC_DESCRIPTOR1(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    Graphics::gDevice->CreateSampler(&samDesc, Handle);
    return sSamplerCache.emplace(hashValue, Handle).first->second;
}

DescriptorHandle SamplerManager::GetOrCreateDescriptor(SamplerDesc samDesc, DescriptorHandle handle)
{
    Graphics::gDevice->CreateSampler(&samDesc, handle);
    return handle;
}

void SamplerManager::Clear()
{
    for (auto& kv : sSamplerCache)
    {
        DEALLOC_DESCRIPTOR(kv.second, 1);
    }
}
