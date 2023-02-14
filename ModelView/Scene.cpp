#include "Scene.h"
#include "Model.h"
#include "MeshRenderer.h"
#include "FrameContext.h"
#include "Graphics.h"
#include "Mesh.h"
#include "PixelBuffer.h"
#include "PipelineState.h"

void Scene::Destroy()
{
}

void Scene::Startup()
{
    mSceneBoundingSphere = Math::BoundingSphere(kZero);
    mSceneCamera.SetZRange(1.0f, 10000.0f);
    mDirtyModels = true;

    mSunDirectionTheta = 0.75f;
    mSunDirectionPhi = -0.5f;
    mSunLightIntensity = Vector3(1, 1, 1);

    mCameraController.reset(new FlyingFPSCamera(mSceneCamera, Vector3(kYUnitVector)));
    mMeshConstantsUploader.Create(L"Mesh Constants Buffer", Math::AlignUp(sizeof(ModelConstants), 256) * mModelTransform.size());

    UpdateModels();
}

CommandList* Scene::RenderScene(CommandList* context)
{
    GraphicsCommandList& ghContext = context->GetGraphicsCommandList().Begin(L"Render Scene");

    MeshManager::GetInstance()->TransitionStateToRead(ghContext);

    float costheta = cosf(mSunDirectionTheta);
    float sintheta = sinf(mSunDirectionTheta);
    float cosphi = cosf(mSunDirectionPhi * 3.14159f * 0.5f);
    float sinphi = sinf(mSunDirectionPhi * 3.14159f * 0.5f);

    Vector3 SunDirection = Normalize(Vector3(costheta * cosphi, sinphi, sintheta * cosphi));
    Vector3 ShadowBounds = Vector3(mSceneBoundingSphere.GetRadius());
    mShadowCamera.UpdateMatrix(-SunDirection, Vector3(0, -500.0f, 0), Vector3(5000, 3000, 3000));

    GlobalConstants globals;
    globals.SunShadowMatrix = mShadowCamera.GetShadowMatrix();
    globals.SunDirection = SunDirection;
    globals.SunIntensity = mSunLightIntensity;

    // Begin rendering depth
    DepthBuffer& depthBuffer = CURRENT_SCENE_DEPTH_BUFFER;
    ColorBuffer& colorBuffer = CURRENT_SCENE_COLOR_BUFFER;
    ghContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    ghContext.ClearDepth(depthBuffer);

    //MeshRenderer meshRenderer(MeshRenderer::kDefault);
    //meshRenderer.SetCamera(mSceneCamera);
    //meshRenderer.SetScene(*this);
    //meshRenderer.SetViewport(Graphics::GetDefaultViewPort());
    //meshRenderer.SetScissor(Graphics::GetDefaultScissor());
    //meshRenderer.SetDepthStencilTarget(depthBuffer);
    //meshRenderer.AddRenderTarget(colorBuffer);

    //for (size_t i = 0; i < mModels.size(); i++)
    //{
    //    mModels[i].Render(meshRenderer, mModelTransform[i], 
    //        mMeshConstantsUploader.GetGpuVirtualAddress() + Math::AlignUp(sizeof(ModelConstants), 256) * i);
    //}

    //meshRenderer.Sort();
    //meshRenderer.RenderMeshes(ghContext, globals, MeshRenderer::kZPass);

    //MeshRenderer shadowRenderer(MeshRenderer::kShadows);
    //ShadowBuffer& shadowBuffer = ModelRenderer::GetCurrentShadowBuffer();
    //shadowRenderer.SetScene(*this);
    //shadowRenderer.SetCamera(mShadowCamera);
    //shadowRenderer.SetDepthStencilTarget(shadowBuffer);

    //for (size_t i = 0; i < mModels.size(); i++)
    //{
    //    mModels[i].Render(shadowRenderer, mModelTransform[i], 
    //        mMeshConstantsUploader.GetGpuVirtualAddress() + Math::AlignUp(sizeof(ModelConstants), 256) * i);
    //}

    //shadowRenderer.Sort();
    //shadowRenderer.RenderMeshes(ghContext, globals, MeshRenderer::kZPass);

    //ghContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    //ghContext.ClearColor(colorBuffer);

    //context->TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    //ghContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
    //ghContext.SetRenderTarget(colorBuffer.GetRTV(), depthBuffer.GetDSV_DepthReadOnly());
    //ghContext.SetViewportAndScissor(Graphics::GetDefaultViewPort(), Graphics::GetDefaultScissor());
    //meshRenderer.RenderMeshes(ghContext, globals, MeshRenderer::kOpaque);

    RenderSkyBox(ghContext);

    //meshRenderer.RenderMeshes(ghContext, globals, MeshRenderer::kTransparent);

    ghContext.Finish();
    return context;
}

