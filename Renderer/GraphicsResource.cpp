#include "GraphicsResource.h"
#include "Texture.h"
#include "ShaderCompositor.h"

namespace Graphics
{
    SamplerDesc SamplerLinearWrapDesc;
    SamplerDesc SamplerAnisoWrapDesc;
    SamplerDesc SamplerShadowDescGE;
    SamplerDesc SamplerShadowDescLE;
    SamplerDesc SamplerLinearClampDesc;
    SamplerDesc SamplerVolumeWrapDesc;
    SamplerDesc SamplerPointClampDesc;
    SamplerDesc SamplerPointBorderDesc;
    SamplerDesc SamplerLinearBorderDesc;

    DescriptorHandle SamplerLinearWrap;
    DescriptorHandle SamplerAnisoWrap;
    DescriptorHandle SamplerShadowGE;
    DescriptorHandle SamplerShadowLE;
    DescriptorHandle SamplerLinearClamp;
    DescriptorHandle SamplerVolumeWrap;
    DescriptorHandle SamplerPointClamp;
    DescriptorHandle SamplerPointBorder;
    DescriptorHandle SamplerLinearBorder;

    Texture DefaultTextures[kNumDefaultTextures];
    Texture& GetDefaultTexture(eDefaultTexture texID)
    {
        ASSERT(texID < kNumDefaultTextures);
        if (!DefaultTextures[texID].isValid())
            DefaultTextures[texID].ForceWaitContext();
        return DefaultTextures[texID];
    }

    D3D12_RASTERIZER_DESC RasterizerDefault;	// Counter-clockwise
    D3D12_RASTERIZER_DESC RasterizerDefaultMsaa;
    D3D12_RASTERIZER_DESC RasterizerDefaultCw;	// Clockwise winding
    D3D12_RASTERIZER_DESC RasterizerDefaultCwMsaa;
    D3D12_RASTERIZER_DESC RasterizerTwoSided;
    D3D12_RASTERIZER_DESC RasterizerTwoSidedMsaa;
    D3D12_RASTERIZER_DESC RasterizerShadow;
    D3D12_RASTERIZER_DESC RasterizerShadowCW;
    D3D12_RASTERIZER_DESC RasterizerShadowTwoSided;

    D3D12_BLEND_DESC BlendNoColorWrite;
    D3D12_BLEND_DESC BlendDisable;
    D3D12_BLEND_DESC BlendPreMultiplied;
    D3D12_BLEND_DESC BlendTraditional;
    D3D12_BLEND_DESC BlendAdditive;
    D3D12_BLEND_DESC BlendTraditionalAdditive;

    D3D12_DEPTH_STENCIL_DESC DepthStateDisabled;
    D3D12_DEPTH_STENCIL_DESC DepthStateReadWrite;
    D3D12_DEPTH_STENCIL_DESC DepthStateReadOnly;
    D3D12_DEPTH_STENCIL_DESC DepthStateReadOnlyReversed;
    D3D12_DEPTH_STENCIL_DESC DepthStateTestEqual;

    ComputePipelineState* gGenerateMipsLinearPSO[4];
    ComputePipelineState* gGenerateMipsSRGBPSO[4];
    ComputePipelineState* gDepthResloveMsaaPSO;
    ComputePipelineState* gDepthArrayResloveMsaaPSO;
    RootSignature* gCommonRS;
    
