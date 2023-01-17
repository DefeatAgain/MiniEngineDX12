#pragma once
#include "Graphics.h"
#include "Color.h"
#include "Common.h"
#include "Utils/Hash.h"
#include "Utils/DebugUtils.h"
 
class RootParameter
{
    friend class RootSignature;
public:

    RootParameter()
    {
        mRootParam.ParameterType = (D3D12_ROOT_PARAMETER_TYPE)0xFFFFFFFF;
    }

    ~RootParameter()
    {
        Clear();
    }

    void Clear()
    {
        if (mRootParam.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            delete[] mRootParam.DescriptorTable.pDescriptorRanges;

        mRootParam.ParameterType = (D3D12_ROOT_PARAMETER_TYPE)0xFFFFFFFF;
    }

    void InitAsConstants(UINT Register, UINT NumDwords, D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL, UINT Space = 0)
    {
        mRootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        mRootParam.ShaderVisibility = Visibility;
        mRootParam.Constants.Num32BitValues = NumDwords;
        mRootParam.Constants.ShaderRegister = Register;
        mRootParam.Constants.RegisterSpace = Space;
    }

    void InitAsConstantBuffer(UINT Register, D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL, UINT Space = 0)
    {
        mRootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        mRootParam.ShaderVisibility = Visibility;
        mRootParam.Descriptor.ShaderRegister = Register;
        mRootParam.Descriptor.RegisterSpace = Space;
    }

    void InitAsBufferSRV(UINT Register, D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL, UINT Space = 0)
    {
        mRootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        mRootParam.ShaderVisibility = Visibility;
        mRootParam.Descriptor.ShaderRegister = Register;
        mRootParam.Descriptor.RegisterSpace = Space;
    }

    void InitAsBufferUAV(UINT Register, D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL, UINT Space = 0)
    {
        mRootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        mRootParam.ShaderVisibility = Visibility;
        mRootParam.Descriptor.ShaderRegister = Register;
        mRootParam.Descriptor.RegisterSpace = Space;
    }

    void InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE Type, UINT Register, UINT Count, D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL, UINT Space = 0)
    {
        InitAsDescriptorTable(1, Visibility);
        SetTableRange(0, Type, Register, Count, Space);
    }

    void InitAsDescriptorTable(UINT RangeCount, D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL)
    {
        mRootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        mRootParam.ShaderVisibility = Visibility;
        mRootParam.DescriptorTable.NumDescriptorRanges = RangeCount;
        mRootParam.DescriptorTable.pDescriptorRanges = new D3D12_DESCRIPTOR_RANGE[RangeCount];
    }

    void SetTableRange(UINT RangeIndex, D3D12_DESCRIPTOR_RANGE_TYPE Type, UINT Register, UINT Count, UINT Space = 0)
    {
        D3D12_DESCRIPTOR_RANGE& range = const_cast<D3D12_DESCRIPTOR_RANGE*>(mRootParam.DescriptorTable.pDescriptorRanges)[RangeIndex];
        range.RangeType = Type;
        range.NumDescriptors = Count;
        range.BaseShaderRegister = Register;
        range.RegisterSpace = Space;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }

    const D3D12_ROOT_PARAMETER& operator() () const { return mRootParam; }

    UINT GetDescriptorsSize() const
    {
        assert(mRootParam.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);
        return std::accumulate(
            mRootParam.DescriptorTable.pDescriptorRanges, 
            mRootParam.DescriptorTable.pDescriptorRanges + mRootParam.DescriptorTable.NumDescriptorRanges, 
            (UINT)0,
            [](UINT a, auto& b) { return a + b.NumDescriptors; });
    }

protected:
    D3D12_ROOT_PARAMETER mRootParam;
};


class RootSignature
{
public:
    RootSignature(const std::wstring& name) : RootSignature(name, 0, 0) {}

    RootSignature(const std::wstring& name, UINT numRootParams, UINT numStaticSamplers) :
        mName(name), mRootDesc(), mParamArray(nullptr), mD3dRootSignature(nullptr)
    {
        Reset(numRootParams, numStaticSamplers);
    }

    ~RootSignature() {}

    void Reset(UINT numRootParams = 0, UINT numStaticSamplers = 0);

    void InitStaticSampler(UINT register_, const D3D12_SAMPLER_DESC& nonStaticSamplerDesc,
        D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

    RootParameter& GetParam(size_t entryIndex) { return mParamArray[entryIndex]; }

    const RootParameter& GetParam(size_t entryIndex) const { return mParamArray[entryIndex]; }

    RootParameter& operator[] (size_t entryIndex) { return mParamArray[entryIndex]; }

    const RootParameter& operator[] (size_t entryIndex) const { return mParamArray[entryIndex]; }

    void Finalize(D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE);

    const D3D12_ROOT_SIGNATURE_DESC& GetRootDesc() const { return mRootDesc; };
    ID3D12RootSignature* GetRootSignature() const { return mD3dRootSignature.Get(); };
private:
    std::wstring mName;
    D3D12_ROOT_SIGNATURE_DESC mRootDesc;
    std::unique_ptr<RootParameter[]> mParamArray;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mD3dRootSignature;
    std::vector<D3D12_STATIC_SAMPLER_DESC> mSamDescs;
};


class RootSignatureManager : public Singleton<RootSignatureManager>
{
    USE_SINGLETON;
private:
    RootSignatureManager() {}
    ~RootSignatureManager() {}
public:
    void InitAllRootSignatures();

    RootSignature* GetRootSignature(const std::wstring& name);
};

#define GET_RSO(name) RootSignatureManager::GetInstance()->GetRootSignature(name)
