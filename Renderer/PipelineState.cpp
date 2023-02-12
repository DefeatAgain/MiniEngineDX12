#include "PipelineState.h"
#include "Graphics.h"
#include "RootSignature.h"
#include "Utils/ThreadPoolExecutor.h"

namespace
{
	std::queue<std::future<void>> sInitPipeStatTasks;
	bool sIsFirstInitPipeStatMgr = false;
}


// --------------------------------------------------- GraphicsPipelineState --------------------------------------------------- 
GraphicsPipelineState::GraphicsPipelineState(std::wstring name): 
	PipelineState(name)
{
	ZeroMemory(&mPipelineStateDesc, sizeof(mPipelineStateDesc));
	mPipelineStateDesc.SampleMask = 0xFFFFFFFFu;
	mPipelineStateDesc.SampleDesc.Count = 1;
	mPipelineStateDesc.InputLayout.NumElements = 0;
}

void GraphicsPipelineState::SetBlendState(const D3D12_BLEND_DESC& blendDesc)
{
	mPipelineStateDesc.BlendState = blendDesc;
}

void GraphicsPipelineState::SetRasterizerState(const D3D12_RASTERIZER_DESC& rasterizerDesc)
{
	mPipelineStateDesc.RasterizerState = rasterizerDesc;
}

void GraphicsPipelineState::SetDepthStencilState(const D3D12_DEPTH_STENCIL_DESC& depthStencilDesc)
{
	mPipelineStateDesc.DepthStencilState = depthStencilDesc;
}

void GraphicsPipelineState::SetSampleMask(UINT sampleMask)
{
	mPipelineStateDesc.SampleMask = sampleMask;
}

void GraphicsPipelineState::SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType)
{
	ASSERT(topologyType != D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED, "Can't draw with undefined topology");
	mPipelineStateDesc.PrimitiveTopologyType = topologyType;
}

void GraphicsPipelineState::SetDepthTargetFormat(DXGI_FORMAT dsvFormat, UINT msaaCount, UINT msaaQuality)
{
	SetRenderTargetFormats(0, nullptr, dsvFormat, msaaCount, msaaQuality);
}

void GraphicsPipelineState::SetRenderTargetFormat(DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, UINT msaaCount, UINT msaaQuality)
{
	SetRenderTargetFormats(1, &rtvFormat, dsvFormat, msaaCount, msaaQuality);
}

void GraphicsPipelineState::SetRenderTargetFormats(UINT numRTVs, const DXGI_FORMAT* rtvFormats, DXGI_FORMAT dsvFormat, UINT msaaCount, UINT msaaQuality)
{
	ASSERT(numRTVs == 0 || rtvFormats != nullptr, "Null format array conflicts with non-zero length");
	for (UINT i = 0; i < numRTVs; ++i)
	{
		ASSERT(rtvFormats[i] != DXGI_FORMAT_UNKNOWN);
		mPipelineStateDesc.RTVFormats[i] = rtvFormats[i];
	}
	for (UINT i = numRTVs; i < mPipelineStateDesc.NumRenderTargets; ++i)
		mPipelineStateDesc.RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
	mPipelineStateDesc.NumRenderTargets = numRTVs;
	mPipelineStateDesc.DSVFormat = dsvFormat;
	mPipelineStateDesc.SampleDesc.Count = msaaCount;
	mPipelineStateDesc.SampleDesc.Quality = msaaQuality;
}

void GraphicsPipelineState::SetInputLayout(size_t numDescs, D3D12_INPUT_ELEMENT_DESC pInputElementDescs[])
{
	mPipelineStateDesc.InputLayout.NumElements = numDescs;

	if (numDescs > 0)
	{
		mInputLayouts = std::make_unique<D3D12_INPUT_ELEMENT_DESC[]>(numDescs);
		CopyMemory(mInputLayouts.get(), pInputElementDescs, numDescs * sizeof(D3D12_INPUT_ELEMENT_DESC));
		mPipelineStateDesc.InputLayout.pInputElementDescs = mInputLayouts.get();
	}
	else
		mInputLayouts = nullptr;
}

