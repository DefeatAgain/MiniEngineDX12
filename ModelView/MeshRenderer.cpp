#include "MeshRenderer.h"
#include "GraphicsResource.h"
#include "PixelBuffer.h"
#include "FrameContext.h"
#include "Mesh.h"
#include "Material.h"
#include "Scene.h"
#include "Model.h"

namespace ModelRenderer
{
    std::map<size_t, uint32_t> sPSOIndices;
    GraphicsPipelineState sDefaultPSO; // Not finalized.  Used as a template.

    ShadowBuffer sShadowBuffer[SWAP_CHAIN_BUFFER_COUNT];


    void Initialize()
    {
        for (size_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
        {
            sShadowBuffer[i].Create(L"Shadow Map", 2048, 2048, DXGI_FORMAT_D32_FLOAT);
        }

        Graphics::AddRSSTask([]()
        {
            SamplerDesc DefaultSamplerDesc;
            DefaultSamplerDesc.MaxAnisotropy = 8;

            SamplerDesc CubeMapSamplerDesc = DefaultSamplerDesc;

            sForwardRootSig = GET_RSO(L"MeshRenderer RSO");
            sForwardRootSig->Reset(kNumRootBindings, 3);
            sForwardRootSig->InitStaticSampler(10, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
            sForwardRootSig->InitStaticSampler(11, Graphics::SamplerShadowDesc, D3D12_SHADER_VISIBILITY_PIXEL);
            sForwardRootSig->InitStaticSampler(12, Graphics::SamplerPointClampDesc, D3D12_SHADER_VISIBILITY_PIXEL);
            sForwardRootSig->GetParam(kMeshConstants).InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
            sForwardRootSig->GetParam(kMaterialConstants).InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
            sForwardRootSig->GetParam(kGlobalConstants).InitAsConstantBuffer(1, D3D12_SHADER_VISIBILITY_ALL);

            sForwardRootSig->GetParam(kModelTextures).InitAsDescriptorRange(
                D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 5, D3D12_SHADER_VISIBILITY_PIXEL);
            sForwardRootSig->GetParam(kModelTextureSamplers).InitAsDescriptorRange(
                D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0, 5, D3D12_SHADER_VISIBILITY_PIXEL);
            sForwardRootSig->GetParam(kSceneTextures).InitAsDescriptorRange(
                D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 5, D3D12_SHADER_VISIBILITY_PIXEL);
            sForwardRootSig->Finalize(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

            ADD_SHADER("SkyBoxVS", L"MeshRender/SkyBoxVS.hlsl", kVS);
            ADD_SHADER("SkyBoxPS", L"MeshRender/SkyBoxPS.hlsl", kPS);
            ADD_SHADER("ForwardVS", L"MeshRender/ForwardVS.hlsl", kVS);
            ADD_SHADER("ForwardVSNoSecondUV", L"MeshRender/ForwardVS.hlsl", kVS, { "NO_SECOND_UV", "" });
            ADD_SHADER("ForwardPS", L"MeshRender/ForwardPS.hlsl", kPS);
            ADD_SHADER("ForwardPSNoSecondUV", L"MeshRender/ForwardPS.hlsl", kPS, { "NO_SECOND_UV", "" });
            ADD_SHADER("DepthOnlyVS", L"MeshRender/DepthOnlyVS.hlsl", kVS);
            ADD_SHADER("DepthOnlyPS", L"MeshRender/DepthOnlyPS.hlsl", kPS);
            ADD_SHADER("DepthOnlyVSCutOff", L"MeshRender/DepthOnlyVS.hlsl", kVS, { "ENABLE_ALPHATEST", "" });
            ADD_SHADER("DepthOnlyPSCutOff", L"MeshRender/DepthOnlyPS.hlsl", kPS, { "ENABLE_ALPHATEST", "" });
        });

        Graphics::AddPSTask([]()
        {
            PipeLineStateManager* psoInstance = PipeLineStateManager::GetInstance();

            GraphicsPipelineState* depthOnlyPSO = GET_GPSO(L"ModelRender: Depth Only PSO");
            GraphicsPipelineState* depthOnlyCutOffPSO = GET_GPSO(L"ModelRender: Depth Only CutOff PSO");
            GraphicsPipelineState* shadowPSO = GET_GPSO(L"ModelRender: Shadow PSO");
            GraphicsPipelineState* shadowCutOffPSO = GET_GPSO(L"ModelRender: Shadow CutOff PSO");
            sSkyboxPSO = GET_GPSO(L"ModelRender: SkyBox PSO");
            sAllPSOs.push_back(depthOnlyPSO);
            sAllPSOs.push_back(depthOnlyCutOffPSO);
            sAllPSOs.push_back(shadowPSO);
            sAllPSOs.push_back(shadowCutOffPSO);
            //sAllPSOs.push_back(sSkyboxPSO);

            D3D12_INPUT_ELEMENT_DESC posOnly[] =
            {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };

            D3D12_INPUT_ELEMENT_DESC posAndUV[] =
            {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R8G8_UNORM,         0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
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

            *depthOnlyCutOffPSO = *depthOnlyPSO;
            depthOnlyCutOffPSO->SetInputLayout(ARRAYSIZE(posAndUV), posAndUV);
            depthOnlyCutOffPSO->SetRasterizerState(Graphics::RasterizerTwoSided);
            depthOnlyCutOffPSO->SetVertexShader(GET_SHADER("DepthOnlyVSCutOff"));
            depthOnlyCutOffPSO->SetPixelShader(GET_SHADER("DepthOnlyPSCutOff"));
            depthOnlyCutOffPSO->Finalize();

            *shadowPSO = *depthOnlyPSO;
            shadowPSO->SetRenderTargetFormats(0, nullptr, sShadowBuffer[0].GetFormat());
            shadowPSO->Finalize();

            *shadowCutOffPSO = *depthOnlyCutOffPSO;
            shadowCutOffPSO->SetRenderTargetFormats(0, nullptr, sShadowBuffer[0].GetFormat());
            shadowCutOffPSO->Finalize();

            DXGI_FORMAT renderFormat = HDR_FORMAT;
            sDefaultPSO.SetRootSignature(*sForwardRootSig);
            sDefaultPSO.SetRasterizerState(Graphics::RasterizerDefault);
            sDefaultPSO.SetBlendState(Graphics::BlendDisable);
            sDefaultPSO.SetDepthStencilState(Graphics::DepthStateReadWrite);
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
        });
    }

    void Destroy()
    {
        for (size_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
        {
            sShadowBuffer[i].Destroy();
        }
    }

    uint16_t GetPsoIndex(ePSOFlags psoFlags)
    {
        auto psoIndexIter = sPSOIndices.find(psoFlags);
        if (psoIndexIter != sPSOIndices.end())
        {
            return psoIndexIter->second;
        }

        GraphicsPipelineState* colorPSO;
        colorPSO = GET_GPSO(std::wstring(L"MeshRenderer: PSO ") + std::to_wstring(psoFlags));

        *colorPSO = sDefaultPSO;

        uint16_t Requirements = kHasPosition | kHasNormal | kHasTangent;
        ASSERT((psoFlags & Requirements) == Requirements);

        std::vector<D3D12_INPUT_ELEMENT_DESC> vertexLayout;
        vertexLayout.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT });
        vertexLayout.push_back({ "NORMAL",   0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, D3D12_APPEND_ALIGNED_ELEMENT });
        vertexLayout.push_back({ "TANGENT",  0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, D3D12_APPEND_ALIGNED_ELEMENT });
        vertexLayout.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R8G8_UNORM,       0, D3D12_APPEND_ALIGNED_ELEMENT });

        if (psoFlags & kHasUV1)
            vertexLayout.push_back({ "TEXCOORD", 1, DXGI_FORMAT_R8G8_UNORM,       0, D3D12_APPEND_ALIGNED_ELEMENT });
        
        colorPSO->SetInputLayout((uint32_t)vertexLayout.size(), vertexLayout.data());
        if (psoFlags & kHasUV1)
        {
            colorPSO->SetVertexShader(GET_SHADER("ForwardVS"));
            colorPSO->SetPixelShader(GET_SHADER("ForwardPS"));
        }
        else
        {
            colorPSO->SetVertexShader(GET_SHADER("ForwardVSNoSecondUV"));
            colorPSO->SetPixelShader(GET_SHADER("ForwardPSNoSecondUV"));
        }

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
        sPSOIndices[psoFlags] = psoIndex;

        return psoIndex;
    }

    uint16_t GetDepthPsoIndex(ePSOFlags psoFlags, bool isShadow)
    {
        uint16_t depthPso = 0;
        if (psoFlags & ePSOFlags::kAlphaTest)
            depthPso += 1;
        if (isShadow)
            depthPso += 2;
        return depthPso;
    }

    ShadowBuffer& GetCurrentShadowBuffer()
    {
        return sShadowBuffer[CURRENT_FARME_BUFFER_INDEX];
    }
}

void MeshRenderer::AddMesh(const SubMesh& subMesh, const Model* model, float distance, D3D12_GPU_VIRTUAL_ADDRESS meshCBV)
{
    SortKey key;
    key.value = mSortObjects.size();

    bool alphaBlend = (subMesh.psoFlags & ePSOFlags::kAlphaBlend) == ePSOFlags::kAlphaBlend;
    bool alphaTest = (subMesh.psoFlags & ePSOFlags::kAlphaTest) == ePSOFlags::kAlphaTest;

    union float_or_int { float f; uint32_t u; } dist;
    dist.f = Math::Max(distance, 0.0f);

    key.distance = dist.u;
    if (mBatchType == kShadows)
    {
        if (alphaBlend)
            return;

        key.passID = kZPass;
        key.psoIdx = ModelRenderer::GetDepthPsoIndex((ePSOFlags)subMesh.psoFlags, true);
        mSortKeys.push_back(key);
        mPassCounts[kZPass]++;
    }
    else if (subMesh.psoFlags & ePSOFlags::kAlphaBlend)
    {
        key.passID = kTransparent;
        key.psoIdx = ModelRenderer::GetPsoIndex((ePSOFlags)subMesh.psoFlags);
        key.distance = ~dist.u;
        mSortKeys.push_back(key);
        mPassCounts[kTransparent]++;
    }
    else
    {
        key.passID = kZPass;
        key.psoIdx = ModelRenderer::GetDepthPsoIndex((ePSOFlags)subMesh.psoFlags, false);;
        mSortKeys.push_back(key);
        mPassCounts[kZPass]++;

        key.passID = kOpaque;
        key.psoIdx = ModelRenderer::GetPsoIndex((ePSOFlags)subMesh.psoFlags);;
        mSortKeys.push_back(key);
        mPassCounts[kOpaque]++;
    }
    /*else
    {
        key.passID = kOpaque;
        key.psoIdx = ModelRenderer::GetPsoIndex(subMesh.psoFlags);;
        mSortKeys.push_back(key);
        mPassCounts[kOpaque]++;
    }*/

    SortObject object = { model, meshCBV };
    mSortObjects.push_back(object);
}

void MeshRenderer::Sort()
{
    std::sort(mSortKeys.begin(), mSortKeys.end(), [](uint64_t a, uint64_t b) {return a < b; });
}

void MeshRenderer::RenderMeshes(GraphicsCommandList& context, GlobalConstants& globals, DrawPass pass)
{
    context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Set common shader constants
    globals.ViewProjMatrix = mCamera->GetViewProjMatrix();
    globals.CameraPos = mCamera->GetPosition();
    globals.IBLRange = mScene->GetIBLRange() - mScene->GetIBLBias();
    globals.IBLBias = mScene->GetIBLBias();

    context.SetRootSignature(*ModelRenderer::sForwardRootSig);
    context.SetDynamicConstantBufferView(ModelRenderer::kGlobalConstants, sizeof(GlobalConstants), &globals);

    if (mBatchType == kShadows)
    {
        context.TransitionResource(*mDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
        context.ClearDepth(*mDepthBuffer);
        context.SetDepthStencilTarget(mDepthBuffer->GetDSV());
        mScissor = (reinterpret_cast<ShadowBuffer*>(mDepthBuffer)->mScissor);
        mViewport = (reinterpret_cast<ShadowBuffer*>(mDepthBuffer)->mViewport);
    }

    for (; mCurrentPass <= pass; mCurrentPass = (DrawPass)(mCurrentPass + 1))
    {
        const uint32_t passCount = mPassCounts[mCurrentPass];
        if (passCount == 0)
            continue;

        if (mBatchType == kDefault)
        {
            switch (mCurrentPass)
            {
            case kZPass:
                context.TransitionResource(*mDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                context.SetDepthStencilTarget(mDepthBuffer->GetDSV());
                break;
            case kOpaque:
                //{
                //    context.TransitionResource(*mDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
                //    context.TransitionResource(*mRenderTarges[0], D3D12_RESOURCE_STATE_RENDER_TARGET);
                //    context.SetRenderTarget(mRenderTarges[0]->GetRTV(), mDepthBuffer->GetDSV_DepthReadOnly());
                //}
                //{
                //    context.TransitionResource(*mDSV, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                //    context.TransitionResource(gSceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
                //    context.SetRenderTarget(gSceneColorBuffer.GetRTV(), mDSV->GetDSV());
                //}
                //break;
            case kTransparent:
                context.TransitionResource(*mDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
                context.TransitionResource(*mRenderTarges[0], D3D12_RESOURCE_STATE_RENDER_TARGET);
                context.SetRenderTarget(mRenderTarges[0]->GetRTV(), mDepthBuffer->GetDSV_DepthReadOnly());
                break;
            }
        }

        context.SetViewportAndScissor(mViewport, mScissor);
        context.FlushResourceBarriers();

        const uint32_t lastDraw = mCurrentDraw + passCount;

        while (mCurrentDraw < lastDraw)
        {
            SortKey key = mSortKeys[mCurrentDraw];
            const SortObject& object = mSortObjects[key.objectIdx];
            const Mesh& mesh = *object.model->mMesh;

            context.SetConstantBuffer(ModelRenderer::kMeshConstants, object.meshCBV);
            context.SetPipelineState(*ModelRenderer::sAllPSOs[key.psoIdx]);

            for (uint32_t i = 0; i < mesh.subMeshCount; ++i)
            {
                const SubMesh& subMesh = mesh.subMeshes[i];
                const Material& material = *GET_MATERIAL(subMesh.materialIdx);

                context.SetConstantBuffer(ModelRenderer::kMaterialConstants, GET_MAT_VPTR(subMesh.materialIdx));
                context.SetDescriptorTable(ModelRenderer::kModelTextures, material.GetTextureHandles());
                context.SetDescriptorTable(ModelRenderer::kModelTextureSamplers, material.GetSamplerHandles());

                if (mCurrentPass == kZPass)
                    context.SetVertexBuffer(0, { GET_MESH_DepthVB + mesh.vbDepthOffset, mesh.sizeDepthVB, subMesh.depthVertexStride });
                else
                    context.SetVertexBuffer(0, { GET_MESH_VB + mesh.vbOffset, mesh.sizeVB, subMesh.vertexStride });

                DXGI_FORMAT indexFormat = subMesh.index32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
                context.SetIndexBuffer({ GET_MESH_IB + mesh.ibOffset, mesh.sizeIB, indexFormat });

                context.DrawIndexed(subMesh.indexCount, subMesh.startIndex, subMesh.baseVertex);
            }

            ++mCurrentDraw;
        }
    }

    if (mBatchType == kShadows)
    {
        context.TransitionResource(*mDepthBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
}