void Scene::RenderSkyBox(GraphicsCommandList& ghContext)
{
    __declspec(align(16)) struct SkyboxVSCB
    {
        Matrix4 ProjInverse;
        Matrix3 ViewInverse;
    } skyVSCB;
    skyVSCB.ProjInverse = Invert(mSceneCamera.GetProjMatrix());
    skyVSCB.ViewInverse = Invert(mSceneCamera.GetViewMatrix()).Get3x3();

    __declspec(align(16)) struct SkyboxPSCB
    {
        float TextureLevel;
    } skyPSCB;
    skyPSCB.TextureLevel = mSpecularIBLBias;

    DepthBuffer& depthBuffer = CURRENT_SCENE_DEPTH_BUFFER;
    ColorBuffer& colorBuffer = CURRENT_SCENE_COLOR_BUFFER;
    ShadowBuffer& shadowBuffer = ModelRenderer::GetCurrentShadowBuffer();

    ghContext.SetPipelineState(*ModelRenderer::sSkyboxPSO);

    ghContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
    ghContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ghContext.SetRenderTarget(colorBuffer.GetRTV(), depthBuffer.GetDSV_DepthReadOnly());
    ghContext.SetViewportAndScissor(Graphics::GetDefaultViewPort(), Graphics::GetDefaultScissor());

    ghContext.SetDynamicConstantBufferView(ModelRenderer::kMeshConstants, sizeof(SkyboxVSCB), &skyVSCB);
    ghContext.SetDynamicConstantBufferView(ModelRenderer::kMaterialConstants, sizeof(SkyboxPSCB), &skyPSCB);
    ghContext.SetDescriptorTable(ModelRenderer::kSceneTextures, mSceneTexturesAlloc.GetStart());
    ghContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ghContext.Draw(3);
}

void Scene::Update(float deltaTime)
{
    UpdateModels();

    mCameraController->Update(deltaTime);
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

    if (!diffuseIBL.IsValid())
        const_cast<Texture*>(diffuseIBL.Get())->ForceWaitContext();

    if (!specularIBL.IsValid())
        const_cast<Texture*>(specularIBL.Get())->ForceWaitContext();
}

void Scene::UpdateModels()
{
    if (!mDirtyModels)
        return;

    ASSERT(mModelTransform.size() == mModels.size());

    void* modelTransBuffer = mMeshConstantsUploader.Map();
    size_t bufferSize = Math::AlignUp(sizeof(ModelConstants), 256);

    for (size_t i = 0; i < mModels.size(); i++)
    {
        Model& model = mModels[i];

        const Math::AffineTransform transformAffine(model.mLocalTrans);
        if (model.mParentIndex != (uint32_t)-1)
        {
            mModelTransform[i] = transformAffine * mModelTransform[model.mParentIndex];
        }
        else
        {
            mModelTransform[i] = transformAffine;
        }

        ModelConstants modelConstants;
        modelConstants.World = mModelTransform[i];
        modelConstants.WorldIT = Math::Transpose(Math::Invert(modelConstants.World)).Get3x3();

        CopyMemory((uint8_t*)modelTransBuffer + i * bufferSize, &modelConstants, sizeof(ModelConstants));
        Math::BoundingSphere boundingSphere(mModelTransform[i].GetTranslation(), mModelTransform[i].GetUniformScale());
        model.m_BSOS = boundingSphere;
        model.m_BBoxOS = Math::AxisAlignedBox::CreateFromSphere(boundingSphere);
    }

    UpdateModelBoundingSphere();

    mDirtyModels = false;
}

void Scene::MapLinearDescriptors()
{
    ShadowBuffer& shadowBuffer = ModelRenderer::GetCurrentShadowBuffer();

    std::vector<DescriptorHandle> allHandles;
    allHandles.push_back(mRadianceCubeMap.GetSRV());
    allHandles.push_back(mIrradianceCubeMap.GetSRV());
    allHandles.emplace_back();
    allHandles.push_back(shadowBuffer.GetSRV());


}

void Scene::UpdateModelBoundingSphere()
{
    BoundingSphere boundingSphere;
    for (size_t i = 0; i < mModels.size(); i++)
    {
        boundingSphere.Union(mModels[i].m_BSOS);
    }
    mSceneBoundingSphere = boundingSphere;
}
