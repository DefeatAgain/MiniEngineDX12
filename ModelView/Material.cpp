#include "Material.h"
#include "DescriptorHandle.h"

void MaterialManager::Reserve(size_t size)
{
    if (size <= mAllMaterials.size())
        return;

    mDirtyMaterialIndices.clear();
    for (size_t i = 0; i < mAllMaterials.size(); i++)
    {
        mAllMaterials[i]->mNumDirtyCount = SWAP_CHAIN_BUFFER_COUNT;
        mDirtyMaterialIndices.push_back(i);
    }
    mNumDirtyCount = SWAP_CHAIN_BUFFER_COUNT;

    mAllMaterials.reserve(size);
}

void MaterialManager::Update()
{
    if (mNumDirtyCount > 0 && mConstantBufferSize > 0)
    {
        mGpuBuffer[CURRENT_FARME_BUFFER_INDEX].Create(L"Material Buffer", mConstantBufferSize * 256 * 2);
        mNumDirtyCount--;
    }

    for (auto beg = mDirtyMaterialIndices.begin(); beg != mDirtyMaterialIndices.end();)
    {
        if (UpdateMaterial(*beg) == 0)
            beg = mDirtyMaterialIndices.erase(beg);
        else
            beg++;
    }
}

uint16_t MaterialManager::UpdateMaterial(size_t index)
{
    Material* material = mAllMaterials[index].get();
    ASSERT(material->mNumDirtyCount > 0);

    void* mappedBuffer = mGpuBuffer[CURRENT_FARME_BUFFER_INDEX].Map();
    mappedBuffer = (uint8_t*)mappedBuffer + 256 * material->mBufferOffset;
    CopyMemory(mappedBuffer, material->GetMaterialConstant(), Math::AlignUp(material->GetMaterialConstantSize(), 256));
    return --material->mNumDirtyCount;
}

void PBRMaterial::CreateHandles()
{
    mTextureHandles = ALLOC_DESCRIPTOR(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kNumTextures);
    mSamplerHandles = ALLOC_DESCRIPTOR(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kNumTextures);
}

void PBRMaterial::DestroyHandles()
{
    DEALLOC_DESCRIPTOR(mTextureHandles, kNumTextures);
    DEALLOC_DESCRIPTOR(mSamplerHandles, kNumTextures);
}
