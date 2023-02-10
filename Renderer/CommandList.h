#pragma once
#include "CoreHeader.h"
#include "Common.h"
#include "Color.h"
#include "Utils/DebugUtils.h"
#include "LinearAllocator2.h"
#include "DescriptorHandle.h"

class GraphicsCommandList;
class ComputeCommandList;
class CppyCommandList;
class GpuResource;
class GpuBuffer;
class StructuredBuffer;
class ReadbackBuffer;
class UploadBuffer;
class PixelBuffer;
class ColorBuffer;
class DepthBuffer;
class RootSignature;
class PipelineState;

struct DWParam
{
    DWParam(FLOAT f) : Float(f) {}
    DWParam(UINT u) : Uint(u) {}
    DWParam(INT i) : Int(i) {}

    void operator= (FLOAT f) { Float = f; }
    void operator= (UINT u) { Uint = u; }
    void operator= (INT i) { Int = i; }

    union
    {
        FLOAT Float;
        UINT Uint;
        INT Int;
    };
};

struct ResourceStateCache
{
    ResourceStateCache(GpuResource& resource) :
        mGpuResource(resource), mStateCurrent(resource.mUsageState), mStateTransition((D3D12_RESOURCE_STATES)-1)
    {}
    ~ResourceStateCache() {}

    GpuResource& mGpuResource;
    D3D12_RESOURCE_STATES mStateCurrent;
    D3D12_RESOURCE_STATES mStateTransition;
};

template<enum D3D12_COMMAND_LIST_TYPE> 
struct CommandListType {};
template<> 
struct CommandListType<D3D12_COMMAND_LIST_TYPE_DIRECT> { using type = GraphicsCommandList; };
template<> 
struct CommandListType<D3D12_COMMAND_LIST_TYPE_COMPUTE> { using type = ComputeCommandList; };
template<> 
struct CommandListType<D3D12_COMMAND_LIST_TYPE_COPY> { using type = CopyCommandList; };


class CommandList : NonCopyable
{
    friend class FrameContextManager;
    friend class FrameContext;
private:
    CommandList* Reset();
public:
    ~CommandList() {}

    // Prepare to render by reset command list and command allocator
    CommandList& Begin(const std::wstring& id = L"");
    void Initialize();
    void Finish();

    void PIXBeginEvent(const wchar_t* label);
    void PIXEndEvent();
    void PIXSetMarker(const wchar_t* label);

    GraphicsCommandList& GetGraphicsCommandList() { return GetCommandList<D3D12_COMMAND_LIST_TYPE_DIRECT>(); }
    ComputeCommandList& GetComputeCommandList() { return GetCommandList<D3D12_COMMAND_LIST_TYPE_COMPUTE>(); }
    CopyCommandList& GetCopyCommandList() { return GetCommandList<D3D12_COMMAND_LIST_TYPE_COPY>(); }
    ID3D12GraphicsCommandList* GetDeviceCommandList() { return mCommandList.Get(); }
    ID3D12GraphicsCommandList** GetDeviceCommandListOf() { return mCommandList.GetAddressOf(); }

    void ExceptResourceBeginState(GpuResource& resource, D3D12_RESOURCE_STATES newState);
    void TransitionResource(GpuResource& resource, D3D12_RESOURCE_STATES newState, bool flushImmediate = false);
    void TransitionResource(ResourceStateCache& curStateCache, ResourceStateCache& newStateCahce);
    void BeginResourceTransition(GpuResource& resource, D3D12_RESOURCE_STATES newState, bool flushImmediate = false);
    void InsertUAVBarrier(GpuResource& resource, bool flushImmediate = false);
    void InsertAliasBarrier(GpuResource& before, GpuResource& after, bool flushImmediate = false);
    void FlushResourceBarriers();
    void UpdateResourceState();

