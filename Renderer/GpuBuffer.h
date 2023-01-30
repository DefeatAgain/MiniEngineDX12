#pragma once
#include "GpuResource.h"
#include "DescriptorHandle.h"
#include "GraphicsContext.h"

class CommandList;
class UploadBuffer;

class GpuBuffer : public GpuResource, CopyContext
{
public:
    GpuBuffer() : mBufferSize(0), mElementCount(0), mElementSize(0)
    {
        mResourceFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    virtual void Destroy() override;

    // Create a buffer.  If initial data is provided, it will be copied into the buffer using the default command context.
    void Create(const std::wstring& name, uint32_t numElements, uint32_t elementSize, const void* initialData = nullptr);

    void Create(const std::wstring& name, uint32_t numElements, uint32_t elementSize, const UploadBuffer& srcData, uint32_t srcOffset = 0);
    
    CommandList* InitBufferTask(CommandList* commandList, const void* data, size_t numBytes);
    CommandList* InitBufferUploadTask(CommandList* commandList, const UploadBuffer& src, size_t srcOffset);

    const DescriptorHandle& GetUAV() const { return mUAV; }
    const DescriptorHandle& GetSRV() const { return mSRV; }

    D3D12_GPU_VIRTUAL_ADDRESS RootConstantBufferView() const { return mGpuVirtualAddress; }

    DescriptorHandle CreateConstantBufferView(uint32_t offset, uint32_t size) const;

    D3D12_VERTEX_BUFFER_VIEW VertexBufferView(size_t offset, uint32_t size, uint32_t stride) const;
    D3D12_VERTEX_BUFFER_VIEW VertexBufferView(size_t baseVertexIndex = 0) const;

    D3D12_INDEX_BUFFER_VIEW IndexBufferView(size_t offset, uint32_t size, bool b32Bit = false) const;
    D3D12_INDEX_BUFFER_VIEW IndexBufferView(size_t StartIndex = 0) const;

    size_t GetBufferSize() const { return mBufferSize; }
    uint32_t GetElementCount() const { return mElementCount; }
    uint32_t GetElementSize() const { return mElementSize; }
protected:
    D3D12_RESOURCE_DESC DescribeBuffer();
    virtual void CreateDerivedViews() {};

    DescriptorHandle mUAV;
    DescriptorHandle mSRV;

    size_t mBufferSize;
    uint32_t mElementCount;
    uint32_t mElementSize;
    D3D12_RESOURCE_FLAGS mResourceFlags;
};


class ByteAddressBuffer : public GpuBuffer
{
public:
    virtual void CreateDerivedViews() override;
};


class IndirectArgsBuffer : public ByteAddressBuffer
{
};


class StructuredBuffer : public GpuBuffer
{
public:
    virtual void Destroy() override
    {
        mCounterBuffer.Destroy();
        GpuBuffer::Destroy();
    }

    virtual void CreateDerivedViews() override;

    ByteAddressBuffer& GetCounterBuffer() { return mCounterBuffer; }

    const DescriptorHandle& GetCounterSRV(CommandList& commandList);
    const DescriptorHandle& GetCounterUAV(CommandList& commandList);

private:
    ByteAddressBuffer mCounterBuffer;
};


class TypedBuffer : public GpuBuffer
{
public:
    TypedBuffer(DXGI_FORMAT Format) : mDataFormat(Format) {}
    virtual void CreateDerivedViews() override;

protected:
    DXGI_FORMAT mDataFormat;
};


class ReadbackBuffer : public GpuBuffer
{
public:
    virtual ~ReadbackBuffer() { Destroy(); }

    void Create(const std::wstring& name, uint32_t numElements, uint32_t elementSize);

    void* Map();
    void Unmap();
protected:
    void CreateDerivedViews() {}
};


class UploadBuffer : public GpuResource
{
public:
    UploadBuffer() : mMappedBuffer(nullptr), mBufferSize(0) {}
    virtual ~UploadBuffer() { Destroy(); }

    virtual void Destroy() override
    {
        GpuResource::Destroy();
        mBufferSize = 0;
    }

    void Create(const std::wstring& name, size_t bufferSize);

    void* Map();
    void Unmap(size_t begin = 0, size_t end = -1);

    size_t GetBufferSize() const { return mBufferSize; }
protected:
    void* mMappedBuffer;
    size_t mBufferSize;
};
