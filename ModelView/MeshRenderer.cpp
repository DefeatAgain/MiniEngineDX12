#include "MeshRenderer.h"
#include "Mesh.h"
#include "Material.h"
#include "GraphicsResource.h"

namespace ModelRenderer
{
    std::map<size_t, uint32_t> sPSOIndices;


    void Initialize()
    {
        Graphics::AddRSSTask([]()
        {
            SamplerDesc DefaultSamplerDesc;
            DefaultSamplerDesc.MaxAnisotropy = 8;

            SamplerDesc CubeMapSamplerDesc = DefaultSamplerDesc;

            mRootSig = GET_RSO(L"MeshRenderer RSO");
            mRootSig->Reset(kNumRootBindings, 3);
            mRootSig->InitStaticSampler(10, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
            mRootSig->InitStaticSampler(11, Graphics::SamplerShadowDesc, D3D12_SHADER_VISIBILITY_PIXEL);
            mRootSig->InitStaticSampler(12, CubeMapSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
            mRootSig->GetParam(kMeshConstants).InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
            mRootSig->GetParam(kMaterialConstants).InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
            mRootSig->GetParam(kMaterialSRVs).InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 10, D3D12_SHADER_VISIBILITY_PIXEL);
            mRootSig->GetParam(kMaterialSamplers).InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0, 10, D3D12_SHADER_VISIBILITY_PIXEL);
            mRootSig->GetParam(kCommonSRVs).InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 10, D3D12_SHADER_VISIBILITY_PIXEL);
            mRootSig->GetParam(kCommonCBV).InitAsConstantBuffer(1);
            mRootSig->Finalize(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

            ADD_SHADER("TextVS", L"TextVS.hlsl", kVS);
            ADD_SHADER("TextAntialiasPS", L"TextAntialiasPS.hlsl", kPS);
        });

        //m_DefaultPSO.SetRootSignature(m_RootSig);
        //m_DefaultPSO.SetRasterizerState(RasterizerDefault);
        //m_DefaultPSO.SetBlendState(BlendDisable);
        //m_DefaultPSO.SetDepthStencilState(DepthStateReadWrite);
        //m_DefaultPSO.SetInputLayout(0, nullptr);
        //m_DefaultPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        //m_DefaultPSO.SetRenderTargetFormats(1, &ColorFormat, DepthFormat);
        //m_DefaultPSO.SetVertexShader(g_pDefaultVS, sizeof(g_pDefaultVS));
        //m_DefaultPSO.SetPixelShader(g_pDefaultPS, sizeof(g_pDefaultPS));
    }
}

void MeshRenderer::AddMesh(const Mesh& mesh, const Model* model, float distance, 
    D3D12_GPU_VIRTUAL_ADDRESS meshCBV, D3D12_GPU_VIRTUAL_ADDRESS materialCBV)
{
    SortKey key;
    key.value = m_SortObjects.size();

    for (size_t i = 0; i < mesh.subMeshCount; i++)
    {
        const SubMesh& subMesh = mesh.subMeshes[i];

        bool alphaBlend = (subMesh.psoFlags & ePSOFlags::kAlphaBlend) == ePSOFlags::kAlphaBlend;
        bool alphaTest = (subMesh.psoFlags & ePSOFlags::kAlphaTest) == ePSOFlags::kAlphaTest;

        union float_or_int { float f; uint32_t u; } dist;

        dist.f = Math::Max(distance, 0.0f);

        if (m_BatchType == kShadows)
        {
            if (alphaBlend)
                return;

            key.passID = kZPass;
            key.psoIdx = ModelRenderer::GetPsoIndex(subMesh.psoFlags, true);
            key.key = dist.u;
            m_SortKeys.push_back(key.value);
            m_PassCounts[kZPass]++;
        }
        else if (subMesh.psoFlags & ePSOFlags::kAlphaBlend)
        {
            key.passID = kTransparent;
            key.psoIdx = ModelRenderer::GetPsoIndex(subMesh.psoFlags, false);
            key.key = ~dist.u;
            m_SortKeys.push_back(key.value);
            m_PassCounts[kTransparent]++;
        }
        else if (ModelRenderer::gIsPreZ || alphaTest)
        {
            key.passID = kZPass;
            key.psoIdx = ModelRenderer::GetPsoIndex(subMesh.psoFlags, true);;
            key.key = dist.u;
            m_SortKeys.push_back(key.value);
            m_PassCounts[kZPass]++;

            key.passID = kOpaque;
            key.psoIdx = ModelRenderer::GetPsoIndex(subMesh.psoFlags, false);;
            key.key = dist.u;
            m_SortKeys.push_back(key.value);
            m_PassCounts[kOpaque]++;
        }
        else
        {
            key.passID = kOpaque;
            key.psoIdx = ModelRenderer::GetPsoIndex(subMesh.psoFlags, true);;
            key.key = dist.u;
            m_SortKeys.push_back(key.value);
            m_PassCounts[kOpaque]++;
        }

        SortObject object = { model, &subMesh, materialCBV };
        m_SortObjects.push_back(object);
    }
}

void MeshRenderer::Sort()
{
    struct { bool operator()(uint64_t a, uint64_t b) const { return a < b; } } Cmp;
    std::sort(m_SortKeys.begin(), m_SortKeys.end(), Cmp);
}

void MeshRenderer::RenderMeshes(GraphicsCommandList& context, GlobalConstants& globals, DrawPass pass)
{

}
