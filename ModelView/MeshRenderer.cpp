#include "MeshRenderer.h"
#include "GraphicsResource.h"
#include "PixelBuffer.h"
#include "FrameContext.h"
#include "CommandQueue.h"
#include "Mesh.h"
#include "Material.h"
#include "Scene.h"
#include "Model.h"

#include <sstream>

namespace ModelRenderer
{
    std::map<size_t, uint32_t> sPSOIndices;
    GraphicsPipelineState sDefaultPSO; // Not finalized.  Used as a template.
    GraphicsPipelineState sDefaultShadowPSO; // Not finalized.  Used as a template.
    GraphicsPipelineState sFullScreenPSO;

    std::wstring sShadowMapName = L"Shadow Map";
    uint32_t sShadowMapSize = 2048;

    std::vector<GraphicsPipelineState*> sAllPSOs;
    RootSignature* sForwardRootSig = nullptr;
    RootSignature* sDeferredRootSig = nullptr;
    GraphicsPipelineState* sSkyboxPSO = nullptr;

    ShadowBuffer sShadowBuffer[SWAP_CHAIN_BUFFER_COUNT];
    ColorBuffer sNonMsaaShadowBuffer[SWAP_CHAIN_BUFFER_COUNT];

    ColorBuffer sGubffer0[SWAP_CHAIN_BUFFER_COUNT];

    uint32_t gMsaaShadowSample = 2;
    uint32_t gNumCSMDivides = 3;
    float gCSMDivides[] = { 0.15f, 0.35f, 0.65f };
}

