#pragma once
#include "CoreHeader.h"
#include "Common.h"

class DescriptorHandle;

class DescriptorAllocator
{
    friend class DescriptorHandle;

    using BITMAP_TYPE = uint32_t;

    struct SubHeap
    {
        DescriptorAllocator& mOwningAllocator;
        size_t mNumRemainHandles;
        size_t mNumReleasedHandles;
        D3D12_CPU_DESCRIPTOR_HANDLE mCpuStartHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE mGpuStartHandle;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDescriptorHeap;
        std::vector<BITMAP_TYPE> mBitMap;
        //std::vector<size_t> mPerLayerRemainHandles; // simple handle all layer has same remain handles

        SubHeap(DescriptorAllocator& owningAlloc) : mOwningAllocator(owningAlloc),
            mNumRemainHandles(MAX_DESCRIPTOR_HEAP_SIZE), mNumReleasedHandles(0)
        {}
        ~SubHeap() {}
    };
public:
    DescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE heapType);

    ~DescriptorAllocator() { Clear(); }

    void Clear();

    D3D12_CPU_DESCRIPTOR_HANDLE GetHeapCpuStart(uint8_t index) const { return mDescriptorHeapPool[index]->mCpuStartHandle; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetHeapGpuStart(uint8_t index) const { return mDescriptorHeapPool[index]->mGpuStartHandle; }
    ID3D12DescriptorHeap* GetHeap(uint8_t index) { return mDescriptorHeapPool[index]->mDescriptorHeap.Get(); }
    size_t GetDescribeSize() const { return mDescriptorSize; }

    // Deallocate count must equal to allocation count
    void Deallocate(DescriptorHandle& handle, uint32_t count);

    DescriptorHandle Allocate(uint32_t count);
private:
    SubHeap* GetSubHeap(uint8_t index) const { return mDescriptorHeapPool[index]; }

    DescriptorHandle AllocateLayer(uint32_t startLayerNodeIdx, uint32_t totalNodeSize, size_t alignedCount);
    
    uint8_t AllocNewHeap();

    void TreeShiftSetBit(std::vector<uint32_t>& bitMap, int startIdx, size_t startBitIdx);

    void TreeShiftResetBit(std::vector<uint32_t>& bitMap, int startIdx, size_t startBitIdx);
private:
    D3D12_DESCRIPTOR_HEAP_TYPE mType;
    UINT mDescriptorSize;
    uint8_t mCurrentHeapIndex;
    std::mutex mAllocatorMutex;
    std::vector<SubHeap*> mDescriptorHeapPool;
};

#define UNKNOWN_OFFSET (((uint64_t)-1) >> 10)

class DescriptorHandle
{
    friend class DescriptorAllocator;
    friend class DescriptorAllocatorManager;

    friend DescriptorHandle operator+(const DescriptorHandle& handle, INT offset)
    {
        DescriptorHandle other(handle);
        return other + offset;
    }

    friend DescriptorHandle operator+(const DescriptorHandle& handle, UINT offset)
    {
        DescriptorHandle other(handle);
        return other + offset;
    }

    friend DescriptorHandle operator-(const DescriptorHandle& handle, INT offset)
    {
        DescriptorHandle other(handle);
        return other - offset;
    }
private:
    DescriptorHandle(size_t offset, uint8_t owningIndex, uint8_t type) :
        mOffset(offset), mOwningHeapIndex(owningIndex), mType(type)
    {}
public:
    DescriptorHandle() :
        mOffset(UNKNOWN_OFFSET), mOwningHeapIndex(0), mType(0)
    {}

    void operator+= (INT offset)
    {
        if (mOffset != 0)
            mOffset = offset + static_cast<INT>(mOffset);
        mOffset = offset + static_cast<INT>(mOffset);
    }

    void operator+= (UINT offset)
    {
        if (mOffset != 0)
            mOffset += offset;
    }

    void operator-= (INT Offset) { *this += -1; }

    DescriptorHandle operator+ (INT offset)
    {
        DescriptorHandle lhs = *this;
        lhs += offset;
        return lhs;
    }

    DescriptorHandle operator+ (UINT offset)
    {
        DescriptorHandle lhs = *this;
        lhs += offset;
        return lhs;
    }

    DescriptorHandle operator- (INT offset)
    {
        DescriptorHandle lhs(*this);
        lhs += -offset;
        return lhs;
    }

    DescriptorHandle& operator++ ()
    {
        *this += 1;
        return *this;
    }

    DescriptorHandle operator-- ()
    {
        *this += -1;
        return *this;
    }

    D3D12_DESCRIPTOR_HEAP_TYPE GetType() const { return (D3D12_DESCRIPTOR_HEAP_TYPE)mType; }

    ID3D12DescriptorHeap* GetDescriptorHeap() const;

    operator bool() const { return mOffset != UNKNOWN_OFFSET; }

    operator D3D12_CPU_DESCRIPTOR_HANDLE()
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
        cpuHandle.ptr = GetCpuPtr();
        return cpuHandle;
    }

    operator D3D12_GPU_DESCRIPTOR_HANDLE()
    {
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
        gpuHandle.ptr = GetGpuPtr();
        return gpuHandle;
    }

    operator const D3D12_CPU_DESCRIPTOR_HANDLE() const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
        cpuHandle.ptr = GetCpuPtr();
        return cpuHandle;
    }

    operator const D3D12_GPU_DESCRIPTOR_HANDLE() const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
        gpuHandle.ptr = GetGpuPtr();
        return gpuHandle;
    }

    size_t GetCpuPtr() const;

    uint64_t GetGpuPtr() const;
private:
    uint64_t mOffset : 54;
    uint64_t mOwningHeapIndex : 8;
    uint64_t mType : 2;
    //DescriptorAllocator::SubHeap* mOwningHeap;
};


class DescriptorAllocatorManager : public Singleton<DescriptorAllocatorManager>
{
    USE_SINGLETON;

    DescriptorAllocatorManager() 
    {
        mDescriptorAllocators.emplace_back(new DescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
        mDescriptorAllocators.emplace_back(new DescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER));
        mDescriptorAllocators.emplace_back(new DescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
        mDescriptorAllocators.emplace_back(new DescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE_DSV));
    }
public:
    ~DescriptorAllocatorManager() 
    {
        for (size_t i = 0; i < mDescriptorAllocators.size(); i++)
        {
            delete mDescriptorAllocators[i];
        }
    }

    DescriptorHandle AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count = 1)
    {
        return mDescriptorAllocators[type]->Allocate(count);
    }

    void DeAllocateDescriptor(DescriptorHandle& handle, UINT count)
    {
        mDescriptorAllocators[handle.mType]->Deallocate(handle, count);
    }

    DescriptorAllocator& GetAlloc(D3D12_DESCRIPTOR_HEAP_TYPE type) { return *mDescriptorAllocators[type]; }
private:
    std::vector<DescriptorAllocator*> mDescriptorAllocators;
};

#define GET_DESCRIPTOR_ALLOC(type) DescriptorAllocatorManager::GetInstance()->GetAlloc(type)
#define ALLOC_DESCRIPTOR(type, count) DescriptorAllocatorManager::GetInstance()->AllocateDescriptor(type, count)
#define ALLOC_DESCRIPTOR1(type) ALLOC_DESCRIPTOR(type, 1)
#define DEALLOC_DESCRIPTOR(handle, count) DescriptorAllocatorManager::GetInstance()->DeAllocateDescriptor(handle, count)
