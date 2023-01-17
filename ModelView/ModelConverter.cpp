#include "ModelConverter.h"
#include "glTF.h"
#include "Material.h"
#include "Texture.h"
#include "SamplerManager.h"
#include "Model.h"
#include "Utils/DirectXMesh/DirectXMesh.h"

static DXGI_FORMAT AccessorFormat(const glTF::Accessor& accessor)
{
    switch (accessor.componentType)
    {
    case glTF::Accessor::kUnsignedByte:
        switch (accessor.type)
        {
        case glTF::Accessor::kScalar: return DXGI_FORMAT_R8_UNORM;
        case glTF::Accessor::kVec2:   return DXGI_FORMAT_R8G8_UNORM;
        default:                return DXGI_FORMAT_R8G8B8A8_UNORM;
        }
    case glTF::Accessor::kUnsignedShort:
        switch (accessor.type)
        {
        case glTF::Accessor::kScalar: return DXGI_FORMAT_R16_UNORM;
        case glTF::Accessor::kVec2:   return DXGI_FORMAT_R16G16_UNORM;
        default:                return DXGI_FORMAT_R16G16B16A16_UNORM;
        }
    case glTF::Accessor::kFloat:
        switch (accessor.type)
        {
        case glTF::Accessor::kScalar: return DXGI_FORMAT_R32_FLOAT;
        case glTF::Accessor::kVec2:   return DXGI_FORMAT_R32G32_FLOAT;
        case glTF::Accessor::kVec3:   return DXGI_FORMAT_R32G32B32_FLOAT;
        default:                return DXGI_FORMAT_R32G32B32A32_FLOAT;
        }
    default:
        ASSERT("Invalid accessor format");
        return DXGI_FORMAT_UNKNOWN;
    }
}

struct GeometryData
{
    std::unique_ptr<std::vector<byte>> VB;
    std::unique_ptr<std::vector<byte>> depthVB;
    std::unique_ptr<byte> IB;
};

namespace ModelConverter
{
    inline uint16_t GetTextureFlag(uint32_t type, bool alpha = false)
    {
#define SetFlag(BitIdx) (1 << (BitIdx - 1))
        switch (type)
        {
        case PBRMaterial::kBaseColor:
            return SetFlag(kSRGB) | SetFlag(kDefaultBC) | alpha ? SetFlag(kPreserveAlpha) : 0;
        case PBRMaterial::kMetallicRoughness:
            return SetFlag(kDefaultBC);
        case PBRMaterial::kOcclusion:
            return SetFlag(kDefaultBC);
        case PBRMaterial::kEmissive:
            return SetFlag(kSRGB);
        case PBRMaterial::kNormal: // Use BC5 Compression
            return SetFlag(kDefaultBC);
        case PBRMaterial::kNumTextures:
        default:
            return kNoneTextureFlag;
#undef SetFlag
        }
    }

	void BuildMaterials(glTF::Asset& asset)
	{
        MaterialManager* matMgr = MaterialManager::GetOrCreateInstance();
        matMgr->Reserve(asset.m_materials.size());

        for (uint32_t i = 0; i < asset.m_materials.size(); ++i)
        {
            glTF::Material& gltfMat = asset.m_materials[i];
            PBRMaterial& pbrMat = matMgr->AddMaterial<PBRMaterial>();

            pbrMat.mMaterialConstant.baseColorFactor[0] = gltfMat.baseColorFactor[0];
            pbrMat.mMaterialConstant.baseColorFactor[1] = gltfMat.baseColorFactor[1];
            pbrMat.mMaterialConstant.baseColorFactor[2] = gltfMat.baseColorFactor[2];
            pbrMat.mMaterialConstant.baseColorFactor[3] = gltfMat.baseColorFactor[3];

            pbrMat.mMaterialConstant.emissiveFactor[0] = gltfMat.emissiveFactor[0];
            pbrMat.mMaterialConstant.emissiveFactor[1] = gltfMat.emissiveFactor[1];
            pbrMat.mMaterialConstant.emissiveFactor[2] = gltfMat.emissiveFactor[2];

            pbrMat.mMaterialConstant.normalTextureScale = gltfMat.normalTextureScale;
            pbrMat.mMaterialConstant.metallicFactor = gltfMat.metallicFactor;
            pbrMat.mMaterialConstant.roughnessFactor = gltfMat.roughnessFactor;

            pbrMat.mMaterialConstant.flags = gltfMat.flags;

            std::filesystem::path imagePath[PBRMaterial::kNumTextures];
            for (uint32_t ti = 0; ti < PBRMaterial::kNumTextures; ++ti)
            {
                SamplerDesc texSampleDesc;

                if (gltfMat.textures[ti]->sampler != nullptr)
                {
                    texSampleDesc.AddressU = gltfMat.textures[ti]->sampler->wrapS;
                    texSampleDesc.AddressV = gltfMat.textures[ti]->sampler->wrapT;
                    texSampleDesc.Filter = gltfMat.textures[ti]->sampler->filter;
                }
                else
                {
                    // for default
                    texSampleDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                    texSampleDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                    texSampleDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                }

                pbrMat.mSamplers[ti] = GET_SAM(texSampleDesc);

                std::filesystem::path imagePath = asset.m_basePath / gltfMat.textures[ti]->source->path;
                pbrMat.mTextures[ti] = GET_TEXF(imagePath, 
                    GetTextureFlag(ti, (gltfMat.alphaBlend | gltfMat.alphaTest) && ti == PBRMaterial::kBaseColor));
            }
        }
	}

