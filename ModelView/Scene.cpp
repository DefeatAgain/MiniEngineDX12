#include "Scene.h"
#include "Model.h"
#include "MeshRenderer.h"
#include "FrameContext.h"
#include "Graphics.h"
#include "Mesh.h"
#include "glTF.h"
#include "PixelBuffer.h"
#include "PipelineState.h"

void Scene::Destroy()
{
}

void Scene::Startup()
{
    mSceneBS_WS = Math::BoundingSphere(kZero);
    mSceneCamera.SetZRange(1.0f, 10000.0f);
    mModelDirtyFrameCount = SWAP_CHAIN_BUFFER_COUNT;

    mSunDirectionTheta = 0.2f * 3.14f;
    mSunDirectionPhi = 0.0f;
    mSunLightIntensity = Vector3(1, 1, 1) * 0.5f;

    mCameraController.reset(new FlyingFPSCamera(mSceneCamera, Vector3(kYUnitVector)));

    if (mModelWorldTransform.size() != mModels.size())
        mModelWorldTransform.resize(mModels.size());
    for (size_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
    {
        mMeshConstantsUploader[i].Create(L"Mesh Constants Buffer " + std::to_wstring(i),
            Math::AlignUp(sizeof(ModelConstants), 256) * mModelWorldTransform.size());
    }
}

CommandList* Scene::RenderScene(CommandList* context)
{
    GraphicsCommandList& ghContext = context->GetGraphicsCommandList().Begin(L"Render Scene");

    MeshManager::GetInstance()->TransitionStateToRead(ghContext);

    size_t currentFrameIdx = CURRENT_FARME_BUFFER_INDEX;

    float costheta = std::cos(mSunDirectionTheta);
    float sintheta = std::sin(mSunDirectionTheta);
    float cosphi = std::cos(mSunDirectionPhi);
    float sinphi = std::sin(mSunDirectionPhi);

    Vector3 SunDirection = Normalize(Vector3(sintheta * cosphi, costheta, sintheta * sinphi));
    mShadowCamera.UpdateMatrix(-SunDirection, mSceneBS_WS.GetCenter(), Vector3(mSceneBS_WS.GetRadius() * 2.0f));

    GlobalConstants globals;
    globals.SunShadowMatrix = mShadowCamera.GetShadowMatrix();
    globals.SunDirection = SunDirection;
    globals.SunIntensity = mSunLightIntensity;

    // Begin rendering depth
    DepthBuffer& depthBuffer = CURRENT_SCENE_DEPTH_BUFFER;
    ColorBuffer& colorBuffer = CURRENT_SCENE_COLOR_BUFFER;

    ghContext.PIXBeginEvent(L"PreZ"); // render preZ
    ghContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    ghContext.ClearDepth(depthBuffer);

    MeshRenderer meshRenderer(MeshRenderer::kDefault);
    meshRenderer.SetCamera(mSceneCamera);
    meshRenderer.SetScene(*this);
    meshRenderer.SetViewport(Graphics::GetDefaultViewPort());
    meshRenderer.SetScissor(Graphics::GetDefaultScissor());
    meshRenderer.SetDepthStencilTarget(depthBuffer);
    meshRenderer.AddRenderTarget(colorBuffer);

    for (size_t i = 0; i < mModels.size(); i++)
    {
        mModels[i].Render(meshRenderer, mModelWorldTransform[i], 
            mMeshConstantsUploader[currentFrameIdx].GetGpuVirtualAddress() + Math::AlignUp(sizeof(ModelConstants), 256) * i);
    }

    meshRenderer.Sort();
    meshRenderer.RenderMeshes(ghContext, globals, MeshRenderer::kZPass);
    ghContext.PIXEndEvent(); // render preZ end

    MeshRenderer shadowRenderer(MeshRenderer::kShadows);
    ShadowBuffer& shadowBuffer = ModelRenderer::GetCurrentShadowBuffer();
    shadowRenderer.SetScene(*this);
    shadowRenderer.SetCamera(mShadowCamera);
    shadowRenderer.SetDepthStencilTarget(shadowBuffer);

    for (size_t i = 0; i < mModels.size(); i++)
    {
        mModels[i].Render(shadowRenderer, mModelWorldTransform[i], 
            mMeshConstantsUploader[currentFrameIdx].GetGpuVirtualAddress() + Math::AlignUp(sizeof(ModelConstants), 256) * i);
    }

    shadowRenderer.Sort();
    ghContext.PIXBeginEvent(L"ShadowMap"); // render shadowmap
    shadowRenderer.RenderMeshes(ghContext, globals, MeshRenderer::kZPass);
    ghContext.PIXEndEvent(); // render shadowmap end

    ghContext.PIXBeginEvent(L"Mesh"); // render mesh
    ghContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    ghContext.ClearColor(colorBuffer);

    //context->TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ghContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
    ghContext.SetRenderTarget(colorBuffer.GetRTV(), depthBuffer.GetDSV_DepthReadOnly());
    ghContext.SetViewportAndScissor(Graphics::GetDefaultViewPort(), Graphics::GetDefaultScissor());
    meshRenderer.RenderMeshes(ghContext, globals, MeshRenderer::kOpaque);

    RenderSkyBox(ghContext);

    meshRenderer.RenderMeshes(ghContext, globals, MeshRenderer::kTransparent);

    ghContext.PIXEndEvent(); // render mesh end
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
    skyPSCB.TextureLevel = mSpecularIBLBias;

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

    MapShadowDescriptors();
}

void Scene::Render()
{
    PUSH_MUTIRENDER_TASK({ D3D12_COMMAND_LIST_TYPE_DIRECT, PushGraphicsTaskBind(&Scene::RenderScene, this) });
}

void Scene::SetIBLTextures(TextureRef diffuseIBL, TextureRef specularIBL)
{
    ASSERT(diffuseIBL && specularIBL);

    mRadianceCubeMap = specularIBL;
    mIrradianceCubeMap = diffuseIBL;

    diffuseIBL.WaitForValid();
    specularIBL.WaitForValid();

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
    size_t bufferSize = Math::AlignUp(sizeof(ModelConstants), 256);

    for (size_t i = 0; i < mModels.size(); i++)
    {
        ModelConstants modelConstants;
        modelConstants.World = mModelWorldTransform[i];
        modelConstants.WorldIT = Math::Transpose(Math::Invert(modelConstants.World)).Get3x3();
        CopyMemory((uint8_t*)modelTransBuffer + i * bufferSize, &modelConstants, sizeof(ModelConstants));
    }
    //Math::BoundingSphere boundingSphere(mModelWorldTransform[i].GetTranslation(), mModelWorldTransform[i].GetUniformScale());
    //model.m_BSOS = boundingSphere;
    //model.m_BBoxOS = Math::AxisAlignedBox::CreateFromSphere(boundingSphere);

    mModelDirtyFrameCount--;
}

void Scene::MapGpuDescriptors()
{
    ShadowBuffer& shadowBuffer = ModelRenderer::GetCurrentShadowBuffer();

    std::vector<DescriptorHandle> allHandles;
    allHandles.push_back(mRadianceCubeMap.GetSRV());
    allHandles.push_back(mIrradianceCubeMap.GetSRV());
    allHandles.emplace_back();
    allHandles.push_back(shadowBuffer.GetSRV());

    ASSERT(allHandles.size() <= 8);

    if (!mSceneTextureGpuHandle)
        mSceneTextureGpuHandle = ALLOC_DESCRIPTOR_GPU(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8);

    for (uint32_t i = 0; i < allHandles.size(); i++)
    {
        if (allHandles[i])
            Graphics::gDevice->CopyDescriptorsSimple(1, mSceneTextureGpuHandle + i, allHandles[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
}

void Scene::MapShadowDescriptors()
{
    ShadowBuffer& shadowBuffer = ModelRenderer::GetCurrentShadowBuffer();
    Graphics::gDevice->CopyDescriptorsSimple(1, mSceneTextureGpuHandle + kSunShadowTexture, shadowBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void Scene::UpdateModelBoundingSphere()
{
    mSceneBS_WS = BoundingSphere(kZero);
    for (size_t i = 0; i < mModels.size(); i++)
    {
        mSceneBS_WS = mSceneBS_WS.Union(mModels[i].GetWorldBoundingSphere());
    }
}
