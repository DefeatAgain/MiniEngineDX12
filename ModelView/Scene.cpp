#include "Scene.h"
#include "Model.h"
#include "MeshRenderer.h"
#include "ModelConverter.h"
#include "FrameContext.h"
#include "Graphics.h"
#include "Mesh.h"
#include "glTF.h"
#include "PixelBuffer.h"
#include "PipelineState.h"
#include "SSAO.h"
#include "Utils/ThreadPoolExecutor.h"

void Scene::Destroy()
{
    if (mShadowGpuHandle)
        DEALLOC_DESCRIPTOR_GPU(mShadowGpuHandle, SWAP_CHAIN_BUFFER_COUNT);

    if (mSceneTextureGpuHandle)
        DEALLOC_DESCRIPTOR_GPU(mSceneTextureGpuHandle, 8);
}

void Scene::Startup()
{
    mSceneBS_WS = Math::BoundingSphere(kZero);
    mSceneCamera.SetZRange(1.0f, 200.0f);
    mSceneCamera.SetLookDirection(-Vector3(kZUnitVector), Vector3(kYUnitVector));
    mModelDirtyFrameCount = SWAP_CHAIN_BUFFER_COUNT;

    mSunDirectionTheta = 0.0f;
    mSunDirectionPhi = 0.0f;
    mSunLightIntensity = Vector3(1, 1, 1) * 0.5f;
    mShadowBias = 0.001f;

    mCameraController.reset(new FlyingFPSCamera(mSceneCamera, Vector3(kYUnitVector)));

    if (mModelWorldTransform.size() != mModels.size())
        mModelWorldTransform.resize(mModels.size());
    for (size_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
    {
        mMeshConstantsUploader[i].Create(L"Mesh Constants Buffer " + std::to_wstring(i),
            sizeof(ModelConstants) * mModelWorldTransform.size());
    }
}

CommandList* Scene::RenderScene(CommandList* context, std::shared_ptr<MeshRendererBuilder> meshRendererBuilder)
{
    GraphicsCommandList& ghContext = context->GetGraphicsCommandList().Begin(L"Render Scene");

    MeshManager::GetInstance()->TransitionStateToRead(ghContext);

    GlobalConstants globals;
    for (size_t i = 0; i < std::min((size_t)MAX_CSM_DIVIDES + 1, mShadowCameras.size()); i++)
        globals.SunShadowMatrix[i] = mShadowCameras[i].GetShadowMatrix();
    ShadowCamera::GetDivideCSMZRange(globals.CSMDivides, mSceneCamera, ModelRenderer::gCSMDivides, 
        ModelRenderer::gNumCSMDivides, MAX_CSM_DIVIDES);
    globals.SunDirection = mSunDirection;
    globals.SunIntensity = mSunLightIntensity;
    globals.IBLRange = mSpecularIBLRange;
    globals.ShadowBias = mShadowBias;
    globals.gNearZ = mSceneCamera.GetNearClip();
    globals.gFarZ = mSceneCamera.GetFarClip();

    // Begin rendering depth
    DepthBuffer& depthBuffer = CURRENT_SCENE_DEPTH_BUFFER;
    ColorBuffer& colorBuffer = CURRENT_SCENE_COLOR_BUFFER;

    MeshRenderer& meshRenderer = meshRendererBuilder->Get<MeshRenderer>(MeshRenderer::kDefault);
    ShadowMeshRenderer& shadowRenderer = meshRendererBuilder->Get<ShadowMeshRenderer>(MeshRenderer::kShadows);

    ghContext.PIXBeginEvent(L"PreZ"); // render preZ
    meshRenderer.RenderMeshes(ghContext, globals, MeshRenderer::kZPass);
    ghContext.PIXEndEvent(); // render preZ end

    ghContext.PIXBeginEvent(L"ShadowMap"); // render shadowmap
    shadowRenderer.RenderMeshes(ghContext, globals, MeshRenderer::kZPass);
    ghContext.PIXEndEvent(); // render shadowmap end

    //context->TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    ghContext.PIXBeginEvent(L"Mesh"); // render mesh
    meshRenderer.RenderMeshes(ghContext, globals, MeshRenderer::kOpaque);
    ghContext.PIXSetMarker(L"SkyBox"); // render mesh
    RenderSkyBox(ghContext);

    meshRenderer.RenderMeshes(ghContext, globals, MeshRenderer::kTransparent);
    ghContext.PIXEndEvent(); // render mesh end

    ghContext.Finish();
    return context;
}

CommandList* Scene::RenderSceneDeferred(CommandList* context, std::shared_ptr<MeshRendererBuilder> meshRendererBuilder,
    std::shared_ptr<FullScreenRenderer> deferredRender)
{
    GraphicsCommandList& ghContext = context->GetGraphicsCommandList().Begin(L"Render Scene");

    MeshManager::GetInstance()->TransitionStateToRead(ghContext);

    GlobalConstants globals;
    for (size_t i = 0; i < std::min((size_t)MAX_CSM_DIVIDES + 1, mShadowCameras.size()); i++)
        globals.SunShadowMatrix[i] = mShadowCameras[i].GetShadowMatrix();
    ShadowCamera::GetDivideCSMZRange(globals.CSMDivides, mSceneCamera, ModelRenderer::gCSMDivides,
        ModelRenderer::gNumCSMDivides, MAX_CSM_DIVIDES);
    globals.SunDirection = mSunDirection;
    globals.SunIntensity = mSunLightIntensity;
    globals.IBLRange = mSpecularIBLRange;
    globals.ShadowBias = mShadowBias;
    globals.gNearZ = mSceneCamera.GetNearClip();
    globals.gFarZ = mSceneCamera.GetFarClip();

    MeshRenderer& meshRenderer = meshRendererBuilder->Get<MeshRenderer>(MeshRenderer::kGBuffer);
    ShadowMeshRenderer& shadowRenderer = meshRendererBuilder->Get<ShadowMeshRenderer>(MeshRenderer::kShadows);

    ghContext.PIXBeginEvent(L"GBuffer"); // render GBuffer
    meshRenderer.RenderMeshes(ghContext, globals, MeshRenderer::kOpaque);
    ghContext.PIXEndEvent(); // render GBuffer end

    ghContext.PIXBeginEvent(L"ShadowMap"); // render shadowmap
    shadowRenderer.RenderMeshes(ghContext, globals, MeshRenderer::kZPass);
    ghContext.PIXEndEvent(); // render shadowmap end

    SSAORenderer::RenderTaskSSAO(ghContext, GetDeferredTextureHandle(), meshRenderer.GetRenderTarget(0), meshRenderer.GetDepthStencilTarget());

    ghContext.PIXBeginEvent(L"DeferredFinal"); // render DeferredFinal
    deferredRender->RenderScreen(ghContext, globals);
    ghContext.PIXSetMarker(L"SkyBox"); // render mesh
    RenderSkyBox(ghContext);
    ghContext.PIXEndEvent(); // render DeferredFinal end
    
    ghContext.Finish();
    return context;
}

void Scene::RenderSkyBox(GraphicsCommandList& ghContext)
{
    __declspec(align(256)) struct SkyboxVSCB
    {
        Matrix4 ProjInverse;
        Matrix3 ViewInverse;
    } skyVSCB;
    skyVSCB.ProjInverse = Invert(mSceneCamera.GetProjMatrix());
    skyVSCB.ViewInverse = Invert(mSceneCamera.GetViewMatrix()).Get3x3();

    __declspec(align(256)) struct SkyboxPSCB
    {
        float TextureLevel;
    } skyPSCB;
    skyPSCB.TextureLevel = 0.0f;

    DepthBuffer& depthBuffer = CURRENT_SCENE_DEPTH_BUFFER;
    ColorBuffer& colorBuffer = CURRENT_SCENE_COLOR_BUFFER;

    ghContext.SetPipelineState(*ModelRenderer::sSkyboxPSO);

    ghContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
    ghContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ghContext.SetRenderTarget(colorBuffer.GetRTV(), depthBuffer.GetDSV_DepthReadOnly());
    ghContext.SetViewportAndScissor(Graphics::GetDefaultViewPort(), Graphics::GetDefaultScissor());

    ghContext.SetDynamicConstantBufferView(ModelRenderer::kMeshConstants, sizeof(SkyboxVSCB), &skyVSCB);
    ghContext.SetDynamicConstantBufferView(ModelRenderer::kMaterialConstants, sizeof(SkyboxPSCB), &skyPSCB);
    ghContext.SetDescriptorTable(ModelRenderer::kSceneTextures, mSceneTextureGpuHandle);
    ghContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ghContext.Draw(3);
}

void Scene::Update(float deltaTime)
{
    UpdateModels();

    mCameraController->Update(deltaTime);

    UpdateLight();
}

void Scene::Render()
{
#ifdef DEFERRED_RENDER
    std::pair<std::shared_ptr<MeshRendererBuilder>, std::shared_ptr<FullScreenRenderer>> renderers = 
        SetMeshRenderersDeferred();
    PUSH_MUTIRENDER_TASK({ D3D12_COMMAND_LIST_TYPE_DIRECT, PushGraphicsTaskBind(
        &Scene::RenderSceneDeferred, this, renderers.first, renderers.second) });

#else
    std::shared_ptr<MeshRendererBuilder> allMeshRenderers = SetMeshRenderers();
    PUSH_MUTIRENDER_TASK({ D3D12_COMMAND_LIST_TYPE_DIRECT, PushGraphicsTaskBind(&Scene::RenderScene, this, allMeshRenderers) });

#endif // DEFERRED_RENDER
}

void Scene::SetIBLTextures(TextureRef diffuseIBL, TextureRef specularIBL)
{
    ASSERT(diffuseIBL && specularIBL);

    mRadianceCubeMap = specularIBL;
    mIrradianceCubeMap = diffuseIBL;
    mPreComputeBRDF = GET_TEX(ModelConverter::GetIBLTextureFilename(L"GGX_BRDF_LUT"));

    diffuseIBL.WaitForValid();
    specularIBL.WaitForValid();
    mPreComputeBRDF.WaitForValid();

    mSpecularIBLRange = const_cast<ID3D12Resource*>(mRadianceCubeMap->GetResource())->GetDesc().MipLevels - 1;

    MapGpuDescriptors();
}

void Scene::WalkGraph(const std::vector<glTF::Node*>& siblings, 
    uint32_t curIndex, const Math::Matrix4& xform)
{
    using namespace Math;

    size_t numSiblings = siblings.size();
    for (size_t i = 0; i < numSiblings; ++i)
    {
        glTF::Node* curNode = siblings[i];
        Model& model = mModels[curNode->linearIdx];
        model.mScene = this;
        model.mHasChildren = false;
        model.mCurIndex = curNode->linearIdx;
        model.mParentIndex = curIndex;

        Math::Matrix4 modelXForm;
        if (curNode->hasMatrix)
        {
            modelXForm = Matrix4(curNode->matrix);
            const AffineTransform& affineTrans = (const AffineTransform&)modelXForm;
            XMStoreFloat3(&model.mScale, affineTrans.GetScale());
            XMStoreFloat3(&model.mPosition, affineTrans.GetTranslation());
            XMStoreFloat4(&model.mRotation, affineTrans.GetRotation());
        }
        else
        {
            CopyMemory((float*)&model.mPosition, curNode->translation, sizeof(curNode->translation));
            CopyMemory((float*)&model.mScale, curNode->scale, sizeof(curNode->scale));
            CopyMemory((float*)&model.mRotation, curNode->rotation, sizeof(curNode->rotation));
            modelXForm = Matrix4(
                Matrix3(Quaternion(model.mRotation)) * Matrix3::MakeScale(Vector3(model.mScale)),
                Vector3(*(const XMFLOAT3*)curNode->translation)
            );
        }

        const Matrix4 LocalXform = xform * modelXForm;

        if (!curNode->pointsToCamera && curNode->mesh != nullptr)
        {
            model.mMesh = GET_MESH(curNode->mesh->index);

            // Model bounding for object space
            //const AffineTransform& localAffineTrans = (const AffineTransform&)LocalXform;
            //Vector3 sphereCenter(*model.mMesh->bounds);
            //sphereCenter = static_cast<Vector3>(LocalXform * sphereCenter);
            //Scalar sphereRadius = localAffineTrans.GetUniformScale() * model.mMesh->bounds[3];
            //model.m_BSOS = Math::BoundingSphere(sphereCenter, sphereRadius);
            //model.m_BBoxOS = AxisAlignedBox::CreateFromSphere(model.m_BSOS);

            model.m_BSLS = Math::BoundingSphere((const XMFLOAT4*)model.mMesh->bounds);
            model.m_BBoxLS = AxisAlignedBox::CreateFromSphere(model.m_BSLS);
        }
        else
        {
            model.mMesh = nullptr;
        }

        if (curNode->children.size() > 0)
        {
            model.mHasChildren = true;
            WalkGraph(curNode->children, model.mCurIndex, LocalXform);
        }

        // Are there more siblings?
        if (i + 1 < numSiblings)
        {
            model.mHasSiblings = true;
        }
    }
}

DescriptorHandle Scene::GetShadowTextureHandle() const
{
    return mShadowGpuHandle + (UINT)CURRENT_FARME_BUFFER_INDEX;
}

DescriptorHandle Scene::GetDeferredTextureHandle() const
{
    return mDeferredTextureGpuHandle + (UINT)(CURRENT_FARME_BUFFER_INDEX * 4);
}

void Scene::ResetShadowMapHandle()
{
    ShadowBuffer* shadowBuffers = ModelRenderer::GetShadowBuffers();
    ColorBuffer* nonMsaaShadowBuffers = ModelRenderer::GetNonMsaaShadowBuffers();

    if (ModelRenderer::gMsaaShadowSample > 0)
    {
        for (uint32_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
        {
            Graphics::gDevice->CopyDescriptorsSimple(1, mShadowGpuHandle + i, nonMsaaShadowBuffers[i].GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }
    else
    {
        for (uint32_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
        {
            Graphics::gDevice->CopyDescriptorsSimple(1, mShadowGpuHandle + i, shadowBuffers[i].GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }
}

void Scene::SetRenderModels(MeshRenderer& renderer)
{
    size_t currentFrameIdx = CURRENT_FARME_BUFFER_INDEX;

    for (size_t i = 0; i < mModels.size(); i++)
    {
        if (mModels[i].mMesh == nullptr)
            continue;

        mModels[i].Render(renderer, mModelWorldTransform[i],
            mMeshConstantsUploader[currentFrameIdx].GetGpuVirtualAddress() + sizeof(ModelConstants) * i);
    }

    renderer.Sort();
}

std::shared_ptr<MeshRendererBuilder> Scene::SetMeshRenderers()
{
    std::shared_ptr<MeshRendererBuilder> meshRendererBuilder = std::make_shared<MeshRendererBuilder>();
    std::queue<std::future<void>> renderTaskQueue;

    DepthBuffer& depthBuffer = CURRENT_SCENE_DEPTH_BUFFER;
    ColorBuffer& colorBuffer = CURRENT_SCENE_COLOR_BUFFER;

    MeshRenderer& meshRenderer = meshRendererBuilder->Add<MeshRenderer>(MeshRenderer::kDefault);
    meshRenderer.SetPassCount(1);
    meshRenderer.SetCamera(mSceneCamera);
    meshRenderer.SetScene(*this);
    meshRenderer.SetViewport(Graphics::GetDefaultViewPort());
    meshRenderer.SetScissor(Graphics::GetDefaultScissor());
    meshRenderer.SetDepthStencilTarget(depthBuffer);
    meshRenderer.AddRenderTarget(colorBuffer);

    //SetRenderModels(meshRenderer);
    renderTaskQueue.emplace(Utility::gThreadPoolExecutor.Submit(&Scene::SetRenderModels, this, std::ref(meshRenderer)));

    ShadowMeshRenderer& shadowRenderer = meshRendererBuilder->Add<ShadowMeshRenderer>(MeshRenderer::kShadows);
    shadowRenderer.SetPassCount(ModelRenderer::gNumCSMDivides + 1);
    ShadowBuffer& shadowBuffer = ModelRenderer::GetCurrentShadowBuffer();
    ColorBuffer& nonMsaaShadowBuffer = ModelRenderer::GetCurrentNonMsaaShadowBuffer();
    shadowRenderer.SetBatchType(MeshRenderer::kShadows);
    shadowRenderer.SetScene(*this);
    shadowRenderer.SetCameras(mShadowCameras.data(), mShadowCameras.size());
    shadowRenderer.SetDepthStencilTarget(shadowBuffer, nonMsaaShadowBuffer);

    //SetRenderModels(shadowRenderer);
    renderTaskQueue.emplace(Utility::gThreadPoolExecutor.Submit(&Scene::SetRenderModels, this, std::ref(shadowRenderer)));

    while (!renderTaskQueue.empty())
    {
        renderTaskQueue.front().get();
        renderTaskQueue.pop();
    }

    meshRenderer.SetObjectsPSO();
    shadowRenderer.SetObjectsPSO();

    return meshRendererBuilder;
}

std::pair<std::shared_ptr<MeshRendererBuilder>, std::shared_ptr<FullScreenRenderer>> Scene::SetMeshRenderersDeferred()
{
    std::shared_ptr<MeshRendererBuilder> meshRendererBuilder = std::make_shared<MeshRendererBuilder>();
    std::shared_ptr<FullScreenRenderer> deferredRenderer = std::make_shared<FullScreenRenderer>();
    std::queue<std::future<void>> renderTaskQueue;

    DepthBuffer& depthBuffer = CURRENT_SCENE_DEPTH_BUFFER;
    ColorBuffer& colorBuffer = CURRENT_SCENE_COLOR_BUFFER;

    DeferredRenderer& meshRenderer = meshRendererBuilder->Add<DeferredRenderer>(MeshRenderer::kGBuffer);
    meshRenderer.SetPassCount(1);
    meshRenderer.SetCamera(mSceneCamera);
    meshRenderer.SetScene(*this);
    meshRenderer.SetViewport(Graphics::GetDefaultViewPort());
    meshRenderer.SetScissor(Graphics::GetDefaultScissor());
    meshRenderer.SetDepthStencilTarget(depthBuffer);
    meshRenderer.AddRenderTarget(ModelRenderer::GetCurrentGBuffer());

    SetRenderModels(meshRenderer);
    //renderTaskQueue.emplace(Utility::gThreadPoolExecutor.Submit(&Scene::SetRenderModels, this, std::ref(meshRenderer)));

    ShadowMeshRenderer& shadowRenderer = meshRendererBuilder->Add<ShadowMeshRenderer>(MeshRenderer::kShadows);
    shadowRenderer.SetPassCount(ModelRenderer::gNumCSMDivides + 1);
    ShadowBuffer& shadowBuffer = ModelRenderer::GetCurrentShadowBuffer();
    ColorBuffer& nonMsaaShadowBuffer = ModelRenderer::GetCurrentNonMsaaShadowBuffer();
    shadowRenderer.SetBatchType(MeshRenderer::kShadows);
    shadowRenderer.SetScene(*this);
    shadowRenderer.SetCameras(mShadowCameras.data(), mShadowCameras.size());
    shadowRenderer.SetDepthStencilTarget(shadowBuffer, nonMsaaShadowBuffer);

    SetRenderModels(shadowRenderer);
    //renderTaskQueue.emplace(Utility::gThreadPoolExecutor.Submit(&Scene::SetRenderModels, this, std::ref(shadowRenderer)));

    meshRenderer.SetObjectsPSO();
    shadowRenderer.SetObjectsPSO();

    deferredRenderer->AddRenderTarget(colorBuffer);
    deferredRenderer->SetCamera(mSceneCamera);
    deferredRenderer->SetViewport(Graphics::GetDefaultViewPort());
    deferredRenderer->SetScissor(Graphics::GetDefaultScissor());
    deferredRenderer->SetScene(*this);
    deferredRenderer->SetPSO();

    return std::pair(meshRendererBuilder, deferredRenderer);
}

void Scene::UpdateModels()
{
    if (mModelDirtyFrameCount == 0)
        return;

    ASSERT(mModelWorldTransform.size() == mModels.size());

    if (mModelDirtyFrameCount == SWAP_CHAIN_BUFFER_COUNT)
    {
        for (size_t i = 0; i < mModels.size(); i++)
        {
            Model& model = mModels[i];

            Matrix4 localTrans(
                Matrix3(Quaternion(model.mRotation)) * Matrix3::MakeScale(Vector3(model.mScale)),
                Vector3(model.mPosition));
            const Math::AffineTransform& transformAffine = (const Math::AffineTransform&)localTrans;
            if (model.mParentIndex != (uint32_t)-1)
            {
                ASSERT(model.mParentIndex < i);
                mModelWorldTransform[i] = mModelWorldTransform[model.mParentIndex] * transformAffine;
            }
            else
            {
                mModelWorldTransform[i] = transformAffine;
            }
        }

        UpdateModelBoundingSphere();
    }

    void* modelTransBuffer = mMeshConstantsUploader[CURRENT_FARME_BUFFER_INDEX].Map();

    for (size_t i = 0; i < mModels.size(); i++)
    {
        ModelConstants modelConstants;
        modelConstants.World = mModelWorldTransform[i];
        modelConstants.WorldIT = Math::Transpose(Math::Invert(modelConstants.World)).Get3x3();
        CopyMemory((uint8_t*)modelTransBuffer + i * sizeof(ModelConstants), &modelConstants, sizeof(ModelConstants));
    }
    //Math::BoundingSphere boundingSphere(mModelWorldTransform[i].GetTranslation(), mModelWorldTransform[i].GetUniformScale());
    //model.m_BSOS = boundingSphere;
    //model.m_BBoxOS = Math::AxisAlignedBox::CreateFromSphere(boundingSphere);

    mModelDirtyFrameCount--;
}

void Scene::UpdateLight()
{
    float costheta = std::cos(mSunDirectionTheta);
    float sintheta = std::sin(mSunDirectionTheta);
    float cosphi = std::cos(mSunDirectionPhi);
    float sinphi = std::sin(mSunDirectionPhi);

    mSunDirection = Normalize(Vector3(sintheta * cosphi, costheta, sintheta * sinphi));
    if (ModelRenderer::gNumCSMDivides == 0)
    {
        mShadowCameras.resize(1);
        //mShadowCamera.UpdateMatrix(-mSunDirection, mSceneBS_WS.GetCenter(), Vector3(mSceneBS_WS.GetRadius() * 2.0f));
        mShadowCameras[0].UpdateMatrix(-mSunDirection, mSceneCamera.GetWorldSpaceFrustum(), mSceneBS_WS.GetRadius(), 
            2048, 2048, 8);
    }
    else
    {
        ShadowCamera::GetDivideCSMCameras(mShadowCameras, ModelRenderer::gCSMDivides, ModelRenderer::gNumCSMDivides,
            MAX_CSM_DIVIDES, -mSunDirection, mSceneCamera, mSceneBS_WS.GetRadius(), 2048, 2048, 8);
    }
}

void Scene::MapGpuDescriptors()
{
    ShadowBuffer* shadowBuffers = ModelRenderer::GetShadowBuffers();
    ColorBuffer* GBuffers = ModelRenderer::GetGBuffers();
    DepthBuffer* depthBuffer = Graphics::GetSceneDepthBuffers();

    std::vector<DescriptorHandle> allHandles;
    allHandles.push_back(mRadianceCubeMap.GetSRV());
    allHandles.push_back(mIrradianceCubeMap.GetSRV());
    allHandles.push_back(mPreComputeBRDF.GetSRV());
    //allHandles.push_back(shadowBuffer.GetSRV());

    ASSERT(allHandles.size() <= 8);

    if (!mSceneTextureGpuHandle)
        mSceneTextureGpuHandle = ALLOC_DESCRIPTOR_GPU(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8);

    if (!mShadowGpuHandle)
        mShadowGpuHandle = ALLOC_DESCRIPTOR_GPU(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, SWAP_CHAIN_BUFFER_COUNT);

    

    for (uint32_t i = 0; i < allHandles.size(); i++)
    {
        if (allHandles[i])
            Graphics::gDevice->CopyDescriptorsSimple(1, mSceneTextureGpuHandle + i, allHandles[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

#ifdef DEFERRED_RENDER
    if (!mDeferredTextureGpuHandle)
        mDeferredTextureGpuHandle = ALLOC_DESCRIPTOR_GPU(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4 * SWAP_CHAIN_BUFFER_COUNT);
    for (uint32_t i = 0, j = 0; i < SWAP_CHAIN_BUFFER_COUNT * 4; i += 4, j++)
    {
        Graphics::gDevice->CopyDescriptorsSimple(1, mDeferredTextureGpuHandle + i,
            GBuffers[j].GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        Graphics::gDevice->CopyDescriptorsSimple(1, mDeferredTextureGpuHandle + (i + 1),
            depthBuffer[j].GetDepthSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        Graphics::gDevice->CopyDescriptorsSimple(1, mDeferredTextureGpuHandle + (i + 2),
            SSAORenderer::GetSSAOFinalHandle(j), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
#endif // DEFERRED_RENDER

    ResetShadowMapHandle();
}

void Scene::UpdateModelBoundingSphere()
{
    mSceneBS_WS = BoundingSphere(kZero);
    for (size_t i = 0; i < mModels.size(); i++)
    {
        mSceneBS_WS = mSceneBS_WS.Union(mModels[i].GetWorldBoundingSphere());
    }
}
