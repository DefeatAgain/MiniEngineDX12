#include "GpuBuffer.h"
#include "Graphics.h"

void GpuBuffer::Destroy()
{
	GpuResource::Destroy();

	mBufferSize = mElementSize = mElementCount = 0;

	if (mUAV)
		Graphics::DeAllocateDescriptor(mUAV, 1);
	if (mSRV)
		Graphics::DeAllocateDescriptor(mSRV, 1);
}

void GpuBuffer::Create(const std::wstring& name, uint32_t numElements, uint32_t elementSize, const void* initialData)
{
	Destroy();

	mElementCount = numElements;
	mElementSize = elementSize;
	mBufferSize = numElements * elementSize;

	D3D12_RESOURCE_DESC resourceDesc = DescribeBuffer();

	mUsageState = D3D12_RESOURCE_STATE_COMMON;

	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
	CheckHR(Graphics::gDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
		&resourceDesc, mUsageState, nullptr, IID_PPV_ARGS(mResource.GetAddressOf())));

	mGpuVirtualAddress = mResource->GetGPUVirtualAddress();

	if (initialData)
		PushGraphicsTaskSync(&GpuBuffer::InitBufferTask, this, initialData, mBufferSize);

#ifndef RELEASE
	mResource->SetName(name.c_str());
#else
	(name);
#endif

	CreateDerivedViews();
}

void GpuBuffer::Create(const std::wstring& name, uint32_t numElements, uint32_t elementSize, const UploadBuffer& srcData, uint32_t srcOffset)
{
	Destroy();

	mElementCount = numElements;
	mElementSize = elementSize;
	mBufferSize = numElements * elementSize;

	D3D12_RESOURCE_DESC resourceDesc = DescribeBuffer();

	mUsageState = D3D12_RESOURCE_STATE_COMMON;

	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
	CheckHR(Graphics::gDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
		&resourceDesc, mUsageState, nullptr, IID_PPV_ARGS(mResource.GetAddressOf())));

	mGpuVirtualAddress = mResource->GetGPUVirtualAddress();

	PushGraphicsTaskSync(&GpuBuffer::InitBufferUploadTask, this, srcData, srcOffset);
#ifndef RELEASE
	mResource->SetName(name.c_str());
#else
	(name);
#endif

	CreateDerivedViews();
}

CommandList* GpuBuffer::InitBufferTask(CommandList* commandList, const void* data, size_t numBytes)
{
	CopyCommandList& copyList = commandList->GetCopyCommandList().Begin(L"Buffer Inital");
	copyList.InitializeBuffer(*this, data, numBytes);
	copyList.Finish();
	return commandList;
}

CommandList* GpuBuffer::InitBufferUploadTask(CommandList* commandList, const UploadBuffer& src, size_t srcOffset)
{
	CopyCommandList& copyList = commandList->GetCopyCommandList().Begin(L"Buffer Inital");
	copyList.InitializeBuffer(*this, src, srcOffset);
	copyList.Finish();
	return commandList;
}

DescriptorHandle GpuBuffer::CreateConstantBufferView(uint32_t offset, uint32_t size) const
{
	ASSERT(offset + size <= mBufferSize);

	size = Math::AlignUp(size, 16);

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
	cbvDesc.BufferLocation = mGpuVirtualAddress + (size_t)offset;
	cbvDesc.SizeInBytes = size;

	DescriptorHandle hCBV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	Graphics::gDevice->CreateConstantBufferView(&cbvDesc, hCBV);
	return hCBV;
}

D3D12_VERTEX_BUFFER_VIEW GpuBuffer::VertexBufferView(size_t offset, uint32_t size, uint32_t stride) const
{
	D3D12_VERTEX_BUFFER_VIEW VBView;
	VBView.BufferLocation = mGpuVirtualAddress + offset;
	VBView.SizeInBytes = size;
	VBView.StrideInBytes = stride;
	return VBView;
}

D3D12_VERTEX_BUFFER_VIEW GpuBuffer::VertexBufferView(size_t baseVertexIndex) const
{
	size_t Offset = baseVertexIndex * mElementSize;
	return VertexBufferView(Offset, (uint32_t)(mBufferSize - Offset), mElementSize);
}

D3D12_INDEX_BUFFER_VIEW GpuBuffer::IndexBufferView(size_t offset, uint32_t size, bool b32Bit) const
{
	D3D12_INDEX_BUFFER_VIEW IBView;
	IBView.BufferLocation = mGpuVirtualAddress + offset;
	IBView.Format = b32Bit ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
	IBView.SizeInBytes = size;
	return IBView;
}

D3D12_INDEX_BUFFER_VIEW GpuBuffer::IndexBufferView(size_t StartIndex) const
{
	size_t Offset = StartIndex * mElementSize;
	return IndexBufferView(Offset, (uint32_t)(mBufferSize - Offset), mElementSize == 4);
}

D3D12_RESOURCE_DESC GpuBuffer::DescribeBuffer()
{
	ASSERT(mBufferSize != 0);

	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Flags = mResourceFlags;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.Height = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Width = (UINT64)mBufferSize;
	return desc;
}


