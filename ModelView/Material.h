#pragma once
#include "GpuBuffer.h"
#include "Texture.h"
#include "FrameContext.h"
#include "ConstantBuffer.h"

enum eMaterialType : uint8_t
{
    kPBRMaterial,
    kNumMaterialTypes
};

const char* MaterialTypeStr[kNumMaterialTypes] =
{
    "PBRMaterial"
};

enum ePSOFlags : uint16_t
{
    kHasPosition = 0x001,   // Required
    kHasColor = 0x002,
    kHasNormal = 0x004,     // Required
    kHasTangent = 0x008,
    kHasUV0 = 0x010,        // Required (for now)
    kAlphaBlend = 0x020,
    kAlphaTest = 0x040,
    kTwoSided = 0x080,
    kHasUV1 = 0x100,
    //kHasUV2 = 0x200,
    //kHasUV3 = 0x400,

    //kHasSkin = 0x200,     // Implies having indices and weights
};

class Material;
class GraphicsPipelineState;


class MaterialManager : public Singleton<MaterialManager>
{
    USE_SINGLETON;
private:
    MaterialManager() : mConstantBufferSize(0), mNumDirtyCount(0)
    {
    }
public:
    ~MaterialManager() {}

    void Reserve(size_t size) 
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

    Material* GetMaterial(size_t index) { return mAllMaterials[index].get(); }

    template<typename MaterialType>
    MaterialType& AddMaterial(bool isShared = true)
    {
        static_assert(std::is_base_of_v<Material, MaterialType>);

        if (mAllMaterials.capacity() == 0)
            Reserve(mAllMaterials.size() * 2);

        MaterialType& material = *mAllMaterials.emplace_back(std::make_unique<MaterialType>());
        material.mIsShared = isShared;
        material.mMaterialIdx = mAllMaterials.size() - 1;
        material.mBufferOffset = mConstantBufferSize;

        mConstantBufferSize += Math::AlignUp(material.GetMaterialConstantSize(), 256) / 256;
        return material;
    }

    void DirtyMaterial(size_t index)
    {
        ASSERT(index <= 0x7FFF);
        mAllMaterials[index]->mNumDirtyCount = SWAP_CHAIN_BUFFER_COUNT;
        mDirtyMaterialIndices.push_back(index);
    }

    void Update()
    {
        if (mNumDirtyCount > 0 && mConstantBufferSize > 0)
        {
            mGpuBuffer[CURRENT_SCENE_COLOR_BUFFER_INDEX].Create(L"Material Buffer", mConstantBufferSize * 256 * 2);
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

    D3D12_GPU_VIRTUAL_ADDRESS GetGpuBufferView(size_t index) const
    {
        Material* material = mAllMaterials[index].get();
        return mGpuBuffer[CURRENT_SCENE_COLOR_BUFFER_INDEX].GetGpuVirtualAddress() + 256 * material->mBufferOffset;
    }
private:
    uint16_t UpdateMaterial(size_t index)
    {
        Material* material = mAllMaterials[index].get();
        ASSERT(material->mNumDirtyCount > 0);

        void* mappedBuffer = mGpuBuffer[CURRENT_SCENE_COLOR_BUFFER_INDEX].Map();
        mappedBuffer = (uint8_t*)mappedBuffer + 256 * material->mBufferOffset;
        CopyMemory(mappedBuffer, material->GetMaterialConstant(), Math::AlignUp(material->GetMaterialConstantSize(), 256));
        return --material->mNumDirtyCount;
    }
private:
    std::vector<std::unique_ptr<Material>> mAllMaterials;       // store by index
    UploadBuffer mGpuBuffer[SWAP_CHAIN_BUFFER_COUNT];        // store frame resource index
    std::list<uint16_t> mDirtyMaterialIndices;
    uint16_t mConstantBufferSize;                                  // divided by 256
    size_t mNumDirtyCount;
    std::vector<GraphicsPipelineState*> mPSOs;
};

#define UPDATE_MATERIAL(index) MaterialManager::GetInstance()->DirtyMaterial(index)
#define GET_MATERIAL(index) MaterialManager::GetInstance()->GetMaterial(index)
#define GET_MAT_VPTR(index) MaterialManager::GetInstance()->GetGpuBufferView(index)


class Material
{
    friend class MaterialManager;
public:
    ~Material() {}

    virtual const void* GetMaterialConstant() const = 0;

    virtual size_t GetMaterialConstantSize() const = 0;

    uint16_t GetMaterialIdx() const { return mMaterialIdx; }

    //uint16_t GetPSOIdx() const { return mPSOIndex; }
    eMaterialType GetType() const { return (eMaterialType)mType; }
protected:
    Material(eMaterialType type) : mIsShared(0), mType(type), mMaterialIdx(0)/*, mPSOIndex(0)*/ {}
private:
    uint16_t mIsShared : 1;
protected:
    uint16_t mType : 8;
    uint16_t mNumDirtyCount : 7;
    uint16_t mMaterialIdx;           // Index of material
    uint32_t mBufferOffset;          // Offset of GpuBuffer, mutipile of 256 
    //uint16_t mPSOIndex;              // Index of pipeline state object
};


class PBRMaterial : public Material
{
public:
    enum eTextureType
    { 
        kBaseColor, 
        kMetallicRoughness, 
        kOcclusion, 
        kEmissive,
        kNormal, 
        kNumTextures 
    };
public:
    PBRMaterial() : Material(kPBRMaterial) {}
    ~PBRMaterial() {}
protected:
    virtual const void* GetMaterialConstant() const override { return reinterpret_cast<const void*>(&mMaterialConstant); }

    virtual size_t GetMaterialConstantSize() const override { return sizeof(mMaterialConstant); }
public:
    PBRMaterialConstants mMaterialConstant;
    TextureRef mTextures[kNumTextures];
    DescriptorHandle mSamplers[kNumTextures];
};
