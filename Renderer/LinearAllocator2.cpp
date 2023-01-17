#include "LinearAllocator2.h"
#include "Graphics.h"
#include "Utils/DebugUtils.h"
#include "Math/Common.h"

AllocSpan LinearAllocator::Allocate(size_t size, size_t alignment)
{
    // Align the allocation
    alignment = std::max<size_t>(alignment, 2);
    const size_t alignedSize = Math::AlignUp(size, alignment);

    if (alignedSize > mPerPageSize)
        return AllocateLargePage(alignedSize);

    if (!mCurrentPage || mPerPageSize - mCurrentPage->mOffset < alignedSize)
    {
        mCurrentPage = &AllocNewPage();
    }

    AllocSpan ret(*mCurrentPage);
    ret.mCpuAddress = (uint8_t*)mCurrentPage->mStart + mCurrentPage->mOffset;
    ret.mGpuAddress = mCurrentPage->mGpuVirtualAddress + mCurrentPage->mOffset;
    ret.mOffset = mCurrentPage->mOffset;
    ret.mSize = alignedSize;

    mCurrentPage->mOffset += alignedSize;

    return ret;
}

void LinearAllocator::CleanupUsedPages()
{
    mCurrentPage = nullptr;
    mLargePages.clear();

    for (auto& page : mPages)
    {
        page.mOffset = 0;
    }

    if (mUnusedPages.empty())
        mPages.swap(mUnusedPages);
    else
        mUnusedPages.splice(mUnusedPages.cend(), mPages);
    while (mUnusedPages.size() > MAX_ALLOCATOR_PAGES)
    {
        mUnusedPages.pop_back();
        //mUnusedPages.resize(MAX_ALLOCATOR_PAGES);
    }
}

void LinearAllocator::Clear()
{
    mPages.clear();
    mUnusedPages.clear();
    mLargePages.clear();
    mCurrentPage = nullptr;
}

LinearAllocationPage& LinearAllocator::AllocNewPage(size_t size)
{
    if (size == 0 && !mUnusedPages.empty())
    {
        mPages.splice(mPages.end(), mUnusedPages, mUnusedPages.begin());
        return mPages.back();
    }

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC resourceDesc;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = 0;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_RESOURCE_STATES defaultUsage;

    if (mType == kGpuExclusive)
    {
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        resourceDesc.Width = size == 0 ? kGpuAllocatorPageSize : size;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        defaultUsage = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    else
    {
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        resourceDesc.Width = size == 0 ? kCpuAllocatorPageSize : size;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        defaultUsage = D3D12_RESOURCE_STATE_GENERIC_READ;
    }

    ID3D12Resource* pBuffer;
    CheckHR(Graphics::gDevice->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc, defaultUsage, nullptr, IID_PPV_ARGS(&pBuffer)));
    pBuffer->SetName(L"LinearAllocator Page");

    return size == 0 ? mPages.emplace_back(pBuffer, defaultUsage) : mLargePages.emplace_back(pBuffer, defaultUsage);
}

AllocSpan LinearAllocator::AllocateLargePage(size_t size)
{
    LinearAllocationPage& newPage = AllocNewPage(size);
    return AllocSpan(newPage, newPage.mStart, size, newPage.mGpuVirtualAddress);
}
