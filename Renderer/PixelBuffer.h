#pragma once
#include "GpuResource.h"
#include "Color.h"
#include "DescriptorHandle.h"
#include "GraphicsContext.h"
#include "Utils/DebugUtils.h"

class CommandList;

class PixelBuffer : public GpuResource, public Graphics::CopyContext
{
public:
    PixelBuffer() : mWidth(0), mHeight(0), mArraySize(0), mNumMipMaps(0), mSampleCount(1), mFormat(DXGI_FORMAT_UNKNOWN) {}

    uint32_t GetWidth() const { return mWidth; }
    uint32_t GetHeight() const { return mHeight; }
    uint32_t GetDepth() const { return mArraySize; }
    const DXGI_FORMAT& GetFormat() const { return mFormat; }

    // Write the raw pixel buffer contents to a file
    // Note that data is preceded by a 16-byte header:  { DXGI_FORMAT, Pitch (in pixels), width (in pixels), height }
    void ExportToFile(const std::wstring& filePath);

    static DXGI_FORMAT GetBaseFormat(DXGI_FORMAT format);
    static DXGI_FORMAT GetSRVFormat(DXGI_FORMAT format);
    static DXGI_FORMAT GetUAVFormat(DXGI_FORMAT format);
    static DXGI_FORMAT GetDSVFormat(DXGI_FORMAT format);
    static DXGI_FORMAT GetDepthFormat(DXGI_FORMAT format);
    static DXGI_FORMAT GetStencilFormat(DXGI_FORMAT format);
    static size_t BytesPerPixel(DXGI_FORMAT format);
    static bool CanTypedUAV(DXGI_FORMAT format);
protected:
    CommandList* ExportToFileTask(CommandList* copyList, ReadbackBuffer& dstBuffer, D3D12_PLACED_SUBRESOURCE_FOOTPRINT& placedFootprint);

    D3D12_RESOURCE_DESC DescribeTex2D(uint32_t width, uint32_t height, uint32_t depthOrArraySize, uint32_t numMips, DXGI_FORMAT format, UINT flags);

    void AssociateWithResource(const std::wstring& name, ID3D12Resource* resource, D3D12_RESOURCE_STATES currentState);

    void CreateTextureResource(const std::wstring& name, const D3D12_RESOURCE_DESC& resourceDesc, D3D12_CLEAR_VALUE ClearValue);

    uint32_t mWidth;
    uint32_t mHeight;
    uint32_t mArraySize;
    uint32_t mNumMipMaps; // number of texture sublevels
    uint32_t mSampleCount;
    DXGI_FORMAT mFormat;
};


class ColorBuffer : public PixelBuffer
{
public:
    ColorBuffer(Color ClearColor = Color(0.0f, 0.0f, 0.0f, 0.0f))
        : mClearColor(ClearColor)
    {}

    virtual void Destroy() override;

    // Create a color buffer from a swap chain buffer.  Unordered access is restricted.
    void CreateFromSwapChain(const std::wstring& name, ID3D12Resource* baseResource);

    // Create a color buffer.  If an address is supplied, memory will not be allocated.
    void Create(const std::wstring& name, uint32_t width, uint32_t height, uint32_t numMips,
        DXGI_FORMAT format);

    // Create a color buffer.  If an address is supplied, memory will not be allocated.
    void CreateArray(const std::wstring& name, uint32_t width, uint32_t height, uint32_t arrayCount,
        DXGI_FORMAT format);

    // Get pre-created CPU-visible descriptor handles
    DescriptorHandle GetSRV(UINT arrIndex = 0) const { return mSRVHandle + arrIndex; }
    DescriptorHandle GetRTV(UINT arrIndex = 0) const { return mRTVHandle + arrIndex; }
    DescriptorHandle GetUAV(UINT arrIndex = 0) const { return mUAVHandle + arrIndex; }

    void SetClearColor(Color ClearColor) { mClearColor = ClearColor; }

    void SetMsaaMode(uint32_t numCoverageSamples) { mSampleCount = numCoverageSamples; }

    Color GetClearColor() const { return mClearColor; }

    // Compute the number of texture levels needed to reduce to 1x1.  This uses
    // _BitScanReverse to find the highest set bit.  Each dimension reduces by
    // half and truncates bits.  The dimension 256 (0x100) has 9 mip levels, same
    // as the dimension 511 (0x1FF).
    static uint32_t ComputeNumMips(uint32_t width, uint32_t height)
    {
        uint32_t HighBit;
        _BitScanReverse((unsigned long*)&HighBit, width | height);
        return HighBit + 1;
    }