    void SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, ID3D12DescriptorHeap* heapPtr);
    void SetDescriptorHeaps(UINT heapCount, D3D12_DESCRIPTOR_HEAP_TYPE type[], ID3D12DescriptorHeap* heapPtrs[]);
    virtual void SetPipelineState(const PipelineState& pso);

    void SetPredication(ID3D12Resource* resource, UINT64 bufferOffset, D3D12_PREDICATION_OP op);
    void InsertTimeStamp(ID3D12QueryHeap* pQueryHeap, uint32_t queryIdx);
    void ResolveTimeStamps(ID3D12Resource* pReadbackHeap, ID3D12QueryHeap* pQueryHeap, uint32_t numQueries);

    void BeforeCommandListSubmit();
protected:
    CommandList(D3D12_COMMAND_LIST_TYPE type);

    void SetID(const std::wstring& id) { mID = id; }

    void BindDescriptorHeaps();

    ResourceStateCache& GetResourceStateCache(GpuResource& resource);

    template<enum D3D12_COMMAND_LIST_TYPE ListType>
    decltype(auto) GetCommandList()
    {
        ASSERT(mType == ListType, CONSTRA(L"Cannot convert to ", Graphics::QUEUE_TYPE_NAME<ListType>, L"CommandList"));
        return reinterpret_cast<CommandListType<ListType>::type&>(*this);
    }
protected:
    D3D12_COMMAND_LIST_TYPE mType;
    std::wstring mID;

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mCommandAllocator;

    ID3D12RootSignature* mCurGraphicsRootSignature;
    ID3D12RootSignature* mCurComputeRootSignature;
    ID3D12PipelineState* mCurPipelineState;
    ID3D12DescriptorHeap* mCurrentDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

    std::map<size_t, ResourceStateCache> mResourceStateCache;
    D3D12_RESOURCE_BARRIER mResourceBarrierBuffer[16];
    UINT mNumBarriersToFlush;

    LinearAllocator mCpuLinearAllocator;
    LinearAllocator mGpuLinearAllocator;

    size_t mCommandListIndex;
    uint64_t mRetiredFenceValue;
};


class CopyCommandList : public CommandList
{
    friend class FrameContextManager;
protected:
    CopyCommandList() : CommandList(D3D12_COMMAND_LIST_TYPE_COPY) {}
public:
    CopyCommandList& Begin(const std::wstring& id = L"")
    {
        CommandList::Begin(id);
        return *this;
    }

    void CopyBuffer(GpuResource& dest, GpuResource& src);
    void CopyBufferRegion(GpuResource& dest, size_t destOffset, GpuResource& src, size_t srcOffset, size_t numBytes);
    void CopyBuffer(GpuResource& dest, UploadBuffer& src);
    void CopyBufferRegion(GpuResource& dest, size_t destOffset, UploadBuffer& src, size_t srcOffset, size_t numBytes);
    void CopySubresource(GpuResource& dest, UINT destSubIndex, GpuResource& src, UINT srcSubIndex);
    void CopyCounter(GpuResource& dest, size_t destOffset, StructuredBuffer& src);
    void CopyTextureRegion(GpuResource& dest, UINT x, UINT y, UINT z, GpuResource& source, RECT& rect);
    void ResetCounter(StructuredBuffer& buffer, uint32_t value = 0);

    void InitializeTexture(GpuResource& dest, UINT numSubresources, D3D12_SUBRESOURCE_DATA subData[]);
    void InitializeBuffer(GpuBuffer& dest, const void* data, size_t numBytes, size_t destOffset = 0);
    void InitializeBuffer(GpuBuffer& dest, const UploadBuffer& src, size_t srcOffset, size_t numBytes = -1, size_t destOffset = 0);
    void InitializeTextureArraySlice(GpuResource& dest, UINT sliceIndex, GpuResource& src);

    void WriteBuffer(GpuResource& dest, size_t destOffset, const void* data, size_t numBytes);
    void FillBuffer(GpuResource& dest, size_t destOffset, DWParam value, size_t numBytes);

