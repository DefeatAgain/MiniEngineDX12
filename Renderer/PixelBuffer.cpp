#include "PixelBuffer.h"
#include "Graphics.h"
#include "GraphicsResource.h"
#include "GpuBuffer.h"
#include "Utils/DebugUtils.h"
#include "Utils/ThreadPoolExecutor.h"

#include <fstream>

void PixelBuffer::ExportToFile(const std::wstring& filePath)
{
    uint64_t copySize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT placedFootprint;
    Graphics::gDevice->GetCopyableFootprints(make_rvalue_ptr(this->GetResource()->GetDesc()), 0, 1, 0,
        &placedFootprint, nullptr, nullptr, &copySize);

    std::shared_ptr<ReadbackBuffer> tempBuffer = std::make_shared<ReadbackBuffer>();
    tempBuffer->Create(L"Readback", (uint32_t)copySize, 1);

    PushGraphicsTaskSync(&PixelBuffer::ExportToFileTask, this, std::ref(*tempBuffer), std::ref(placedFootprint));

    Utility::gThreadPoolExecutor.Submit([=]() {
        // Retrieve a CPU-visible pointer to the buffer memory.  Map the whole range for reading.
        void* Memory = tempBuffer->Map();

        // Open the file and write the header followed by the texel data.
        std::ofstream OutFile(filePath, std::ios::out | std::ios::binary);
        OutFile.write((const char*)&mFormat, 4);
        OutFile.write((const char*)&placedFootprint.Footprint.RowPitch, 4);
        OutFile.write((const char*)&mWidth, 4);
        OutFile.write((const char*)&mHeight, 4);
        OutFile.write((const char*)Memory, tempBuffer->GetBufferSize());
        OutFile.close();

        // No values were written to the buffer, so use a null range when unmapping.
        tempBuffer->Unmap();
    });
}

CommandList* PixelBuffer::ExportToFileTask(CommandList* commandList, ReadbackBuffer& dstBuffer, D3D12_PLACED_SUBRESOURCE_FOOTPRINT& placedFootprint)
{
    CopyCommandList& copyList = commandList->GetCopyCommandList().Begin(L"Copy texture to memory");
    copyList.ReadbackTexture(dstBuffer, *this, placedFootprint);
    copyList.Finish();
    return commandList;
}

D3D12_RESOURCE_DESC PixelBuffer::DescribeTex2D(
    uint32_t width, uint32_t height, uint32_t depthOrArraySize, uint32_t numMips, DXGI_FORMAT format, UINT flags)
{
    mWidth = width;
    mHeight = height;
    mArraySize = depthOrArraySize;
    mFormat = format;

    D3D12_RESOURCE_DESC desc = {};
    desc.Alignment = 0;
    desc.DepthOrArraySize = (UINT16)depthOrArraySize;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Flags = (D3D12_RESOURCE_FLAGS)flags;
    desc.Format = GetBaseFormat(format);
    desc.Height = (UINT)height;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.MipLevels = (UINT16)numMips;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Width = (UINT64)width;
    return desc;
}

void PixelBuffer::AssociateWithResource(const std::wstring& name, ID3D12Resource* resource, D3D12_RESOURCE_STATES currentState)
{
    ASSERT(resource != nullptr);
    D3D12_RESOURCE_DESC ResourceDesc = resource->GetDesc();

    mResource.Attach(resource);
    mUsageState = currentState;

    mWidth = (uint32_t)ResourceDesc.Width;		// We don't care about large virtual textures yet
    mHeight = ResourceDesc.Height;
    mArraySize = ResourceDesc.DepthOrArraySize;
    mFormat = ResourceDesc.Format;

#ifndef RELEASE
    mResource->SetName(name.c_str());
#else
    (name);
#endif
}