    GeometryData BuildSubMesh(glTF::Primitive& primitive, SubMesh& subMesh, const Math::Matrix4& toObjectRoot)
    {
        ASSERT(primitive.attributes[glTF::Primitive::kPosition] != nullptr, "Must have POSITION");
        uint32_t vertexCount = primitive.attributes[glTF::Primitive::kPosition]->count;
        GeometryData geoData;

        // process index
        bool b32BitIndices;
        uint32_t maxIndex;
        uint32_t indexCount;
        std::unique_ptr<byte> newIndices;
        if (primitive.indices != nullptr)
        {
            void* indicesPtr = primitive.indices->dataPtr;
            indexCount = primitive.indices->count;
            maxIndex = primitive.maxIndex;

            if (primitive.indices->componentType == glTF::Accessor::kUnsignedInt)
            {
                uint32_t* ib = (uint32_t*)primitive.indices->dataPtr;
                for (uint32_t k = 0; k < indexCount; ++k)
                    maxIndex = std::max(ib[k], maxIndex);
            }
            else
            {
                uint16_t* ib = (uint16_t*)primitive.indices->dataPtr;
                for (uint32_t k = 0; k < indexCount; ++k)
                    maxIndex = std::max<uint32_t>(ib[k], maxIndex);
            }
            b32BitIndices = maxIndex > 0xFFFF;
            uint32_t perIndexSize = b32BitIndices ? 4 : 2;

            // Index Optimize
            uint32_t nFaces = indexCount / 3;
            std::unique_ptr<byte> newIndices = std::make_unique<byte>(perIndexSize * nFaces);
            std::unique_ptr<byte> faceRemap = std::make_unique<byte>(nFaces * sizeof(uint32_t));
            if (b32BitIndices) // must be 32Bit
            {
                ASSERT(OptimizeFacesLRU((uint32_t*)primitive.indices->dataPtr, nFaces, (uint32_t*)faceRemap.get(), 64));
                ASSERT(ReorderIB((uint32_t*)primitive.indices->dataPtr, nFaces, (uint32_t*)faceRemap.get(), (uint32_t*)newIndices.get()));
            }
            else if (primitive.indices->componentType == glTF::Accessor::kUnsignedShort)
            {
                ASSERT(OptimizeFacesLRU((uint16_t*)primitive.indices->dataPtr, nFaces, (uint32_t*)faceRemap.get(), 64));
                ASSERT(ReorderIB((uint16_t*)primitive.indices->dataPtr, nFaces, (uint32_t*)faceRemap.get(), (uint16_t*)newIndices.get()));
            }
            else
            {
                ASSERT(OptimizeFacesLRU((uint32_t*)primitive.indices->dataPtr, nFaces, (uint32_t*)faceRemap.get(), 64));
                ASSERT(ReorderIB((uint32_t*)primitive.indices->dataPtr, nFaces, (uint32_t*)faceRemap.get(), (uint16_t*)newIndices.get()));
            }
        }
        else
        {
            WARN_IF(primitive.mode == D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, "Impossible primitive topology when lacking indices");

            indexCount = vertexCount * 3;
            maxIndex = indexCount - 1;
            std::unique_ptr<byte> newIndices;
            if (indexCount > 0xFFFF)
            {
                b32BitIndices = true;
                newIndices = std::make_unique<byte>(4 * indexCount);
                uint32_t* tmp = (uint32_t*)newIndices.get();
                for (uint32_t i = 0; i < indexCount; ++i)
                    tmp[i] = i;
            }
            else
            {
                b32BitIndices = false;
                newIndices = std::make_unique<byte>(2 * indexCount);
                uint16_t* tmp = (uint16_t*)newIndices.get();
                for (uint16_t i = 0; i < indexCount; ++i)
                    tmp[i] = i;
            }
        }
        ASSERT(maxIndex > 0 && newIndices);
        geoData.IB.swap(newIndices);

        // process vertex
        const bool HasNormals = primitive.attributes[glTF::Primitive::kNormal] != nullptr;
        const bool HasTangents = primitive.attributes[glTF::Primitive::kTangent] != nullptr;
        const bool HasUV0 = primitive.attributes[glTF::Primitive::kTexcoord0] != nullptr;
        const bool HasUV1 = primitive.attributes[glTF::Primitive::kTexcoord1] != nullptr;
        const bool HasUV2 = primitive.attributes[glTF::Primitive::kTexcoord2] != nullptr;
        const bool HasUV3 = primitive.attributes[glTF::Primitive::kTexcoord3] != nullptr;

        std::vector<D3D12_INPUT_ELEMENT_DESC> InputElements;
        InputElements.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, glTF::Primitive::kPosition });
        if (HasNormals)
        {
            InputElements.push_back({ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, glTF::Primitive::kNormal });
        }
        if (HasTangents)
        {
            InputElements.push_back({ "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,  glTF::Primitive::kTangent });
        }
        if (HasUV0)
        {
            InputElements.push_back({ "TEXCOORD", 0,
                AccessorFormat(*primitive.attributes[glTF::Primitive::kTexcoord0]),
                glTF::Primitive::kTexcoord0 });
        }
        if (HasUV1)
        {
            InputElements.push_back({ "TEXCOORD", 1,
                AccessorFormat(*primitive.attributes[glTF::Primitive::kTexcoord1]),
                glTF::Primitive::kTexcoord1 });
        }
        if (HasUV2)
        {
            InputElements.push_back({ "TEXCOORD", 2,
               AccessorFormat(*primitive.attributes[glTF::Primitive::kTexcoord2]),
               glTF::Primitive::kTexcoord2 });
        }
        if (HasUV3)
        {
            InputElements.push_back({ "TEXCOORD", 3,
               AccessorFormat(*primitive.attributes[glTF::Primitive::kTexcoord3]),
               glTF::Primitive::kTexcoord3 });
        }

        VBReader vbr;
        vbr.Initialize({ InputElements.data(), (uint32_t)InputElements.size() });

        for (uint32_t i = 0; i < glTF::Primitive::kNumAttribs; ++i)
        {
            glTF::Accessor* attrib = primitive.attributes[i];
            if (attrib)
                vbr.AddStream(attrib->dataPtr, vertexCount, i, attrib->stride);
        }

        std::unique_ptr<XMFLOAT3[]> position = std::make_unique<XMFLOAT3[]>(vertexCount);
        std::unique_ptr<XMFLOAT3[]> normal = std::make_unique<XMFLOAT3[]>(vertexCount);
        std::unique_ptr<XMFLOAT4[]> tangent;
        std::unique_ptr<XMFLOAT2[]> texcoords[4];

        CheckHR(vbr.Read(position.get(), "POSITION", 0, vertexCount));
        {
            using namespace Math;
            // Local space bounds
            Vector3 sphereCenterLS = (Vector3(*(XMFLOAT3*)primitive.minPos) + Vector3(*(XMFLOAT3*)primitive.maxPos)) * 0.5f;
            Vector3 sphereCenterOS = Vector3(toObjectRoot * Vector4(sphereCenterLS));
            Scalar maxRadiusLSSq(kZero);
            Scalar maxRadiusOSSq(kZero);

            subMesh.m_BBoxLS = AxisAlignedBox(kZero);
            subMesh.m_BBoxOS = AxisAlignedBox(kZero);

            for (uint32_t v = 0; v < vertexCount/*maxIndex*/; ++v)
            {
                Vector3 positionLS = Vector3(position[v]);
                Vector3 positionOS = Vector3(toObjectRoot * Vector4(positionLS));

                subMesh.m_BBoxLS.AddPoint(positionLS);
                subMesh.m_BBoxOS.AddPoint(positionOS);

                maxRadiusLSSq = Max(maxRadiusLSSq, LengthSquare(sphereCenterLS - positionLS));
                maxRadiusOSSq = Max(maxRadiusOSSq, LengthSquare(sphereCenterOS - positionOS));
            }

            subMesh.m_BSLS = Math::BoundingSphere(sphereCenterLS, Sqrt(maxRadiusLSSq));
            subMesh.m_BSOS = Math::BoundingSphere(sphereCenterOS, Sqrt(maxRadiusOSSq));
        }

        if (HasNormals)
        {
            CheckHR(vbr.Read(normal.get(), "NORMAL", 0, vertexCount));
        }
        else
        {
            const size_t faceCount = indexCount / 3;

            if (b32BitIndices)
                ComputeNormals((const uint32_t*)geoData.IB.get(), faceCount, position.get(), vertexCount, CNORM_DEFAULT, normal.get());
            else
                ComputeNormals((const uint16_t*)geoData.IB.get(), faceCount, position.get(), vertexCount, CNORM_DEFAULT, normal.get());
        }

        if (HasUV0)
        {
            texcoords[0].reset(new XMFLOAT2[vertexCount]);
            CheckHR(vbr.Read(texcoords[0].get(), "TEXCOORD", 0, vertexCount));
        }
        if (HasUV1)
        {
            texcoords[1].reset(new XMFLOAT2[vertexCount]);
            CheckHR(vbr.Read(texcoords[1].get(), "TEXCOORD", 1, vertexCount));
        }
        if (HasUV2)
        {
            texcoords[2].reset(new XMFLOAT2[vertexCount]);
            CheckHR(vbr.Read(texcoords[2].get(), "TEXCOORD", 2, vertexCount));
        }
        if (HasUV3)
        {
            texcoords[3].reset(new XMFLOAT2[vertexCount]);
            CheckHR(vbr.Read(texcoords[3].get(), "TEXCOORD", 3, vertexCount));
        }

        if (HasTangents)
        {
            tangent.reset(new XMFLOAT4[vertexCount]);
            CheckHR(vbr.Read(tangent.get(), "TANGENT", 0, vertexCount));
        }
        else
        {
            ASSERT(maxIndex < vertexCount);
            ASSERT(indexCount % 3 == 0);
            if (texcoords[primitive.material->normalUV])
            {
                tangent.reset(new XMFLOAT4[vertexCount]);

                if (b32BitIndices)
                {
                    CheckHR(ComputeTangentFrame((uint32_t*)geoData.IB.get(), indexCount / 3, position.get(), normal.get(), 
                        texcoords[primitive.material->normalUV].get(), vertexCount, tangent.get()));
                }
                else
                {
                    CheckHR(ComputeTangentFrame((uint16_t*)geoData.IB.get(), indexCount / 3, position.get(), normal.get(),
                        texcoords[primitive.material->normalUV].get(), vertexCount, tangent.get()));
                }
            }
        }

        // Use VBWriter to generate a new, interleaved and compressed vertex buffer
        std::vector<D3D12_INPUT_ELEMENT_DESC> OutputElements;

        subMesh.psoFlags = ePSOFlags::kHasPosition | ePSOFlags::kHasNormal;
        OutputElements.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT });
        OutputElements.push_back({ "NORMAL", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT });
        if (tangent.get())
        {
            OutputElements.push_back({ "TANGENT", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT });
            subMesh.psoFlags |= ePSOFlags::kHasTangent;
        }
        if (texcoords[0].get())
        {
            OutputElements.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R8G8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT });
            subMesh.psoFlags |= ePSOFlags::kHasUV0;
        }
        if (texcoords[1].get())
        {
            OutputElements.push_back({ "TEXCOORD", 1, DXGI_FORMAT_R8G8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT });
            subMesh.psoFlags |= ePSOFlags::kHasUV1;
        }
        if (texcoords[2].get())
        {
            OutputElements.push_back({ "TEXCOORD", 2, DXGI_FORMAT_R8G8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT });
            subMesh.psoFlags |= ePSOFlags::kHasUV2;
        }
        if (texcoords[3].get())
        {
            OutputElements.push_back({ "TEXCOORD", 3, DXGI_FORMAT_R8G8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT });
            subMesh.psoFlags |= ePSOFlags::kHasUV3;
        }
        if (primitive.material->alphaBlend)
            subMesh.psoFlags |= ePSOFlags::kAlphaBlend;
        if (primitive.material->alphaTest)
            subMesh.psoFlags |= ePSOFlags::kAlphaTest;
        if (primitive.material->twoSided)
            subMesh.psoFlags |= ePSOFlags::kTwoSided;

        D3D12_INPUT_LAYOUT_DESC layout = { OutputElements.data(), (uint32_t)OutputElements.size() };

        VBWriter vbw;
        vbw.Initialize(layout);

        uint32_t offsets[D3D12_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT];
        uint32_t strides[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
        ComputeInputLayout(layout, offsets, strides);
        uint32_t stride = strides[0];

        geoData.VB = std::make_unique<std::vector<byte>>(stride * vertexCount);
        CheckHR(vbw.AddStream(geoData.VB->data(), vertexCount, 0, stride));

        vbw.Write(position.get(), "POSITION", 0, vertexCount);
        vbw.Write(normal.get(), "NORMAL", 0, vertexCount, true);
        if (tangent.get())
            CheckHR(vbw.Write(tangent.get(), "TANGENT", 0, vertexCount, true));
        if (texcoords[0].get())
            CheckHR(vbw.Write(texcoords[0].get(), "TEXCOORD", 0, vertexCount));
        if (texcoords[1].get())
            CheckHR(vbw.Write(texcoords[1].get(), "TEXCOORD", 1, vertexCount));
        if (texcoords[2].get())
            CheckHR(vbw.Write(texcoords[2].get(), "TEXCOORD", 2, vertexCount));
        if (texcoords[3].get())
            CheckHR(vbw.Write(texcoords[3].get(), "TEXCOORD", 3, vertexCount));

        // Now write a VB for positions only (or positions and UV when alpha testing)
        std::vector<D3D12_INPUT_ELEMENT_DESC> DepthElements;
        DepthElements.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT });
        if (primitive.material->alphaTest)
        {
            DepthElements.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R8G8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT });
        }
        D3D12_INPUT_LAYOUT_DESC depthLayout = { OutputElements.data(), (uint32_t)OutputElements.size() };

        VBWriter dvbw;
        dvbw.Initialize(depthLayout);
        ComputeInputLayout(depthLayout, offsets, strides);
        uint32_t depthStride = strides[0];
        geoData.depthVB = std::make_unique<std::vector<byte>>(depthStride * vertexCount);
        CheckHR(dvbw.AddStream(geoData.depthVB->data(), vertexCount, 0, depthStride));

        dvbw.Write(position.get(), "POSITION", 0, vertexCount);
        if (primitive.material->alphaTest && texcoords[primitive.material->baseColorUV])
        {
            dvbw.Write(texcoords[primitive.material->baseColorUV].get(), "TEXCOORD", 0, vertexCount);
        }

        ASSERT(primitive.material->index < 0x8000, "Only 15-bit material indices allowed");

        subMesh.vertexStride = (uint16_t)stride;
        subMesh.index32 = b32BitIndices;
        subMesh.materialIdx = primitive.material->index;
        subMesh.indexCount = indexCount;

        return geoData;
    }

    void BuildAllMeshes(glTF::Asset& asset)
    {
        Mesh& mesh = Mesh::sAllMeshs.emplace_back();
        std::vector<byte> VB;
        std::vector<byte> DepthVB;
        std::vector<byte> IB;

        for (size_t i = 0; i < asset.m_meshes.size(); i++)
        {
            glTF::Mesh& gltfMesh = asset.m_meshes[i];
            mesh.subMeshes.reserve(gltfMesh.primitives.size());
            for (size_t pi = 0; pi < gltfMesh.primitives.size(); pi++)
            {
                SubMesh& subMesh = mesh.subMeshes.emplace_back();

                BuildSubMesh(gltfMesh.primitives[pi], subMesh);
            }
        }
    }
};
