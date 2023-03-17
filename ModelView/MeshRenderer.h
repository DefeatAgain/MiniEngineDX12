#pragma once
#include "CoreHeader.h"
#include "Math/VectorMath.h"
#include "Camera.h"
#include "Material.h"
#include "Utils/DebugUtils.h"

#define SHADOW_MAP_FORMAT DXGI_FORMAT_D32_FLOAT

struct Mesh;
struct SubMesh;

class Model;
class Scene;
class ColorBuffer;
class DepthBuffer;
class ShadowBuffer;
class CameraController;
class GraphicsCommandList;
class GraphicsPipelineState;

struct GlobalConstants;

namespace ModelRenderer
{
    enum eDepthPsoFlags : uint8_t
    {
        kIsDepth = 0x1,
        kIsShadow = 0x2,
    };

    __declspec(align(4)) struct RendererPsoDesc
    {
        uint16_t meshPSOFlags;
        union
        {
            uint8_t depthPsoFlags;
            struct
            {
                uint8_t isDepth : 1;
                uint8_t isShadow : 1;
                uint8_t shadowMsaaCount : 2;
                uint8_t : 4;
            };
        };
        uint8_t numCSMDividesCount : 2;
        uint8_t : 6;
    };

    enum ForwardRendererBindings
    {
        kMeshConstants,
        kMaterialConstants,
        kGlobalConstants,

        kModelTextures,
        kModelTextureSamplers,

        kSceneTextures,
        kShadowTexture,

        kNumRootBindings
    };

    extern std::vector<GraphicsPipelineState*> sAllPSOs;

    extern RootSignature* sForwardRootSig;
    extern GraphicsPipelineState* sSkyboxPSO;

    extern uint32_t gMsaaShadowSample;
    extern uint32_t gNumCSMDivides;
    extern float gCSMDivides[];

    void Initialize();
    void Destroy();

    uint16_t GetPsoIndex(RendererPsoDesc rendererPsoDesc);
    ShadowBuffer* GetShadowBuffers();
    ColorBuffer* GetNonMsaaShadowBuffers();
    ShadowBuffer& GetCurrentShadowBuffer();
    ColorBuffer& GetCurrentNonMsaaShadowBuffer();

    void ResetShadowMsaa(uint32_t sampleCount);
}

class MeshRenderer : public NonCopyable
{
public:
    enum BatchType { kDefault, kShadows, kNumBachTypes };
    enum DrawPass { kZPass, kOpaque, kTransparent, kNumPasses };

protected:
    struct SortKey
    {
        union
        {
            uint64_t value;
            struct
            {
                uint64_t objectIdx : 16;
                uint64_t psoIdx : 12;
                uint64_t distance : 32;
                uint64_t passID : 4;    // high bit is priority
            };
        };

        operator uint64_t() { return value; }
    };

    struct SortObject
    {
        const Model* model;
        const SubMesh* subMesh;
        D3D12_GPU_VIRTUAL_ADDRESS meshCBV;
    };

    struct RenderPass
    {
        std::vector<SortObject> sortObjects;
        std::vector<SortKey> sortKeys;
        uint32_t passCounts[kNumPasses];
        DrawPass currentPass;
        uint32_t currentDraw;
        const Math::BaseCamera* camera;
    };
public:

    MeshRenderer() : mBatchType(kDefault)
    {
        Reset();
    }

    void Reset();