    void GenerateMipMaps();
protected:
    // This will work for all texture sizes, but it's recommended for speed and quality
    // that you use dimensions with powers of two (but not necessarily square.)  Pass
    // 0 for ArrayCount to reserve space for mips at creation time.
    CommandList* GenerateMipMapsTask(CommandList* commandList, const DescriptorHandle& uavHandle);

    D3D12_RESOURCE_FLAGS CombineResourceFlags(DXGI_FORMAT format) const
    {
        D3D12_RESOURCE_FLAGS Flags = D3D12_RESOURCE_FLAG_NONE;

        if (CanTypedUAV(format) && Flags == D3D12_RESOURCE_FLAG_NONE && mSampleCount == 1)
            Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        return D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | Flags;
    }

    void CreateDerivedViews(DXGI_FORMAT format, uint32_t arraySize, uint32_t numMips = 1);
protected:
    Color mClearColor;
    DescriptorHandle mSRVHandle;
    DescriptorHandle mRTVHandle;
    DescriptorHandle mUAVHandle;
};


class DepthBuffer : public PixelBuffer
{
public:
    DepthBuffer(float clearDepth = 0.0f, uint8_t clearStencil = 0)
        : mClearDepth(clearDepth), mClearStencil(clearStencil), mHasStencilView(false)
    {}

    virtual void Destroy() override;

    // Create a depth buffer. 
    void Create(const std::wstring& name, uint32_t width, uint32_t height, uint32_t numMips, DXGI_FORMAT format);

    void Create(const std::wstring& name, uint32_t width, uint32_t height, uint32_t numSamples, uint32_t numMips, DXGI_FORMAT format);
    
    void Create(const std::wstring& name, uint32_t width, uint32_t height, uint32_t arraySize, uint32_t numSamples, uint32_t numMips, DXGI_FORMAT format);

    // Get pre-created CPU-visible descriptor handles
    DescriptorHandle GetDSV(UINT index = 0) const { return mDSV + index; }
    DescriptorHandle GetDSV_DepthReadOnly(UINT index = 0) const { return mDSV + (1 + index); }
    DescriptorHandle GetDSV_StencilReadOnly(UINT index = 0) const { ASSERT(mHasStencilView); return mDSV + (2 + index); }
    DescriptorHandle GetDSV_ReadOnly(UINT index = 0) const { ASSERT(mHasStencilView); return mDSV + (3 + index); }
    DescriptorHandle GetDepthSRV(UINT index = 0) const { return mDepthSRV + index; }
    DescriptorHandle GetStencilSRV(UINT index = 0) const { return mStencilSRV + index; }

    float GetClearDepth() const { return mClearDepth; }
    uint8_t GetClearStencil() const { return mClearStencil; }
protected:
    void CreateDerivedViews(DXGI_FORMAT format, uint32_t numMips);

    float mClearDepth;
    uint8_t mClearStencil;
    bool mHasStencilView;
    DescriptorHandle mDSV;
    DescriptorHandle mDepthSRV;
    DescriptorHandle mStencilSRV;
};


class ShadowBuffer : public DepthBuffer
{
public:
    ShadowBuffer() {}
    ~ShadowBuffer() {}

    virtual void Destroy() override;

    void Create(const std::wstring& name, uint32_t width, uint32_t height, uint32_t arraySize = 1, uint32_t numSamples = 1, DXGI_FORMAT dsvFormat = DSV_FORMAT);

    DescriptorHandle GetSRV(UINT arrIndex = 0) const { return GetDepthSRV(arrIndex); }

    void ResolveMsaa(ComputeCommandList& commandList, ColorBuffer& nonMsaaBuffer, uint32_t sampleCount);

    void BeginRendering(GraphicsCommandList& commandList);
    void EndRendering(GraphicsCommandList& commandList);

    const D3D12_VIEWPORT& GetViewPort() const { return mViewport; }
    const D3D12_RECT& GetScissor() const { return mScissor; }
private:
    D3D12_VIEWPORT mViewport;
    D3D12_RECT mScissor;

    DescriptorHandle mhandleForResolveMsaa;
};