    void InitRootSigAndShader()
    {
        gCommonRS = GET_RSO(L"CommonRS");
        gCommonRS->Reset(4, 3);
        gCommonRS->GetParam(0).InitAsConstants(0, 4);
        gCommonRS->GetParam(1).InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 10);
        gCommonRS->GetParam(2).InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 10);
        gCommonRS->GetParam(3).InitAsConstantBuffer(1);
        gCommonRS->InitStaticSampler(0, SamplerLinearClampDesc);
        gCommonRS->InitStaticSampler(1, SamplerPointBorderDesc);
        gCommonRS->InitStaticSampler(2, SamplerLinearBorderDesc);
        gCommonRS->Finalize();

        ADD_SHADER("GenMipsLinear", "GenerateMipsCS.hlsl", kCS);
        ADD_SHADER("GenMipsLinearOddX", "GenerateMipsCS.hlsl", kCS, { "NON_POWER_OF_TWO", "1" });
        ADD_SHADER("GenMipsLinearOddY", "GenerateMipsCS.hlsl", kCS, { "NON_POWER_OF_TWO", "2" });
        ADD_SHADER("GenMipsLinearOddXY", "GenerateMipsCS.hlsl", kCS, { "NON_POWER_OF_TWO", "3" });

        ADD_SHADER("GenMipsSRGB", "GenerateMipsCS.hlsl", kCS);
        ADD_SHADER("GenMipsSRGBOddX", "GenerateMipsCS.hlsl", kCS, { "CONVERT_TO_SRGB", "", "NON_POWER_OF_TWO", "1" });
        ADD_SHADER("GenMipsSRGBOddY", "GenerateMipsCS.hlsl", kCS, { "CONVERT_TO_SRGB", "", "NON_POWER_OF_TWO", "2" });
        ADD_SHADER("GenMipsSRGBOddXY", "GenerateMipsCS.hlsl", kCS, { "CONVERT_TO_SRGB", "", "NON_POWER_OF_TWO", "3" });

        ADD_SHADER("DepthResolveMsaa", "DepthResolveMsaa.hlsl", kCS);
        ADD_SHADER("DepthArrayResolveMsaa", "DepthResolveMsaa.hlsl", kCS, { "USE_ARRAY", "" });

        // custom
        for (auto& initTask : gCustomRootSigShaderTasks)
            initTask();

        RootSignatureManager::GetInstance()->InitAllRootSignatures();
        ShaderCompositor::GetInstance()->InitAllShaders();
    }

    void InitPipeLineStat()
    {
        gGenerateMipsLinearPSO[0] = GET_CPSO(L"GenMipsLinear");
        gGenerateMipsLinearPSO[1] = GET_CPSO(L"GenMipsLinearOddX");
        gGenerateMipsLinearPSO[2] = GET_CPSO(L"GenMipsLinearOddY");
        gGenerateMipsLinearPSO[3] = GET_CPSO(L"GenMipsLinearOddXY");
        gGenerateMipsSRGBPSO[0] = GET_CPSO(L"GenMipsSRGB");
        gGenerateMipsSRGBPSO[1] = GET_CPSO(L"GenMipsSRGBOddX");
        gGenerateMipsSRGBPSO[2] = GET_CPSO(L"GenMipsSRGBOddY");
        gGenerateMipsSRGBPSO[3] = GET_CPSO(L"GenMipsSRGBOddXY");
        gDepthResloveMsaaPSO = GET_CPSO(L"DepthResolveMsaa");
        gDepthArrayResloveMsaaPSO = GET_CPSO(L"DepthArrayResolveMsaa");

        for (auto& initTask : gCustomPipeStatTasks)
            initTask();


#define CreatePSO(ObjName, shaderUnit ) \
    ObjName->SetRootSignature(*gCommonRS); \
    ObjName->SetComputeShader(shaderUnit); \
    ObjName->Finalize();

        CreatePSO(gGenerateMipsLinearPSO[0], GET_SHADER("GenMipsLinear"));
        CreatePSO(gGenerateMipsLinearPSO[1], GET_SHADER("GenMipsLinearOddX"));
        CreatePSO(gGenerateMipsLinearPSO[2], GET_SHADER("GenMipsLinearOddY"));
        CreatePSO(gGenerateMipsLinearPSO[3], GET_SHADER("GenMipsLinearOddXY"));
        CreatePSO(gGenerateMipsLinearPSO[0], GET_SHADER("GenMipsSRGB"));
        CreatePSO(gGenerateMipsLinearPSO[1], GET_SHADER("GenMipsSRGBOddX"));
        CreatePSO(gGenerateMipsLinearPSO[2], GET_SHADER("GenMipsSRGBOddY"));
        CreatePSO(gGenerateMipsLinearPSO[3], GET_SHADER("GenMipsSRGBOddXY"));
        CreatePSO(gDepthResloveMsaaPSO, GET_SHADER("DepthResolveMsaa"));
        CreatePSO(gDepthArrayResloveMsaaPSO, GET_SHADER("DepthArrayResolveMsaa"));
#undef CreatePSO

        PipeLineStateManager::GetInstance()->InitAllPipeLineStates();
    }

    void InitializeResource()
    {
        SamplerLinearWrapDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        SamplerLinearWrap = SamplerLinearWrapDesc.CreateDescriptor();

        SamplerAnisoWrapDesc.MaxAnisotropy = 4;
        SamplerAnisoWrap = SamplerAnisoWrapDesc.CreateDescriptor();

        SamplerShadowDescGE.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        SamplerShadowDescGE.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        SamplerShadowDescGE.SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        SamplerShadowGE = SamplerShadowDescGE.CreateDescriptor();

        SamplerShadowDescLE.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        SamplerShadowDescLE.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        SamplerShadowDescLE.SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        SamplerShadowLE = SamplerShadowDescLE.CreateDescriptor();

        SamplerLinearClampDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        SamplerLinearClampDesc.SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        SamplerLinearClamp = SamplerLinearClampDesc.CreateDescriptor();

        SamplerVolumeWrapDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        SamplerVolumeWrap = SamplerVolumeWrapDesc.CreateDescriptor();

        SamplerPointClampDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        SamplerPointClampDesc.SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        SamplerPointClamp = SamplerPointClampDesc.CreateDescriptor();

        SamplerLinearBorderDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        SamplerLinearBorderDesc.SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE_BORDER);
        SamplerLinearBorderDesc.SetBorderColor(Color(0.0f, 0.0f, 0.0f, 0.0f));
        SamplerLinearBorder = SamplerLinearBorderDesc.CreateDescriptor();

        SamplerPointBorderDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        SamplerPointBorderDesc.SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE_BORDER);
        SamplerPointBorderDesc.SetBorderColor(Color(0.0f, 0.0f, 0.0f, 0.0f));
        SamplerPointBorder = SamplerPointBorderDesc.CreateDescriptor();

        DefaultTextures[kMagenta2D].mName = WSTRINGIFY(kMagenta2D);
        DefaultTextures[kBlackOpaque2D].mName = WSTRINGIFY(kBlackOpaque2D);
        DefaultTextures[kBlackTransparent2D].mName = WSTRINGIFY(kBlackTransparent2D);
        DefaultTextures[kWhiteOpaque2D].mName = WSTRINGIFY(kWhiteOpaque2D);
        DefaultTextures[kWhiteTransparent2D].mName = WSTRINGIFY(kWhiteTransparent2D);
        DefaultTextures[kDefaultNormalMap].mName = WSTRINGIFY(kDefaultNormalMap);
        DefaultTextures[kBlackCubeMap].mName = WSTRINGIFY(kBlackCubeMap);
        uint32_t MagentaPixel = 0xFFFF00FF;
        DefaultTextures[kMagenta2D].Create2D(4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &MagentaPixel);
        uint32_t BlackOpaqueTexel = 0xFF000000;
        DefaultTextures[kBlackOpaque2D].Create2D(4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &BlackOpaqueTexel);
        uint32_t BlackTransparentTexel = 0x00000000;
        DefaultTextures[kBlackTransparent2D].Create2D(4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &BlackTransparentTexel);
        uint32_t WhiteOpaqueTexel = 0xFFFFFFFF;
        DefaultTextures[kWhiteOpaque2D].Create2D(4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &WhiteOpaqueTexel);
        uint32_t WhiteTransparentTexel = 0x00FFFFFF;
        DefaultTextures[kWhiteTransparent2D].Create2D(4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &WhiteTransparentTexel);
        uint32_t FlatNormalTexel = 0x00FF8080;
        DefaultTextures[kDefaultNormalMap].Create2D(4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &FlatNormalTexel);
        uint32_t BlackCubeTexels[6] = {};
        DefaultTextures[kBlackCubeMap].CreateCube(4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, BlackCubeTexels);

        // Default rasterizer states
        RasterizerDefault.FillMode = D3D12_FILL_MODE_SOLID;
        RasterizerDefault.CullMode = D3D12_CULL_MODE_BACK;
        RasterizerDefault.FrontCounterClockwise = TRUE;
        RasterizerDefault.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        RasterizerDefault.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        RasterizerDefault.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        RasterizerDefault.DepthClipEnable = TRUE;
        RasterizerDefault.MultisampleEnable = FALSE;
        RasterizerDefault.AntialiasedLineEnable = FALSE;
        RasterizerDefault.ForcedSampleCount = 0;
        RasterizerDefault.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        RasterizerDefaultMsaa = RasterizerDefault;
        RasterizerDefaultMsaa.MultisampleEnable = TRUE;

        RasterizerDefaultCw = RasterizerDefault;
        RasterizerDefaultCw.FrontCounterClockwise = FALSE;

        RasterizerDefaultCwMsaa = RasterizerDefaultCw;
        RasterizerDefaultCwMsaa.MultisampleEnable = TRUE;

        RasterizerTwoSided = RasterizerDefault;
        RasterizerTwoSided.CullMode = D3D12_CULL_MODE_NONE;

        RasterizerTwoSidedMsaa = RasterizerTwoSided;
        RasterizerTwoSidedMsaa.MultisampleEnable = TRUE;

        // Shadows need their own rasterizer state so we can reverse the winding of faces
        RasterizerShadow = RasterizerDefault;
        //RasterizerShadow.CullMode = D3D12_CULL_FRONT;  // Hacked here rather than fixing the content
        RasterizerShadow.SlopeScaledDepthBias = -1.5f;
        RasterizerShadow.DepthBias = -100;

        RasterizerShadowTwoSided = RasterizerShadow;
        RasterizerShadowTwoSided.CullMode = D3D12_CULL_MODE_NONE;

        RasterizerShadowCW = RasterizerShadow;
        RasterizerShadowCW.FrontCounterClockwise = FALSE;

        DepthStateDisabled.DepthEnable = FALSE;
        DepthStateDisabled.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        DepthStateDisabled.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        DepthStateDisabled.StencilEnable = FALSE;
        DepthStateDisabled.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        DepthStateDisabled.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
        DepthStateDisabled.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        DepthStateDisabled.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        DepthStateDisabled.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
        DepthStateDisabled.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        DepthStateDisabled.BackFace = DepthStateDisabled.FrontFace;

        DepthStateReadWrite = DepthStateDisabled;
        DepthStateReadWrite.DepthEnable = TRUE;
        DepthStateReadWrite.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        DepthStateReadWrite.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;

        DepthStateReadOnly = DepthStateReadWrite;
        DepthStateReadOnly.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

        DepthStateReadOnlyReversed = DepthStateReadOnly;
        DepthStateReadOnlyReversed.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

        DepthStateTestEqual = DepthStateReadOnly;
        DepthStateTestEqual.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;

        D3D12_BLEND_DESC alphaBlend = {};
        alphaBlend.IndependentBlendEnable = FALSE;
        alphaBlend.RenderTarget[0].BlendEnable = FALSE;
        alphaBlend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        alphaBlend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        alphaBlend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        alphaBlend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        alphaBlend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        alphaBlend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        alphaBlend.RenderTarget[0].RenderTargetWriteMask = 0;
        BlendNoColorWrite = alphaBlend;

        alphaBlend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        BlendDisable = alphaBlend;

        alphaBlend.RenderTarget[0].BlendEnable = TRUE;
        BlendTraditional = alphaBlend;

        alphaBlend.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        BlendPreMultiplied = alphaBlend;

        alphaBlend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        BlendAdditive = alphaBlend;

        alphaBlend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        BlendTraditionalAdditive = alphaBlend;

        //DispatchIndirectCommandSignature[0].Dispatch();
        //DispatchIndirectCommandSignature.Finalize();

        //DrawIndirectCommandSignature[0].Draw();
        //DrawIndirectCommandSignature.Finalize();

        //BitonicSort::Initialize();

        // ------------ Shader And RootSignarture Must be Inited Before PSO
        InitRootSigAndShader();

        InitPipeLineStat();
    }

    void DestroyResource()
    {
        for (uint32_t i = 0; i < kNumDefaultTextures; ++i)
            DefaultTextures[i].Destroy();

        SamplerLinearWrapDesc.DestroyDescriptor(SamplerLinearWrap);
        SamplerAnisoWrapDesc.DestroyDescriptor(SamplerAnisoWrap);
        SamplerShadowDescGE.DestroyDescriptor(SamplerShadowGE);
        SamplerLinearClampDesc.DestroyDescriptor(SamplerLinearClamp);
        SamplerVolumeWrapDesc.DestroyDescriptor(SamplerVolumeWrap);
        SamplerPointClampDesc.DestroyDescriptor(SamplerPointClamp);
        SamplerPointBorderDesc.DestroyDescriptor(SamplerPointBorder);
        SamplerLinearBorderDesc.DestroyDescriptor(SamplerLinearBorder);
    }
}