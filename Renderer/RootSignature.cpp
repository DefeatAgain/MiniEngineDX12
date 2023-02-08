#include "RootSignature.h"
#include "Utils/DebugUtils.h"
#include "Utils/ThreadPoolExecutor.h"
#include "Graphics.h"

namespace
{
    std::unordered_map<std::wstring, RootSignature> sRootSigs;

    std::queue<std::future<void>> sInitRootSigTasks;
    bool sIsFirstInitRootSigManager = false;
}

void RootSignature::InitStaticSampler(
    UINT register_, const D3D12_SAMPLER_DESC& nonStaticSamplerDesc, D3D12_SHADER_VISIBILITY visibility)
{
    D3D12_STATIC_SAMPLER_DESC& staticSamplerDesc = mSamDescs.emplace_back();

    staticSamplerDesc.Filter = nonStaticSamplerDesc.Filter;
    staticSamplerDesc.AddressU = nonStaticSamplerDesc.AddressU;
    staticSamplerDesc.AddressV = nonStaticSamplerDesc.AddressV;
    staticSamplerDesc.AddressW = nonStaticSamplerDesc.AddressW;
    staticSamplerDesc.MipLODBias = nonStaticSamplerDesc.MipLODBias;
    staticSamplerDesc.MaxAnisotropy = nonStaticSamplerDesc.MaxAnisotropy;
    staticSamplerDesc.ComparisonFunc = nonStaticSamplerDesc.ComparisonFunc;
    staticSamplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    staticSamplerDesc.MinLOD = nonStaticSamplerDesc.MinLOD;
    staticSamplerDesc.MaxLOD = nonStaticSamplerDesc.MaxLOD;
    staticSamplerDesc.ShaderRegister = register_;
    staticSamplerDesc.RegisterSpace = 0;
    staticSamplerDesc.ShaderVisibility = visibility;

    if (staticSamplerDesc.AddressU == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
        staticSamplerDesc.AddressV == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
        staticSamplerDesc.AddressW == D3D12_TEXTURE_ADDRESS_MODE_BORDER)
    {
        WARN_IF_NOT(
            // Transparent Black
            nonStaticSamplerDesc.BorderColor[0] == 0.0f &&
            nonStaticSamplerDesc.BorderColor[1] == 0.0f &&
            nonStaticSamplerDesc.BorderColor[2] == 0.0f &&
            nonStaticSamplerDesc.BorderColor[3] == 0.0f ||
            // Opaque Black
            nonStaticSamplerDesc.BorderColor[0] == 0.0f &&
            nonStaticSamplerDesc.BorderColor[1] == 0.0f &&
            nonStaticSamplerDesc.BorderColor[2] == 0.0f &&
            nonStaticSamplerDesc.BorderColor[3] == 1.0f ||
            // Opaque White
            nonStaticSamplerDesc.BorderColor[0] == 1.0f &&
            nonStaticSamplerDesc.BorderColor[1] == 1.0f &&
            nonStaticSamplerDesc.BorderColor[2] == 1.0f &&
            nonStaticSamplerDesc.BorderColor[3] == 1.0f,
            "Sampler border color does not match static sampler limitations");

        if (nonStaticSamplerDesc.BorderColor[3] == 1.0f)
        {
            if (nonStaticSamplerDesc.BorderColor[0] == 1.0f)
                staticSamplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
            else
                staticSamplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        }
        else
            staticSamplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    }
}

void RootSignature::Reset(UINT numRootParams, UINT numStaticSamplers)
{
    mParamArray = std::make_unique<RootParameter[]>(numRootParams);
    mRootDesc.NumParameters = numRootParams;
    mRootDesc.pParameters = (const D3D12_ROOT_PARAMETER*)mParamArray.get();

    mSamDescs.reserve(numStaticSamplers);
    mRootDesc.NumStaticSamplers = numStaticSamplers;
    mRootDesc.pStaticSamplers = nullptr;
}

void RootSignature::Finalize(D3D12_ROOT_SIGNATURE_FLAGS flags)
{
    mRootDesc.Flags = flags;
    mRootDesc.pStaticSamplers = mSamDescs.data();

    auto initTask = [this]() {
        Microsoft::WRL::ComPtr<ID3DBlob> pOutBlob, pErrorBlob;
        CheckHR(D3D12SerializeRootSignature(
            &mRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, pOutBlob.GetAddressOf(), pErrorBlob.GetAddressOf()));
        if (pErrorBlob)
        {
            ::OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
        }

        CheckHR(Graphics::gDevice->CreateRootSignature(
            0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(mD3dRootSignature.GetAddressOf())));

        if (pErrorBlob)
        {
            ::OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
        }

        mD3dRootSignature->SetName(mName.c_str());
    };

    if (sIsFirstInitRootSigManager)
        sInitRootSigTasks.emplace(Utility::gThreadPoolExecutor.Submit(initTask));
    else
        initTask();
}


void RootSignatureManager::InitAllRootSignatures()
{
    while (!sInitRootSigTasks.empty())
    {
        sInitRootSigTasks.front().get();
        sInitRootSigTasks.pop();
    }

    sIsFirstInitRootSigManager = true;
}

RootSignature* RootSignatureManager::GetRootSignature(const std::wstring& name)
{
    auto findIter = sRootSigs.find(name);
    if (findIter != sRootSigs.end())
        return &findIter->second;

    auto insertIter = sRootSigs.emplace(name, name);
    return &insertIter.first->second;
}
