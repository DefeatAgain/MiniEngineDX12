#include "SSAO.h"
#include "PixelBuffer.h"
#include "Graphics.h"
#include "GraphicsResource.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "FrameContext.h"

#define NUM_SSAO_MIP 4
#define NUM_SSAO_MERGES 5
#define NUM_SSAO_DESC 17

namespace
{
    RootSignature* sSSAORS = nullptr;

    ComputePipelineState* sSSAODownSamplePSO = nullptr;
    ComputePipelineState* sSSAOComputePSO = nullptr;
    ComputePipelineState* sSSAOUpSamplePSO = nullptr;

    ColorBuffer sSSAODepthBuffers[SWAP_CHAIN_BUFFER_COUNT][NUM_SSAO_MIP];
    ColorBuffer sSSAONormalBuffers[SWAP_CHAIN_BUFFER_COUNT][NUM_SSAO_MIP];
    ColorBuffer sSSAOMergeBuffers[SWAP_CHAIN_BUFFER_COUNT][NUM_SSAO_MERGES];
    ColorBuffer sSSAOSmoothBuffers[SWAP_CHAIN_BUFFER_COUNT][NUM_SSAO_MIP];

    DescriptorHandle sSSAOSRVs[SWAP_CHAIN_BUFFER_COUNT];
    DescriptorHandle sSSAOUAVs[SWAP_CHAIN_BUFFER_COUNT];
    DescriptorHandle sSSAOUpSampleSRVs[SWAP_CHAIN_BUFFER_COUNT]; // layout changed

    const float sSampleThickness[7] = {
        //sqrt(1.0f - 0.2f * 0.2f),
        sqrt(1.0f - 0.4f * 0.4f), // (2, 0)
        //sqrt(1.0f - 0.6f * 0.6f),
        sqrt(1.0f - 0.8f * 0.8f), // (4, 0)
        sqrt(1.0f - 0.2f * 0.2f - 0.2f * 0.2f), // (1, 1)
        //sqrt(1.0f - 0.2f * 0.2f - 0.4f * 0.4f),
        sqrt(1.0f - 0.2f * 0.2f - 0.6f * 0.6f), // (1, 3)
        //sqrt(1.0f - 0.2f * 0.2f - 0.8f * 0.8f),
        sqrt(1.0f - 0.4f * 0.4f - 0.4f * 0.4f), // (2, 2)
        //sqrt(1.0f - 0.4f * 0.4f - 0.6f * 0.6f),
        sqrt(1.0f - 0.4f * 0.4f - 0.8f * 0.8f), // (2, 4)
        sqrt(1.0f - 0.6f * 0.6f - 0.6f * 0.6f), // (3, 3)
    };
}

enum eRootSigBingdings
{
    kGbffers,
    kSSAOSRVs,
    kSSAOUAVs,
    kSSAOCBVs,
    kNumBindings
};