    void SetPassCount(size_t count) { mRenderPasses.resize(count); }
    size_t GetPassCount() const { return mRenderPasses.size(); }
    void SetBatchType(BatchType type) { mBatchType = type; }
    void SetScene(const Scene& scene) { mScene = &scene; }
    void SetViewport(const D3D12_VIEWPORT& viewport) { mViewport = viewport; }
    void SetScissor(const D3D12_RECT& scissor) { mScissor = scissor; }
    template<typename CameraType>
    void SetCamera(const CameraType& camera, size_t passIndex = 0) 
    { 
        static_assert(std::is_base_of_v<Math::BaseCamera, CameraType>);
        mRenderPasses[passIndex].camera = &camera;
    }
    template<typename CameraType>
    void SetCameras(const CameraType cameras[], size_t numCameras)
    { 
        static_assert(std::is_base_of_v<Math::BaseCamera, CameraType>);
        ASSERT(numCameras == mRenderPasses.size());
        for (size_t i = 0; i < numCameras; i++)
            mRenderPasses[i].camera = &cameras[i];
    }
    void AddRenderTarget(ColorBuffer& RTV)
    {
        ASSERT(mNumRTVs < 8);
        mRenderTargets[mNumRTVs++] = &RTV;
    }
    void AddRenderTarget(ColorBuffer& RTV, ColorBuffer& msaaRTV)
    {
        ASSERT(mNumRTVs < 8);
        mRenderTargets[mNumRTVs] = &RTV;
        mMsaaRenderTargets[mNumRTVs] = &msaaRTV;
        mNumRTVs++;
    }
    void SetDepthStencilTarget(DepthBuffer& DSV) { mDepthBuffer = &DSV; }
    void SetDepthStencilTarget(DepthBuffer& DSV, ColorBuffer& nonMsaaDepthBuufer) { mDepthBuffer = &DSV; mNonMsaaDepthBuffer = &nonMsaaDepthBuufer; }

    const Math::Frustum& GetWorldFrustum(size_t passIndex = 0) const { return mRenderPasses[passIndex].camera->GetWorldSpaceFrustum(); }
    const Math::Frustum& GetViewFrustum(size_t passIndex = 0) const { return mRenderPasses[passIndex].camera->GetViewSpaceFrustum(); }
    const Math::Matrix4& GetViewMatrix(size_t passIndex = 0) const { return mRenderPasses[passIndex].camera->GetViewMatrix(); }

    void SetObjectsPSO();

    void AddMesh(size_t passIndex, const SubMesh& mesh, const Model* model, float distance, D3D12_GPU_VIRTUAL_ADDRESS meshCBV);

    void Sort();

    void RenderMeshes(GraphicsCommandList& context, GlobalConstants& globals, DrawPass pass);
protected:
    virtual void RenderMeshesBegin(GraphicsCommandList& context, GlobalConstants& globals, DrawPass pass) {}
    virtual void RenderMeshesImpl(GraphicsCommandList& context, GlobalConstants& globals, DrawPass pass, RenderPass& renderPass);
    virtual void RenderMeshesEnd(GraphicsCommandList& context, GlobalConstants& globals, DrawPass pass) {}

    BatchType mBatchType;
    std::vector<RenderPass> mRenderPasses;
    uint32_t mCurrentRenderPassIdx;

    D3D12_VIEWPORT mViewport;
    D3D12_RECT mScissor;
    uint32_t mNumRTVs;
    const Scene* mScene;
    ColorBuffer* mRenderTargets[8];
    DepthBuffer* mDepthBuffer;
    ColorBuffer* mMsaaRenderTargets[8];
    ColorBuffer* mNonMsaaDepthBuffer;
};


class ShadowMeshRenderer : public MeshRenderer
{
public:
    ShadowMeshRenderer()
    {
        mBatchType = kShadows;
    }

    virtual void RenderMeshesBegin(GraphicsCommandList& context, GlobalConstants& globals,
        DrawPass pass) override;
    virtual void RenderMeshesImpl(GraphicsCommandList& context, GlobalConstants& globals, 
        DrawPass pass, RenderPass& renderPass) override;
    virtual void RenderMeshesEnd(GraphicsCommandList& context, GlobalConstants& globals, 
        DrawPass pass) override;
};


class MeshRendererBuilder : public NonCopyable
{
public:
    MeshRendererBuilder() : mMeshRenderers(MeshRenderer::kNumBachTypes) { }
    ~MeshRendererBuilder() {}

    template<typename T>
    T& Add(MeshRenderer::BatchType type) 
    {
        ASSERT(mMeshRenderers[type] == nullptr);
        mMeshRenderers[type] = std::make_unique<T>();
        return static_cast<T&>(*mMeshRenderers[type]); 
    }

    template<typename T>
    T& Get(MeshRenderer::BatchType type) { return static_cast<T&>(*mMeshRenderers[type]); }
private:
    std::vector<std::unique_ptr<MeshRenderer>> mMeshRenderers;
};
