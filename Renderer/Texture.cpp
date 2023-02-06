#include "Texture.h"
#include "Graphics.h"
#include "GraphicsResource.h"
#include "CommandList.h"
#include "Utils/DebugUtils.h"
#include "Utils/FileUtility.h"
#include "Utils/ThreadPoolExecutor.h"
#include "Utils/DDSTextureLoader12.h"
#include "Utils/DirectXTex/DirectXTex.h"

#include <chrono>

static UINT BytesPerPixel(_In_ DXGI_FORMAT Format)
{
    return (UINT)BitsPerPixel(Format) / 8;
};


void Texture::Create2D(size_t rowPitchBytes, size_t width, size_t height, DXGI_FORMAT format, const void* initData)
{
    Reset();

    mUsageState = D3D12_RESOURCE_STATE_COPY_DEST;

    mWidth = (uint32_t)width;
    mHeight = (uint32_t)height;
    mDepth = 1;

    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = (UINT)height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = format;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    CheckHR(Graphics::gDevice->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, mUsageState, nullptr, IID_PPV_ARGS(mResource.GetAddressOf())));

    mResource->SetName((mName + L" Texture").c_str());

    D3D12_SUBRESOURCE_DATA texResource;
    texResource.pData = initData;
    texResource.RowPitch = rowPitchBytes;
    texResource.SlicePitch = rowPitchBytes * height;

    if (!mDescriptorHandle)
        mDescriptorHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    Graphics::gDevice->CreateShaderResourceView(mResource.Get(), nullptr, mDescriptorHandle);

    PushGraphicsTaskSync(&Texture::InitTextureTask, this, 1, &texResource);
    //if (waitFinished)
    //    PushGraphicsTaskSync(&Texture::InitTextureTask, this, 1, &texResource);
    //else
    //    PushGraphicsTaskAsync(&Texture::InitTextureTask1, this, 1, &texResource, std::shared_ptr<const void>(initData));
}

void Texture::CreateCube(size_t rowPitchBytes, size_t width, size_t height, DXGI_FORMAT format, const void* initialData)
{
    Reset();

    mUsageState = D3D12_RESOURCE_STATE_COPY_DEST;

    mWidth = (uint32_t)width;
    mHeight = (uint32_t)height;
    mDepth = 6;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = (UINT)height;
    texDesc.DepthOrArraySize = 6;
    texDesc.MipLevels = 1;
    texDesc.Format = format;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProps;
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 0;
    heapProps.VisibleNodeMask = 0;

    CheckHR(Graphics::gDevice->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, mUsageState, nullptr, IID_PPV_ARGS(mResource.GetAddressOf())));

    mResource->SetName((mName + L" CubeTexture").c_str());

    D3D12_SUBRESOURCE_DATA texResource;
    texResource.pData = initialData;
    texResource.RowPitch = rowPitchBytes;
    texResource.SlicePitch = rowPitchBytes * height;

    //if (waitFinished)
    //    PushGraphicsTaskSync(&Texture::InitTextureTask, this, 1, &texResource);
    //else
    //    PushGraphicsTaskAsync(&Texture::InitTextureTask1, this, 1, &texResource, std::shared_ptr<const void>(initialData));

    if (!mDescriptorHandle)
        mDescriptorHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
    srvDesc.Format = format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MipLevels = 1;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
    Graphics::gDevice->CreateShaderResourceView(mResource.Get(), &srvDesc, mDescriptorHandle);

    PushGraphicsTaskSync(&Texture::InitTextureTask, this, 1, &texResource);
}

