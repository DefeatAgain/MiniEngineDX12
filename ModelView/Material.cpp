#include "Material.h"
#include "DescriptorHandle.h"
#include "Graphics.h"
#include "CommandQueue.h"

void Material::UpdateDescriptor()
{
    std::vector<DescriptorHandle> texHandles = GetTextureCpuHandles();
    std::vector<DescriptorHandle> samHandles = GetSamplerCpuHandles();

    if (mAllGpuTextureHandles)
        DEALLOC_DESCRIPTOR_GPU(mAllGpuTextureHandles, 8);
    if (mAllGpuSamplerHandles)
        DEALLOC_DESCRIPTOR_GPU(mAllGpuSamplerHandles, 8);

    ASSERT(texHandles.size() <= 8 && samHandles.size() <= 8);

    mAllGpuTextureHandles = ALLOC_DESCRIPTOR_GPU(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8);
    mAllGpuSamplerHandles = ALLOC_DESCRIPTOR_GPU(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 8);

    for (uint32_t i = 0; i < texHandles.size(); i++)
        Graphics::gDevice->CopyDescriptorsSimple(1, mAllGpuTextureHandles + i, texHandles[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (uint32_t i = 0; i < samHandles.size(); i++)
        Graphics::gDevice->CopyDescriptorsSimple(1, mAllGpuSamplerHandles + i, samHandles[i], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
}

std::vector<DescriptorHandle> PBRMaterial::GetTextureCpuHandles() const
{
    std::vector<DescriptorHandle> res;
    for (size_t i = 0; i < kNumTextures; i++)
    {
        mTextures[i].WaitForValid();
        res.push_back(mTextures[i].GetSRV());
    }
    return res;
}

std::vector<DescriptorHandle> PBRMaterial::GetSamplerCpuHandles() const
{
    std::vector<DescriptorHandle> res;
    for (uint32_t i = 0; i < kNumTextures; i++)
        res.push_back(mSamplerHandles[i]);
    return res;
}


void MaterialManager::Reserve(size_t size)
{
    if (size <= mAllMaterials.size())
        return;

    for (size_t i = 0; i < mAllMaterials.size(); i++)
    {
        mAllMaterials[i]->mNumDirtyCount = SWAP_CHAIN_BUFFER_COUNT;
    }
    mNumDirtyCount = SWAP_CHAIN_BUFFER_COUNT;

    mAllMaterials.reserve(size);
}

void MaterialManager::Update()
{
    if (mNumDirtyCount == SWAP_CHAIN_BUFFER_COUNT)
    {
        CommandQueueManager::GetInstance()->IdleGPU();

        for (auto& mat : mAllMaterials)
        {
            mat->UpdateDescriptor();
        }
    }

    if (mNumDirtyCount > 0 && mConstantBufferSize > 0)
    {
        mGpuBuffer[CURRENT_FARME_BUFFER_INDEX].Destroy();
        mGpuBuffer[CURRENT_FARME_BUFFER_INDEX].Create(L"Material Buffer", mConstantBufferSize * 256 * 2);
        mNumDirtyCount--;
    }

    for (size_t i = 0; i < mAllMaterials.size(); i++)
    {
        UpdateMaterial(i);
    }
}

void MaterialManager::UpdateMaterial(size_t index)
{
    Material* material = mAllMaterials[index].get();
    size_t a = material->mNumDirtyCount;
    if (material->mNumDirtyCount == 0)
        return;
    --material->mNumDirtyCount;

    void* mappedBuffer = mGpuBuffer[CURRENT_FARME_BUFFER_INDEX].Map();
    mappedBuffer = (uint8_t*)mappedBuffer + 256 * material->mBufferOffset;

    CopyMemory(mappedBuffer, material->GetMaterialConstant(), material->GetMaterialConstantSize());

    material->UpdateDescriptor();
}
