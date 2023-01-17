#pragma once
#include "GpuBuffer.h"
#include "Texture.h"
#include "FrameContext.h"
#include "ConstantBuffer.h"

enum eMaterialType
{
    kPBRMaterial
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
    kHasUV2 = 0x200,
    kHasUV3 = 0x400,

    //kHasSkin = 0x200,     // Implies having indices and weights
};

class Material;
class GraphicsPipelineState;


class MaterialManager : public Singleton<MaterialManager>
{
    USE_SINGLETON;
private:
    MaterialManager() {}
public:
    ~MaterialManager() {}

    void Reserve(size_t size) { mAllMaterials.reserve(size); }

    Material* GetMaterial(size_t index) { return mAllMaterials[index].get(); }

    template<typename MaterialType>
    MaterialType& AddMaterial()
    {
        static_assert(std::is_base_of_v<Material, MaterialType>);

        return *mAllMaterials.emplace_back(std::make_unique<MaterialType>());
    }

    template<typename MaterialType>
    MaterialType& SetMaterial(size_t index, MaterialType& mat)
    {
        static_assert(std::is_base_of_v<Material, MaterialType>);

        return *mAllMaterials[index] = mat;
    }
private:
    std::vector<std::unique_ptr<Material>> mAllMaterials;
    std::vector<GraphicsPipelineState*> mPSOs;
};


class Material
{
public:
    ~Material() {}

    virtual const void* GetMaterialConstant() const = 0;

    virtual size_t GetMaterialConstantSize() const = 0;

    D3D12_GPU_VIRTUAL_ADDRESS GetGpuBufferView() const { return mMaterialBuffer[CURRENT_SCENE_COLOR_BUFFER_INDEX].GetGpuVirtualAddress(); }

    void Update()
    {
        void* mappedBuffer = mMaterialBuffer[CURRENT_SCENE_COLOR_BUFFER_INDEX].Map();
        CopyMemory(mappedBuffer, GetMaterialConstant(), GetMaterialConstantSize());
    }
    
    uint16_t GetMaterialIdx() const { return mMaterialIdx; }

    uint16_t GetPSOIdx() const { return mPSOIndex; }
protected:
    Material() : mMaterialIdx(0), mPSOIndex(0) {}
private:
    void CreateUploadBuffer()
    {
        mMaterialBuffer.resize(SWAP_CHAIN_BUFFER_COUNT);
        for (size_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
            mMaterialBuffer[i].Create(L"Material " + std::to_wstring(mMaterialIdx), GetMaterialConstantSize());
    }
protected:
    eMaterialType mType;
    uint16_t mMaterialIdx;         // Index of material
    uint16_t mPSOIndex;           // Index of pipeline state object
private:
    std::vector<UploadBuffer> mMaterialBuffer;
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
    PBRMaterial() { mType = kPBRMaterial; }
    ~PBRMaterial() {}
protected:
    virtual const void* GetMaterialConstant() const override { return reinterpret_cast<const void*>(&mMaterialConstant); }

    virtual size_t GetMaterialConstantSize() const override { return sizeof(mMaterialConstant); }
public:
    PBRMaterialConstants mMaterialConstant;
    TextureRef mTextures[kNumTextures];
    DescriptorHandle mSamplers[kNumTextures];
};