//void Texture::CreateTGAFromMemory(const void* memBuffer, size_t fileSize)
//{
//    const uint8_t* filePtr = (const uint8_t*)memBuffer;
//
//    // Skip first two bytes
//    filePtr += 2;
//
//    /*uint8_t imageTypeCode =*/ *filePtr++;
//
//    // Ignore another 9 bytes
//    filePtr += 9;
//
//    uint16_t imageWidth = *(uint16_t*)filePtr;
//    filePtr += sizeof(uint16_t);
//    uint16_t imageHeight = *(uint16_t*)filePtr;
//    filePtr += sizeof(uint16_t);
//    uint8_t bitCount = *filePtr++;
//
//    // Ignore another byte
//    filePtr++;
//
//    uint32_t* formattedData = new uint32_t[imageWidth * imageHeight];
//    uint32_t* iter = formattedData;
//
//    uint8_t numChannels = bitCount / 8;
//    uint32_t numBytes = imageWidth * imageHeight * numChannels;
//
//    switch (numChannels)
//    {
//    default:
//        break;
//    case 3:
//        for (uint32_t byteIdx = 0; byteIdx < numBytes; byteIdx += 3)
//        {
//            *iter++ = 0xff000000 | filePtr[0] << 16 | filePtr[1] << 8 | filePtr[2];
//            filePtr += 3;
//        }
//        break;
//    case 4:
//        for (uint32_t byteIdx = 0; byteIdx < numBytes; byteIdx += 4)
//        {
//            *iter++ = filePtr[3] << 24 | filePtr[0] << 16 | filePtr[1] << 8 | filePtr[2];
//            filePtr += 4;
//        }
//        break;
//    }
//
//    Create2D(4 * imageWidth, imageWidth, imageHeight, DXGI_FORMAT_R8G8B8A8_UNORM, formattedData, true);
//
//    delete[] formattedData;
//}

//bool Texture::CreateDDSFromMemory(const void* memBuffer, size_t fileSize)
//{
//    if (!mDescriptorHandle)
//        mDescriptorHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
//
//    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
//    HRESULT hr = DirectX::LoadDDSTextureFromMemory(
//        Graphics::gDevice.Get(), (const uint8_t*)memBuffer, fileSize, mResource.GetAddressOf(), subresources);
//    
//    PushGraphicsTaskAsync(&Texture::InitTextureTask, this, subresources.size(), subresources.data());
//
//    return SUCCEEDED(hr);
//}

void Texture::CreatePIXImageFromMemory(const void* memBuffer, size_t fileSize)
{
    struct Header
    {
        DXGI_FORMAT Format;
        uint32_t Pitch;
        uint32_t Width;
        uint32_t Height;
    };
    const Header& header = *(Header*)memBuffer;

    ASSERT(fileSize >= header.Pitch * BytesPerPixel(header.Format) * header.Height + sizeof(Header),
        "Raw PIX image dump has an invalid file size");

    Create2D(header.Pitch, header.Width, header.Height, header.Format, (uint8_t*)memBuffer + sizeof(Header));
}

bool Texture::CreateFromDirectXTex(std::filesystem::path filepath, uint16_t flags)
{
    if (filepath.extension() != L".dds")
    {
        ASSERT(ConvertToDDS(filepath, flags));
    }

    std::shared_ptr<std::vector<D3D12_SUBRESOURCE_DATA>> subresources = std::make_shared<std::vector<D3D12_SUBRESOURCE_DATA>>();
    std::shared_ptr<std::vector<uint8_t>> ddsData;
    bool isCubeMap;
    HRESULT hr = DirectX::LoadDDSTextureFromFile(
        Graphics::gDevice.Get(), filepath.replace_extension(L".dds").c_str(), mResource.GetAddressOf(), *ddsData, *subresources,
        0, nullptr, &isCubeMap);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
    srvDesc.Format = mResource->GetDesc().Format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (isCubeMap)
    {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MipLevels = -1;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
    }
    else
    {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = -1;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.PlaneSlice = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    }

    {
        static std::mutex sMutex;
        std::lock_guard<std::mutex> lockGuard(sMutex);

        if (!mDescriptorHandle)
            mDescriptorHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        Graphics::gDevice->CreateShaderResourceView(mResource.Get(), &srvDesc, mDescriptorHandle);

        PushGraphicsTaskAsync(&Texture::InitTextureTask1, this, subresources, std::move(ddsData));
    }

    return SUCCEEDED(hr);
}

void Texture::Destroy()
{
    Reset();

    Graphics::DeAllocateDescriptor(mDescriptorHandle, 1);
}

void Texture::Reset()
{
    WaitAsyncFence();

    GpuResource::Destroy();

    *mVersionId = -1;
}

