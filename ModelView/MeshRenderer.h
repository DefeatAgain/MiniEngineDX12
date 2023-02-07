#pragma once
#include "CoreHeader.h"
#include "Math/VectorMath.h"
#include "Camera.h"
#include "Material.h"
#include "Utils/DebugUtils.h"

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
    enum ForwardRendererBindings
    {
        kMeshConstants,
        kMaterialConstants,
        kGlobalConstants,

        kBaseColorTextureSRV,
        kMetallicRoughnessTextureSRV,
        kOcclusionTextureSRV,
        kEmissiveTextureSRV,
        kNormalTextureSRV,

        kBaseColorTextureSampler,
        kMetallicTextureSampler,
        kOcclusionTextureSampler,
        kEmissiveTextureSampler,
        kNormalTextureSampler,

        kRadianceIBLTexture,
        kIrradianceIBLTexture,
        kPreComputeGGXBRDFTexture,
        kTexSunShadowTexture,

        kNumRootBindings
    };

    inline std::vector<GraphicsPipelineState*> sAllPSOs;

    inline RootSignature* sForwardRootSig;
    inline GraphicsPipelineState* sSkyboxPSO;

    void Initialize();
    void Destroy();

    uint16_t GetPsoIndex(ePSOFlags psoFlags);
    uint16_t GetDepthPsoIndex(ePSOFlags psoFlags, bool isShadow);
    ShadowBuffer& GetCurrentShadowBuffer();
}

class MeshRenderer
{
public:
    enum BatchType { kDefault, kShadows };
    enum DrawPass { kZPass, kOpaque, kTransparent, kNumPasses };

    MeshRenderer(BatchType type)
    {
        mBatchType = type;
        mScene = nullptr;
        mViewport = {};
        mScissor = {};
        mNumRTVs = 0;
        mDepthBuffer = nullptr;
        mSortObjects.clear();
        mSortKeys.clear();
        std::memset(mPassCounts, 0, sizeof(mPassCounts));
        mCurrentPass = kZPass;
        mCurrentDraw = 0;
    }

    void SetScene(const Scene& scene) { mScene = &scene; }
    void SetCamera(const Math::BaseCamera& camera) { mCamera = &camera; }
    void SetViewport(const D3D12_VIEWPORT& viewport) { mViewport = viewport; }
    void SetScissor(const D3D12_RECT& scissor) { mScissor = scissor; }
    void AddRenderTarget(ColorBuffer& RTV)
    {
        ASSERT(mNumRTVs < 8);
        mRenderTarges[mNumRTVs++] = &RTV;
    }
    void SetDepthStencilTarget(DepthBuffer& DSV) { mDepthBuffer = &DSV; }

    const Math::Frustum& GetWorldFrustum() const { return mCamera->GetWorldSpaceFrustum(); }
    const Math::Frustum& GetViewFrustum() const { return mCamera->GetViewSpaceFrustum(); }
    const Math::Matrix4& GetViewMatrix() const { return mCamera->GetViewMatrix(); }

    void AddMesh(const SubMesh& mesh, const Model* model, float distance, D3D12_GPU_VIRTUAL_ADDRESS meshCBV);

    void Sort();

    void RenderMeshes(GraphicsCommandList& context, GlobalConstants& globals, DrawPass pass);

private:

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
        D3D12_GPU_VIRTUAL_ADDRESS meshCBV;
    };

    std::vector<SortObject> mSortObjects;
    std::vector<SortKey> mSortKeys;
    BatchType mBatchType;
    uint32_t mPassCounts[kNumPasses];
    DrawPass mCurrentPass;
    uint32_t mCurrentDraw;

    const Scene* mScene;
    const Math::BaseCamera* mCamera;
    D3D12_VIEWPORT mViewport;
    D3D12_RECT mScissor;
    uint32_t mNumRTVs;
    ColorBuffer* mRenderTarges[8];
    DepthBuffer* mDepthBuffer;
};