    // Creates a readback buffer of sufficient size, copies the texture into it,
    // and returns row pitch in bytes.
    void ReadbackTexture(ReadbackBuffer& dstBuffer, PixelBuffer& srcBuffer, D3D12_PLACED_SUBRESOURCE_FOOTPRINT& placedFootprint);
};


class GraphicsCommandList : public CommandList
{
    friend class FrameContextManager;
protected:
    GraphicsCommandList() : CommandList(D3D12_COMMAND_LIST_TYPE_DIRECT) {}
public:
    GraphicsCommandList& Begin(const std::wstring& id = L"")
    {
        CommandList::Begin(id);
        return *this;
    }

    void ClearUAV(GpuBuffer& target);
    void ClearUAV(ColorBuffer& target);
    void ClearColor(ColorBuffer& target, D3D12_RECT* rect = nullptr);
    void ClearColor(ColorBuffer& target, float color[4], D3D12_RECT* rect = nullptr);
    void ClearDepth(DepthBuffer& target);
    void ClearStencil(DepthBuffer& target);
    void ClearDepthAndStencil(DepthBuffer& target);

    void BeginQuery(ID3D12QueryHeap* queryHeap, D3D12_QUERY_TYPE type, UINT heapIndex);
    void EndQuery(ID3D12QueryHeap* queryHeap, D3D12_QUERY_TYPE type, UINT heapIndex);
    void ResolveQueryData(ID3D12QueryHeap* queryHeap, D3D12_QUERY_TYPE type, UINT startIndex, UINT numQueries, ID3D12Resource* destinationBuffer, UINT64 destinationBufferOffset);

    void SetRenderTargets(UINT numRTVs, const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[]);
    void SetRenderTargets(UINT numRTVs, const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[], D3D12_CPU_DESCRIPTOR_HANDLE dsv);
    void SetRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv) { SetRenderTargets(1, &rtv); }
    void SetRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv) { SetRenderTargets(1, &rtv, dsv); }
    void SetDepthStencilTarget(D3D12_CPU_DESCRIPTOR_HANDLE dsv) { SetRenderTargets(0, nullptr, dsv); }

    void SetViewport(const D3D12_VIEWPORT& vp);
    void SetViewport(FLOAT x, FLOAT y, FLOAT w, FLOAT h, FLOAT minDepth = 0.0f, FLOAT maxDepth = 1.0f);
    void SetScissor(const D3D12_RECT& rect);
    void SetScissor(UINT left, UINT top, UINT right, UINT bottom);
    void SetViewportAndScissor(const D3D12_VIEWPORT& vp, const D3D12_RECT& rect);
    void SetViewportAndScissor(UINT x, UINT y, UINT w, UINT h);
    void SetStencilRef(UINT stencilRef);
    void SetBlendFactor(Color blendFactor);
    void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology);

    void SetConstantArray(UINT rootIndex, UINT numConstants, const void* pConstants);
    void SetConstant(UINT rootIndex, UINT offset, DWParam val);
    void SetConstants(UINT rootIndex, DWParam x);
    void SetConstants(UINT rootIndex, DWParam x, DWParam y);
    void SetConstants(UINT rootIndex, DWParam x, DWParam y, DWParam z);
    void SetConstants(UINT rootIndex, DWParam x, DWParam y, DWParam z, DWParam w);
    void SetConstantBuffer(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS cbv);
    void SetDynamicConstantBufferView(UINT rootIndex, size_t bufferSize, const void* bufferData);
    void SetBufferSRV(UINT rootIndex, const GpuBuffer& srv, UINT64 offset = 0);
    void SetBufferUAV(UINT rootIndex, const GpuBuffer& uav, UINT64 offset = 0);

    void SetRootSignature(const RootSignature& rootSig);
    void SetPipelineState(const PipelineState& pso);
    void SetDescriptorTable(UINT rootIndex, UINT offset, const DescriptorHandle& handle);
    void SetDescriptorTable(UINT rootIndex, const DescriptorHandle& firstHandle);

    void SetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW& ibView);
    void SetVertexBuffer(UINT slot, const D3D12_VERTEX_BUFFER_VIEW& vbView);
    void SetVertexBuffers(UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW vbViews[]);
    void SetDynamicVB(UINT slot, size_t numVertices, size_t vertexStride, const void* vbData);
    void SetDynamicIB(size_t indexCount, const uint16_t* ibData);
    void SetDynamicSRV(UINT rootIndex, size_t bufferSize, const void* bufferData);

    void Draw(UINT vertexCount, UINT vertexStartOffset = 0);
    void DrawIndexed(UINT indexCount, UINT startIndexLocation = 0, INT baseVertexLocation = 0);
    void DrawInstanced(UINT vertexCountPerInstance, UINT instanceCount, UINT startVertexLocation = 0, UINT startInstanceLocation = 0);
    void DrawIndexedInstanced(UINT indexCountPerInstance, UINT instanceCount, UINT startIndexLocation, INT baseVertexLocation, UINT startInstanceLocation);
    //void DrawIndirect(GpuBuffer& argumentBuffer, uint64_t argumentBufferOffset = 0);
    //void ExecuteIndirect(CommandSignature& commandSig, GpuBuffer& argumentBuffer, uint64_t argumentStartOffset = 0,
    //    uint32_t maxCommands = 1, GpuBuffer* commandCounterBuffer = nullptr, uint64_t counterOffset = 0);
};