void GraphicsPipelineState::SetPrimitiveRestart(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE ibProps)
{
	mPipelineStateDesc.IBStripCutValue = ibProps;
}

void GraphicsPipelineState::SetVertexShader(const ShaderUnit& shaderUnit)
{
	SetVertexShader(shaderUnit.GetBlob()->GetBufferPointer(), shaderUnit.GetBlob()->GetBufferSize());
}

void GraphicsPipelineState::SetPixelShader(const ShaderUnit& shaderUnit)
{
	SetPixelShader(shaderUnit.GetBlob()->GetBufferPointer(), shaderUnit.GetBlob()->GetBufferSize());
}

void GraphicsPipelineState::SetGeometryShader(const ShaderUnit& shaderUnit)
{
	SetGeometryShader(shaderUnit.GetBlob()->GetBufferPointer(), shaderUnit.GetBlob()->GetBufferSize());
}

void GraphicsPipelineState::SetHullShader(const ShaderUnit& shaderUnit)
{
	SetHullShader(shaderUnit.GetBlob()->GetBufferPointer(), shaderUnit.GetBlob()->GetBufferSize());
}

void GraphicsPipelineState::SetDomainShader(const ShaderUnit& shaderUnit)
{
	SetDomainShader(shaderUnit.GetBlob()->GetBufferPointer(), shaderUnit.GetBlob()->GetBufferSize());
}

void GraphicsPipelineState::Finalize()
{
	// Make sure the root signature is finalized first
	mPipelineStateDesc.pRootSignature = mRootSignature->GetRootSignature();
	ASSERT(mPipelineStateDesc.pRootSignature != nullptr);

	auto initTask = [this]() {
		ASSERT(mPipelineStateDesc.DepthStencilState.DepthEnable != (mPipelineStateDesc.DSVFormat == DXGI_FORMAT_UNKNOWN));
		CheckHR(Graphics::gDevice->CreateGraphicsPipelineState(&mPipelineStateDesc, IID_PPV_ARGS(mPipelineState.GetAddressOf())));
		mPipelineState->SetName(mName.c_str());
	};
	if (!sIsFirstInitPipeStatMgr)
		sInitPipeStatTasks.emplace(Utility::gThreadPoolExecutor.Submit(initTask));
	else
		initTask();
}

// --------------------------------------------------- ComputePipelineState --------------------------------------------------- 
ComputePipelineState::ComputePipelineState(std::wstring name) :
	PipelineState(name)
{
	ZeroMemory(&mPipelineStateDesc, sizeof(mPipelineStateDesc));
}

void ComputePipelineState::SetComputeShader(const ShaderUnit& shaderUnit)
{
	SetComputeShader(shaderUnit.GetBlob()->GetBufferPointer(), shaderUnit.GetBlob()->GetBufferSize());
}

void ComputePipelineState::Finalize()
{
	// Make sure the root signature is finalized first
	mPipelineStateDesc.pRootSignature = mRootSignature->GetRootSignature();
	ASSERT(mPipelineStateDesc.pRootSignature != nullptr);

	auto initTask = [this]() {
		CheckHR(Graphics::gDevice->CreateComputePipelineState(&mPipelineStateDesc, IID_PPV_ARGS(mPipelineState.GetAddressOf())));
		mPipelineState->SetName(mName.c_str());
	};
	if (!sIsFirstInitPipeStatMgr)
		sInitPipeStatTasks.emplace(Utility::gThreadPoolExecutor.Submit(initTask));
	else
		initTask();
}


// --------------------------------------------------- PipeLineStateManager --------------------------------------------------- 
void PipeLineStateManager::InitAllPipeLineStates()
{
	while (!sInitPipeStatTasks.empty())
	{
		sInitPipeStatTasks.pop();
	}

	sIsFirstInitPipeStatMgr = true;
}
