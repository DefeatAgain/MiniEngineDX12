#pragma once
#include "CoreHeader.h"
#include "Common.h"
#include "Utils/Hash.h"
#include "Utils/DebugUtils.h"
#include "ShaderCompositor.h"

class RootSignature;
class ShaderUnit;

class PipelineState
{
public:
    PipelineState(std::wstring name) : mName(name), mRootSignature(nullptr), mPipelineState(nullptr) {}

    void SetRootSignature(const RootSignature& bindMappings)
    {
        mRootSignature = &bindMappings;
    }

    const RootSignature& GetRootSignature() const 
    {
        ASSERT(mRootSignature != nullptr);
        return *mRootSignature;
    }

    ID3D12PipelineState* GetPipelineStateObject() const { return mPipelineState.Get(); }

    PipelineState& operator=(const PipelineState& pipeState)
    {
        if (this == &pipeState)
            return *this;

        mRootSignature = pipeState.mRootSignature;
        return *this;
    }
protected:
    const std::wstring mName;
    const RootSignature* mRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mPipelineState;
};

class GraphicsPipelineState : public PipelineState
{
    friend struct std::hash<GraphicsPipelineState>;
public:
    GraphicsPipelineState& operator=(const GraphicsPipelineState& pipeState)
    {
        if (this == &pipeState)
            return *this;

        PipelineState::operator=(pipeState);

        mPipelineStateDesc = pipeState.mPipelineStateDesc;
        return *this;
    }

    // Start with empty state
    GraphicsPipelineState(std::wstring name = L"Unnamed Graphics PSO");

    void SetBlendState(const D3D12_BLEND_DESC& blendDesc);
    void SetRasterizerState(const D3D12_RASTERIZER_DESC& rasterizerDesc);
    void SetDepthStencilState(const D3D12_DEPTH_STENCIL_DESC& depthStencilDesc);
    void SetSampleMask(UINT sampleMask);
    void SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType);
    void SetDepthTargetFormat(DXGI_FORMAT dsvFormat, UINT msaaCount = 1, UINT msaaQuality = 0);
    void SetRenderTargetFormat(DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, UINT msaaCount = 1, UINT msaaQuality = 0);
    void SetRenderTargetFormats(UINT numRTVs, const DXGI_FORMAT* rtvFormats, DXGI_FORMAT dsvFormat, UINT msaaCount = 1, UINT msaaQuality = 0);
    void SetInputLayout(size_t numDescs, D3D12_INPUT_ELEMENT_DESC pInputElementDescs[]);
    void SetPrimitiveRestart(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE ibProps);

    // These const_casts shouldn't be necessary, but we need to fix the API to accept "const void* pShaderBytecode"
    void SetVertexShader(const void* binaryData, size_t size) 
        { mPipelineStateDesc.VS = CD3DX12_SHADER_BYTECODE(const_cast<void*>(binaryData), size); }
    void SetPixelShader(const void* binaryData, size_t size) 
        { mPipelineStateDesc.PS = CD3DX12_SHADER_BYTECODE(const_cast<void*>(binaryData), size); }
    void SetGeometryShader(const void* binaryData, size_t size) 
        { mPipelineStateDesc.GS = CD3DX12_SHADER_BYTECODE(const_cast<void*>(binaryData), size); }
    void SetHullShader(const void* binaryData, size_t size)
        { mPipelineStateDesc.HS = CD3DX12_SHADER_BYTECODE(const_cast<void*>(binaryData), size); }
    void SetDomainShader(const void* binaryData, size_t size) 
        { mPipelineStateDesc.DS = CD3DX12_SHADER_BYTECODE(const_cast<void*>(binaryData), size); }

    void SetVertexShader(const D3D12_SHADER_BYTECODE& binaryData) { mPipelineStateDesc.VS = binaryData; }
    void SetPixelShader(const D3D12_SHADER_BYTECODE& binaryData) { mPipelineStateDesc.PS = binaryData; }
    void SetGeometryShader(const D3D12_SHADER_BYTECODE& binaryData) { mPipelineStateDesc.GS = binaryData; }
    void SetHullShader(const D3D12_SHADER_BYTECODE& binaryData) { mPipelineStateDesc.HS = binaryData; }
    void SetDomainShader(const D3D12_SHADER_BYTECODE& binaryData) { mPipelineStateDesc.DS = binaryData; }

    void SetVertexShader(const ShaderUnit& shaderUnit);
    void SetPixelShader(const ShaderUnit& shaderUnit);
    void SetGeometryShader(const ShaderUnit& shaderUnit);
    void SetHullShader(const ShaderUnit& shaderUnit);
    void SetDomainShader(const ShaderUnit& shaderUnit);

    void Finalize();
private:
    D3D12_GRAPHICS_PIPELINE_STATE_DESC mPipelineStateDesc;
    std::unique_ptr<D3D12_INPUT_ELEMENT_DESC[]> mInputLayouts;
};

class ComputePipelineState : public PipelineState
{
    friend struct std::hash<ComputePipelineState>;
public:
    ComputePipelineState(std::wstring name = L"Unnamed Compute PSO");

    void SetComputeShader(const void* binaryData, size_t size) 
        { mPipelineStateDesc.CS = CD3DX12_SHADER_BYTECODE(const_cast<void*>(binaryData), size); }
    void SetComputeShader(const D3D12_SHADER_BYTECODE& binaryData) { mPipelineStateDesc.CS = binaryData; }
    void SetComputeShader(const ShaderUnit& shaderUnit);

    void Finalize();
private:
    D3D12_COMPUTE_PIPELINE_STATE_DESC mPipelineStateDesc;
};


class PipeLineStateManager : public Singleton<PipeLineStateManager>
{
    USE_SINGLETON;
public:
    PipeLineStateManager() {}
    ~PipeLineStateManager() {}

    void InitAllPipeLineStates();

    template<typename T>
    T* GetPipelineState(const std::wstring& name);
private:
    std::unordered_map<std::wstring, std::unique_ptr<PipelineState>> mPipStats;
};

template<typename T>
inline T* PipeLineStateManager::GetPipelineState(const std::wstring& name)
{
    static_assert(std::is_base_of_v<PipelineState, T>);

    auto findIter = mPipStats.find(name);
    if (findIter != mPipStats.end())
        return static_cast<T*>(findIter->second.get());

    auto insertIter = mPipStats.emplace(name, std::make_unique<T>(name));
    return static_cast<T*>(insertIter.first->second.get());
}

#define GET_PSO(name, cls) PipeLineStateManager::GetInstance()->GetPipelineState<cls>(name)
#define GET_GPSO(name) GET_PSO(name, GraphicsPipelineState)
#define GET_CPSO(name) GET_PSO(name, ComputePipelineState)