void Initialize()
{
    Graphics::AddRSSTask([]()
    {
        ADD_SHADER("SSAODownSample", "SSAO/SSAODownSample.hlsl", kCS);
        ADD_SHADER("SSAOCompute", "SSAO/SSAOCompute.hlsl", kCS, {"REVERSED_Z", ""});
        ADD_SHADER("SSAOUpSample", "SSAO/SSAOUpSample.hlsl", kCS);

        sSSAORS = GET_RSO(L"SSAO RSO");
        sSSAORS->Reset(kNumBindings, 1);
        //sSSAORS->InitStaticSampler(10, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
        //sSSAORS->InitStaticSampler(11, Graphics::SamplerPointClampDesc, D3D12_SHADER_VISIBILITY_PIXEL);
        //sSSAORS->InitStaticSampler(12, Graphics::SamplerShadowDescGE, D3D12_SHADER_VISIBILITY_PIXEL);
        sSSAORS->InitStaticSampler(0, Graphics::SamplerPointClampDesc);
        sSSAORS->GetParam(kGbffers).InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 2);
        sSSAORS->GetParam(kSSAOSRVs).InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 8);
        sSSAORS->GetParam(kSSAOUAVs).InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 8);
        sSSAORS->GetParam(kSSAOCBVs).InitAsConstantBuffer(0);
        //sSSAORS->GetParam(kMaterialConstants).InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
        //sSSAORS->GetParam(kGlobalConstants).InitAsConstantBuffer(1, D3D12_SHADER_VISIBILITY_ALL);
        sSSAORS->Finalize();
    });
    
    Graphics::AddPSTask([]()
    {
        sSSAODownSamplePSO = GET_CPSO(L"SSAO DownSample PSO");
        sSSAODownSamplePSO->SetRootSignature(*sSSAORS);
        sSSAODownSamplePSO->SetComputeShader(GET_SHADER("SSAODownSample"));
        sSSAODownSamplePSO->Finalize();

        sSSAOComputePSO = GET_CPSO(L"SSAO Compute PSO");
        sSSAOComputePSO->SetRootSignature(*sSSAORS);
        sSSAOComputePSO->SetComputeShader(GET_SHADER("SSAOCompute"));
        sSSAOComputePSO->Finalize();

        sSSAOUpSamplePSO = GET_CPSO(L"SSAO UpSample PSO");
        sSSAOUpSamplePSO->SetRootSignature(*sSSAORS);
        sSSAOUpSamplePSO->SetComputeShader(GET_SHADER("SSAOUpSample"));
        sSSAOUpSamplePSO->Finalize();
    });

    for (size_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
    {
        DEALLOC_DESCRIPTOR_GPU(sSSAOSRVs[i], 32);
        DEALLOC_DESCRIPTOR_GPU(sSSAOUAVs[i], 32);
        DEALLOC_DESCRIPTOR_GPU(sSSAOUpSampleSRVs[i], 32);
        sSSAOSRVs[i] = ALLOC_DESCRIPTOR_GPU(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 32);
        sSSAOUAVs[i] = ALLOC_DESCRIPTOR_GPU(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 32);
        sSSAOUpSampleSRVs[i] = ALLOC_DESCRIPTOR_GPU(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 32);
    }
}