void PixelBuffer::CreateTextureResource(const std::wstring& name, const D3D12_RESOURCE_DESC& resourceDesc, D3D12_CLEAR_VALUE clearValue)
{
    Destroy();

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    CheckHR(Graphics::gDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc, 
        D3D12_RESOURCE_STATE_COMMON, &clearValue, IID_PPV_ARGS(mResource.GetAddressOf())));

    mUsageState = D3D12_RESOURCE_STATE_COMMON;
    mGpuVirtualAddress = D3D12_VIRTUAL_ADDRESS_NULL;

#ifndef RELEASE
    mResource->SetName(name.c_str());
#else
    (name);
#endif
}

DXGI_FORMAT PixelBuffer::GetBaseFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_TYPELESS;

    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_TYPELESS;

    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8X8_TYPELESS;

        // 32-bit Z w/ Stencil
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
        return DXGI_FORMAT_R32G8X24_TYPELESS;

        // No Stencil
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
        return DXGI_FORMAT_R32_TYPELESS;

        // 24-bit Z
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
        return DXGI_FORMAT_R24G8_TYPELESS;

        // 16-bit Z w/o Stencil
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_UNORM:
        return DXGI_FORMAT_R16_TYPELESS;

    default:
        return format;
    }
}

DXGI_FORMAT PixelBuffer::GetUAVFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM;

    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM;

    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8X8_UNORM;

    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_R32_FLOAT:
        return DXGI_FORMAT_R32_FLOAT;

#ifdef _DEBUG
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
    case DXGI_FORMAT_D16_UNORM:

        ASSERT(false, "Requested a UAV Format for a depth stencil Format.");
#endif

    default:
        return format;
    }
}

DXGI_FORMAT PixelBuffer::GetDSVFormat(DXGI_FORMAT format)
{
    switch (format)
    {
        // 32-bit Z w/ Stencil
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
        return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;

        // No Stencil
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
        return DXGI_FORMAT_D32_FLOAT;

        // 24-bit Z
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
        return DXGI_FORMAT_D24_UNORM_S8_UINT;

        // 16-bit Z w/o Stencil
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_UNORM:
        return DXGI_FORMAT_D16_UNORM;

    default:
        return format;
    }
}

