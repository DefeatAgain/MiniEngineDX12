#include "CommandList.h"
#include "GpuResource.h"
#include "GpuBuffer.h"
#include "PixelBuffer.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "Graphics.h"
#include "CommandQueue.h"

#include <pix/pix3.h>

#define VALID_COMPUTE_QUEUE_RESOURCE_STATES \
    ( D3D12_RESOURCE_STATE_UNORDERED_ACCESS \
    | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE \
    | D3D12_RESOURCE_STATE_COPY_DEST \
    | D3D12_RESOURCE_STATE_COPY_SOURCE )

CommandList::CommandList(D3D12_COMMAND_LIST_TYPE Type) :
    mType(Type),
    mCommandList(nullptr),
    mCommandAllocator(nullptr),
    mCurGraphicsRootSignature(nullptr),
    mCurComputeRootSignature(nullptr),
    mCurPipelineState(nullptr),
    mNumBarriersToFlush(0),
    mCpuLinearAllocator(kCpuWritable),
    mGpuLinearAllocator(kGpuExclusive),
    mCommandListIndex(0),
    mRetiredFenceValue(0)
{
    ZeroMemory(mCurrentDescriptorHeaps, sizeof(mCurrentDescriptorHeaps));
}

CommandList* CommandList::Reset()
{
    ASSERT(mCommandList && mCommandAllocator);
    CheckHR(mCommandAllocator->Reset());
    CheckHR(mCommandList->Reset(mCommandAllocator.Get(), nullptr));

    mCpuLinearAllocator.CleanupUsedPages();
    mGpuLinearAllocator.CleanupUsedPages();
    mResourceStateCache.clear();

    mCurGraphicsRootSignature = nullptr;
    mCurComputeRootSignature = nullptr;
    mCurPipelineState = nullptr;
    mNumBarriersToFlush = 0;

    mCommandListIndex = 0;

    BindDescriptorHeaps();

    return this;
}

CommandList& CommandList::Begin(const std::wstring& id)
{
    PIXBeginEvent(id.c_str());
    SetID(id);

    return *this;
}

void CommandList::Initialize()
{
    std::wstring queueNamePrefix = Graphics::GetQueueName(mType);

    CheckHR(Graphics::gDevice->CreateCommandAllocator(mType, IID_PPV_ARGS(mCommandAllocator.GetAddressOf())));
    mCommandAllocator->SetName((queueNamePrefix + L" CommandAllocator").c_str());

    CheckHR(Graphics::gDevice->CreateCommandList(0, mType, mCommandAllocator.Get(), nullptr, IID_PPV_ARGS(mCommandList.GetAddressOf())));
    mCommandList->SetName((queueNamePrefix + L" CommandList").c_str());
}

void CommandList::Finish()
{
    PIXEndEvent();
}

void CommandList::PIXBeginEvent(const wchar_t* label)
{
#ifndef RELEASE
    ::PIXBeginEvent(GetDeviceCommandList(), 0, label);
#else
    (label);
#endif
}

void CommandList::PIXEndEvent()
{
#ifndef RELEASE
    ::PIXEndEvent(GetDeviceCommandList());
#endif
}

void CommandList::PIXSetMarker(const wchar_t* label)
{
#ifndef RELEASE
    ::PIXSetMarker(GetDeviceCommandList(), 0, label);
#else
    (label);
#endif
}

void CommandList::ExceptResourceBeginState(GpuResource& resource, D3D12_RESOURCE_STATES newState)
{
    ResourceStateCache& resStatae = GetResourceStateCache(resource);

    if (newState == resStatae.mStateCurrent)
        return;

    // if is first commandList, just transite it.
    if (mCommandListIndex == 0)
    {
        TransitionResource(resource, newState);
        return;
    }

    ASSERT(newState != resStatae.mStateTransition, "This method only effect prev commandList. not support transition begin");

    // record only, real transition resolved by frame context 
    resStatae.mStateCurrent = newState;
}