void InitializeBuffer()
{
    const uint32_t renderWidth = Graphics::gRenderWidth;
    const uint32_t renderHeight = Graphics::gRenderHeight;

    for (size_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
    {
        sSSAODepthBuffers[i][0].Destroy();
        sSSAODepthBuffers[i][1].Destroy();
        sSSAODepthBuffers[i][2].Destroy();
        sSSAODepthBuffers[i][3].Destroy();
        sSSAONormalBuffers[i][0].Destroy();
        sSSAONormalBuffers[i][1].Destroy();
        sSSAONormalBuffers[i][2].Destroy();
        sSSAONormalBuffers[i][3].Destroy(); 
        sSSAOSmoothBuffers[i][0].Destroy();
        sSSAOSmoothBuffers[i][1].Destroy();
        sSSAOSmoothBuffers[i][2].Destroy();
        sSSAOSmoothBuffers[i][3].Destroy();
        sSSAOMergeBuffers[i][0].Destroy();
        sSSAOMergeBuffers[i][1].Destroy();
        sSSAOMergeBuffers[i][2].Destroy();
        sSSAOMergeBuffers[i][3].Destroy();
        sSSAOMergeBuffers[i][4].Destroy();


        sSSAODepthBuffers[i][0].Create(L"SSAO depth downsample 1", renderWidth / 2, renderHeight / 2, 1, DXGI_FORMAT_R16_UNORM);
        sSSAODepthBuffers[i][1].Create(L"SSAO depth downsample 2", renderWidth / 4, renderHeight / 4, 1, DXGI_FORMAT_R16_UNORM);
        sSSAODepthBuffers[i][2].Create(L"SSAO depth downsample 3", renderWidth / 8, renderHeight / 8, 1, DXGI_FORMAT_R16_UNORM);
        sSSAODepthBuffers[i][3].Create(L"SSAO depth downsample 4", renderWidth / 16, renderHeight / 16, 1, DXGI_FORMAT_R16_UNORM);
        sSSAONormalBuffers[i][0].Create(L"SSAO normal downsample 1", renderWidth / 2, renderHeight / 2, 1, DXGI_FORMAT_R10G10B10A2_UNORM);
        sSSAONormalBuffers[i][1].Create(L"SSAO normal downsample 2", renderWidth / 4, renderHeight / 4, 1, DXGI_FORMAT_R10G10B10A2_UNORM);
        sSSAONormalBuffers[i][2].Create(L"SSAO normal downsample 3", renderWidth / 8, renderHeight / 8, 1, DXGI_FORMAT_R10G10B10A2_UNORM);
        sSSAONormalBuffers[i][3].Create(L"SSAO normal downsample 4", renderWidth / 16, renderHeight / 16, 1, DXGI_FORMAT_R10G10B10A2_UNORM);
        sSSAOMergeBuffers[i][0].Create(L"SSAO merge 1", renderWidth, renderHeight, 1, DXGI_FORMAT_R8_UNORM);
        sSSAOMergeBuffers[i][1].Create(L"SSAO merge 2", renderWidth / 2, renderHeight / 2, 1, DXGI_FORMAT_R8_UNORM);
        sSSAOMergeBuffers[i][2].Create(L"SSAO merge 3", renderWidth / 4, renderHeight / 4, 1, DXGI_FORMAT_R8_UNORM);
        sSSAOMergeBuffers[i][3].Create(L"SSAO merge 4", renderWidth / 8, renderHeight / 8, 1, DXGI_FORMAT_R8_UNORM);   
        sSSAOMergeBuffers[i][4].Create(L"SSAO merge 5", renderWidth / 16, renderHeight / 16, 1, DXGI_FORMAT_R8_UNORM);
        sSSAOSmoothBuffers[i][0].Create(L"SSAO Smooth 1", renderWidth, renderHeight, 1, DXGI_FORMAT_R8_UNORM);
        sSSAOSmoothBuffers[i][1].Create(L"SSAO Smooth 2", renderWidth / 2, renderHeight / 2, 1, DXGI_FORMAT_R8_UNORM);
        sSSAOSmoothBuffers[i][2].Create(L"SSAO Smooth 3", renderWidth / 4, renderHeight / 4, 1, DXGI_FORMAT_R8_UNORM);
        sSSAOSmoothBuffers[i][3].Create(L"SSAO Smooth 4", renderWidth / 8, renderHeight / 8, 1, DXGI_FORMAT_R8_UNORM);
#define COPY_DESC Graphics::gDevice->CopyDescriptorsSimple
#define SRV_TYPE D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        COPY_DESC(1, sSSAOSRVs[i] + 0, sSSAONormalBuffers[i][0].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOSRVs[i] + 1, sSSAONormalBuffers[i][1].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOSRVs[i] + 2, sSSAONormalBuffers[i][2].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOSRVs[i] + 3, sSSAONormalBuffers[i][3].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOSRVs[i] + 4, sSSAODepthBuffers[i][0].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOSRVs[i] + 5, sSSAODepthBuffers[i][1].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOSRVs[i] + 6, sSSAODepthBuffers[i][2].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOSRVs[i] + 7, sSSAODepthBuffers[i][3].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOSRVs[i] + 8, sSSAOMergeBuffers[i][0].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOSRVs[i] + 9, sSSAOMergeBuffers[i][1].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOSRVs[i] + 10, sSSAOMergeBuffers[i][2].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOSRVs[i] + 11, sSSAOMergeBuffers[i][3].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOSRVs[i] + 12, sSSAOMergeBuffers[i][4].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOSRVs[i] + 13, sSSAOSmoothBuffers[i][0].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOSRVs[i] + 14, sSSAOSmoothBuffers[i][1].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOSRVs[i] + 15, sSSAOSmoothBuffers[i][2].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOSRVs[i] + 16, sSSAOSmoothBuffers[i][3].GetSRV(), SRV_TYPE);

        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 0, sSSAOMergeBuffers[i][4].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 1, sSSAONormalBuffers[i][3].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 2, sSSAODepthBuffers[i][3].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 3, sSSAOMergeBuffers[i][3].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 4, sSSAONormalBuffers[i][2].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 5, sSSAODepthBuffers[i][2].GetSRV(), SRV_TYPE);

        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 6, sSSAOSmoothBuffers[i][3].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 7, sSSAONormalBuffers[i][2].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 8, sSSAODepthBuffers[i][2].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 9, sSSAOMergeBuffers[i][2].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 10, sSSAONormalBuffers[i][1].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 11, sSSAODepthBuffers[i][1].GetSRV(), SRV_TYPE);

        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 12, sSSAOSmoothBuffers[i][2].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 13, sSSAONormalBuffers[i][1].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 14, sSSAODepthBuffers[i][1].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 15, sSSAOMergeBuffers[i][1].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 16, sSSAONormalBuffers[i][0].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 17, sSSAODepthBuffers[i][0].GetSRV(), SRV_TYPE);

        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 18, sSSAOSmoothBuffers[i][1].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 19, sSSAONormalBuffers[i][0].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 20, sSSAODepthBuffers[i][0].GetSRV(), SRV_TYPE);
        COPY_DESC(1, sSSAOUpSampleSRVs[i] + 21, sSSAOMergeBuffers[i][0].GetSRV(), SRV_TYPE);