DXGI_FORMAT PixelBuffer::GetDepthFormat(DXGI_FORMAT format)
{
    switch (format)
    {
        // 32-bit Z w/ Stencil
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
        return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;

        // No Stencil
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
        return DXGI_FORMAT_R32_FLOAT;

        // 24-bit Z
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
        return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

        // 16-bit Z w/o Stencil
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_UNORM:
        return DXGI_FORMAT_R16_UNORM;

    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

DXGI_FORMAT PixelBuffer::GetStencilFormat(DXGI_FORMAT format)
{
    switch (format)
    {
        // 32-bit Z w/ Stencil
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
        return DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;

        // 24-bit Z
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
        return DXGI_FORMAT_X24_TYPELESS_G8_UINT;

    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

size_t PixelBuffer::BytesPerPixel(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
        return 16;

    case DXGI_FORMAT_R32G32B32_TYPELESS:
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
        return 12;

    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
        return 8;

    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R16G16_TYPELESS:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
    case DXGI_FORMAT_R8G8_B8G8_UNORM:
    case DXGI_FORMAT_G8R8_G8B8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return 4;

    case DXGI_FORMAT_R8G8_TYPELESS:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_B5G6R5_UNORM:
    case DXGI_FORMAT_B5G5R5A1_UNORM:
    case DXGI_FORMAT_A8P8:
    case DXGI_FORMAT_B4G4R4A4_UNORM:
        return 2;

    case DXGI_FORMAT_R8_TYPELESS:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_A8_UNORM:
    case DXGI_FORMAT_P8:
        return 1;

    default:
        return 0;
    }
}


// -- ColorBuffer --
void ColorBuffer::Destroy()
{
    PixelBuffer::Destroy();

    if (mSRVHandle)
        Graphics::DeAllocateDescriptor(mSRVHandle, 1);
    if (mRTVHandle)
        Graphics::DeAllocateDescriptor(mRTVHandle, 1);
    if (mUAVHandle)
        Graphics::DeAllocateDescriptor(mUAVHandle, 1);
}

void ColorBuffer::CreateFromSwapChain(const std::wstring& name, ID3D12Resource* baseResource)
{
    AssociateWithResource(name, baseResource, D3D12_RESOURCE_STATE_PRESENT);

    //m_UAVHandle[0] = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    //Graphics::g_Device->CreateUnorderedAccessView(m_pResource.Get(), nullptr, nullptr, m_UAVHandle[0]);

    mRTVHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    Graphics::gDevice->CreateRenderTargetView(mResource.Get(), nullptr, mRTVHandle);
}

void ColorBuffer::Create(const std::wstring& name, uint32_t width, uint32_t height, uint32_t numMips, DXGI_FORMAT format)
{
    numMips = (numMips == 0 ? ComputeNumMips(width, height) : numMips);
    D3D12_RESOURCE_FLAGS Flags = CombineResourceFlags();
    D3D12_RESOURCE_DESC resourceDesc = DescribeTex2D(width, height, 1, numMips, format, Flags);

    resourceDesc.SampleDesc.Count = mFragmentCount;
    resourceDesc.SampleDesc.Quality = 0;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = format;
    clearValue.Color[0] = mClearColor.R();
    clearValue.Color[1] = mClearColor.G();
    clearValue.Color[2] = mClearColor.B();
    clearValue.Color[3] = mClearColor.A();

    CreateTextureResource(name, resourceDesc, clearValue);
    CreateDerivedViews(format, 1, numMips);
}

void ColorBuffer::CreateArray(const std::wstring& name, uint32_t width, uint32_t height, uint32_t arrayCount, DXGI_FORMAT format)
{
    D3D12_RESOURCE_FLAGS flags = CombineResourceFlags();
    D3D12_RESOURCE_DESC resourceDesc = DescribeTex2D(width, height, arrayCount, 1, format, flags);

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = format;
    clearValue.Color[0] = mClearColor.R();
    clearValue.Color[1] = mClearColor.G();
    clearValue.Color[2] = mClearColor.B();
    clearValue.Color[3] = mClearColor.A();

    CreateTextureResource(name, resourceDesc, clearValue);
    CreateDerivedViews(format, arrayCount, 1);
}

void ColorBuffer::CreateDerivedViews(DXGI_FORMAT format, uint32_t arraySize, uint32_t numMips)
{
    ASSERT(arraySize == 1 || numMips == 1, "We don't support auto-mips on texture arrays");
    ASSERT(mFragmentCount == 1 || numMips == 1, "We don't support muti sample mips");

    mNumMipMaps =  numMips - 1;

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};

    rtvDesc.Format = format;
    srvDesc.Format = format;
    uavDesc.Format = GetUAVFormat(format);
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    if (arraySize > 1)
    {
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        rtvDesc.Texture2DArray.MipSlice = 0;
        rtvDesc.Texture2DArray.FirstArraySlice = 0;
        rtvDesc.Texture2DArray.ArraySize = arraySize;

        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srvDesc.Texture2DArray.MipLevels = numMips;
        srvDesc.Texture2DArray.MostDetailedMip = 0;
        srvDesc.Texture2DArray.FirstArraySlice = 0;
        srvDesc.Texture2DArray.ArraySize = arraySize;

        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uavDesc.Texture2DArray.ArraySize = arraySize;
        uavDesc.Texture2DArray.FirstArraySlice = 0;
        uavDesc.Texture2DArray.MipSlice = 0;
    }
    else if (mFragmentCount > 1)
    {
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
    }
    else
    {
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;

        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = numMips;
        srvDesc.Texture2D.MostDetailedMip = 0;

        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
    }

    if (!mSRVHandle)
    {
        mRTVHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        mSRVHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    ID3D12Resource* resource = mResource.Get();

    // Create the render target view
    Graphics::gDevice->CreateRenderTargetView(resource, &rtvDesc, mRTVHandle);

    // Create the shader resource view
    Graphics::gDevice->CreateShaderResourceView(resource, &srvDesc, mSRVHandle);

    if (!mUAVHandle)
        mUAVHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    Graphics::gDevice->CreateUnorderedAccessView(resource, nullptr, &uavDesc, mUAVHandle);
}

void ColorBuffer::GenerateMipMaps()
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = GetUAVFormat(mFormat);
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;

    DescriptorHandle uavHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, mNumMipMaps);
    // Create the UAVs for each mip level (RWTexture2D)
    for (uint32_t i = 0; i <= mNumMipMaps; ++i)
    {
        Graphics::gDevice->CreateUnorderedAccessView(mResource.Get(), nullptr, &uavDesc, uavHandle + i);
        uavDesc.Texture2D.MipSlice++;
    }

    PushGraphicsTaskSync(&ColorBuffer::GenerateMipMapsTask, this, std::cref(uavHandle));

    Graphics::DeAllocateDescriptor(uavHandle, mNumMipMaps);
}

CommandList* ColorBuffer::GenerateMipMapsTask(CommandList* commandList, const DescriptorHandle& uavHandle)
{
    if (mNumMipMaps == 0)
        return commandList;

    ComputeCommandList& computeCommandList = commandList->GetComputeCommandList().Begin();

    computeCommandList.SetRootSignature(*Graphics::gCommonRS);

    computeCommandList.TransitionResource(*this, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    computeCommandList.SetDescriptorTable(1, 0, mSRVHandle);

    for (uint32_t TopMip = 0; TopMip < mNumMipMaps; )
    {
        uint32_t SrcWidth = mWidth >> TopMip;
        uint32_t SrcHeight = mHeight >> TopMip;
        uint32_t DstWidth = SrcWidth >> 1;
        uint32_t DstHeight = SrcHeight >> 1;

        // Determine if the first downsample is more than 2:1.  This happens whenever
        // the source width or height is odd.
        uint32_t NonPowerOfTwo = (SrcWidth & 1) | (SrcHeight & 1) << 1;
        if (mFormat == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
            computeCommandList.SetPipelineState(*Graphics::gGenerateMipsSRGBPSO[NonPowerOfTwo]);
        else
            computeCommandList.SetPipelineState(*Graphics::gGenerateMipsLinearPSO[NonPowerOfTwo]);

        // We can downsample up to four times, but if the ratio between levels is not
        // exactly 2:1, we have to shift our blend weights, which gets complicated or
        // expensive.  Maybe we can update the code later to compute sample weights for
        // each successive downsample.  We use _BitScanForward to count number of zeros
        // in the low bits.  Zeros indicate we can divide by two without truncating.
        uint32_t AdditionalMips;
        _BitScanForward((unsigned long*)&AdditionalMips,
            (DstWidth == 1 ? DstHeight : DstWidth) | (DstHeight == 1 ? DstWidth : DstHeight));
        uint32_t NumMips = 1 + (AdditionalMips > 3 ? 3 : AdditionalMips);
        if (TopMip + NumMips > mNumMipMaps)
            NumMips = mNumMipMaps - TopMip;

        // These are clamped to 1 after computing additional mips because clamped
        // dimensions should not limit us from downsampling multiple times.  (E.g.
        // 16x1 -> 8x1 -> 4x1 -> 2x1 -> 1x1.)
        if (DstWidth == 0)
            DstWidth = 1;
        if (DstHeight == 0)
            DstHeight = 1;

        computeCommandList.SetConstants(0, TopMip, NumMips, 1.0f / DstWidth, 1.0f / DstHeight);
        computeCommandList.SetDescriptorTable(2, 0, uavHandle + TopMip + 1);
        computeCommandList.Dispatch2D(DstWidth, DstHeight);

        computeCommandList.InsertUAVBarrier(*this);

        TopMip += NumMips;
    }

    computeCommandList.TransitionResource(*this, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    computeCommandList.Finish();

    return commandList;
}


// -- DepthBuffer --
void DepthBuffer::Destroy()
{
    PixelBuffer::Destroy();

    Graphics::DeAllocateDescriptor(mDSV, mHasStencilView ? 4 : 2);
    Graphics::DeAllocateDescriptor(mDepthSRV, 1);
    Graphics::DeAllocateDescriptor(mStencilSRV, 1);
}

void DepthBuffer::Create(const std::wstring& name, uint32_t width, uint32_t height, uint32_t numMips, DXGI_FORMAT format)
{
    DepthBuffer::Create(name, width, height, 1, numMips, format);
}

void DepthBuffer::Create(const std::wstring& name, uint32_t width, uint32_t height, uint32_t numSamples, uint32_t numMips, DXGI_FORMAT format)
{
    D3D12_RESOURCE_DESC resourceDesc = DescribeTex2D(width, height, 1, numMips, format, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    resourceDesc.SampleDesc.Count = numSamples;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = format;
    CreateTextureResource(name, resourceDesc, clearValue);
    CreateDerivedViews(format, numMips);
}

void DepthBuffer::CreateDerivedViews(DXGI_FORMAT format, uint32_t numMips)
{
    ID3D12Resource* resource = mResource.Get();

    mNumMipMaps = numMips - 1;

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
    dsvDesc.Format = GetDSVFormat(format);
    if (resource->GetDesc().SampleDesc.Count == 1)
    {
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;
    }
    else
    {
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
    }

    DXGI_FORMAT stencilReadFormat = GetStencilFormat(format);
    mHasStencilView = stencilReadFormat != DXGI_FORMAT_UNKNOWN;

    if (!mDSV)
        mDSV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, mHasStencilView ? 4 : 2);

    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    Graphics::gDevice->CreateDepthStencilView(resource, &dsvDesc, mDSV);

    dsvDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
    Graphics::gDevice->CreateDepthStencilView(resource, &dsvDesc, mDSV + 1);

    if (stencilReadFormat != DXGI_FORMAT_UNKNOWN)
    {
        dsvDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_STENCIL;
        Graphics::gDevice->CreateDepthStencilView(resource, &dsvDesc, mDSV + 2);

        dsvDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL;
        Graphics::gDevice->CreateDepthStencilView(resource, &dsvDesc, mDSV + 3);
    }
    //else
    //{
    //    mDSV[2] = mDSV[0];
    //    mDSV[3] = mDSV[1];
    //}

    if (mDepthSRV)
        mDepthSRV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Create the shader resource view
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = GetDepthFormat(format);
    if (dsvDesc.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2D)
    {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = numMips;
    }
    else
    {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
    }
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    Graphics::gDevice->CreateShaderResourceView(resource, &srvDesc, mDepthSRV);

    if (mHasStencilView)
    {
        if (!mStencilSRV)
            mStencilSRV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        srvDesc.Format = stencilReadFormat;
        Graphics::gDevice->CreateShaderResourceView(resource, &srvDesc, mStencilSRV);
    }
}


// -- ShadowBuffer --
void ShadowBuffer::Create(const std::wstring& name, uint32_t width, uint32_t height)
{
    DepthBuffer::Create(name, width, height, 1, DXGI_FORMAT_D16_UNORM);

    mViewport.TopLeftX = 0.0f;
    mViewport.TopLeftY = 0.0f;
    mViewport.Width = (float)width;
    mViewport.Height = (float)height;
    mViewport.MinDepth = 0.0f;
    mViewport.MaxDepth = 1.0f;

    // Prevent drawing to the boundary pixels so that we don't have to worry about shadows stretching
    mScissor.left = 1;
    mScissor.top = 1;
    mScissor.right = (LONG)width - 2;
    mScissor.bottom = (LONG)height - 2;
}

void ShadowBuffer::BeginRendering(GraphicsCommandList& commandList)
{
    commandList.TransitionResource(*this, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
    commandList.ClearDepth(*this);
    commandList.SetDepthStencilTarget(GetDSV());
    commandList.SetViewportAndScissor(mViewport, mScissor);
}

void ShadowBuffer::EndRendering(GraphicsCommandList& commandList)
{
    commandList.TransitionResource(*this, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}