bool Texture::ConvertToDDS(std::filesystem::path filepath, uint16_t flags)
{
    using namespace DirectX;

#define GetFlag(f) ((flags & f) != 0)
    bool bInterpretAsSRGB = GetFlag(kSRGB);
    bool bPreserveAlpha = GetFlag(kPreserveAlpha);
    bool bContainsNormals = GetFlag(kNormalMap);
    bool bBumpMap = GetFlag(kBumpToNormal);
    bool bBlockCompress = GetFlag(kDefaultBC);
    bool bUseBestBC = GetFlag(kQualityBC);
    bool bFlipImage = GetFlag(kFlipVertical);
#undef GetFlag

    ASSERT(!bInterpretAsSRGB || !bContainsNormals);
    ASSERT(!bPreserveAlpha || !bContainsNormals);

    std::filesystem::path ext = filepath.extension();
    TexMetadata info;
    std::unique_ptr<ScratchImage> image = std::make_unique<ScratchImage>();

    bool isHDR = false;
    if (ext == L"tga")
    {
        HRESULT hr = LoadFromTGAFile(filepath.c_str(), &info, *image);
        if (FAILED(hr))
        {
            Utility::PrintMessage("Could not load texture \"%ws\" (TGA: %08X).\n", filepath.generic_string().c_str(), hr);
            return false;
        }
}
    else if (ext == L"hdr")
    {
        isHDR = true;
        HRESULT hr = LoadFromHDRFile(filepath.c_str(), &info, *image);
        if (FAILED(hr))
        {
            Utility::PrintMessage("Could not load texture \"%ws\" (HDR: %08X).\n", filepath.generic_string().c_str(), hr);
            return false;
        }
}
    else
    {
        HRESULT hr = LoadFromWICFile(filepath.c_str(), WIC_FLAGS_NONE, &info, *image);
        if (FAILED(hr))
        {
            Utility::PrintMessage("Could not load texture \"%ws\" (WIC: %08X).\n", filepath.generic_string().c_str(), hr);
            return false;
        }
    }

    if (info.width > 16384 || info.height > 16384)
    {
        Utility::PrintMessage("Texture size (%Iu,%Iu) too large for feature level 11.0 or later (16384) \"%ws\".\n",
            info.width, info.height, filepath.c_str());
        return false;
    }

    if (bFlipImage)
    {
        std::unique_ptr<ScratchImage> newImage = std::make_unique<ScratchImage>();

        HRESULT hr = FlipRotate(image->GetImages(), image->GetImageCount(), info, TEX_FR_FLIP_VERTICAL, *newImage);

        if (FAILED(hr))
        {
            Utility::PrintMessage("Could not flip image \"%ws\" (%08X).\n", filepath.generic_string().c_str(), hr);
        }
        else
        {
            image.swap(newImage);
        }
    }

    DXGI_FORMAT d3dFormat;
    DXGI_FORMAT compressedFormat;
    if (isHDR)
    {
        d3dFormat = DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
        compressedFormat = bBlockCompress ? DXGI_FORMAT_BC6H_UF16 : DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
    }
    else if (bBlockCompress)
    {
        d3dFormat = bInterpretAsSRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        if (bContainsNormals)
            compressedFormat = DXGI_FORMAT_BC5_UNORM;
        else if (bUseBestBC)
            compressedFormat = bInterpretAsSRGB ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
        else if (bPreserveAlpha)
            compressedFormat = bInterpretAsSRGB ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM;
        else
            compressedFormat = bInterpretAsSRGB ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM;
    }
    else
    {
        compressedFormat = d3dFormat = bInterpretAsSRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    if (info.format != d3dFormat)
    {
        std::unique_ptr<ScratchImage> newImage = std::make_unique<ScratchImage>();

        HRESULT hr = Convert(image->GetImages(), image->GetImageCount(), image->GetMetadata(), 
            d3dFormat, TEX_FILTER_DEFAULT, 0.5f, *newImage);

        if (FAILED(hr))
        {
            Utility::PrintMessage("Could not convert \"%ws\" (%08X).\n", filepath.generic_string().c_str(), hr);
        }
        else
        {
            image.swap(newImage);
            info.format = d3dFormat;
        }
    }

    if (info.mipLevels == 1)
    {
        std::unique_ptr<ScratchImage> newImage = std::make_unique<ScratchImage>();

        HRESULT hr = GenerateMipMaps(image->GetImages(), image->GetImageCount(), image->GetMetadata(), TEX_FILTER_DEFAULT, 0, *newImage);

        if (FAILED(hr))
        {
            Utility::PrintMessage("Failing generating mimaps for \"%ws\" (WIC: %08X).\n", filepath.generic_string().c_str(), hr);
        }
        else
        {
            image.swap(newImage);
        }
    }

    if (bBlockCompress)
    {
        if (info.width % 4 || info.height % 4)
        {
            Utility::PrintMessage("Texture size (%Iux%Iu) not a multiple of 4 \"%ws\", so skipping compress\n", 
                info.width, info.height, filepath.generic_string().c_str());
        }
        else
        {
            std::unique_ptr<ScratchImage> newImage = std::make_unique<ScratchImage>();

            HRESULT hr = Compress(image->GetImages(), image->GetImageCount(), image->GetMetadata(), 
                compressedFormat, TEX_COMPRESS_DEFAULT, 0.5f, *newImage);
            if (FAILED(hr))
            {
                Utility::PrintMessage("Failing compressing \"%ws\" (WIC: %08X).\n", filepath.generic_string().c_str(), hr);
            }
            else
            {
                image.swap(newImage);
            }
        }
    }

    std::filesystem::path newPath = filepath.replace_extension(L".dds");
    HRESULT hr = SaveToDDSFile(image->GetImages(), image->GetImageCount(), image->GetMetadata(), DDS_FLAGS_NONE, newPath.c_str());
    if (FAILED(hr))
    {
        Utility::PrintMessage("Could not write texture to file \"%ws\" (%08X).\n", newPath.generic_string().c_str(), hr);
        return false;
    }

    return true;
}

CommandList* Texture::InitTextureTask(CommandList* commandList, UINT numSubresources, D3D12_SUBRESOURCE_DATA subData[])
{
    CopyCommandList& copyList = commandList->GetCopyCommandList().Begin(L"Texture Copy" + mName);
    copyList.InitializeTexture(*this, 1, subData);
    copyList.Finish();
    return commandList;
}

CommandList* Texture::InitTextureTask1(CommandList* commandList, std::shared_ptr<std::vector<D3D12_SUBRESOURCE_DATA>> subData,
    std::shared_ptr<std::vector<uint8_t>> initData)
{
    (initData); // just for life cycle
    InitTextureTask(commandList, subData->size(), subData->data());
    return commandList;
}


// -- TextureRef --
DescriptorHandle TextureRef::GetSRV() const
{
    if (IsValid())
        return mRef->GetSRV();
    return Graphics::GetDefaultTexture(Graphics::kWhiteOpaque2D).GetSRV();
}

const Texture* TextureRef::Get() const
{
    if (IsValid())
        return mRef;
    return &Graphics::GetDefaultTexture(Graphics::kWhiteOpaque2D);
}


// -- TextureManager --
static std::queue<std::future<bool>> sPrepareList;

TextureRef TextureManager::GetTexture(const std::filesystem::path& filename, uint16_t flags, Graphics::eDefaultTexture fallback)
{
    ASSERT(!std::filesystem::is_directory(filename));

    auto iter = mTextures.find(filename);
    if (iter != mTextures.end())
        return TextureRef(&iter->second);

    std::filesystem::path realPath(mRootPath / filename);

    ASSERT(std::filesystem::exists(realPath));
        //return TextureRef(Graphics::GetDefaultTexture(fallback));

    const auto& insertIter = mTextures.emplace(filename, filename.stem());
    Texture& newTexture = insertIter.first->second;

    sPrepareList.emplace(Utility::gThreadPoolExecutor.Submit(&Texture::CreateFromDirectXTex, &newTexture, realPath, flags));
    return TextureRef(&newTexture);
}