#define UAV_TYPE D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        COPY_DESC(1, sSSAOUAVs[i] + 0, sSSAONormalBuffers[i][0].GetUAV(), UAV_TYPE);
        COPY_DESC(1, sSSAOUAVs[i] + 1, sSSAONormalBuffers[i][1].GetUAV(), UAV_TYPE);
        COPY_DESC(1, sSSAOUAVs[i] + 2, sSSAONormalBuffers[i][2].GetUAV(), UAV_TYPE);
        COPY_DESC(1, sSSAOUAVs[i] + 3, sSSAONormalBuffers[i][3].GetUAV(), UAV_TYPE);
        COPY_DESC(1, sSSAOUAVs[i] + 4, sSSAODepthBuffers[i][0].GetUAV(), UAV_TYPE);
        COPY_DESC(1, sSSAOUAVs[i] + 5, sSSAODepthBuffers[i][1].GetUAV(), UAV_TYPE);
        COPY_DESC(1, sSSAOUAVs[i] + 6, sSSAODepthBuffers[i][2].GetUAV(), UAV_TYPE);
        COPY_DESC(1, sSSAOUAVs[i] + 7, sSSAODepthBuffers[i][3].GetUAV(), UAV_TYPE);
        COPY_DESC(1, sSSAOUAVs[i] + 8, sSSAOMergeBuffers[i][0].GetUAV(), UAV_TYPE);
        COPY_DESC(1, sSSAOUAVs[i] + 9, sSSAOMergeBuffers[i][1].GetUAV(), UAV_TYPE);
        COPY_DESC(1, sSSAOUAVs[i] + 10, sSSAOMergeBuffers[i][2].GetUAV(), UAV_TYPE);
        COPY_DESC(1, sSSAOUAVs[i] + 11, sSSAOMergeBuffers[i][3].GetUAV(), UAV_TYPE);
        COPY_DESC(1, sSSAOUAVs[i] + 12, sSSAOMergeBuffers[i][4].GetUAV(), UAV_TYPE);
        COPY_DESC(1, sSSAOUAVs[i] + 13, sSSAOSmoothBuffers[i][0].GetUAV(), UAV_TYPE);
        COPY_DESC(1, sSSAOUAVs[i] + 14, sSSAOSmoothBuffers[i][1].GetUAV(), UAV_TYPE);
        COPY_DESC(1, sSSAOUAVs[i] + 15, sSSAOSmoothBuffers[i][2].GetUAV(), UAV_TYPE);
        COPY_DESC(1, sSSAOUAVs[i] + 16, sSSAOSmoothBuffers[i][3].GetUAV(), UAV_TYPE);
