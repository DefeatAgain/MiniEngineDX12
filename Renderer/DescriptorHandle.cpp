#include "DescriptorHandle.h"
#include "Graphics.h"
#include "Utils/DebugUtils.h"
#include "Math/Common.h"

void DescriptorAllocator::Deallocate(DescriptorHandle& handle, uint32_t count)
{
    constexpr uint32_t perNodeSize = (sizeof(BITMAP_TYPE) * 8);
    constexpr uint32_t firstLayerSize = MAX_DESCRIPTOR_HEAP_SIZE / perNodeSize;
    ASSERT(count > 0 && count <= firstLayerSize);

    SubHeap* subHeap = GetSubHeap(handle.mOwningHeapIndex);
    ASSERT(subHeap);

    if (count > 1)
        count = Math::AlignUp(count, 2);

    unsigned long maxLayerIdx = 0;
    unsigned long allocLayerIdx = 0;
    _BitScanForward(&maxLayerIdx, firstLayerSize);
    _BitScanForward(&allocLayerIdx, count);
    unsigned long startLayerIdx = maxLayerIdx - allocLayerIdx;

    std::unique_lock<std::mutex> lock1(mAllocatorMutex, std::defer_lock);
    while (!lock1.try_lock())
        std::this_thread::yield();

    uint32_t startLayerNodeIdx = (1 << startLayerIdx) - 1;
    //uint32_t offset = (handle.GetCpuPtr() - subHeap->mCpuStartHandle.ptr) / subHeap->mDescriptorSize / count; // Calc Address Offset
    uint32_t offset = (handle.GetCpuPtr() - subHeap->mCpuStartHandle.ptr) / mDescriptorSize / count; // Calc Address Offset
    uint32_t nodeIdx = startLayerNodeIdx + offset / perNodeSize; // Tree Node Index
    uint32_t bitIdx = offset % perNodeSize; // BitSet Index

    subHeap->mBitMap[nodeIdx] ^= 1 << bitIdx;

    TreeShiftResetBit(subHeap->mBitMap, nodeIdx, bitIdx);
    subHeap->mNumRemainHandles += count;
    subHeap->mNumReleasedHandles += count;

    if (subHeap->mNumReleasedHandles > MAX_DESCRIPTOR_ALLOC_CACHE_SIZE)
    {
        subHeap->mNumReleasedHandles = 0;
        auto maxHandleIter = std::max_element(mDescriptorHeapPool.cbegin(), mDescriptorHeapPool.cend(),
            [](auto& p1, auto& p2) { return p1->mNumRemainHandles > p2->mNumRemainHandles; });
        mCurrentHeapIndex = std::distance(mDescriptorHeapPool.cbegin(), maxHandleIter);
    }

    handle.mOffset = UNKNOWN_OFFSET;
}

DescriptorHandle DescriptorAllocator::Allocate(uint32_t count)
{
    constexpr uint32_t perNodeSize = (sizeof(BITMAP_TYPE) * 8);
    constexpr uint32_t firstLayerSize = MAX_DESCRIPTOR_HEAP_SIZE / perNodeSize;
    ASSERT(count > 0 && count <= firstLayerSize);

    if (count > 1)
        count = Math::AlignUp(count, 2);

    unsigned long maxLayerIdx = 0;
    unsigned long allocLayerIdx = 0;
    _BitScanForward(&maxLayerIdx, firstLayerSize);
    _BitScanForward(&allocLayerIdx, count);
    unsigned long startLayerIdx = maxLayerIdx - allocLayerIdx; // Calc tree layer
    uint32_t totalNodeSize = (1UL << (startLayerIdx + 1)) - 1; // Calc total tree Nodes
    uint32_t startLayerNodeIdx = (1 << startLayerIdx) - 1; // Prev layer total nodes

    std::unique_lock<std::mutex> lock1(mAllocatorMutex, std::defer_lock);
    while (!lock1.try_lock())
        std::this_thread::yield();

    if (mCurrentHeapIndex == (uint8_t)-1 || mDescriptorHeapPool[mCurrentHeapIndex]->mNumRemainHandles < count)
        mCurrentHeapIndex = AllocNewHeap();

    DescriptorHandle newHandle = AllocateLayer(startLayerNodeIdx, totalNodeSize, count);
    if (newHandle)
        return newHandle;

    mCurrentHeapIndex = AllocNewHeap();
    return AllocateLayer(startLayerNodeIdx, totalNodeSize, count);
}

