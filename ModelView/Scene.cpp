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

    UpdateModelBoundingSphere();

    m_CameraController.reset(new OrbitCamera(mSceneCamera, mSceneBoundingSphere, Vector3(kYUnitVector)));

    mSunDirectionTheta = 0.75f;
    mSunDirectionPhi = -0.5f;
    mSunLightIntensity = Vector3(1, 1, 1);
}

GraphicsCommandList* Scene::RenderScene(GraphicsCommandList* context)
{
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
    context->TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    context->ClearDepth(depthBuffer);

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
    meshRenderer.RenderMeshes(*context, globals, MeshRenderer::kZPass);

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
    shadowRenderer.RenderMeshes(*context, globals, MeshRenderer::kZPass);

    context->TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    context->ClearColor(colorBuffer);

    //context->TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    context->TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
    context->SetRenderTarget(colorBuffer.GetRTV(), depthBuffer.GetDSV_DepthReadOnly());
    context->SetViewportAndScissor(Graphics::GetDefaultViewPort(), Graphics::GetDefaultScissor());
    meshRenderer.RenderMeshes(*context, globals, MeshRenderer::kOpaque);

    RenderSkyBox(context);

    meshRenderer.RenderMeshes(*context, globals, MeshRenderer::kTransparent);
}

GraphicsCommandList* Scene::RenderSkyBox(GraphicsCommandList* context)
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
    context->SetPipelineState(*ModelRenderer::sSkyboxPSO);

    context->TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
    context->TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    context->SetRenderTarget(colorBuffer.GetRTV(), depthBuffer.GetDSV_DepthReadOnly());
    context->SetViewportAndScissor(Graphics::GetDefaultViewPort(), Graphics::GetDefaultScissor());

    //context->SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, s_TextureHeap.GetHeapPointer());
    context->SetDynamicConstantBufferView(ModelRenderer::kMeshConstants, sizeof(SkyboxVSCB), &skyVSCB);
    context->SetDynamicConstantBufferView(ModelRenderer::kMaterialConstants, sizeof(SkyboxPSCB), &skyPSCB);
    context->SetDescriptorTable(ModelRenderer::kRadianceIBLTexture, mRadianceCubeMap.GetSRV());
    context->Draw(3);

    return context;
}

void Scene::Update(float deltaTime)
{
}

void Scene::Render()
{
}

void Scene::SetIBLTextures(TextureRef diffuseIBL, TextureRef specularIBL)
{
}

void Scene::SetIBLBias(float LODBias)
{
}

void Scene::UpdateGlobalDescriptors()
{
}

BoundingSphere Scene::UpdateModelBoundingSphere()
{
    BoundingSphere boundingSphere;
    for (size_t i = 0; i < mModels.size(); i++)
    {
        boundingSphere.Union(mModels[i].m_BSOS);
    }
    mSceneBoundingSphere = boundingSphere;
}
