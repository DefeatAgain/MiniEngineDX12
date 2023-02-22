#pragma once
#include "CoreHeader.h"
#include "Common.h"

class DescriptorHandle;

class DescriptorAllocator
{
    friend class DescriptorHandle;

    struct SubHeap
    {
        DescriptorAllocator& mOwningAllocator;
        size_t mNumRemainHandles;
        size_t mNumReleasedHandles;
        D3D12_CPU_DESCRIPTOR_HANDLE mCpuStartHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE mGpuStartHandle;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDescriptorHeap;
        uint32_t mBitMap[63]; // all tree node size 2^6 - 1
        //std::vector<size_t> mPerLayerRemainHandles; // simple handle all layer has same remain handles

        SubHeap(DescriptorAllocator& owningAlloc) : mOwningAllocator(owningAlloc),
            mNumRemainHandles(MAX_DESCRIPTOR_HEAP_SIZE), mNumReleasedHandles(0)
        {
            ZeroMemory(&mBitMap, sizeof(mBitMap));
        }
        ~SubHeap() {}
    };
public:
    DescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE heapType, bool gpuVisible = false);

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
private:
    D3D12_DESCRIPTOR_HEAP_TYPE mType;
    bool mGpuVisible;
    UINT mDescriptorSize;
    uint8_t mCurrentHeapIndex;
    std::mutex mAllocatorMutex;
    std::vector<SubHeap*> mDescriptorHeapPool;
};

#define UNKNOWN_OFFSET (((uint64_t)-1) >> 11)

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
    DescriptorHandle(size_t offset, uint8_t owningIndex, uint8_t type, bool gpuVisible) :
        mOffset(offset), mOwningHeapIndex(owningIndex), mType(type), mGpuVisible(gpuVisible)
    {}

    DescriptorAllocator& GetAlloc() const;
public:
    DescriptorHandle() :
        mOffset(UNKNOWN_OFFSET), mOwningHeapIndex(0), mType(0), mGpuVisible(0)
    {}

    void operator+= (INT offset)
    {
        ASSERT(offset != UNKNOWN_OFFSET);
        mOffset = offset + static_cast<INT>(mOffset);
    }

    void operator+= (UINT offset)
    {
        ASSERT(offset != UNKNOWN_OFFSET);
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

    bool IsNull() const { return mOffset == UNKNOWN_OFFSET; }

    operator bool() const { return !IsNull(); }

    operator D3D12_CPU_DESCRIPTOR_HANDLE()
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
        cpuHandle.ptr = GetCpuPtr();
        return cpuHandle;
    }

    operator D3D12_GPU_DESCRIPTOR_HANDLE()
    {
        ASSERT(mGpuVisible);

        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
        gpuHandle.ptr = GetGpuPtr();
        return gpuHandle;
    }

    operator D3D12_CPU_DESCRIPTOR_HANDLE() const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
        cpuHandle.ptr = GetCpuPtr();
        return cpuHandle;
    }

    operator D3D12_GPU_DESCRIPTOR_HANDLE() const
    {
        ASSERT(mGpuVisible);

        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
        gpuHandle.ptr = GetGpuPtr();
        return gpuHandle;
    }

    size_t GetCpuPtr() const;

    uint64_t GetGpuPtr() const;
private:
    uint64_t mOffset : 53;
    uint64_t mOwningHeapIndex : 8;
    uint64_t mType : 2;
    uint64_t mGpuVisible : 1;
};


class DescriptorAllocatorManager : public Singleton<DescriptorAllocatorManager>
{
    USE_SINGLETON;

    DescriptorAllocatorManager();
public:
    ~DescriptorAllocatorManager();

    DescriptorHandle AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count = 1)
    {
        return mDescriptorAllocators[type]->Allocate(count);
    }

    void DeAllocateDescriptor(DescriptorHandle& handle, UINT count)
    {
        if (mDescriptorAllocators.empty())
            return;
        mDescriptorAllocators[handle.mType]->Deallocate(handle, count);
    }

    DescriptorHandle AllocateDescriptorGpu(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count = 1)
    {
        return mDescriptorAllocators[type + D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES]->Allocate(count);
    }

    void DeAllocateDescriptorGpu(DescriptorHandle& handle, UINT count)
    {
        if (mDescriptorAllocators.empty())
            return;
        mDescriptorAllocators[handle.mType + D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES]->Deallocate(handle, count);
    }

    DescriptorAllocator& GetAlloc(D3D12_DESCRIPTOR_HEAP_TYPE type) { return *mDescriptorAllocators[type]; }

    DescriptorAllocator& GetAllocGpu(D3D12_DESCRIPTOR_HEAP_TYPE type) { return *mDescriptorAllocators[type + D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES]; }
private:
    std::vector<DescriptorAllocator*> mDescriptorAllocators;
};

#define GET_DESCRIPTOR_ALLOC(type) DescriptorAllocatorManager::GetInstance()->GetAlloc(type)
#define GET_DESCRIPTOR_ALLOC_GPU(type) DescriptorAllocatorManager::GetInstance()->GetAllocGpu(type)
#define ALLOC_DESCRIPTOR(type, count) DescriptorAllocatorManager::GetInstance()->AllocateDescriptor(type, count)
#define ALLOC_DESCRIPTOR1(type) ALLOC_DESCRIPTOR(type, 1)
#define DEALLOC_DESCRIPTOR(handle, count) DescriptorAllocatorManager::GetInstance()->DeAllocateDescriptor(handle, count)
#define ALLOC_DESCRIPTOR_GPU(type, count) DescriptorAllocatorManager::GetInstance()->AllocateDescriptorGpu(type, count)
#define DEALLOC_DESCRIPTOR_GPU(handle, count) DescriptorAllocatorManager::GetInstance()->DeAllocateDescriptorGpu(handle, count)


// LinearAllocForGpu
class DescriptorLinearAlloc
{
public:
    DescriptorLinearAlloc(D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, size_t size = 64);
    ~DescriptorLinearAlloc() {}

    D3D12_GPU_DESCRIPTOR_HANDLE Map(DescriptorHandle handles[], size_t size, size_t index = -1);
    D3D12_GPU_DESCRIPTOR_HANDLE Map(DescriptorHandle handle, size_t index);

    D3D12_GPU_DESCRIPTOR_HANDLE GetStart() const { return mGpuStart; }
    D3D12_DESCRIPTOR_HEAP_TYPE GetType() const { return mType; }
    ID3D12DescriptorHeap* GetHeap() { return mDescriptorHeap.Get(); }
private:
    D3D12_DESCRIPTOR_HEAP_TYPE mType;
    D3D12_CPU_DESCRIPTOR_HANDLE mCpuStart;
    D3D12_GPU_DESCRIPTOR_HANDLE mGpuStart;
    size_t mCurrentOffset;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDescriptorHeap;
};