void CommandList::TransitionResource(GpuResource& resource, D3D12_RESOURCE_STATES newState, bool flushImmediate)
{
    ResourceStateCache& resStatae = GetResourceStateCache(resource);

    D3D12_RESOURCE_STATES oldState = resStatae.mStateCurrent;

    if (mType == D3D12_COMMAND_LIST_TYPE_COMPUTE)
    {
        ASSERT((oldState & VALID_COMPUTE_QUEUE_RESOURCE_STATES) == oldState);
        ASSERT((newState & VALID_COMPUTE_QUEUE_RESOURCE_STATES) == newState);
    }

    if (oldState != newState)
    {
        ASSERT(mNumBarriersToFlush < MAX_BARRIERS_CACHE_FLUSH, "Exceeded arbitrary limit on buffered barriers");
        D3D12_RESOURCE_BARRIER& barrierDesc = mResourceBarrierBuffer[mNumBarriersToFlush++];

        barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierDesc.Transition.pResource = resource.GetResource();
        barrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierDesc.Transition.StateBefore = oldState;
        barrierDesc.Transition.StateAfter = newState;

        // Check to see if we already started the transition
        if (newState == resStatae.mStateTransition)
        {
            barrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
            resStatae.mStateTransition = (D3D12_RESOURCE_STATES)-1;
        }
        else
        {
            barrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        }

        resStatae.mStateCurrent = newState;
    }
    else if (newState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        InsertUAVBarrier(resource, flushImmediate);

    if (flushImmediate || mNumBarriersToFlush == MAX_BARRIERS_CACHE_FLUSH)
        FlushResourceBarriers();
}

void CommandList::TransitionResource(ResourceStateCache& curStateCache, ResourceStateCache& newStateCahce)
{
    ASSERT(newStateCahce.mStateCurrent != D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "UAV barrier could not transite by this method");

    if (mType == D3D12_COMMAND_LIST_TYPE_COMPUTE)
    {
        ASSERT((newStateCahce.mStateCurrent & VALID_COMPUTE_QUEUE_RESOURCE_STATES) == newStateCahce.mStateCurrent);
    }

    D3D12_RESOURCE_BARRIER& barrierDesc = mResourceBarrierBuffer[mNumBarriersToFlush++];

    barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc.Transition.pResource = curStateCache.mGpuResource.GetResource();
    barrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc.Transition.StateBefore = curStateCache.mStateCurrent;
    barrierDesc.Transition.StateAfter = newStateCahce.mStateCurrent;

    curStateCache.mStateCurrent = newStateCahce.mStateCurrent;
}

void CommandList::BeginResourceTransition(GpuResource& resource, D3D12_RESOURCE_STATES newState, bool flushImmediate)
{
    ResourceStateCache& resStatae = GetResourceStateCache(resource);

    // If it's already transitioning, finish that transition
    if (resStatae.mStateTransition != (D3D12_RESOURCE_STATES)-1)
        TransitionResource(resource, resStatae.mStateTransition);

    D3D12_RESOURCE_STATES oldState = resStatae.mStateCurrent;

    if (oldState != newState)
    {
        ASSERT(mNumBarriersToFlush < 16, "Exceeded arbitrary limit on buffered barriers");
        D3D12_RESOURCE_BARRIER& BarrierDesc = mResourceBarrierBuffer[mNumBarriersToFlush++];

        BarrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        BarrierDesc.Transition.pResource = resource.GetResource();
        BarrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        BarrierDesc.Transition.StateBefore = oldState;
        BarrierDesc.Transition.StateAfter = newState;

        BarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;

        resStatae.mStateTransition = newState;
    }

    if (flushImmediate || mNumBarriersToFlush == MAX_BARRIERS_CACHE_FLUSH)
        FlushResourceBarriers();
}

void CommandList::InsertUAVBarrier(GpuResource& resource, bool flushImmediate)
{
    ASSERT(mNumBarriersToFlush < MAX_BARRIERS_CACHE_FLUSH, "Exceeded arbitrary limit on buffered barriers");
    D3D12_RESOURCE_BARRIER& BarrierDesc = mResourceBarrierBuffer[mNumBarriersToFlush++];

    BarrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    BarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    BarrierDesc.UAV.pResource = resource.GetResource();

    if (flushImmediate)
        FlushResourceBarriers();
}

void CommandList::InsertAliasBarrier(GpuResource& before, GpuResource& after, bool flushImmediate)
{
    ASSERT(mNumBarriersToFlush < MAX_BARRIERS_CACHE_FLUSH, "Exceeded arbitrary limit on buffered barriers");
    D3D12_RESOURCE_BARRIER& BarrierDesc = mResourceBarrierBuffer[mNumBarriersToFlush++];

    BarrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    BarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    BarrierDesc.Aliasing.pResourceBefore = before.GetResource();
    BarrierDesc.Aliasing.pResourceAfter = after.GetResource();

    if (flushImmediate)
        FlushResourceBarriers();
}

void CommandList::FlushResourceBarriers()
{
    if (mNumBarriersToFlush > 0)
    {
        mCommandList->ResourceBarrier(mNumBarriersToFlush, mResourceBarrierBuffer);
        mNumBarriersToFlush = 0;
    }
}

void CommandList::UpdateResourceState()
{
    // now resource has real state
    for (auto& resourceCache : mResourceStateCache)
        resourceCache.second.mGpuResource.mUsageState = resourceCache.second.mStateCurrent;
}

void CommandList::SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, ID3D12DescriptorHeap* heapPtr)
{
    if (mCurrentDescriptorHeaps[type] != heapPtr)
    {
        mCurrentDescriptorHeaps[type] = heapPtr;
        BindDescriptorHeaps();
    }
}

void CommandList::SetDescriptorHeaps(UINT heapCount, D3D12_DESCRIPTOR_HEAP_TYPE type[], ID3D12DescriptorHeap* heapPtrs[])
{
    bool anyChanged = false;

    for (UINT i = 0; i < heapCount; ++i)
    {
        if (mCurrentDescriptorHeaps[type[i]] != heapPtrs[i])
        {
            mCurrentDescriptorHeaps[type[i]] = heapPtrs[i];
            anyChanged = true;
        }
    }

    if (anyChanged)
        BindDescriptorHeaps();
}

void CommandList::SetPipelineState(const PipelineState& pso)
{
    ID3D12PipelineState* pipelineState = pso.GetPipelineStateObject();
    ASSERT(pipelineState);

    if (pipelineState == mCurPipelineState)
        return;

    mCommandList->SetPipelineState(pipelineState);
    mCurPipelineState = pipelineState;
}

