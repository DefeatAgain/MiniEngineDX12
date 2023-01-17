#pragma once
#include "GpuResource.h"

class AllocSpan
{
    friend class LinearAllocationPage;
    friend class LinearAllocator;
private:
    AllocSpan(LinearAllocationPage& page,
        void* ptr,
        size_t offset,
        size_t size,
        D3D12_GPU_VIRTUAL_ADDRESS gpuAddress)
        : mPage(page), mCpuAddress(ptr), mNext(nullptr), mOffset(offset), mSize(size), mGpuAddress(gpuAddress)
    {}

    AllocSpan(LinearAllocationPage& page)
        : AllocSpan(page, nullptr, 0, 0, D3D12_GPU_VIRTUAL_ADDRESS_NULL)
    {}
public:
    void* mCpuAddress;			             // The CPU-writeable address
    size_t mSize;
    D3D12_GPU_VIRTUAL_ADDRESS mGpuAddress;	// The GPU-visible address
private:
    size_t mOffset;			                // Offset from start of buffer resource
    AllocSpan* mNext;
    LinearAllocationPage& mPage;
};

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


class LinearAllocationPage : public GpuResource
{
    friend class LinearAllocator;
public:
    LinearAllocationPage(ID3D12Resource* pResource, D3D12_RESOURCE_STATES usage, size_t pageSize) : 
        GpuResource(), mSpan(*this)
    {
        mResource.Attach(pResource);
        mUsageState = usage;
        mGpuVirtualAddress = mSpan.mGpuAddress = mResource->GetGPUVirtualAddress();
        mResource->Map(0, nullptr, &mSpan.mCpuAddress);
    }

    ~LinearAllocationPage()
    {
        DeleteSpan(mSpan.mNext);
        mSpan.mNext = nullptr;
    }
private:
    AllocSpan AllocateSpan(size_t size);

    void DiscardSpan(const AllocSpan& span);

    void DeleteSpan(AllocSpan* span);
private:
    AllocSpan mSpan;
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

    ~LinearAllocator()  { Clear(); }

    AllocSpan Allocate(size_t size);

    void Deallocate(const AllocSpan& span);

    void Clear();
private:
    LinearAllocationPage* AllocNewPage(size_t size, bool isLargePage);
private:
    LinearAllocatorType mType;
    LinearAllocationPage* mCurrentPage;
    size_t mPerPageSize;
    std::mutex mAllocLock;
    std::list<LinearAllocationPage> mPages;
};
