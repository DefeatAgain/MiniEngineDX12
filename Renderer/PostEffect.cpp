#include "PostEffect.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "GraphicsResource.h"
#include "FrameContext.h"
#include "PixelBuffer.h"
#include "SSAO.h"

namespace
{
    RootSignature* sPresentRS = nullptr;

    GraphicsPipelineState* sPresentSDRPS = nullptr;
    //GraphicsPipelineState* sCompositeSDRPS = GET_GPSO(L"PostEffect: CompositeSDR");
    GraphicsPipelineState* sScaleAndCompositeSDRPS = nullptr;

}

namespace PostEffectRenderer
{
    PostEffect* gPostEffectContext = nullptr;

    void Initialize()
    {
        Graphics::AddRSSTask([]() {
            sPresentRS = GET_RSO(L"Present");
            sPresentRS->Reset(4, 2);
            sPresentRS->GetParam(0).InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 2);
            sPresentRS->GetParam(1).InitAsConstants(0, 6, D3D12_SHADER_VISIBILITY_ALL);
            sPresentRS->GetParam(2).InitAsBufferSRV(2, D3D12_SHADER_VISIBILITY_PIXEL);
            sPresentRS->GetParam(3).InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 2);
            sPresentRS->InitStaticSampler(0, Graphics::SamplerLinearClampDesc);
            sPresentRS->InitStaticSampler(1, Graphics::SamplerPointClampDesc);
            sPresentRS->Finalize();

            ADD_SHADER("ScreenQuadCommonVS", L"ScreenQuadCommonVS.hlsl", kVS);
            ADD_SHADER("ScaleAndPresentSDRPS", L"ScaleAndCompositeSDRPS.hlsl", kPS);
            ADD_SHADER("PresentSDRPS", L"PresentSDRPS.hlsl", kPS);
        });

        Graphics::AddPSTask([]() {
            using namespace Graphics;
            sPresentSDRPS = GET_GPSO(L"PostEffect: PresentSDR");
        //GraphicsPipelineState* sCompositeSDRPS = GET_GPSO(L"PostEffect: CompositeSDR");
            sScaleAndCompositeSDRPS = GET_GPSO(L"PostEffect: ScaleAndCompositeSDR");

            sPresentSDRPS->SetRootSignature(*sPresentRS);
            sPresentSDRPS->SetBlendState(BlendDisable);
            sPresentSDRPS->SetDepthStencilState(DepthStateDisabled);
            sPresentSDRPS->SetRasterizerState(RasterizerTwoSided);
            sPresentSDRPS->SetSampleMask(D3D12_DEFAULT_SAMEPLE_MASK);
            sPresentSDRPS->SetInputLayout(0, nullptr);
            sPresentSDRPS->SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
            sPresentSDRPS->SetVertexShader(GET_SHADER("ScreenQuadCommonVS"));
            sPresentSDRPS->SetPixelShader(GET_SHADER("PresentSDRPS"));
            sPresentSDRPS->SetRenderTargetFormat(SWAP_CHAIN_FORMAT, DXGI_FORMAT_UNKNOWN);
            sPresentSDRPS->Finalize();

            *sScaleAndCompositeSDRPS = *sPresentSDRPS;
            sScaleAndCompositeSDRPS->SetPixelShader(GET_SHADER("ScaleAndPresentSDRPS"));
            sScaleAndCompositeSDRPS->Finalize();
//#define CreatePSO( ObjName, ShaderName ) \
//    *ObjName = *sPresentSDRPS; \
//    ObjName->SetRootSignature(PostEffectsRS); \
//    ObjName.SetComputeShader(GET_SHADER(ShaderName)); \
//    ObjName.Finalize();
//
//            CreatePSO(sPresentSDRPS, g_pToneMapCS);
//            CreatePSO(ToneMapHDRCS, g_pToneMapHDRCS);
//            CreatePSO(ApplyBloomCS, g_pApplyBloomCS);
//            CreatePSO(DebugLuminanceHdrCS, g_pDebugLuminanceHdrCS);
//            CreatePSO(DebugLuminanceLdrCS, g_pDebugLuminanceLdrCS);
//
//        CreatePSO(GenerateHistogramCS, g_pGenerateHistogramCS);
//        CreatePSO(DrawHistogramCS, g_pDebugDrawHistogramCS);
//        CreatePSO(AdaptExposureCS, g_pAdaptExposureCS);
//        CreatePSO(DownsampleBloom2CS, g_pDownsampleBloomCS);
//        CreatePSO(DownsampleBloom4CS, g_pDownsampleBloomAllCS);
//        CreatePSO(UpsampleAndBlurCS, g_pUpsampleAndBlurCS);
//        CreatePSO(BlurCS, g_pBlurCS);
//        CreatePSO(BloomExtractAndDownsampleHdrCS, g_pBloomExtractAndDownsampleHdrCS);
//        CreatePSO(BloomExtractAndDownsampleLdrCS, g_pBloomExtractAndDownsampleLdrCS);
//        CreatePSO(ExtractLumaCS, g_pExtractLumaCS);
//        CreatePSO(AverageLumaCS, g_pAverageLumaCS);
//        CreatePSO(CopyBackPostBufferCS, g_pCopyBackPostBufferCS);
//
//
//#undef CreatePSO
        });

    }
}


PostEffect::PostEffect()
{
}

void PostEffect::Initialize()
{
    PostEffectRenderer::Initialize();
    SSAORenderer::Initialize();
}

void PostEffect::Update(float deltaTime)
{
    //if (!mTextureGpuHandles)
    //{
    //    mTextureGpuHandles = ALLOC_DESCRIPTOR_GPU(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    //}
}

void PostEffect::Render()
{
    PUSH_MUTIRENDER_TASK({ D3D12_COMMAND_LIST_TYPE_DIRECT, PushGraphicsTaskBind(&PostEffect::RenderTaskToneMapping, this) });
}

CommandList* PostEffect::RenderTaskToneMapping(CommandList* commandList)
{
    using namespace PostEffectRenderer;

    GraphicsCommandList& ghCommandList = commandList->GetGraphicsCommandList().Begin(L"ToneMapping");
    ghCommandList.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (Graphics::gDisplayWidth == Graphics::gRenderWidth && Graphics::gDisplayHeight == Graphics::gRenderHeight)
    {
        ghCommandList.SetPipelineState(*sPresentSDRPS);
    }
    else
    {
        ghCommandList.SetPipelineState(*sScaleAndCompositeSDRPS);
        ghCommandList.SetConstants(1, 0.7071f / Graphics::gRenderWidth, 0.7071f / Graphics::gRenderHeight);
    }
    ghCommandList.SetDescriptorTable(0, CURRENT_SCENE_COLOR_BUFFERSRV);
    ghCommandList.ExceptResourceBeginState(CURRENT_SCENE_COLOR_BUFFER, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ghCommandList.SetViewportAndScissor(0, 0, Graphics::gDisplayWidth, Graphics::gDisplayHeight);
    ghCommandList.SetRenderTarget(CURRENT_SWAP_CHAIN.GetRTV());
    ghCommandList.Draw(3);
    ghCommandList.Finish();
    return commandList;
}

void PostEffect::OnResizeSceneBuffer(uint32_t width, uint32_t height)
{
}