class ComputeCommandList : public CommandList
{
    friend class FrameContextManager;
protected:
    ComputeCommandList() : CommandList(D3D12_COMMAND_LIST_TYPE_COMPUTE) {}
public:
    ComputeCommandList& Begin(const std::wstring& id = L"")
    {
        CommandList::Begin(id);
        return *this;
    }

    void ClearUAV(GpuBuffer& target);
    void ClearUAV(ColorBuffer& target);

    void SetConstantArray(UINT rootIndex, UINT numConstants, const void* pConstants);
    void SetConstant(UINT rootIndex, UINT offset, DWParam val);
    void SetConstants(UINT rootIndex, DWParam x);
    void SetConstants(UINT rootIndex, DWParam x, DWParam y);
    void SetConstants(UINT rootIndex, DWParam x, DWParam y, DWParam z);
    void SetConstants(UINT rootIndex, DWParam x, DWParam y, DWParam z, DWParam w);
    void SetConstantBuffer(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS cbv);
    void SetDynamicConstantBufferView(UINT rootIndex, size_t bufferSize, const void* bufferData);
    void SetDynamicSRV(UINT rootIndex, size_t bufferSize, const void* bufferData);
    void SetBufferSRV(UINT rootIndex, const GpuBuffer& srv, UINT64 offset = 0);
    void SetBufferUAV(UINT rootIndex, const GpuBuffer& uav, UINT64 offset = 0);

    void SetRootSignature(const RootSignature& rootSig);
    void SetPipelineState(const PipelineState& pso);
    void SetDescriptorTable(UINT rootIndex, const DescriptorHandle& firstHandle);
    void SetDescriptorTable(UINT rootIndex, UINT offset, const DescriptorHandle& handle);

    void Dispatch(size_t groupCountX = 1, size_t groupCountY = 1, size_t groupCountZ = 1);
    void Dispatch1D(size_t threadCountX, size_t groupSizeX = 64);
    void Dispatch2D(size_t threadCountX, size_t threadCountY, size_t groupSizeX = 8, size_t groupSizeY = 8);
    void Dispatch3D(size_t threadCountX, size_t threadCountY, size_t threadCountZ, size_t groupSizeX, size_t groupSizeY, size_t groupSizeZ);
    //void DispatchIndirect(GpuBuffer& ArgumentBuffer, uint64_t ArgumentBufferOffset = 0);
    //void ExecuteIndirect(CommandSignature& CommandSig, GpuBuffer& ArgumentBuffer, uint64_t ArgumentStartOffset = 0,
        //uint32_t MaxCommands = 1, GpuBuffer* CommandCounterBuffer = nullptr, uint64_t CounterOffset = 0);
};
