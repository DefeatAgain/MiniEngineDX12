#include "LinearAllocator.h"
#include "Math/Common.h"
#include "Utils/DebugUtils.h"
#include "D3DCore.h"


AllocSpan LinearAllocator::Allocate(size_t size)
{
    size = Math::AlignUp(size, 2);

    std::unique_lock<std::mutex> lock1(mAllocLock, std::defer_lock);

    LinearAllocationPage* page = mCurrentPage;
    if (size > mPerPageSize)
    {
        page = AllocNewPage(size, true); // thread safe provide by graphics api
    }
    else if (!mCurrentPage || size > mCurrentPage->mSpan.mSize)
    {
        while (!lock1.try_lock())
            std::this_thread::yield();

        page = mCurrentPage = AllocNewPage(size, false);
    }

    return page->AllocateSpan(size);
}

void LinearAllocator::Deallocate(const AllocSpan& span)
{
    LinearAllocationPage& page = span.mPage;
    if (span.mSize > mPerPageSize)
    {
        delete &page;
        return;
    }

    std::unique_lock<std::mutex> lock1(mAllocLock, std::defer_lock);
    while (!lock1.try_lock())
        std::this_thread::yield();

    page.DiscardSpan(span);

    if (mPages.size() > MAX_DESCRIPTOR_ALLOC_CACHE_SIZE)
    {
        mPages.remove_if([&](auto& _page) { return _page.mSpan.mSize == mPerPageSize; }); // remove all free pages

        mPages.sort([](auto& _page0, auto& _page1) { return _page0.mSpan.mSize < _page1.mSpan.mSize; }); // sort pages by remain size
        mCurrentPage = &mPages.back();
    }
}

void LinearAllocator::Clear()
{
    std::unique_lock<std::mutex> lock1(mAllocLock, std::defer_lock);
    ASSERT(lock1.try_lock(), "Lock is owning by other thread!");

    mPages.clear();
}

LinearAllocationPage* LinearAllocator::AllocNewPage(size_t size, bool isLargePage)
{
    D3D12_HEAP_PROPERTIES heapProps;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 0;
    heapProps.VisibleNodeMask = 0;

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

    D3D12_RESOURCE_STATES DefaultUsage;

    switch (mType)
    {
    case kGpuExclusive:
    {
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        resourceDesc.Width = size;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        DefaultUsage = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        break;
    }
    case kCpuWritable:
    {
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        resourceDesc.Width = size;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        DefaultUsage = D3D12_RESOURCE_STATE_GENERIC_READ;
        break;
    }
    case kCpuReadBack:
    {
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;
        resourceDesc.Width = size;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        DefaultUsage = D3D12_RESOURCE_STATE_COPY_DEST;
        break;
    }
    default:
        ASSERT(false, "Unexcepted branch!");
        break;
    }
    
    ID3D12Resource* pBuffer;
    CheckHR(D3DCore::gDevice->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc, DefaultUsage, nullptr, IID_PPV_ARGS(&pBuffer)));

    if (isLargePage)
    {
        return new LinearAllocationPage(pBuffer, DefaultUsage, size);
    }
    return &mPages.emplace_back(pBuffer, DefaultUsage, size);
}

AllocSpan LinearAllocationPage::AllocateSpan(size_t size)
{
    size_t offset = mSpan.mSize - size;
    mSpan.mSize -= size;
    return AllocSpan(*this, (uint8_t*)mSpan.mCpuAddress + offset, offset, size, mGpuVirtualAddress + offset);
}

void LinearAllocationPage::DiscardSpan(const AllocSpan& span)
{
    AllocSpan* curSpan = &mSpan;
    bool hasMerged = false;

    // find the Span start offset
    while (curSpan)
    {
        if (!curSpan->mNext) // list rear
        {
            if (curSpan->mOffset + curSpan->mSize == span.mOffset) // merge front
            {
                curSpan->mSize += span.mSize;
                hasMerged = true;
            }
            break;
        }
        else if (curSpan->mNext->mOffset > span.mOffset)
        {
            size_t curSpanEnd = curSpan->mOffset + curSpan->mSize;
            size_t removeSpanEnd = span.mOffset + span.mSize;
            if (curSpanEnd == span.mOffset && removeSpanEnd == curSpan->mNext->mOffset) // merge front and back
            {
                curSpan->mSize += span.mSize + curSpan->mNext->mSize;
                AllocSpan* nextSpan = curSpan->mNext;
                curSpan->mNext = curSpan->mNext->mNext;
                delete nextSpan; // remove back span
                hasMerged = true;
            }
            else if (curSpanEnd == span.mOffset) // merge front
            {
                curSpan->mSize += span.mSize;
                hasMerged = true;
            }
            else if (removeSpanEnd == curSpan->mNext->mOffset) // merge back
            {
                curSpan->mNext->mOffset = span.mOffset;
                curSpan->mNext->mSize += span.mSize;
                hasMerged = true;
            }
            break;
        }
        curSpan = curSpan->mNext;
    }

    if (!hasMerged)
    {
        ASSERT(curSpan);
        AllocSpan* newSpan = new AllocSpan(span);
        newSpan->mNext = curSpan->mNext;
        curSpan->mNext = newSpan;
    }
}

void LinearAllocationPage::DeleteSpan(AllocSpan* span)
{
    if (span)
    {
        DeleteSpan(span->mNext);
        delete span;
    }
}
