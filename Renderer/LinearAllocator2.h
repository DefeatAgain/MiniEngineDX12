#pragma once
#include "GpuResource.h"

class LinearAllocationPage;

enum LinearAllocatorType
{
    kGpuExclusive = 0,		// DEFAULT   GPU-writeable (via UAV)
    kCpuWritable = 1,		// UPLOAD CPU-writeable (but write combined)
    kCpuReadBack = 2,       // READBACK CPU-readback
    kNumAllocatorTypes
};

enum
{
    kGpuAllocatorPageSize = 0x10000,	// 64K
    kCpuAllocatorPageSize = 0x200000	// 2MB
};


class AllocSpan
{
    friend class LinearAllocator;
    friend class CopyCommandList;
    friend class GraphicsCommandList;
    friend class ComputeCommandList;
private:
    AllocSpan(LinearAllocationPage& page,
        void* ptr,
        size_t size,
        D3D12_GPU_VIRTUAL_ADDRESS gpuAddress)
        : mPage(page), mCpuAddress(ptr), mSize(size), mOffset(0), mGpuAddress(gpuAddress)
    {}

    AllocSpan(LinearAllocationPage& page)
        : AllocSpan(page, nullptr, 0, D3D12_VIRTUAL_ADDRESS_NULL)
    {}
private:
    void* mCpuAddress;			             // The CPU-writeable address
    D3D12_GPU_VIRTUAL_ADDRESS mGpuAddress;	// The GPU-visible address
    size_t mSize;
    size_t mOffset;
    LinearAllocationPage& mPage;
};

class LinearAllocationPage : public GpuResource
{
    friend class LinearAllocator;
    friend struct std::less<LinearAllocationPage>;
public:
    LinearAllocationPage(ID3D12Resource* pResource, D3D12_RESOURCE_STATES usage) :
        GpuResource(), mOffset(0)
    {
        mResource.Attach(pResource);
        mUsageState = usage;
        mGpuVirtualAddress = mResource->GetGPUVirtualAddress();
        mResource->Map(0, nullptr, &mStart);
    }

    ~LinearAllocationPage() {}
private:
    size_t mOffset;
    void* mStart;
};


class LinearAllocator
{
public:
    LinearAllocator(LinearAllocatorType type) : mType(type), mCurrentPage(nullptr)
    {
        switch (mType)
        {
        case kGpuExclusive:
            mPerPageSize = kGpuAllocatorPageSize;
            break;
        case kCpuWritable:
        case kCpuReadBack:
        default:
            mPerPageSize = kCpuAllocatorPageSize;
            break;
        }
    }

    ~LinearAllocator() { Clear(); }

    AllocSpan Allocate(size_t size, size_t alignment = 2);

    void CleanupUsedPages();

    void Clear();
private:
    LinearAllocationPage& AllocNewPage(size_t size = 0);
    AllocSpan AllocateLargePage(size_t size);
private:
    LinearAllocatorType mType;
    LinearAllocationPage* mCurrentPage;
    size_t mPerPageSize;
    std::list<LinearAllocationPage> mPages;
    std::list<LinearAllocationPage> mUnusedPages;
    std::list<LinearAllocationPage> mLargePages;
};
