#pragma once
#include "RootSignature.h"
#include "PipelineState.h"
#include "SamplerManager.h"

class Texture;


namespace Graphics
{
    void InitializeResource();
    void DestroyResource();

    Texture& GetDefaultTexture(eDefaultTexture texID);

    inline std::vector<std::function<void()>> gCustomRootSigShaderTasks;
    inline std::vector<std::function<void()>> gCustomPipeStatTasks;
    // Task before render start
    inline void AddRSSTask(std::function<void()>&& task)
    {
        gCustomRootSigShaderTasks.push_back(std::move(task));
    }
    // Task before render start
    inline void AddPSTask(std::function<void()>&& task)
    {
        gCustomPipeStatTasks.push_back(std::move(task));
    }

    extern SamplerDesc SamplerLinearWrapDesc;
    extern SamplerDesc SamplerAnisoWrapDesc;
    extern SamplerDesc SamplerShadowDescGE;
    extern SamplerDesc SamplerShadowDescLE;
    extern SamplerDesc SamplerLinearClampDesc;
    extern SamplerDesc SamplerVolumeWrapDesc;
    extern SamplerDesc SamplerPointClampDesc;
    extern SamplerDesc SamplerPointBorderDesc;
    extern SamplerDesc SamplerLinearBorderDesc;

    extern DescriptorHandle SamplerLinearWrap;
    extern DescriptorHandle SamplerAnisoWrap;
    extern DescriptorHandle SamplerShadowGE;
    extern DescriptorHandle SamplerShadowLE;
    extern DescriptorHandle SamplerLinearClamp;
    extern DescriptorHandle SamplerVolumeWrap;
    extern DescriptorHandle SamplerPointClamp;
    extern DescriptorHandle SamplerPointBorder;
    extern DescriptorHandle SamplerLinearBorder;

    extern D3D12_RASTERIZER_DESC RasterizerDefault;
    extern D3D12_RASTERIZER_DESC RasterizerDefaultMsaa;
    extern D3D12_RASTERIZER_DESC RasterizerDefaultCw;
    extern D3D12_RASTERIZER_DESC RasterizerDefaultCwMsaa;
    extern D3D12_RASTERIZER_DESC RasterizerTwoSided;
    extern D3D12_RASTERIZER_DESC RasterizerTwoSidedMsaa;
    extern D3D12_RASTERIZER_DESC RasterizerShadow;
    extern D3D12_RASTERIZER_DESC RasterizerShadowCW;
    extern D3D12_RASTERIZER_DESC RasterizerShadowTwoSided;

    extern D3D12_BLEND_DESC BlendNoColorWrite;		// XXX
    extern D3D12_BLEND_DESC BlendDisable;			// 1, 0
    extern D3D12_BLEND_DESC BlendPreMultiplied;		// 1, 1-SrcA
    extern D3D12_BLEND_DESC BlendTraditional;		// SrcA, 1-SrcA
    extern D3D12_BLEND_DESC BlendAdditive;			// 1, 1
    extern D3D12_BLEND_DESC BlendTraditionalAdditive;// SrcA, 1

    extern D3D12_DEPTH_STENCIL_DESC DepthStateDisabled;
    extern D3D12_DEPTH_STENCIL_DESC DepthStateReadWrite;
    extern D3D12_DEPTH_STENCIL_DESC DepthStateReadOnly;
    extern D3D12_DEPTH_STENCIL_DESC DepthStateReadOnlyReversed;
    extern D3D12_DEPTH_STENCIL_DESC DepthStateTestEqual;

    extern RootSignature* gCommonRS;

    extern ComputePipelineState* gGenerateMipsLinearPSO[];
    extern ComputePipelineState* gGenerateMipsSRGBPSO[];
    extern ComputePipelineState* gDepthResloveMsaaPSO;
    extern ComputePipelineState* gDepthArrayResloveMsaaPSO;
}