// -- ByteAddressBuffer --
void ByteAddressBuffer::CreateDerivedViews()
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Buffer.NumElements = (UINT)mBufferSize / 4;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;

	if (!mSRV)
		mSRV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	Graphics::gDevice->CreateShaderResourceView(mResource.Get(), &srvDesc, mSRV);

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	uavDesc.Buffer.NumElements = (UINT)mBufferSize / 4;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

	if (!mUAV)
		mUAV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	Graphics::gDevice->CreateUnorderedAccessView(mResource.Get(), nullptr, &uavDesc, mUAV);
}


// -- StructuredBuffer --
void StructuredBuffer::CreateDerivedViews()
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Buffer.NumElements = mElementCount;
	srvDesc.Buffer.StructureByteStride = mElementSize;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	if (!mSRV)
		mSRV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	Graphics::gDevice->CreateShaderResourceView(mResource.Get(), &srvDesc, mSRV);

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.Buffer.CounterOffsetInBytes = 0;
	uavDesc.Buffer.NumElements = mElementCount;
	uavDesc.Buffer.StructureByteStride = mElementSize;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	mCounterBuffer.Create(L"StructuredBuffer::Counter", 1, 4);

	if (!mUAV)
		mUAV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	Graphics::gDevice->CreateUnorderedAccessView(mResource.Get(), mCounterBuffer.GetResource(), &uavDesc, mUAV);
}

const DescriptorHandle& StructuredBuffer::GetCounterSRV(CommandList& commandList)
{
	commandList.TransitionResource(mCounterBuffer, D3D12_RESOURCE_STATE_GENERIC_READ);
	return mCounterBuffer.GetSRV();
}

const DescriptorHandle& StructuredBuffer::GetCounterUAV(CommandList& commandList)
{
	commandList.TransitionResource(mCounterBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	return mCounterBuffer.GetUAV();
}


// -- TypedBuffer --
void TypedBuffer::CreateDerivedViews()
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Format = mDataFormat;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Buffer.NumElements = mElementCount;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	if (!mSRV)
		mSRV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	Graphics::gDevice->CreateShaderResourceView(mResource.Get(), &srvDesc, mSRV);

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uavDesc.Format = mDataFormat;
	uavDesc.Buffer.NumElements = mElementCount;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	if (!mUAV)
		mUAV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	Graphics::gDevice->CreateUnorderedAccessView(mResource.Get(), nullptr, &uavDesc, mUAV);
}


// -- ReadbackBuffer --
void ReadbackBuffer::Create(const std::wstring& name, uint32_t numElements, uint32_t elementSize)
{
	Destroy();

	mElementCount = numElements;
	mElementSize = elementSize;
	mBufferSize = numElements * elementSize;
	mUsageState = D3D12_RESOURCE_STATE_COPY_DEST;

	// Create a readback buffer large enough to hold all texel data
	D3D12_HEAP_PROPERTIES heapProps{};
	heapProps.Type = D3D12_HEAP_TYPE_READBACK;
	heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	// Readback buffers must be 1-dimensional, i.e. "buffer" not "texture2d"
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = mBufferSize;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	CheckHR(Graphics::gDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(mResource.GetAddressOf())));

	mGpuVirtualAddress = mResource->GetGPUVirtualAddress();

#ifndef RELEASE
	mResource->SetName(name.c_str());
#else
	(name);
#endif
}

void* ReadbackBuffer::Map()
{
	void* memory;
	mResource->Map(0, PTR(CD3DX12_RANGE(0, mBufferSize)), &memory);
	return memory;
}

void ReadbackBuffer::Unmap()
{
	mResource->Unmap(0, PTR(CD3DX12_RANGE(0, 0)));
}


// -- UploadBuffer --
void UploadBuffer::Create(const std::wstring& name, size_t bufferSize)
{
	Destroy();

	mBufferSize = bufferSize;

	// Create an upload buffer.  This is CPU-visible, but it's write combined memory, so
	// avoid reading back from it.
	D3D12_HEAP_PROPERTIES heapProps{};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
	heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	// Upload buffers must be 1-dimensional
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = mBufferSize;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	CheckHR(Graphics::gDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(mResource.GetAddressOf())));

	mGpuVirtualAddress = mResource->GetGPUVirtualAddress();

#ifndef RELEASE
	mResource->SetName(name.c_str());
#else
	(name);
#endif
}

void* UploadBuffer::Map()
{
	if (mMappedBuffer)
		return mMappedBuffer;

	mResource->Map(0, make_rvalue_ptr(CD3DX12_RANGE(0, mBufferSize)), &mMappedBuffer);
	return mMappedBuffer;
}

void UploadBuffer::Unmap(size_t begin, size_t end)
{
	mResource->Unmap(0, make_rvalue_ptr(CD3DX12_RANGE(begin, std::min(end, mBufferSize))));
	mMappedBuffer = nullptr;
}