void DestroyShadowBuffers()
{
    using namespace ModelRenderer;
    for (size_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
    {
        sShadowBuffer[i].Destroy();
        sNonMsaaShadowBuffer[i].Destroy();
    }
}

void CreateShadowBuffers()
{
    using namespace ModelRenderer;

    DestroyShadowBuffers();

    for (size_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
    {
        if (gMsaaShadowSample > 0)
        {
            sShadowBuffer[i].Create(sShadowMapName + L" Msaa", sShadowMapSize, sShadowMapSize, gNumCSMDivides + 1,
                gMsaaShadowSample, SHADOW_MAP_FORMAT);
            sNonMsaaShadowBuffer[i].CreateArray(sShadowMapName + L" None Msaa", sShadowMapSize, sShadowMapSize, gNumCSMDivides + 1,
                PixelBuffer::GetSRVFormat(SHADOW_MAP_FORMAT));
        }
        else
        {
            sShadowBuffer[i].Create(sShadowMapName, sShadowMapSize, sShadowMapSize, gNumCSMDivides + 1, 1, SHADOW_MAP_FORMAT);
        }
    }
}

const ShaderUnit& GetShader(const std::filesystem::path& filename, eShaderType shaderType, const std::vector<std::string>& allMacros = {})
{
    std::stringstream sstrem;
    sstrem << filename.generic_string() << " ";
    for (const std::string& macro : allMacros)
        sstrem << macro;
    return ADD_SHADER_VEC(sstrem.str(), filename, shaderType, allMacros);
}

void ModelRenderer::Initialize()
{
    CreateShadowBuffers();

#ifdef DEFERRED_RENDER
    for (size_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
    {
        sGubffer0[i].Create(L"GBuffer 0", Graphics::gDisplayWidth, Graphics::gDisplayHeight, 1, DEFERRED_GBUFFER0_FORMAT);
    }
#endif // DEFERRED_RENDER


    Graphics::AddRSSTask([]()
    {
        SamplerDesc DefaultSamplerDesc;
        DefaultSamplerDesc.MaxAnisotropy = 8;

        SamplerDesc CubeMapSamplerDesc = DefaultSamplerDesc;

        sForwardRootSig = GET_RSO(L"MeshRendererForward RSO");
        sForwardRootSig->Reset(kNumRootBindings, 3);
        sForwardRootSig->InitStaticSampler(10, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
        sForwardRootSig->InitStaticSampler(11, Graphics::SamplerPointClampDesc, D3D12_SHADER_VISIBILITY_PIXEL);
        sForwardRootSig->InitStaticSampler(12, Graphics::SamplerShadowDescGE, D3D12_SHADER_VISIBILITY_PIXEL);
        sForwardRootSig->GetParam(kMeshConstants).InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
        sForwardRootSig->GetParam(kMaterialConstants).InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
        sForwardRootSig->GetParam(kGlobalConstants).InitAsConstantBuffer(1, D3D12_SHADER_VISIBILITY_ALL);

        sForwardRootSig->GetParam(kModelTextures).InitAsDescriptorRange(
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 8, D3D12_SHADER_VISIBILITY_PIXEL);
        sForwardRootSig->GetParam(kModelTextureSamplers).InitAsDescriptorRange(
            D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0, 8, D3D12_SHADER_VISIBILITY_PIXEL);
        sForwardRootSig->GetParam(kSceneTextures).InitAsDescriptorRange(
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 8, D3D12_SHADER_VISIBILITY_PIXEL);
        sForwardRootSig->GetParam(kShadowTexture).InitAsDescriptorRange(
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 18, 1, D3D12_SHADER_VISIBILITY_PIXEL);
        sForwardRootSig->Finalize(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);


        sDeferredRootSig = GET_RSO(L"MeshRendererDeferred RSO");
        sDeferredRootSig->Reset(kNumRootBindingsDeferred, 3);
        sDeferredRootSig->InitStaticSampler(10, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
        sDeferredRootSig->InitStaticSampler(11, Graphics::SamplerPointClampDesc, D3D12_SHADER_VISIBILITY_PIXEL);
        sDeferredRootSig->InitStaticSampler(12, Graphics::SamplerShadowDescGE, D3D12_SHADER_VISIBILITY_PIXEL);
        sDeferredRootSig->GetParam(kGlobalConstantsDeferred).InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);

        sDeferredRootSig->GetParam(kGBufferTextures).InitAsDescriptorRange(
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 4, D3D12_SHADER_VISIBILITY_PIXEL);
        sDeferredRootSig->GetParam(kSceneTexturesDeferred).InitAsDescriptorRange(
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 8, D3D12_SHADER_VISIBILITY_PIXEL);
        sDeferredRootSig->GetParam(kShadowTextureDeferred).InitAsDescriptorRange(
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 18, 1, D3D12_SHADER_VISIBILITY_PIXEL);
        sDeferredRootSig->Finalize(D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS);

        ADD_SHADER("SkyBoxVS", L"MeshRender/SkyBoxVS.hlsl", kVS);
        ADD_SHADER("SkyBoxPS", L"MeshRender/SkyBoxPS.hlsl", kPS);
        ADD_SHADER("ForwardVS", L"MeshRender/ForwardVS.hlsl", kVS);
        ADD_SHADER("ForwardPS", L"MeshRender/ForwardPS.hlsl", kPS, { "REVERSED_Z", "" });
        ADD_SHADER("DepthOnlyVS", L"MeshRender/DepthOnlyVS.hlsl", kVS);
        ADD_SHADER("DepthOnlyPS", L"MeshRender/DepthOnlyPS.hlsl", kPS);
        ADD_SHADER("DepthOnlyVSCutOff", L"MeshRender/DepthOnlyVS.hlsl", kVS, { "ENABLE_ALPHATEST", "" });
        ADD_SHADER("DepthOnlyPSCutOff", L"MeshRender/DepthOnlyPS.hlsl", kPS, { "ENABLE_ALPHATEST", "" });
        ADD_SHADER("DeferredPS", L"MeshRender/DeferredPS.hlsl", kPS);
    });

    Graphics::AddPSTask([]()
    {
        PipeLineStateManager* psoInstance = PipeLineStateManager::GetInstance();

        GraphicsPipelineState* depthOnlyPSO = GET_GPSO(L"ModelRender: Depth Only PSO");
        GraphicsPipelineState* depthOnlyCutOffPSO = GET_GPSO(L"ModelRender: Depth Only CutOff PSO");
        sSkyboxPSO = GET_GPSO(L"ModelRender: SkyBox PSO");
        sAllPSOs.push_back(depthOnlyPSO);
        sAllPSOs.push_back(depthOnlyCutOffPSO);

        D3D12_INPUT_ELEMENT_DESC posOnly[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_INPUT_ELEMENT_DESC posAndUV[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        depthOnlyPSO->SetRootSignature(*sForwardRootSig);
        depthOnlyPSO->SetRasterizerState(Graphics::RasterizerDefault);
        depthOnlyPSO->SetBlendState(Graphics::BlendNoColorWrite);
        depthOnlyPSO->SetDepthStencilState(Graphics::DepthStateReadWrite);
        depthOnlyPSO->SetInputLayout(ARRAYSIZE(posOnly), posOnly);
        depthOnlyPSO->SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        depthOnlyPSO->SetVertexShader(GET_SHADER("DepthOnlyVS"));
        depthOnlyPSO->SetPixelShader(GET_SHADER("DepthOnlyPS"));
        depthOnlyPSO->SetRenderTargetFormats(0, nullptr, DSV_FORMAT);
        depthOnlyPSO->Finalize();

        sDefaultShadowPSO = *depthOnlyPSO;
        *depthOnlyCutOffPSO = *depthOnlyPSO;
        depthOnlyCutOffPSO->SetInputLayout(ARRAYSIZE(posAndUV), posAndUV);
        depthOnlyCutOffPSO->SetRasterizerState(Graphics::RasterizerTwoSided);
        depthOnlyCutOffPSO->SetVertexShader(GET_SHADER("DepthOnlyVSCutOff"));
        depthOnlyCutOffPSO->SetPixelShader(GET_SHADER("DepthOnlyPSCutOff"));
        depthOnlyCutOffPSO->Finalize();

        DXGI_FORMAT renderFormat = HDR_FORMAT;
        sDefaultPSO.SetRootSignature(*sForwardRootSig);
        sDefaultPSO.SetRasterizerState(Graphics::RasterizerDefault);
        sDefaultPSO.SetBlendState(Graphics::BlendDisable);
        sDefaultPSO.SetDepthStencilState(Graphics::DepthStateTestEqual);
        sDefaultPSO.SetInputLayout(0, nullptr);
        sDefaultPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        sDefaultPSO.SetRenderTargetFormats(1, &renderFormat, DSV_FORMAT);
        sDefaultPSO.SetVertexShader(GET_SHADER("ForwardVS"));
        sDefaultPSO.SetPixelShader(GET_SHADER("ForwardPS"));

        *sSkyboxPSO = sDefaultPSO;
        sSkyboxPSO->SetDepthStencilState(Graphics::DepthStateReadOnly);
        sSkyboxPSO->SetVertexShader(GET_SHADER("SkyBoxVS"));
        sSkyboxPSO->SetPixelShader(GET_SHADER("SkyBoxPS"));
        sSkyboxPSO->Finalize();

        sFullScreenPSO.SetBlendState(Graphics::BlendDisable);
        sFullScreenPSO.SetDepthStencilState(Graphics::DepthStateDisabled);
        sFullScreenPSO.SetRasterizerState(Graphics::RasterizerTwoSided);
        sFullScreenPSO.SetSampleMask(D3D12_DEFAULT_SAMEPLE_MASK);
        sFullScreenPSO.SetInputLayout(0, nullptr);
        sFullScreenPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        sFullScreenPSO.SetVertexShader(GET_SHADER("ScreenQuadCommonVS"));
        sFullScreenPSO.SetPixelShader(GET_SHADER("DeferredPS"));
        sFullScreenPSO.SetRenderTargetFormat(renderFormat, DXGI_FORMAT_UNKNOWN);

        //sDeferredFinalPSO = GET_GPSO(L"DeferredFinal PSO");
        //*sDeferredFinalPSO = sFullScreenPSO;
        //sDeferredFinalPSO->SetRootSignature(*sDeferredRootSig);
        //sDeferredFinalPSO->Finalize();
    });
}

void ModelRenderer::Destroy()
{
    DestroyShadowBuffers();
}


uint16_t GetDepthPsoIndex(ModelRenderer::RendererPsoDesc rendererPsoDesc, size_t psoHash)
{
    using namespace ModelRenderer;

    if (!rendererPsoDesc.isShadow)
    {
        uint16_t depthPso = 0;
        if (rendererPsoDesc.meshPSOFlags & ePSOFlags::kAlphaTest)
            depthPso += 1;
        return depthPso;
    }

    GraphicsPipelineState* shadowPSO;
    shadowPSO = GET_GPSO(std::wstring(L"MeshRenderer: shadow PSO ") + std::to_wstring(rendererPsoDesc.depthPsoFlags));
    *shadowPSO = sDefaultShadowPSO;

    uint32_t msaaCount = 1;
    if (rendererPsoDesc.shadowMsaaCount > 0)
        msaaCount = 1 << rendererPsoDesc.shadowMsaaCount;

    shadowPSO->SetRenderTargetFormats(0, nullptr, SHADOW_MAP_FORMAT, msaaCount);

    if (rendererPsoDesc.meshPSOFlags & ePSOFlags::kAlphaTest)
    {
        if (rendererPsoDesc.shadowMsaaCount > 0)
            shadowPSO->SetRasterizerState(Graphics::RasterizerTwoSidedMsaa);
        else
            shadowPSO->SetRasterizerState(Graphics::RasterizerTwoSided);
    }
    shadowPSO->Finalize();

    sAllPSOs.push_back(shadowPSO);
    uint16_t psoIndex = sAllPSOs.size() - 1;
    sPSOIndices[psoHash] = psoIndex;

    return psoIndex;
}

uint16_t GetPsoIndexUnLocked(ModelRenderer::RendererPsoDesc rendererPsoDesc, size_t psoHash)
{
    using namespace ModelRenderer;

    //static std::mutex sMutex;
    //std::lock_guard<std::mutex> lockGuard(sMutex);

    if (rendererPsoDesc.isDepth)
        return GetDepthPsoIndex(rendererPsoDesc, psoHash);

    ePSOFlags psoFlags = (ePSOFlags)rendererPsoDesc.meshPSOFlags;
    ASSERT(psoFlags <= 65536);

    GraphicsPipelineState* colorPSO;
    colorPSO = GET_GPSO(std::wstring(L"MeshRenderer: PSO ") + std::to_wstring(psoFlags));

    *colorPSO = sDefaultPSO;

    uint16_t Requirements = kHasPosition | kHasNormal | kHasTangent;
    ASSERT((psoFlags & Requirements) == Requirements);

    std::vector<D3D12_INPUT_ELEMENT_DESC> vertexLayout;
    vertexLayout.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT });
    vertexLayout.push_back({ "NORMAL",   0, DXGI_FORMAT_R10G10B10A2_UNORM,     0, D3D12_APPEND_ALIGNED_ELEMENT });
    vertexLayout.push_back({ "TANGENT",  0, DXGI_FORMAT_R10G10B10A2_UNORM,     0, D3D12_APPEND_ALIGNED_ELEMENT });
    vertexLayout.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT });

    if (psoFlags & kHasUV1)
        vertexLayout.push_back({ "TEXCOORD", 1, DXGI_FORMAT_R16G16_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT });

    colorPSO->SetInputLayout((uint32_t)vertexLayout.size(), vertexLayout.data());

    std::vector<std::string> macros = { "REVERSED_Z", "" };

#ifdef DEFERRED_RENDER
    DXGI_FORMAT renderFormat = DEFERRED_GBUFFER0_FORMAT;
    colorPSO->SetDepthStencilState(Graphics::DepthStateReadWrite);
    colorPSO->SetRenderTargetFormats(1, &renderFormat, DSV_FORMAT);

    const std::wstring vsFile = L"MeshRender/GBufferVS.hlsl";
    const std::wstring psFile = L"MeshRender/GBufferPS.hlsl";

    if (psoFlags & kAlphaTest)
    {
        macros.push_back("ENABLE_ALPHATEST");
        macros.push_back("");
    }
#else
    const std::wstring vsFile = L"MeshRender/ForwardVS.hlsl";
    const std::wstring psFile = L"MeshRender/ForwardPS.hlsl";
#endif // DEFERRED_RENDER
    

    if (psoFlags & kHasUV1)
    {
        macros.push_back("SECOND_UV");
        macros.push_back("");
    }
    if (rendererPsoDesc.numCSMDividesCount > 0)
    {
        macros.push_back("NUM_CSM_SHADOW_MAP");
        macros.push_back(std::to_string(rendererPsoDesc.numCSMDividesCount + 1));
    }

    colorPSO->SetVertexShader(GetShader(vsFile, kVS, macros));
    colorPSO->SetPixelShader(GetShader(psFile, kPS, macros));

    if (psoFlags & kAlphaBlend)
    {
        colorPSO->SetBlendState(Graphics::BlendTraditional);
        colorPSO->SetDepthStencilState(Graphics::DepthStateReadOnly);
    }
    if (psoFlags & kTwoSided)
    {
        colorPSO->SetRasterizerState(Graphics::RasterizerTwoSided);
    }
    colorPSO->Finalize();

    sAllPSOs.push_back(colorPSO);
    uint16_t psoIndex = sAllPSOs.size() - 1;
    sPSOIndices[psoHash] = psoIndex;

    return psoIndex;
}

uint16_t ModelRenderer::GetPsoIndex(RendererPsoDesc rendererPsoDesc)
{
    size_t psoHash = Utility::HashState(&rendererPsoDesc);
    auto psoIndexIter = sPSOIndices.find(psoHash);
    if (psoIndexIter != sPSOIndices.end())
    {
        return psoIndexIter->second;
    }
    
    return GetPsoIndexUnLocked(rendererPsoDesc, psoHash);
}

uint16_t ModelRenderer::GetFullScreenPsoIndex(RendererPsoDesc rendererPsoDesc)
{
    size_t psoHash = Utility::HashState(&rendererPsoDesc);
    auto psoIndexIter = sPSOIndices.find(psoHash);
    if (psoIndexIter != sPSOIndices.end())
    {
        return psoIndexIter->second;
    }

    GraphicsPipelineState* colorPSO;
    colorPSO = GET_GPSO(std::wstring(L"FullScreenPSO ") + std::to_wstring(rendererPsoDesc.fullScreenFlags));
    *colorPSO = ModelRenderer::sFullScreenPSO;

    const std::wstring psFile = L"MeshRender/DeferredPS.hlsl";

    std::vector<std::string> macros = { "REVERSED_Z", "" };
    if (rendererPsoDesc.numCSMDividesCount > 0)
    {
        macros.push_back("NUM_CSM_SHADOW_MAP");
        macros.push_back(std::to_string(rendererPsoDesc.numCSMDividesCount + 1));
    }

    colorPSO->SetPixelShader(GetShader(psFile, kPS, macros));
    if (rendererPsoDesc.isDeferredFinal)
    {
        colorPSO->SetRootSignature(*ModelRenderer::sDeferredRootSig);
    }

    colorPSO->Finalize();

    sAllPSOs.push_back(colorPSO);
    uint16_t psoIndex = sAllPSOs.size() - 1;
    sPSOIndices[psoHash] = psoIndex;

    return psoIndex;
}

ShadowBuffer* ModelRenderer::GetShadowBuffers()
{
    return sShadowBuffer;
}

ColorBuffer* ModelRenderer::GetNonMsaaShadowBuffers()
{
    return sNonMsaaShadowBuffer;
}

ColorBuffer* ModelRenderer::GetGBuffers()
{
    return sGubffer0;
}

ShadowBuffer& ModelRenderer::GetCurrentShadowBuffer()
{
    return sShadowBuffer[CURRENT_FARME_BUFFER_INDEX];
}

ColorBuffer& ModelRenderer::GetCurrentNonMsaaShadowBuffer()
{
    return sNonMsaaShadowBuffer[CURRENT_FARME_BUFFER_INDEX];
}

ColorBuffer& ModelRenderer::GetCurrentGBuffer()
{
    return sGubffer0[CURRENT_FARME_BUFFER_INDEX];
}

void ModelRenderer::ResetShadowMsaa(uint32_t sampleCount)
{
    uint32_t alignedSampleCount = Math::AlignUp(sampleCount, 2);
    ASSERT(alignedSampleCount <= 8);

    CommandQueueManager::GetInstance()->IdleGPU();

    gMsaaShadowSample = alignedSampleCount;

    CreateShadowBuffers();
}



void MeshRenderer::Reset()
{
    mScene = nullptr;
    mViewport = {};
    mScissor = {};
    mNumRTVs = 0;
    mCurrentRenderPassIdx = 0;
    mDepthBuffer = nullptr;
    mNonMsaaDepthBuffer = nullptr;
    mRenderPasses.clear();

    for (size_t i = 0; i < 8; i++)
    {
        mMsaaRenderTargets[i] = nullptr;
    }
}

void MeshRenderer::SetObjectsPSO()
{
    for (RenderPass& pass : mRenderPasses)
    {
        for (SortKey& key : pass.sortKeys)
        {
            const SubMesh& subMesh = *pass.sortObjects[key.objectIdx].subMesh;
            ModelRenderer::RendererPsoDesc rendererPsoDesc{};
            rendererPsoDesc.meshPSOFlags = subMesh.psoFlags;
            rendererPsoDesc.numCSMDividesCount = ModelRenderer::gNumCSMDivides;

            switch (key.passID)
            {
            case kZPass:
            {
                if (mBatchType == kShadows)
                {
                    rendererPsoDesc.isDepth = rendererPsoDesc.isShadow = true;
                    rendererPsoDesc.shadowMsaaCount = Math::Log2(ModelRenderer::gMsaaShadowSample);
                }
                else
                {
                    rendererPsoDesc.isDepth = true;
                }
                break;
            }
            case kOpaque:
            case kTransparent:
            {
                break;
            }
            default:
                break;
            }

            key.psoIdx = ModelRenderer::GetPsoIndex(rendererPsoDesc);
        }
    }
}

void MeshRenderer::AddMesh(size_t passIndex, const SubMesh& subMesh, const Model* model, float distance, D3D12_GPU_VIRTUAL_ADDRESS meshCBV)
{
    RenderPass& renderePass = mRenderPasses[passIndex];
    ASSERT(renderePass.camera != nullptr);

    SortKey key;
    key.value = renderePass.sortObjects.size();

    bool alphaBlend = subMesh.psoFlags & ePSOFlags::kAlphaBlend;
    bool alphaTest = subMesh.psoFlags & ePSOFlags::kAlphaTest;

    union float_or_int { float f; uint32_t u; } dist;
    dist.f = Math::Max(distance, 0.0f);

    key.distance = dist.u;

    if (mBatchType == kShadows)
    {
        if (alphaBlend)
            return;

        key.passID = kZPass;
        renderePass.sortKeys.push_back(key);
        renderePass.passCounts[kZPass]++;
    }
    else if (mBatchType == kGBuffer)
    {
        if (alphaBlend)
            return;

        key.passID = kOpaque;
        renderePass.sortKeys.push_back(key);
        renderePass.passCounts[kOpaque]++;
    }
    else if (alphaBlend)
    {
        key.passID = kTransparent;
        key.distance = ~dist.u;
        renderePass.sortKeys.push_back(key);
        renderePass.passCounts[kTransparent]++;
    }
    else
    {
        key.passID = kZPass;
        renderePass.sortKeys.push_back(key);
        renderePass.passCounts[kZPass]++;

        key.passID = kOpaque;
        renderePass.sortKeys.push_back(key);
        renderePass.passCounts[kOpaque]++;
    }


    SortObject object = { model, &subMesh, meshCBV };
    renderePass.sortObjects.push_back(object);
}

void MeshRenderer::Sort()
{
    for (RenderPass& pass : mRenderPasses)
    {
        std::sort(pass.sortKeys.begin(), pass.sortKeys.end(), [](uint64_t a, uint64_t b) {return a < b; });
    }
}

void MeshRenderer::RenderMeshes(GraphicsCommandList& context, GlobalConstants& globals, DrawPass pass)
{
    RenderMeshesBegin(context, globals, pass);

    for (RenderPass& renderPass : mRenderPasses)
    {
        context.PIXBeginEvent((L"Pass " + std::to_wstring(mCurrentRenderPassIdx)).c_str());
        RenderMeshesImpl(context, globals, pass, renderPass);
        context.PIXEndEvent();

        mCurrentRenderPassIdx++;
    }

    RenderMeshesEnd(context, globals, pass);

    mCurrentRenderPassIdx = 0;
}

void MeshRenderer::RenderMeshesImpl(GraphicsCommandList& context, GlobalConstants& globals, DrawPass pass, RenderPass& renderPass)
{
    // Set common shader constants
    globals.ViewProjMatrix = renderPass.camera->GetViewProjMatrix();
    globals.InvViewProjMatrix = Math::Invert(renderPass.camera->GetViewProjMatrix());
    globals.CameraPos = renderPass.camera->GetPosition();

    context.SetRootSignature(*ModelRenderer::sForwardRootSig);
    context.SetDynamicConstantBufferView(ModelRenderer::kGlobalConstants, sizeof(GlobalConstants), &globals);
    context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    for (; renderPass.currentPass <= pass; renderPass.currentPass = (DrawPass)(renderPass.currentPass + 1))
    {
        const uint32_t passCount = renderPass.passCounts[renderPass.currentPass];
        if (passCount == 0)
            continue;

        if (mBatchType == kDefault)
        {
            switch (renderPass.currentPass)
            {
            case kZPass:
                context.TransitionResource(*mDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                context.ClearDepth(*mDepthBuffer);
                context.SetDepthStencilTarget(mDepthBuffer->GetDSV(mCurrentRenderPassIdx));
                break;
            case kOpaque:
                //{
                //    context.TransitionResource(*mDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
                //    context.TransitionResource(*mRenderTargets[0], D3D12_RESOURCE_STATE_RENDER_TARGET);
                //    context.SetRenderTarget(mRenderTargets[0]->GetRTV(), mDepthBuffer->GetDSV_DepthReadOnly());
                //}
                //{
                //    context.TransitionResource(*mDSV, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                //    context.TransitionResource(gSceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
                //    context.SetRenderTarget(gSceneColorBuffer.GetRTV(), mDSV->GetDSV());
                //}
                //break;
            case kTransparent:
                context.TransitionResource(*mDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
                context.TransitionResource(*mRenderTargets[0], D3D12_RESOURCE_STATE_RENDER_TARGET);
                context.ClearColor(*mRenderTargets[0]);
                context.SetRenderTarget(mRenderTargets[0]->GetRTV(), mDepthBuffer->GetDSV_DepthReadOnly());
                break;
            }
        }

        context.SetViewportAndScissor(mViewport, mScissor);
        context.FlushResourceBarriers();

        const uint32_t lastDraw = renderPass.currentDraw + passCount;

        while (renderPass.currentDraw < lastDraw)
        {
            SortKey key = renderPass.sortKeys[renderPass.currentDraw];
            const SortObject& object = renderPass.sortObjects[key.objectIdx];
            const Mesh& mesh = *object.model->GetMesh();
            const SubMesh& subMesh = *object.subMesh;
            const Material& material = *GET_MATERIAL(subMesh.materialIdx);

            context.SetPipelineState(*ModelRenderer::sAllPSOs[key.psoIdx]);
            context.SetConstantBuffer(ModelRenderer::kMeshConstants, object.meshCBV);
            context.SetConstantBuffer(ModelRenderer::kMaterialConstants, GET_MAT_VPTR(subMesh.materialIdx));
            context.SetDescriptorTable(ModelRenderer::kModelTextures, material.GetTextureGpuHandles());
            context.SetDescriptorTable(ModelRenderer::kModelTextureSamplers, material.GetSamplerGpuHandles());
            context.SetDescriptorTable(ModelRenderer::kSceneTextures, mScene->GetSceneTextureHandles());
            context.SetDescriptorTable(ModelRenderer::kShadowTexture, mScene->GetShadowTextureHandle());

            if (renderPass.currentPass == kZPass)
                context.SetVertexBuffer(0, { GET_MESH_DepthVB + mesh.vbDepthOffset, mesh.sizeDepthVB, mesh.depthVertexStride });
            else
                context.SetVertexBuffer(0, { GET_MESH_VB + mesh.vbOffset, mesh.sizeVB, mesh.vertexStride });

            DXGI_FORMAT indexFormat = subMesh.index32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
            context.SetIndexBuffer({ GET_MESH_IB + mesh.ibOffset, mesh.sizeIB, indexFormat });

            context.DrawIndexed(subMesh.indexCount, subMesh.startIndex, subMesh.baseVertex);

            ++renderPass.currentDraw;
        }
    }
}

void ShadowMeshRenderer::RenderMeshesBegin(GraphicsCommandList& context, GlobalConstants& globals, DrawPass pass)
{
    ShadowBuffer* shadowBuffer = dynamic_cast<ShadowBuffer*>(mDepthBuffer);
    context.TransitionResource(*shadowBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
    context.ClearDepth(*shadowBuffer);

    mScissor = shadowBuffer->GetScissor();
    mViewport = shadowBuffer->GetViewPort();
}

void ShadowMeshRenderer::RenderMeshesImpl(GraphicsCommandList& context, GlobalConstants& globals, DrawPass pass, RenderPass& renderPass)
{
    ShadowBuffer* shadowBuffer = dynamic_cast<ShadowBuffer*>(mDepthBuffer);
    context.SetDepthStencilTarget(shadowBuffer->GetDSV(mCurrentRenderPassIdx));

    MeshRenderer::RenderMeshesImpl(context, globals, pass, renderPass);
}

void ShadowMeshRenderer::RenderMeshesEnd(GraphicsCommandList& context, GlobalConstants& globals, DrawPass pass)
{
    ShadowBuffer* shadowBuffer = dynamic_cast<ShadowBuffer*>(mDepthBuffer);

    if (ModelRenderer::gMsaaShadowSample > 0)
    {
        shadowBuffer->ResolveMsaa(context, *mNonMsaaDepthBuffer, ModelRenderer::gMsaaShadowSample);
        context.TransitionResource(*mNonMsaaDepthBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        // Dx12 can not resolve depth msaa
        //context.ResolveMSAAResource(*mDepthBuffer, *mMsaaDepthBuffer, SHADOW_MAP_FORMAT);
    }
    else
    {
        context.TransitionResource(*mDepthBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
}



void DeferredRenderer::RenderMeshesBegin(GraphicsCommandList& context, GlobalConstants& globals, DrawPass pass)
{
    context.TransitionResource(*mDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    context.TransitionResource(*mRenderTargets[0], D3D12_RESOURCE_STATE_RENDER_TARGET);
    context.ClearDepth(*mDepthBuffer);
    context.ClearColor(*mRenderTargets[0]);
    context.SetRenderTarget(mRenderTargets[0]->GetRTV(), mDepthBuffer->GetDSV());
}

void DeferredRenderer::RenderMeshesEnd(GraphicsCommandList& context, GlobalConstants& globals, DrawPass pass)
{
    context.TransitionResource(*mDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
    context.TransitionResource(*mRenderTargets[0], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}


void FullScreenRenderer::Reset()
{
    mNumRTVs = 0;
    mScene = nullptr;
    mPSOIndex = 0;
    for (size_t i = 0; i < 8; i++)
    {
        mRenderTargets[i] = nullptr;
    }
    mCamera = nullptr;
}

void FullScreenRenderer::SetPSO()
{
    ModelRenderer::RendererPsoDesc rendererPsoDesc{};
    rendererPsoDesc.numCSMDividesCount = ModelRenderer::gNumCSMDivides;
    rendererPsoDesc.isDeferredFinal = 1;

    mPSOIndex = ModelRenderer::GetFullScreenPsoIndex(rendererPsoDesc);
}

void FullScreenRenderer::RenderScreen(GraphicsCommandList& context, GlobalConstants& globals)
{
    globals.ViewProjMatrix = mCamera->GetViewProjMatrix();
    globals.InvViewProjMatrix = Math::Invert(mCamera->GetViewProjMatrix());
    globals.CameraPos = mCamera->GetPosition();

    if (mBatchType == mDeferredFinal)
    {
        context.SetRootSignature(*ModelRenderer::sDeferredRootSig);
        context.SetPipelineState(*ModelRenderer::sAllPSOs[mPSOIndex]);
        context.SetDynamicConstantBufferView(ModelRenderer::kGlobalConstantsDeferred, sizeof(GlobalConstants), &globals);
        context.SetDescriptorTable(ModelRenderer::kGBufferTextures, mScene->GetDeferredTextureHandle());
        context.SetDescriptorTable(ModelRenderer::kSceneTexturesDeferred, mScene->GetSceneTextureHandles());
        context.SetDescriptorTable(ModelRenderer::kShadowTextureDeferred, mScene->GetShadowTextureHandle());
    }

    context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context.SetViewportAndScissor(mViewport, mScissor);
    context.SetRenderTarget(mRenderTargets[0]->GetRTV());
    context.ClearColor(*mRenderTargets[0]);
    context.Draw(3);
}
