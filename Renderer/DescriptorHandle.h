#pragma once
#include "CoreHeader.h"

class DescriptorHandle;

class DescriptorAllocator
{
    friend class DescriptorHandle;

    using BITMAP_TYPE = uint32_t;

    struct SubHeap
    {
        DescriptorAllocator& mOwningAllocator;
        UINT mDescriptorSize;
        size_t mNumRemainHandles;
        size_t mNumReleasedHandles;
        D3D12_CPU_DESCRIPTOR_HANDLE mCpuStartHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE mGpuStartHandle;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDescriptorHeap;
        std::vector<BITMAP_TYPE> mBitMap;
        //std::vector<size_t> mPerLayerRemainHandles; // simple handle all layer has same remain handles

        SubHeap(DescriptorAllocator& owningAlloc) : mOwningAllocator(owningAlloc),
            mDescriptorSize(0), mNumRemainHandles(MAX_DESCRIPTOR_HEAP_SIZE), mNumReleasedHandles(0)
        {}
        ~SubHeap() {}
    };
public:
    DescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE heapType) :
        mType(heapType), mCurrentHeap(nullptr)
    {}

    ~DescriptorAllocator() { Clear(); }

    void Clear();

    // Deallocate count must equal to allocation count
    void Deallocate(DescriptorHandle& handle, uint32_t count);

    DescriptorHandle Allocate(uint32_t count);
    DescriptorHandle AllocateLayer(uint32_t startLayerNodeIdx, uint32_t totalNodeSize, size_t alignedCount);
private:
    SubHeap* AllocNewHeap();

    void TreeShiftSetBit(std::vector<uint32_t>& bitMap, int startIdx, size_t startBitIdx);

    void TreeShiftResetBit(std::vector<uint32_t>& bitMap, int startIdx, size_t startBitIdx);
private:
    D3D12_DESCRIPTOR_HEAP_TYPE mType;
    SubHeap* mCurrentHeap;
    std::mutex mAllocatorMutex;
    std::vector<SubHeap*> mDescriptorHeapPool;
};

namespace Graphics
{
    void DeAllocateDescriptor(DescriptorHandle&, UINT);
};

class DescriptorHandle
{
    friend class DescriptorAllocator;
    friend void Graphics::DeAllocateDescriptor(DescriptorHandle&, UINT);

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
    DescriptorHandle(size_t offset, DescriptorAllocator::SubHeap* subHeap) :
        mOffset(offset), mOwningHeap(subHeap)
    {}
public:
    DescriptorHandle() :
        mOffset(0), mOwningHeap(nullptr)
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

    ID3D12DescriptorHeap* GetDescriptorHeap() const { return mOwningHeap->mDescriptorHeap.Get(); }
    D3D12_DESCRIPTOR_HEAP_TYPE GetType() const { return GetOwningAlloc().mType; }

    operator bool() const { return mOwningHeap != nullptr; }

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

    size_t GetCpuPtr() const
    {
        return mOwningHeap->mDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr +
            (mOffset * mOwningHeap->mDescriptorSize);
    }

    uint64_t GetGpuPtr() const
    {
        return mOwningHeap->mDescriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr +
            (mOffset * mOwningHeap->mDescriptorSize);
    }
private:
    DescriptorAllocator& GetOwningAlloc() const { return mOwningHeap->mOwningAllocator; }
private:
    size_t mOffset;
    DescriptorAllocator::SubHeap* mOwningHeap;
};