DescriptorHandle DescriptorAllocator::AllocateLayer(uint32_t startLayerNodeIdx, uint32_t totalNodeSize, size_t alignedCount)
{
    constexpr uint32_t perNodeSize = (sizeof(BITMAP_TYPE) * 8);

    SubHeap* subHeap = mDescriptorHeapPool[mCurrentHeapIndex];

    if (subHeap->mBitMap.size() < totalNodeSize) // Fill tree nodes
        subHeap->mBitMap.resize(totalNodeSize);

    size_t offsetFromHeap = 0;
    for (uint32_t i = startLayerNodeIdx, j = 0; i < totalNodeSize; i++, j++) // iter curlayer node
    {
        unsigned long freeIdx = 0;
        if (_BitScanForward(&freeIdx, ~subHeap->mBitMap[i]))
        {
            offsetFromHeap = (j * perNodeSize + freeIdx) * alignedCount;
            subHeap->mNumRemainHandles -= alignedCount;
            subHeap->mBitMap[i] |= 1 << freeIdx;
            TreeShiftSetBit(subHeap->mBitMap, i, freeIdx);
            break;
        }
    }
    return DescriptorHandle(offsetFromHeap, mCurrentHeapIndex, mType);
}

DescriptorAllocator::DescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE heapType) :
    mType(heapType), mCurrentHeapIndex(-1)
{
    mDescriptorSize = Graphics::gDevice->GetDescriptorHandleIncrementSize(mType);
}

void DescriptorAllocator::Clear()
{
    std::unique_lock<std::mutex> lock1(mAllocatorMutex, std::defer_lock);
    ASSERT(lock1.try_lock(), "Lock is owning by other thread!");

    for (auto& subHeap : mDescriptorHeapPool)
    {
        delete subHeap;
    }

    mCurrentHeapIndex = -1;
    mDescriptorHeapPool.clear();
}

uint8_t DescriptorAllocator::AllocNewHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC desc;
    desc.Type = mType;
    desc.NumDescriptors = MAX_DESCRIPTOR_HEAP_SIZE;
    desc.NodeMask = 0;
    //if (mType == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || mType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    //    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    //else
    //    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    SubHeap* subAllocator = mDescriptorHeapPool.emplace_back(new SubHeap(*this));
    CheckHR(Graphics::gDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(subAllocator->mDescriptorHeap.GetAddressOf())));
    subAllocator->mCpuStartHandle = subAllocator->mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    subAllocator->mGpuStartHandle = subAllocator->mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

    //constexpr uint32_t perNodeSize = (sizeof(BITMAP_TYPE) * 8);
    //constexpr uint32_t firstLayerSize = MAX_DESCRIPTOR_HEAP_SIZE / perNodeSize;
    //unsigned long maxLayerIdx = 0;
    //_BitScanForward(&maxLayerIdx, firstLayerSize);
    //subAllocator->mPerLayerRemainHandles.resize(maxLayerIdx + 1);
    return mDescriptorHeapPool.size() - 1;
}

void DescriptorAllocator::TreeShiftSetBit(std::vector<uint32_t>& bitMap, int startIdx, size_t startBitIdx)
{
    constexpr uint32_t halfBitSize = sizeof(BITMAP_TYPE) * 8 / 2;
    bool isRight = startIdx % 2 == 0; // right leaf
    startIdx = ((startIdx + 1) >> 1) - 1; // parent node (startIdx + 1) / 2 - 1
    startBitIdx >>= 1; // parent node bit pos

    while (startIdx >= 0)
    {
        if (isRight)
            bitMap[startIdx] |= 1 << (startBitIdx + halfBitSize);
        else
            bitMap[startIdx] |= 1 << startBitIdx;

        isRight = startIdx % 2 == 0;
        startIdx = ((startIdx + 1) >> 1) - 1;
        startBitIdx >>= 1;
    }
}

void DescriptorAllocator::TreeShiftResetBit(std::vector<uint32_t>& bitMap, int startIdx, size_t startBitIdx)
{
    static_assert(sizeof(uint32_t) == sizeof(LONG), "CHECK BIT SET SIZE");

    constexpr size_t halfBitSize = sizeof(BITMAP_TYPE) * 8 / 2;
    bool isRight = startIdx % 2 == 0;
    //bool isCompleteReset = true;

    BOOLEAN hasBitSet = _bittest((const LONG*)&bitMap[startIdx], startBitIdx);
    size_t neighborBitIdx = startBitIdx % 2 == 0 ? startBitIdx + 1 : startBitIdx - 1;
    hasBitSet |= _bittest((const LONG*)&bitMap[startIdx], neighborBitIdx);

    startIdx = ((startIdx + 1) >> 1) - 1;
    startBitIdx >>= 1;

    // not same as TreeShiftSetBit, the parent node will not reset, unless child adjacent bit is reset
    while (startIdx >= 0)
    {
        if (!hasBitSet)
        {
            if (isRight)
                bitMap[startIdx] ^= 1 << (startBitIdx + halfBitSize);
            else
                bitMap[startIdx] ^= 1 << startBitIdx;
        }
        else
        {
            //isCompleteReset = false;
            break;
        }

        hasBitSet = _bittest((const LONG*)&bitMap[startIdx], startBitIdx);
        neighborBitIdx = startBitIdx % 2 == 0 ? startBitIdx + 1 : startBitIdx - 1;
        hasBitSet |= _bittest((const LONG*)&bitMap[startIdx], neighborBitIdx);

        isRight = startIdx % 2 == 0;
        startIdx = ((startIdx + 1) >> 1) - 1;
        startBitIdx >>= 1;
    }

    //return isCompleteReset;
}


