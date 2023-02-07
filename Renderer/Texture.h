#pragma once
#include "GpuResource.h"
#include "GraphicsContext.h"
#include "DescriptorHandle.h"
#include "Common.h"

class CommandList;

enum eTextureFlags : uint16_t
{
    kNoneTextureFlag = 0,
    kSRGB = 0x1,          // Texture contains sRGB colors
    kPreserveAlpha = 0x2, // Keep four channels
    kNormalMap = 0x4,     // Texture contains normals
    kBumpToNormal = 0x8,  // Generate a normal map from a bump map
    kDefaultBC = 0x10,    // Apply standard block compression (BC1-5)
    kQualityBC = 0x20,    // Apply quality block compression (BC6H/7)
    kFlipVertical = 0x40,
};

//inline uint16_t SetTextureFlags(bool sRGB = false, bool alpha = false, bool isNormalMap = false, bool bumpToNormal = false)


class Texture : public GpuResource, public Graphics::CopyContext
{
public:
    Texture(const std::wstring& name = L"Textrue") : mWidth(1), mHeight(1), mDepth(1), mName(name) {}
    Texture(DescriptorHandle handle) : mWidth(0), mHeight(0), mDepth(0), mDescriptorHandle(handle) {}

    // sync way to create!
    void Create2D(size_t rowPitchBytes, size_t width, size_t height, DXGI_FORMAT format, const void* initData);
    void CreateCube(size_t rowPitchBytes, size_t width, size_t height, DXGI_FORMAT format, const void* initialData);

    void CreateTGAFromMemory(const void* memBuffer, size_t fileSize);
    bool CreateDDSFromMemory(const void* memBuffer, size_t fileSize);
    void CreatePIXImageFromMemory(const void* memBuffer, size_t fileSize);
    bool CreateFromDirectXTex(std::filesystem::path filepath, uint16_t flags);

    virtual void Destroy() override;
    void Reset();

    const DescriptorHandle& GetSRV() const { return mDescriptorHandle; }
    virtual bool isValid() const { return AsyncContext::isValid() && mContextFence != 0; }

    uint32_t GetWidth() const { return mWidth; }
    uint32_t GetHeight() const { return mHeight; }
    uint32_t GetDepth() const { return mDepth; }

    std::wstring mName;
private:
    bool ConvertToDDS(std::filesystem::path filepath, uint16_t flags);

    CommandList* InitTextureTask(CommandList* commandList, UINT numSubresources, D3D12_SUBRESOURCE_DATA subData[]);
    CommandList* InitTextureTask1(CommandList* commandList, std::shared_ptr<std::vector<D3D12_SUBRESOURCE_DATA>> subData,
        std::shared_ptr<std::vector<uint8_t>> initData);
protected:
    uint32_t mWidth;
    uint32_t mHeight;
    uint32_t mDepth;

    DescriptorHandle mDescriptorHandle;
};


class TextureRef
{
public:
    TextureRef() : mRef(nullptr) {}
    TextureRef(const Texture* ref) : mRef(ref) {}
    ~TextureRef() {}

    // Check that this points to a valid texture (which loaded successfully)
    bool IsValid() const { return mRef->isValid(); }

    // Gets the SRV descriptor handle.  If the reference is invalid,
    // returns a valid descriptor handle (specified by the fallback)
    DescriptorHandle GetSRV() const;

    const Texture* Get() const;

    const Texture* operator->() const { return Get(); }
private:
    const Texture* mRef;
};


class TextureManager : public Singleton<TextureManager>
{
    USE_SINGLETON;
private:
    TextureManager(const std::filesystem::path& rootPath) : mRootPath(rootPath) {}
public:
    ~TextureManager() {}

    TextureRef GetTexture(const std::filesystem::path& filename, uint16_t flags = 0, Graphics::eDefaultTexture fallback = Graphics::kMagenta2D);
    
    std::filesystem::path GetAbsRootPath() const { return std::filesystem::current_path() / mRootPath; }
private:
    std::filesystem::path mRootPath;
    std::unordered_map<std::filesystem::path, Texture, std::path_hash> mTextures;
};

#define GET_TEX(filename) TextureManager::GetInstance()->GetTexture(filename)
#define GET_TEXF(filename, flags) TextureManager::GetInstance()->GetTexture(filename, flags)
#define GET_TEXFF(filename, flags, fallback) TextureManager::GetInstance()->GetTexture(filename, flags, fallback)
