#include "Scene.h"
#include "Model.h"
#include "MeshRenderer.h"
#include "FrameContext.h"
#include "Graphics.h"
#include "PixelBuffer.h"
#include "PipelineState.h"

void Scene::Startup()
{
    mSceneCamera.SetZRange(1.0f, 10000.0f);
    mDirtyModels = true;

    UpdateModels();

    mCameraController.reset(new OrbitCamera(mSceneCamera, mSceneBoundingSphere, Vector3(kYUnitVector)));

    mSunDirectionTheta = 0.75f;
    mSunDirectionPhi = -0.5f;
    mSunLightIntensity = Vector3(1, 1, 1);
}

CommandList* Scene::RenderScene(CommandList* context)
{
    GraphicsCommandList& ghContext = context->GetGraphicsCommandList().Begin();

    float costheta = cosf(mSunDirectionTheta);
    float sintheta = sinf(mSunDirectionTheta);
    float cosphi = cosf(mSunDirectionPhi * 3.14159f * 0.5f);
    float sinphi = sinf(mSunDirectionPhi * 3.14159f * 0.5f);

    Vector3 SunDirection = Normalize(Vector3(costheta * cosphi, sinphi, sintheta * cosphi));
    Vector3 ShadowBounds = Vector3(mSceneBoundingSphere.GetRadius());
    //m_SunShadowCamera.UpdateMatrix(-SunDirection, m_ModelInst.GetCenter(), ShadowBounds,
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

    MeshRenderer meshRenderer(MeshRenderer::kDefault);
    meshRenderer.SetCamera(mSceneCamera);
    meshRenderer.SetViewport(Graphics::GetDefaultViewPort());
    meshRenderer.SetScissor(Graphics::GetDefaultScissor());
    meshRenderer.SetDepthStencilTarget(depthBuffer);
    meshRenderer.AddRenderTarget(colorBuffer);

    for (size_t i = 0; i < mModels.size(); i++)
    {
        mModels[i].Render(meshRenderer, mModelTransform[i], 
            mMeshConstantsUploader.GetGpuVirtualAddress() + Math::AlignUp(sizeof(ModelConstants), 256) * i);
    }

    meshRenderer.Sort();
    meshRenderer.RenderMeshes(ghContext, globals, MeshRenderer::kZPass);

    MeshRenderer shadowRenderer(MeshRenderer::kShadows);
    ShadowBuffer& shadowBuffer = ModelRenderer::GetCurrentShadowBuffer();
    shadowRenderer.SetCamera(mShadowCamera);
    shadowRenderer.SetDepthStencilTarget(shadowBuffer);

    for (size_t i = 0; i < mModels.size(); i++)
    {
        mModels[i].Render(shadowRenderer, mModelTransform[i], 
            mMeshConstantsUploader.GetGpuVirtualAddress() + Math::AlignUp(sizeof(ModelConstants), 256) * i);
    }

    shadowRenderer.Sort();
    shadowRenderer.RenderMeshes(ghContext, globals, MeshRenderer::kZPass);

    ghContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    ghContext.ClearColor(colorBuffer);

    //context->TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ghContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
    ghContext.SetRenderTarget(colorBuffer.GetRTV(), depthBuffer.GetDSV_DepthReadOnly());
    ghContext.SetViewportAndScissor(Graphics::GetDefaultViewPort(), Graphics::GetDefaultScissor());
    meshRenderer.RenderMeshes(ghContext, globals, MeshRenderer::kOpaque);

    RenderSkyBox(ghContext);

    meshRenderer.RenderMeshes(ghContext, globals, MeshRenderer::kTransparent);

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
    //context->SetRootSignature(m_RootSig);
    ghContext.SetPipelineState(*ModelRenderer::sSkyboxPSO);

    ghContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
    ghContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    ghContext.SetRenderTarget(colorBuffer.GetRTV(), depthBuffer.GetDSV_DepthReadOnly());
    ghContext.SetViewportAndScissor(Graphics::GetDefaultViewPort(), Graphics::GetDefaultScissor());

    //context->SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, s_TextureHeap.GetHeapPointer());
    ghContext.SetDynamicConstantBufferView(ModelRenderer::kMeshConstants, sizeof(SkyboxVSCB), &skyVSCB);
    ghContext.SetDynamicConstantBufferView(ModelRenderer::kMaterialConstants, sizeof(SkyboxPSCB), &skyPSCB);
    ghContext.SetDescriptorTable(ModelRenderer::kRadianceIBLTexture, mRadianceCubeMap.GetSRV());
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

void Scene::UpdateModels()
{
    if (!mDirtyModels)
        return;

    ASSERT(mModelTransform.size() == mModels.size());

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

        Math::BoundingSphere boundingSphere(mModelTransform[i].GetTranslation(), mModelTransform[i].GetUniformScale());
        model.m_BSOS = boundingSphere;
        model.m_BBoxOS = Math::AxisAlignedBox::CreateFromSphere(boundingSphere);
    }

    UpdateModelBoundingSphere();

    mDirtyModels = false;
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