#undef SRV_TYPE
#undef UAV_TYPE
#undef COPY_DESC
    }
}


DescriptorHandle GetCurrentDownSampleSRV(size_t frameIndex)
{
    return sSSAOSRVs[frameIndex];
}

DescriptorHandle GetCurrentDownSampleUAV(size_t frameIndex)
{
    return sSSAOUAVs[frameIndex];
}

DescriptorHandle GetCurrentAOResultSRV(size_t frameIndex)
{
    return sSSAOSRVs[frameIndex] + 8;
}

DescriptorHandle GetCurrentAOResultUAV(size_t frameIndex)
{
    return sSSAOUAVs[frameIndex] + 8;
}

DescriptorHandle GetCurrentAOSmoothSRV(size_t frameIndex)
{
    return sSSAOSRVs[frameIndex] + 13;
}

DescriptorHandle GetCurrentAOSmoothUAV(size_t frameIndex)
{
    return sSSAOUAVs[frameIndex] + 13;
}

void SSAODownSample(ComputeCommandList& ghCommandList, size_t frameIndex, DescriptorHandle GBufferSRVandDepthSRV,
    ColorBuffer& Gbuffer0, DepthBuffer& depthBuffer)
{
    ghCommandList.TransitionResource(Gbuffer0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ghCommandList.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
    for (size_t i = 0; i < NUM_SSAO_MIP; i++)
    {
        ghCommandList.TransitionResource(sSSAODepthBuffers[frameIndex][i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        //ghCommandList.TransitionResource(sSSAONormalBuffers[frameIndex][i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    ghCommandList.SetPipelineState(*sSSAODownSamplePSO);
    ghCommandList.SetDescriptorTable(kGbffers, GBufferSRVandDepthSRV);
    ghCommandList.SetDescriptorTable(kSSAOSRVs, GetCurrentDownSampleSRV(frameIndex));
    ghCommandList.SetDescriptorTable(kSSAOUAVs, GetCurrentDownSampleUAV(frameIndex));
    ghCommandList.Dispatch2D(Graphics::gRenderWidth, Graphics::gRenderHeight);
}

void ComputeSSAO(ComputeCommandList& ghCommandList, size_t frameIndex, const float tanHalfFovH, DescriptorHandle GBufferSRVandDepthSRV, 
    DepthBuffer& depthBuffer)
{
    for (size_t i = 0; i < NUM_SSAO_MIP; i++)
        ghCommandList.TransitionResource(sSSAODepthBuffers[frameIndex][i], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for (size_t i = 0; i < NUM_SSAO_MERGES; i++)
        ghCommandList.TransitionResource(sSSAOMergeBuffers[frameIndex][i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ghCommandList.SetPipelineState(*sSSAOComputePSO);

    __declspec(align(256)) struct constantBuffer
    {
        float sampleThinkness[7];
        float _pad0;
        float sampleWeights[7];
        float _pad1;
        float invBufferWidth;
        float invBufferHeight;
    } ssaoCB;
    ssaoCB.sampleWeights[0] = 4.0f * sSampleThickness[0];
    ssaoCB.sampleWeights[1] = 4.0f * sSampleThickness[1];
    ssaoCB.sampleWeights[2] = 4.0f * sSampleThickness[2];
    ssaoCB.sampleWeights[3] = 8.0f * sSampleThickness[3];
    ssaoCB.sampleWeights[4] = 4.0f * sSampleThickness[4];
    ssaoCB.sampleWeights[5] = 8.0f * sSampleThickness[5];
    ssaoCB.sampleWeights[6] = 4.0f * sSampleThickness[6];

    float totalWeight = 0.0f;
    for (size_t i = 0; i < 7; i++)
        totalWeight += ssaoCB.sampleWeights[i];
    for (size_t i = 0; i < 7; i++)
        ssaoCB.sampleWeights[i] /= totalWeight;

    for (size_t i = 0; i < NUM_SSAO_MIP + 1; i++)
    {
        PixelBuffer* curBuffer;
        if (i == 0)
            curBuffer = &depthBuffer;
        else
            curBuffer = &sSSAODepthBuffers[frameIndex][i - 1];

        size_t bufferWidth = curBuffer->GetWidth();
        size_t bufferHeight = curBuffer->GetHeight();
        ssaoCB.invBufferWidth = 1.0f / static_cast<float>(bufferWidth);
        ssaoCB.invBufferHeight = 1.0f / static_cast<float>(bufferHeight);

        const float screenSpaceDiameter = 10.0f;
        float thicknessMultiplier = 2.0f * tanHalfFovH * screenSpaceDiameter / bufferWidth;

        for (size_t i = 0; i < 7; i++)
        {
            ssaoCB.sampleThinkness[i] = thicknessMultiplier * sSampleThickness[i];
        }

        ghCommandList.SetDynamicConstantBufferView(kSSAOCBVs, sizeof(ssaoCB), &ssaoCB);
        //ghCommandList.SetDescriptorTable(kGbffers, GBufferSRVandDepthSRV);
        if (i == 0)
            ghCommandList.SetDescriptorTable(kSSAOSRVs, GBufferSRVandDepthSRV + 1);
        else
            ghCommandList.SetDescriptorTable(kSSAOSRVs, GetCurrentDownSampleSRV(frameIndex) + (UINT)(i - 1) + 4);
        ghCommandList.SetDescriptorTable(kSSAOUAVs, GetCurrentAOResultUAV(frameIndex) + (UINT)i);
        ghCommandList.Dispatch2D(bufferWidth, bufferHeight);
    }
}

void BlurAndUpSample(ComputeCommandList& ghCommandList, size_t frameIndex, DescriptorHandle GBufferSRVandDepthSRV,
    ColorBuffer& Gbuffer0, DepthBuffer& depthBuffer)
{
    for (size_t i = 0; i < NUM_SSAO_MERGES; i++)
        ghCommandList.TransitionResource(sSSAOMergeBuffers[frameIndex][i], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for (size_t i = 0; i < NUM_SSAO_MIP; i++)
        ghCommandList.TransitionResource(sSSAOSmoothBuffers[frameIndex][i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ghCommandList.SetPipelineState(*sSSAOUpSamplePSO);

    __declspec(align(256)) struct constantBuffer
    {
        UINT currentLevel;
        float invBufferWidth;
        float invBufferHeight;
    } ssaoCB;

    ColorBuffer* currentSmoothBuffer = &sSSAOSmoothBuffers[frameIndex][3];
    size_t bufferWidth = currentSmoothBuffer->GetWidth();
    size_t bufferHeight = currentSmoothBuffer->GetHeight();
    ssaoCB.invBufferWidth = 1.0f / static_cast<float>(bufferWidth);
    ssaoCB.invBufferHeight = 1.0f / static_cast<float>(bufferHeight);

    ssaoCB.currentLevel = 4;
    ghCommandList.SetDynamicConstantBufferView(kSSAOCBVs, sizeof(ssaoCB), &ssaoCB);
    ghCommandList.SetDescriptorTable(kSSAOSRVs, sSSAOUpSampleSRVs[frameIndex]);
    ghCommandList.SetDescriptorTable(kSSAOUAVs, GetCurrentAOSmoothUAV(frameIndex) + 3);
    ghCommandList.Dispatch2D(bufferWidth, bufferHeight);

    ssaoCB.currentLevel = 3;
    currentSmoothBuffer = &sSSAOSmoothBuffers[frameIndex][2];
    bufferWidth = currentSmoothBuffer->GetWidth();
    bufferHeight = currentSmoothBuffer->GetHeight();
    ssaoCB.invBufferWidth = 1.0f / static_cast<float>(bufferWidth);
    ssaoCB.invBufferHeight = 1.0f / static_cast<float>(bufferHeight);
    ghCommandList.SetDynamicConstantBufferView(kSSAOCBVs, sizeof(ssaoCB), &ssaoCB);
    ghCommandList.SetDescriptorTable(kSSAOSRVs, sSSAOUpSampleSRVs[frameIndex] + 6);
    ghCommandList.SetDescriptorTable(kSSAOUAVs, GetCurrentAOSmoothUAV(frameIndex) + 2);
    ghCommandList.TransitionResource(sSSAOSmoothBuffers[frameIndex][3], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ghCommandList.Dispatch2D(bufferWidth, bufferHeight);

    ssaoCB.currentLevel = 2;
    currentSmoothBuffer = &sSSAOSmoothBuffers[frameIndex][1];
    bufferWidth = currentSmoothBuffer->GetWidth();
    bufferHeight = currentSmoothBuffer->GetHeight();
    ssaoCB.invBufferWidth = 1.0f / static_cast<float>(bufferWidth);
    ssaoCB.invBufferHeight = 1.0f / static_cast<float>(bufferHeight);
    ghCommandList.SetDynamicConstantBufferView(kSSAOCBVs, sizeof(ssaoCB), &ssaoCB);
    ghCommandList.SetDescriptorTable(kSSAOSRVs, sSSAOUpSampleSRVs[frameIndex] + 12);
    ghCommandList.SetDescriptorTable(kSSAOUAVs, GetCurrentAOSmoothUAV(frameIndex) + 1);
    ghCommandList.TransitionResource(sSSAOSmoothBuffers[frameIndex][2], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ghCommandList.Dispatch2D(bufferWidth, bufferHeight);

    ssaoCB.currentLevel = 1;
    currentSmoothBuffer = &sSSAOSmoothBuffers[frameIndex][0];
    bufferWidth = currentSmoothBuffer->GetWidth();
    bufferHeight = currentSmoothBuffer->GetHeight();
    ssaoCB.invBufferWidth = 1.0f / static_cast<float>(bufferWidth);
    ssaoCB.invBufferHeight = 1.0f / static_cast<float>(bufferHeight);
    ghCommandList.SetDynamicConstantBufferView(kSSAOCBVs, sizeof(ssaoCB), &ssaoCB);
    ghCommandList.SetDescriptorTable(kGbffers, GBufferSRVandDepthSRV);
    ghCommandList.SetDescriptorTable(kSSAOSRVs, sSSAOUpSampleSRVs[frameIndex] + 18);
    ghCommandList.SetDescriptorTable(kSSAOUAVs, GetCurrentAOSmoothUAV(frameIndex));
    ghCommandList.TransitionResource(sSSAOSmoothBuffers[frameIndex][1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ghCommandList.Dispatch2D(bufferWidth, bufferHeight);

    ghCommandList.TransitionResource(sSSAOSmoothBuffers[frameIndex][0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}


void SSAORenderer::Initialize()
{
    ::Initialize();
    ::InitializeBuffer();
}

void SSAORenderer::RenderTaskSSAO(ComputeCommandList& ccCommandList, DescriptorHandle GBufferSRVandDepthSRV,
    ColorBuffer& Gbuffer0, DepthBuffer& depthBuffer)
{
    ccCommandList.PIXBeginEvent(L"SSAO");

    size_t frameIndex = CURRENT_FARME_BUFFER_INDEX;

    SSAODownSample(ccCommandList, frameIndex, GBufferSRVandDepthSRV, Gbuffer0, depthBuffer);

    ComputeSSAO(ccCommandList, frameIndex, 0.41421, GBufferSRVandDepthSRV, depthBuffer);

    BlurAndUpSample(ccCommandList, frameIndex, GBufferSRVandDepthSRV, Gbuffer0, depthBuffer);
    
    ccCommandList.PIXEndEvent();
}

DescriptorHandle SSAORenderer::GetSSAOFinalHandle(size_t frameIndex)
{
    return sSSAOSRVs[frameIndex] + 13;
}
