#pragma once
#include "CoreHeader.h"
#include "Math/VectorMath.h"
#include "Camera.h"
#include "Utils/DebugUtils.h"

class Mesh;
class SubMesh;
class Model;
class ColorBuffer;
class DepthBuffer;
class CameraController;
class GraphicsCommandList;
class GraphicsPipelineState;

struct GlobalConstants;

namespace ModelRenderer
{
    enum RootBindings
    {
        kMeshConstants,
        kMaterialConstants,
        kMaterialSRVs,
        kMaterialSamplers,
        kCommonSRVs,
        kCommonCBV,
        kNumRootBindings
    };

    inline bool gIsPreZ = true;

    //DescriptorHeap s_TextureHeap;
    //DescriptorHeap s_SamplerHeap;
    inline std::vector<GraphicsPipelineState> gPSOs;

    inline RootSignature* mRootSig;
    inline GraphicsPipelineState* mSkyboxPSO;
    inline GraphicsPipelineState* mDefaultPSO; // Not finalized.  Used as a template.

    DescriptorHandle m_CommonTextures;

    void Initialize();

    uint16_t GetPsoIndex(ePSOFlags psoFlags, bool isDepth);
}

class MeshRenderer
{
public:
    enum BatchType { kDefault, kShadows };
    enum DrawPass { kZPass, kOpaque, kTransparent, kNumPasses };

    MeshRenderer(BatchType type)
    {
        m_BatchType = type;
        m_Camera = nullptr;
        m_Viewport = {};
        m_Scissor = {};
        m_NumRTVs = 0;
        m_DSV = nullptr;
        m_SortObjects.clear();
        m_SortKeys.clear();
        std::memset(m_PassCounts, 0, sizeof(m_PassCounts));
        m_CurrentPass = kZPass;
        m_CurrentDraw = 0;
    }

    void SetCamera(const Math::BaseCamera& camera) { m_Camera = &camera; }
    void SetViewport(const D3D12_VIEWPORT& viewport) { m_Viewport = viewport; }
    void SetScissor(const D3D12_RECT& scissor) { m_Scissor = scissor; }
    void AddRenderTarget(ColorBuffer& RTV)
    {
        ASSERT(m_NumRTVs < 8);
        m_RTV[m_NumRTVs++] = &RTV;
    }
    void SetDepthStencilTarget(DepthBuffer& DSV) { m_DSV = &DSV; }

    const Math::Frustum& GetWorldFrustum() const { return m_Camera->GetWorldSpaceFrustum(); }
    const Math::Frustum& GetViewFrustum() const { return m_Camera->GetViewSpaceFrustum(); }
    const Math::Matrix4& GetViewMatrix() const { return m_Camera->GetViewMatrix(); }

    void AddMesh(const Mesh& mesh, const Model* model, float distance,
        D3D12_GPU_VIRTUAL_ADDRESS meshCBV,
        D3D12_GPU_VIRTUAL_ADDRESS materialCBV);

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
                uint64_t key : 32;
                uint64_t passID : 4;
            };
        };
    };

    struct SortObject
    {
        const Model* model;
        const SubMesh* mesh;
        D3D12_GPU_VIRTUAL_ADDRESS materialCBV;
    };

    std::vector<SortObject> m_SortObjects;
    std::vector<uint64_t> m_SortKeys;
    BatchType m_BatchType;
    uint32_t m_PassCounts[kNumPasses];
    DrawPass m_CurrentPass;
    uint32_t m_CurrentDraw;

    const Math::BaseCamera* m_Camera;
    D3D12_VIEWPORT m_Viewport;
    D3D12_RECT m_Scissor;
    uint32_t m_NumRTVs;
    ColorBuffer* m_RTV[8];
    DepthBuffer* m_DSV;
};