void CommandList::SetPredication(ID3D12Resource* resource, UINT64 bufferOffset, D3D12_PREDICATION_OP op)
{
    mCommandList->SetPredication(resource, bufferOffset, op);
}

void CommandList::InsertTimeStamp(ID3D12QueryHeap* pQueryHeap, uint32_t queryIdx)
{
    ASSERT(mType == D3D12_COMMAND_LIST_TYPE_DIRECT || mType == D3D12_COMMAND_LIST_TYPE_COMPUTE);
    mCommandList->EndQuery(pQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, queryIdx);
}

void CommandList::ResolveTimeStamps(ID3D12Resource* pReadbackHeap, ID3D12QueryHeap* pQueryHeap, uint32_t numQueries)
{
    ASSERT(mType == D3D12_COMMAND_LIST_TYPE_DIRECT || mType == D3D12_COMMAND_LIST_TYPE_COMPUTE);
    mCommandList->ResolveQueryData(pQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0, numQueries, pReadbackHeap, 0);
}

void CommandList::BeforeCommandListSubmit()
{
    FlushResourceBarriers();

    CheckHR(mCommandList->Close());
}

void CommandList::BindDescriptorHeaps()
{
    UINT nonNullHeaps = 0;
    ID3D12DescriptorHeap* heapsToBind[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];
    for (UINT i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
    {
        ID3D12DescriptorHeap* HeapIter = mCurrentDescriptorHeaps[i];
        if (HeapIter)
            heapsToBind[nonNullHeaps++] = HeapIter;
    }

    if (nonNullHeaps > 0)
        mCommandList->SetDescriptorHeaps(nonNullHeaps, heapsToBind);
}

ResourceStateCache& CommandList::GetResourceStateCache(GpuResource& resource)
{
    size_t resourceHash = Utility::HashState(&resource);
    auto findIter = mResourceStateCache.find(resourceHash);
    if (findIter != mResourceStateCache.end())
        return findIter->second;

    auto insertIter = mResourceStateCache.emplace(resourceHash, resource);
    return insertIter.first->second;
}


// -- CopyCommandList --
void CopyCommandList::CopyBuffer(GpuResource& dest, GpuResource& src)
{
    TransitionResource(dest, D3D12_RESOURCE_STATE_COPY_DEST);
    TransitionResource(src, D3D12_RESOURCE_STATE_COPY_SOURCE);
    FlushResourceBarriers();
    mCommandList->CopyResource(dest.GetResource(), src.GetResource());
}

void CopyCommandList::CopyBufferRegion(GpuResource& dest, size_t destOffset, GpuResource& src, size_t srcOffset, size_t numBytes)
{
    TransitionResource(dest, D3D12_RESOURCE_STATE_COPY_DEST);
    TransitionResource(src, D3D12_RESOURCE_STATE_COPY_SOURCE);
    FlushResourceBarriers();
    mCommandList->CopyBufferRegion(dest.GetResource(), destOffset, src.GetResource(), srcOffset, numBytes);
}

void CopyCommandList::CopyBuffer(GpuResource& dest, UploadBuffer& src)
{
    TransitionResource(dest, D3D12_RESOURCE_STATE_COPY_DEST);
    FlushResourceBarriers();
    mCommandList->CopyResource(dest.GetResource(), src.GetResource());
}

void CopyCommandList::CopyBufferRegion(GpuResource& dest, size_t destOffset, UploadBuffer& src, size_t srcOffset, size_t numBytes)
{
    TransitionResource(dest, D3D12_RESOURCE_STATE_COPY_DEST);
    FlushResourceBarriers();
    mCommandList->CopyBufferRegion(dest.GetResource(), destOffset, src.GetResource(), srcOffset, numBytes);
}

void CopyCommandList::CopySubresource(GpuResource& dest, UINT destSubIndex, GpuResource& src, UINT srcSubIndex)
{
    FlushResourceBarriers();

    D3D12_TEXTURE_COPY_LOCATION DestLocation =
    {
        dest.GetResource(),
        D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
        destSubIndex
    };

    D3D12_TEXTURE_COPY_LOCATION SrcLocation =
    {
        src.GetResource(),
        D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
        srcSubIndex
    };

    mCommandList->CopyTextureRegion(&DestLocation, 0, 0, 0, &SrcLocation, nullptr);
}

void CopyCommandList::CopyCounter(GpuResource& dest, size_t destOffset, StructuredBuffer& src)
{
    TransitionResource(dest, D3D12_RESOURCE_STATE_COPY_DEST);
    TransitionResource(src.GetCounterBuffer(), D3D12_RESOURCE_STATE_COPY_SOURCE);
    FlushResourceBarriers();
    mCommandList->CopyBufferRegion(dest.GetResource(), destOffset, src.GetCounterBuffer().GetResource(), 0, 4);
}

void CopyCommandList::CopyTextureRegion(GpuResource& dest, UINT x, UINT y, UINT z, GpuResource& source, RECT& rect)
{
    TransitionResource(dest, D3D12_RESOURCE_STATE_COPY_DEST);
    TransitionResource(source, D3D12_RESOURCE_STATE_COPY_SOURCE);
    FlushResourceBarriers();

    D3D12_TEXTURE_COPY_LOCATION destLoc = CD3DX12_TEXTURE_COPY_LOCATION(dest.GetResource(), 0);
    D3D12_TEXTURE_COPY_LOCATION srcLoc = CD3DX12_TEXTURE_COPY_LOCATION(source.GetResource(), 0);

    D3D12_BOX box = {};
    box.back = 1;
    box.left = rect.left;
    box.right = rect.right;
    box.top = rect.top;
    box.bottom = rect.bottom;

    mCommandList->CopyTextureRegion(&destLoc, x, y, z, &srcLoc, &box);
}

void CopyCommandList::ResetCounter(StructuredBuffer& buffer, uint32_t value)
{
    FillBuffer(buffer.GetCounterBuffer(), 0, value, sizeof(uint32_t));
    TransitionResource(buffer.GetCounterBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void CopyCommandList::InitializeTexture(GpuResource& dest, UINT numSubresources, D3D12_SUBRESOURCE_DATA subData[])
{
    UINT64 uploadBufferSize = GetRequiredIntermediateSize(dest.GetResource(), 0, numSubresources);

    // copy data to the intermediate upload heap and then schedule a copy from the upload heap to the default texture
    AllocSpan span = mCpuLinearAllocator.Allocate(uploadBufferSize);
    UpdateSubresources(mCommandList.Get(), dest.GetResource(), span.mPage.GetResource(), 0, 0, numSubresources, subData);
    //TransitionResource(dest, D3D12_RESOURCE_STATE_GENERIC_READ);
}

void CopyCommandList::InitializeBuffer(GpuBuffer& dest, const void* data, size_t numBytes, size_t destOffset)
{
    AllocSpan span = mCpuLinearAllocator.Allocate(numBytes);
    CopyMemory(span.mCpuAddress, data, numBytes);

    // copy data to the intermediate upload heap and then schedule a copy from the upload heap to the default texture
    TransitionResource(dest, D3D12_RESOURCE_STATE_COPY_DEST, true);
    mCommandList->CopyBufferRegion(dest.GetResource(), destOffset, span.mPage.GetResource(), 0, numBytes);
    //TransitionResource(dest, D3D12_RESOURCE_STATE_GENERIC_READ, true);
}

void CopyCommandList::InitializeBuffer(GpuBuffer& dest, const UploadBuffer& src, size_t srcOffset, size_t numBytes, size_t destOffset)
{
    size_t maxBytes = std::min<size_t>(dest.GetBufferSize() - destOffset, src.GetBufferSize() - srcOffset);
    numBytes = std::min<size_t>(maxBytes, numBytes);

    // copy data to the intermediate upload heap and then schedule a copy from the upload heap to the default texture
    TransitionResource(dest, D3D12_RESOURCE_STATE_COPY_DEST, true);
    mCommandList->CopyBufferRegion(dest.GetResource(), destOffset, const_cast<ID3D12Resource*>(src.GetResource()), srcOffset, numBytes);
    //TransitionResource(dest, D3D12_RESOURCE_STATE_GENERIC_READ, true);
}

void CopyCommandList::InitializeTextureArraySlice(GpuResource& dest, UINT sliceIndex, GpuResource& src)
{
    TransitionResource(dest, D3D12_RESOURCE_STATE_COPY_DEST);
    FlushResourceBarriers();

    const D3D12_RESOURCE_DESC& destDesc = dest.GetResource()->GetDesc();
    const D3D12_RESOURCE_DESC& srcDesc = src.GetResource()->GetDesc();

    ASSERT(sliceIndex < destDesc.DepthOrArraySize&&
        srcDesc.DepthOrArraySize == 1 &&
        destDesc.Width == srcDesc.Width &&
        destDesc.Height == srcDesc.Height &&
        destDesc.MipLevels <= srcDesc.MipLevels
    );

    UINT SubResourceIndex = sliceIndex * destDesc.MipLevels;

    for (UINT i = 0; i < destDesc.MipLevels; ++i)
    {
        D3D12_TEXTURE_COPY_LOCATION destCopyLocation =
        {
            dest.GetResource(),
            D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
            SubResourceIndex + i
        };

        D3D12_TEXTURE_COPY_LOCATION srcCopyLocation =
        {
            src.GetResource(),
            D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
            i
        };

        mCommandList->CopyTextureRegion(&destCopyLocation, 0, 0, 0, &srcCopyLocation, nullptr);
    }

    //TransitionResource(dest, D3D12_RESOURCE_STATE_GENERIC_READ);
}

void CopyCommandList::WriteBuffer(GpuResource& dest, size_t destOffset, const void* data, size_t numBytes)
{
    ASSERT(data && Math::IsAligned(data, 4));
    AllocSpan span = mCpuLinearAllocator.Allocate(numBytes, 4);
    CopyMemory(span.mCpuAddress, data, numBytes);
    CopyBufferRegion(dest, destOffset, span.mPage, span.mOffset, numBytes);
}

void CopyCommandList::FillBuffer(GpuResource& dest, size_t destOffset, DWParam value, size_t numBytes)
{
    AllocSpan span = mCpuLinearAllocator.Allocate(numBytes, 4);
    FillMemory(span.mCpuAddress, value.Float, numBytes);
    CopyBufferRegion(dest, destOffset, span.mPage, span.mOffset, numBytes);
}

void CopyCommandList::ReadbackTexture(ReadbackBuffer& dstBuffer, PixelBuffer& srcBuffer, D3D12_PLACED_SUBRESOURCE_FOOTPRINT& placedFootprint)
{
    //uint64_t copySize = 0;

    //// The footprint may depend on the device of the resource, but we assume there is only one device.
    //D3D12_PLACED_SUBRESOURCE_FOOTPRINT placedFootprint;
    //Graphics::gDevice->GetCopyableFootprints(make_rvalue_ptr(srcBuffer.GetResource()->GetDesc()), 0, 1, 0,
    //    &placedFootprint, nullptr, nullptr, &copySize);

    //dstBuffer.Create(L"Readback", (uint32_t)copySize, 1);

    TransitionResource(srcBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, true);

    mCommandList->CopyTextureRegion(
        make_rvalue_ptr(CD3DX12_TEXTURE_COPY_LOCATION(dstBuffer.GetResource(), placedFootprint)), 0, 0, 0,
        make_rvalue_ptr(CD3DX12_TEXTURE_COPY_LOCATION(srcBuffer.GetResource(), 0)), nullptr);

    //return placedFootprint.Footprint.RowPitch;
}


// -- ComputeCommandList --
void ComputeCommandList::ClearUAV(GpuBuffer& target)
{
    FlushResourceBarriers();

    const UINT clearColor[4] = {};
    mCommandList->ClearUnorderedAccessViewUint(target.GetUAV(), target.GetUAV(), target.GetResource(), clearColor, 0, nullptr);
}

void ComputeCommandList::ClearUAV(ColorBuffer& target)
{
    FlushResourceBarriers();

    CD3DX12_RECT clearRect(0, 0, (LONG)target.GetWidth(), (LONG)target.GetHeight());
    const float* clearColor = target.GetClearColor().GetPtr();
    mCommandList->ClearUnorderedAccessViewFloat(target.GetUAV(), target.GetUAV(), target.GetResource(), clearColor, 1, &clearRect);
}

void ComputeCommandList::SetConstantArray(UINT rootIndex, UINT numConstants, const void* pConstants)
{
    mCommandList->SetComputeRoot32BitConstants(rootIndex, numConstants, pConstants, 0);
}

void ComputeCommandList::SetConstant(UINT rootIndex, UINT offset, DWParam val)
{
    mCommandList->SetComputeRoot32BitConstant(rootIndex, val.Uint, offset);
}

void ComputeCommandList::SetConstants(UINT rootIndex, DWParam x)
{
    mCommandList->SetComputeRoot32BitConstant(rootIndex, x.Uint, 0);
}

void ComputeCommandList::SetConstants(UINT rootIndex, DWParam x, DWParam y)
{
    mCommandList->SetComputeRoot32BitConstant(rootIndex, x.Uint, 0);
    mCommandList->SetComputeRoot32BitConstant(rootIndex, y.Uint, 1);
}

void ComputeCommandList::SetConstants(UINT rootIndex, DWParam x, DWParam y, DWParam z)
{
    mCommandList->SetComputeRoot32BitConstant(rootIndex, x.Uint, 0);
    mCommandList->SetComputeRoot32BitConstant(rootIndex, y.Uint, 1);
    mCommandList->SetComputeRoot32BitConstant(rootIndex, z.Uint, 2);
}

void ComputeCommandList::SetConstants(UINT rootIndex, DWParam x, DWParam y, DWParam z, DWParam w)
{
    mCommandList->SetComputeRoot32BitConstant(rootIndex, x.Uint, 0);
    mCommandList->SetComputeRoot32BitConstant(rootIndex, y.Uint, 1);
    mCommandList->SetComputeRoot32BitConstant(rootIndex, z.Uint, 2);
    mCommandList->SetComputeRoot32BitConstant(rootIndex, w.Uint, 3);
}

void ComputeCommandList::SetConstantBuffer(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS cbv)
{
    mCommandList->SetComputeRootConstantBufferView(rootIndex, cbv);
}

void ComputeCommandList::SetDynamicConstantBufferView(UINT rootIndex, size_t bufferSize, const void* bufferData)
{
    ASSERT(bufferData);
    AllocSpan span = mCpuLinearAllocator.Allocate(bufferSize, 256);
    CopyMemory(span.mCpuAddress, bufferData, bufferSize);
    mCommandList->SetComputeRootConstantBufferView(rootIndex, span.mGpuAddress);
}

void ComputeCommandList::SetDynamicSRV(UINT rootIndex, size_t bufferSize, const void* bufferData)
{
    ASSERT(bufferData);
    AllocSpan span = mCpuLinearAllocator.Allocate(bufferSize);
    CopyMemory(span.mCpuAddress, bufferData, bufferSize);
    mCommandList->SetComputeRootShaderResourceView(rootIndex, span.mGpuAddress);
}

void ComputeCommandList::SetBufferSRV(UINT rootIndex, const GpuBuffer& srv, UINT64 offset)
{
    D3D12_RESOURCE_STATES usageState = GetResourceStateCache(const_cast<GpuBuffer&>(srv)).mStateCurrent;
    ASSERT((usageState & (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) != 0);
    mCommandList->SetComputeRootShaderResourceView(rootIndex, srv.GetGpuVirtualAddress() + offset);
}

void ComputeCommandList::SetBufferUAV(UINT rootIndex, const GpuBuffer& uav, UINT64 offset)
{
    D3D12_RESOURCE_STATES usageState = GetResourceStateCache(const_cast<GpuBuffer&>(uav)).mStateCurrent;
    ASSERT((usageState & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) != 0);
    mCommandList->SetComputeRootUnorderedAccessView(rootIndex, uav.GetGpuVirtualAddress() + offset);
}

void ComputeCommandList::SetRootSignature(const RootSignature& rootSig)
{
    if (rootSig.GetRootSignature() == mCurGraphicsRootSignature)
        return;

    mCommandList->SetComputeRootSignature(mCurGraphicsRootSignature = rootSig.GetRootSignature());
}

void ComputeCommandList::SetPipelineState(const PipelineState& pso)
{
    SetRootSignature(pso.GetRootSignature());
    CommandList::SetPipelineState(pso);
}

void ComputeCommandList::SetDescriptorTable(UINT rootIndex, const DescriptorHandle& firstHandle)
{
    SetDescriptorTable(rootIndex, 0, firstHandle);
}

void ComputeCommandList::SetDescriptorTable(UINT rootIndex, UINT offset, const DescriptorHandle& handle)
{
    SetDescriptorHeap(handle.GetType(), handle.GetDescriptorHeap());
    mCommandList->SetComputeRootDescriptorTable(rootIndex, handle + offset);
}

void ComputeCommandList::Dispatch(size_t groupCountX, size_t groupCountY, size_t groupCountZ)
{
    FlushResourceBarriers();
    mCommandList->Dispatch((UINT)groupCountX, (UINT)groupCountY, (UINT)groupCountZ);
}

void ComputeCommandList::Dispatch1D(size_t threadCountX, size_t groupSizeX)
{
    Dispatch(Math::DivideByMultiple(threadCountX, groupSizeX), 1, 1);
}

void ComputeCommandList::Dispatch2D(size_t threadCountX, size_t threadCountY, size_t groupSizeX, size_t groupSizeY)
{
    Dispatch(Math::DivideByMultiple(threadCountX, groupSizeX), Math::DivideByMultiple(threadCountY, groupSizeY), 1);
}

void ComputeCommandList::Dispatch3D(
    size_t threadCountX, size_t threadCountY, size_t threadCountZ, size_t groupSizeX, size_t groupSizeY, size_t groupSizeZ)
{
    Dispatch(Math::DivideByMultiple(threadCountX, groupSizeX), 
        Math::DivideByMultiple(threadCountY, groupSizeY), 
        Math::DivideByMultiple(threadCountZ, groupSizeZ));
}


// -- GraphicsCommandList --
void GraphicsCommandList::ClearUAV(GpuBuffer& target)
{
    FlushResourceBarriers();

    const UINT clearColor[4] = {};
    mCommandList->ClearUnorderedAccessViewUint(target.GetUAV(), target.GetUAV(), target.GetResource(), clearColor, 0, nullptr);
}

void GraphicsCommandList::ClearUAV(ColorBuffer& target)
{
    FlushResourceBarriers();

    CD3DX12_RECT clearRect(0, 0, (LONG)target.GetWidth(), (LONG)target.GetHeight());
    const float* clearColor = target.GetClearColor().GetPtr();
    mCommandList->ClearUnorderedAccessViewFloat(target.GetUAV(), target.GetUAV(), target.GetResource(), clearColor, 1, &clearRect);
}

void GraphicsCommandList::ClearColor(ColorBuffer& target, D3D12_RECT* rect)
{
    FlushResourceBarriers();
    mCommandList->ClearRenderTargetView(target.GetRTV(), target.GetClearColor().GetPtr(), (rect == nullptr) ? 0 : 1, rect);
}

void GraphicsCommandList::ClearColor(ColorBuffer& target, float color[4], D3D12_RECT* rect)
{
    FlushResourceBarriers();
    mCommandList->ClearRenderTargetView(target.GetRTV(), color, (rect == nullptr) ? 0 : 1, rect);
}

void GraphicsCommandList::ClearDepth(DepthBuffer& target)
{
    FlushResourceBarriers();
    mCommandList->ClearDepthStencilView(
        target.GetDSV(), D3D12_CLEAR_FLAG_DEPTH, target.GetClearDepth(), target.GetClearStencil(), 0, nullptr);
}

void GraphicsCommandList::ClearStencil(DepthBuffer& target)
{
    FlushResourceBarriers();
    mCommandList->ClearDepthStencilView(
        target.GetDSV(), D3D12_CLEAR_FLAG_STENCIL, target.GetClearDepth(), target.GetClearStencil(), 0, nullptr);
}

void GraphicsCommandList::ClearDepthAndStencil(DepthBuffer& target)
{
    FlushResourceBarriers();
    mCommandList->ClearDepthStencilView(
        target.GetDSV(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, target.GetClearDepth(), target.GetClearStencil(), 0, nullptr);
}

void GraphicsCommandList::BeginQuery(ID3D12QueryHeap* queryHeap, D3D12_QUERY_TYPE type, UINT heapIndex)
{
    mCommandList->BeginQuery(queryHeap, type, heapIndex);
}

void GraphicsCommandList::EndQuery(ID3D12QueryHeap* queryHeap, D3D12_QUERY_TYPE type, UINT heapIndex)
{
    mCommandList->EndQuery(queryHeap, type, heapIndex);
}

void GraphicsCommandList::ResolveQueryData(
    ID3D12QueryHeap* queryHeap, D3D12_QUERY_TYPE type, UINT startIndex, UINT numQueries, ID3D12Resource* destinationBuffer, UINT64 destinationBufferOffset)
{
    mCommandList->ResolveQueryData(queryHeap, type, startIndex, numQueries, destinationBuffer, destinationBufferOffset);
}

void GraphicsCommandList::SetRenderTargets(UINT numRTVs, const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[])
{
    mCommandList->OMSetRenderTargets(numRTVs, rtvs, FALSE, nullptr);
}

void GraphicsCommandList::SetRenderTargets(UINT numRTVs, const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[], D3D12_CPU_DESCRIPTOR_HANDLE dsv)
{
    mCommandList->OMSetRenderTargets(numRTVs, rtvs, FALSE, &dsv);
}

void GraphicsCommandList::SetViewport(const D3D12_VIEWPORT& vp)
{
    mCommandList->RSSetViewports(1, &vp);
}

void GraphicsCommandList::SetViewport(FLOAT x, FLOAT y, FLOAT w, FLOAT h, FLOAT minDepth, FLOAT maxDepth)
{
    D3D12_VIEWPORT vp;
    vp.Width = w;
    vp.Height = h;
    vp.MinDepth = minDepth;
    vp.MaxDepth = maxDepth;
    vp.TopLeftX = x;
    vp.TopLeftY = y;
    mCommandList->RSSetViewports(1, &vp);
}

void GraphicsCommandList::SetScissor(const D3D12_RECT& rect)
{
    ASSERT(rect.left < rect.right&& rect.top < rect.bottom);
    mCommandList->RSSetScissorRects(1, &rect);
}

void GraphicsCommandList::SetScissor(UINT left, UINT top, UINT right, UINT bottom)
{
    SetScissor(CD3DX12_RECT(left, top, right, bottom));
}

void GraphicsCommandList::SetViewportAndScissor(const D3D12_VIEWPORT& vp, const D3D12_RECT& rect)
{
    SetViewport(vp);
    SetScissor(rect);
}

void GraphicsCommandList::SetViewportAndScissor(UINT x, UINT y, UINT w, UINT h)
{
    SetViewport((float)x, (float)y, (float)w, (float)h);
    SetScissor(x, y, x + w, y + h);
}

void GraphicsCommandList::SetStencilRef(UINT stencilRef)
{
    mCommandList->OMSetStencilRef(stencilRef);
}

void GraphicsCommandList::SetBlendFactor(Color blendFactor)
{
    mCommandList->OMSetBlendFactor(blendFactor.GetPtr());
}

void GraphicsCommandList::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology)
{
    mCommandList->IASetPrimitiveTopology(topology);
}

void GraphicsCommandList::SetConstantArray(UINT rootIndex, UINT numConstants, const void* pConstants)
{
    mCommandList->SetGraphicsRoot32BitConstants(rootIndex, numConstants, pConstants, 0);
}

void GraphicsCommandList::SetConstant(UINT rootIndex, UINT offset, DWParam val)
{
    mCommandList->SetGraphicsRoot32BitConstant(rootIndex, val.Uint, offset);
}

void GraphicsCommandList::SetConstants(UINT rootIndex, DWParam x)
{
    mCommandList->SetGraphicsRoot32BitConstant(rootIndex, x.Uint, 0);
}

void GraphicsCommandList::SetConstants(UINT rootIndex, DWParam x, DWParam y)
{
    mCommandList->SetGraphicsRoot32BitConstant(rootIndex, x.Uint, 0);
    mCommandList->SetGraphicsRoot32BitConstant(rootIndex, y.Uint, 1);
}

void GraphicsCommandList::SetConstants(UINT rootIndex, DWParam x, DWParam y, DWParam z)
{
    mCommandList->SetGraphicsRoot32BitConstant(rootIndex, x.Uint, 0);
    mCommandList->SetGraphicsRoot32BitConstant(rootIndex, y.Uint, 1);
    mCommandList->SetGraphicsRoot32BitConstant(rootIndex, z.Uint, 2);
}

void GraphicsCommandList::SetConstants(UINT rootIndex, DWParam x, DWParam y, DWParam z, DWParam w)
{
    mCommandList->SetGraphicsRoot32BitConstant(rootIndex, x.Uint, 0);
    mCommandList->SetGraphicsRoot32BitConstant(rootIndex, y.Uint, 1);
    mCommandList->SetGraphicsRoot32BitConstant(rootIndex, z.Uint, 2);
    mCommandList->SetGraphicsRoot32BitConstant(rootIndex, w.Uint, 3);
}

void GraphicsCommandList::SetConstantBuffer(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS cbv)
{
    mCommandList->SetGraphicsRootConstantBufferView(rootIndex, cbv);
}

void GraphicsCommandList::SetDynamicConstantBufferView(UINT rootIndex, size_t bufferSize, const void* bufferData)
{
    ASSERT(bufferData);
    AllocSpan span = mCpuLinearAllocator.Allocate(bufferSize, 256);
    CopyMemory(span.mCpuAddress, bufferData, bufferSize);
    mCommandList->SetGraphicsRootConstantBufferView(rootIndex, span.mGpuAddress);
}

void GraphicsCommandList::SetBufferSRV(UINT rootIndex, const GpuBuffer& srv, UINT64 offset)
{
    D3D12_RESOURCE_STATES usageState = GetResourceStateCache(const_cast<GpuBuffer&>(srv)).mStateCurrent;
    ASSERT((usageState & (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) != 0);
    mCommandList->SetGraphicsRootShaderResourceView(rootIndex, srv.GetGpuVirtualAddress() + offset);
}

void GraphicsCommandList::SetBufferUAV(UINT rootIndex, const GpuBuffer& uav, UINT64 offset)
{
    D3D12_RESOURCE_STATES usageState = GetResourceStateCache(const_cast<GpuBuffer&>(uav)).mStateCurrent;
    ASSERT((usageState & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) != 0);
    mCommandList->SetGraphicsRootUnorderedAccessView(rootIndex, uav.GetGpuVirtualAddress() + offset);
}

void GraphicsCommandList::SetRootSignature(const RootSignature& rootSig)
{
    if (rootSig.GetRootSignature() == mCurGraphicsRootSignature)
        return;

    mCommandList->SetGraphicsRootSignature(mCurGraphicsRootSignature = rootSig.GetRootSignature());
}

void GraphicsCommandList::SetPipelineState(const PipelineState& pso)
{
    SetRootSignature(pso.GetRootSignature());
    CommandList::SetPipelineState(pso);
}

void GraphicsCommandList::SetDescriptorTable(UINT rootIndex, UINT offset, const DescriptorHandle& handle)
{
    SetDescriptorHeap(handle.GetType(), handle.GetDescriptorHeap());
    mCommandList->SetGraphicsRootDescriptorTable(rootIndex, handle + offset);
}

void GraphicsCommandList::SetDescriptorTable(UINT rootIndex, const DescriptorHandle& firstHandle)
{
    SetDescriptorTable(rootIndex, 0, firstHandle);
}

void GraphicsCommandList::SetDescriptorTable(UINT rootIndex, D3D12_GPU_DESCRIPTOR_HANDLE handle)
{
    mCommandList->SetGraphicsRootDescriptorTable(rootIndex, handle);
}

void GraphicsCommandList::SetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW& ibView)
{
    mCommandList->IASetIndexBuffer(&ibView);
}

void GraphicsCommandList::SetVertexBuffer(UINT slot, const D3D12_VERTEX_BUFFER_VIEW& vbView)
{
    SetVertexBuffers(slot, 1, &vbView);
}

void GraphicsCommandList::SetVertexBuffers(UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW vbViews[])
{
    mCommandList->IASetVertexBuffers(startSlot, count, vbViews);
}

void GraphicsCommandList::SetDynamicVB(UINT slot, size_t numVertices, size_t vertexStride, const void* vbData)
{
    ASSERT(vbData != nullptr);

    size_t bufferSize = numVertices * vertexStride;
    AllocSpan span = mCpuLinearAllocator.Allocate(bufferSize);

    CopyMemory(span.mCpuAddress, vbData, bufferSize);

    D3D12_VERTEX_BUFFER_VIEW vbView;
    vbView.BufferLocation = span.mGpuAddress;
    vbView.SizeInBytes = (UINT)bufferSize;
    vbView.StrideInBytes = (UINT)vertexStride;

    mCommandList->IASetVertexBuffers(slot, 1, &vbView);
}

void GraphicsCommandList::SetDynamicIB(size_t indexCount, const uint16_t* ibData)
{
    ASSERT(ibData != nullptr);

    size_t bufferSize = indexCount * sizeof(uint16_t);
    AllocSpan span = mCpuLinearAllocator.Allocate(bufferSize);

    CopyMemory(span.mCpuAddress, ibData, bufferSize);

    D3D12_INDEX_BUFFER_VIEW ibView;
    ibView.BufferLocation = span.mGpuAddress;
    ibView.SizeInBytes = (UINT)bufferSize;
    ibView.Format = DXGI_FORMAT_R16_UINT;

    mCommandList->IASetIndexBuffer(&ibView);
}

void GraphicsCommandList::SetDynamicSRV(UINT rootIndex, size_t bufferSize, const void* bufferData)
{
    ASSERT(bufferData);
    AllocSpan span = mCpuLinearAllocator.Allocate(bufferSize);
    CopyMemory(span.mCpuAddress, bufferData, bufferSize);
    mCommandList->SetGraphicsRootShaderResourceView(rootIndex, span.mGpuAddress);
}

void GraphicsCommandList::Draw(UINT vertexCount, UINT vertexStartOffset)
{
    DrawInstanced(vertexCount, 1, vertexStartOffset, 0);
}

void GraphicsCommandList::DrawIndexed(UINT indexCount, UINT startIndexLocation, INT baseVertexLocation)
{
    DrawIndexedInstanced(indexCount, 1, startIndexLocation, baseVertexLocation, 0);
}

void GraphicsCommandList::DrawInstanced(
    UINT vertexCountPerInstance, UINT instanceCount, UINT startVertexLocation, UINT startInstanceLocation)
{
    FlushResourceBarriers();
    mCommandList->DrawInstanced(vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
}

void GraphicsCommandList::DrawIndexedInstanced(
    UINT indexCountPerInstance, UINT instanceCount, UINT startIndexLocation, INT baseVertexLocation, UINT startInstanceLocation)
{
    FlushResourceBarriers();
    mCommandList->DrawIndexedInstanced(
        indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);
}