// DescriptorHandle
ID3D12DescriptorHeap* DescriptorHandle::GetDescriptorHeap() const
{
    return GET_DESCRIPTOR_ALLOC(GetType()).GetHeap(mOwningHeapIndex);
}

size_t DescriptorHandle::GetCpuPtr() const
{
    DescriptorAllocator& alloc = GET_DESCRIPTOR_ALLOC(GetType());
    return alloc.GetHeapCpuStart(mOwningHeapIndex).ptr + (mOffset * alloc.mDescriptorSize);
}

uint64_t DescriptorHandle::GetGpuPtr() const
{
    DescriptorAllocator& alloc = GET_DESCRIPTOR_ALLOC(GetType());
    return alloc.GetHeapGpuStart(mOwningHeapIndex).ptr + (mOffset * alloc.mDescriptorSize);
}

DescriptorLinearAlloc::DescriptorLinearAlloc(D3D12_DESCRIPTOR_HEAP_TYPE type, size_t size)
{
    ASSERT(type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    mType = type;

    D3D12_DESCRIPTOR_HEAP_DESC desc;
    desc.Type = mType;
    desc.NumDescriptors = size;
    desc.NodeMask = 0;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    CheckHR(Graphics::gDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(mDescriptorHeap.GetAddressOf())));
    mCpuStart = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    mGpuStart = mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    mCurrentDescriptorOffset = 0;
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorLinearAlloc::Map(DescriptorHandle handle, size_t index, size_t size)
{
    ASSERT(index + size < 64);

    const D3D12_CPU_DESCRIPTOR_HANDLE& cpuHandle = handle;

    auto findIter = mAddrMap.find(cpuHandle.ptr);
    if (findIter != mAddrMap.end())
    {
        D3D12_GPU_DESCRIPTOR_HANDLE res;
        res.ptr = findIter->second;
        return res;
    }
    else
    {
        D3D12_DESCRIPTOR_HEAP_TYPE heapType = handle.GetType();
        size_t descriptorSize = Graphics::gDevice->GetDescriptorHandleIncrementSize(heapType);
        CD3DX12_CPU_DESCRIPTOR_HANDLE curCpuHandle(mCpuStart);
        CD3DX12_GPU_DESCRIPTOR_HANDLE curGpuHandle(mGpuStart);
        curCpuHandle.Offset(index, descriptorSize);
        curGpuHandle.Offset(index, descriptorSize);

        for (size_t i = 0; i < size; i++)
        {
            const D3D12_CPU_DESCRIPTOR_HANDLE& cpuHandle1 = handle[i];
            Graphics::gDevice->CopyDescriptorsSimple(1, curCpuHandle, cpuHandle1, heapType);
            curCpuHandle.Offset(1, descriptorSize);
        }

        return curGpuHandle;
    }
}

//D3D12_GPU_DESCRIPTOR_HANDLE DescriptorLinearAlloc::Map(DescriptorHandle handles[], size_t size)
//{
//    const D3D12_CPU_DESCRIPTOR_HANDLE& cpuHandle = handles[0];
//
//    auto findIter = mAddrMap.find(cpuHandle.ptr);
//    if (findIter != mAddrMap.end())
//    {
//        D3D12_GPU_DESCRIPTOR_HANDLE res;
//        res.ptr = findIter->second;
//        return res;
//    }
//    else
//    {
//        ASSERT(size + mCurrentDescriptorOffset < 64);
//
//        D3D12_DESCRIPTOR_HEAP_TYPE heapType = handles[0].GetType();
//        size_t descriptorSize = Graphics::gDevice->GetDescriptorHandleIncrementSize(heapType);
//        CD3DX12_CPU_DESCRIPTOR_HANDLE curCpuHandle(mCpuStart);
//        CD3DX12_GPU_DESCRIPTOR_HANDLE curGpuHandle(mGpuStart);
//        curCpuHandle.Offset(mCurrentDescriptorOffset, descriptorSize);
//        curGpuHandle.Offset(mCurrentDescriptorOffset, descriptorSize);
//        mAddrMap[curCpuHandle.ptr] = curGpuHandle.ptr;
//
//        for (size_t i = 0; i < size; i++)
//        {
//            const D3D12_CPU_DESCRIPTOR_HANDLE& cpuHandle1 = handles[0];
//            Graphics::gDevice->CopyDescriptorsSimple(1, curCpuHandle, cpuHandle1, heapType);
//            curCpuHandle.Offset(1, descriptorSize);
//        }
//
//        mCurrentDescriptorOffset += size;
//        return curGpuHandle;
//    }
//}
