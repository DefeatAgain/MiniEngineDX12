#include "ModelConverter.h"
#include "CoreHeader.h"
#include "Graphics.h"
#include "glTF.h"
#include "Material.h"
#include "Texture.h"
#include "SamplerManager.h"
#include "GraphicsResource.h"
#include "Model.h"
#include "Mesh.h"
#include "Scene.h"
#include "Math/BoundingSphere.h"
#include "Math/VectorMath.h"
#include "Utils/DirectXMesh/DirectXMesh.h"
#include "Utils/ThreadPoolExecutor.h"

static std::unordered_map<std::wstring, std::filesystem::path> sIBLTexturePaths;

static uint16_t GetTextureFlag(uint32_t type, bool alpha = false)
{
    switch (type)
    {
    case PBRMaterial::kBaseColor:
        return kSRGB | kDefaultBC | kGenerateMipMaps | (alpha ? kPreserveAlpha : 0);
    case PBRMaterial::kMetallicRoughness:
        return kNoneTextureFlag;
    case PBRMaterial::kOcclusion:
        return kNoneTextureFlag;
    case PBRMaterial::kEmissive:
        return kSRGB;
    case PBRMaterial::kNormal: // Use BC5 Compression
        return kDefaultBC | kNormalMap;
    default:
        return kNoneTextureFlag;
    }
}

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

static void LoadIBLTextures()
{
    std::filesystem::path imagePath = L"Asset\\IBLTextures";
    for (auto& p : std::filesystem::directory_iterator(imagePath))
    {
        std::filesystem::path filePath = p.path();
        std::wstring filestem = filePath.filename().c_str();
        size_t diffuseIdx = filestem.rfind(L"_diffuseIBL.dds");
        if (diffuseIdx != std::wstring::npos)
        {
            std::wstring realname = filestem.substr(0, diffuseIdx) + L"_D";
            GET_TEX(filePath);
            sIBLTexturePaths[realname] = filePath;
        }

        size_t specularIdx = filestem.rfind(L"_specularIBL.dds");
        if (specularIdx != std::wstring::npos)
        {
            std::wstring realname = filestem.substr(0, specularIdx) + L"_S";
            GET_TEX(filePath);
            sIBLTexturePaths[realname] = filePath;
        }
    }
}

struct GeometryData
{
    std::unique_ptr<byte[]> VB;
    std::unique_ptr<byte[]> depthVB;
    std::unique_ptr<byte[]> IB;
    uint32_t vertexBufferSize;
    uint32_t depthVertexBufferSize;
    uint32_t indexBufferSize;
    uint32_t vertexCount;
    uint8_t vertexStride;
    uint8_t depthVertexStride;
};

namespace ModelConverter
{
    std::filesystem::path GetIBLTextureFilename(const std::wstring& name)
    {
        auto findIter = sIBLTexturePaths.find(name);
        ASSERT(findIter != sIBLTexturePaths.end());
        {
            return findIter->second;
        }
        return std::filesystem::path();
    }

    void BuildMaterials(const glTF::Asset& asset)
	{
        MaterialManager* matMgr = MaterialManager::GetInstance();
        matMgr->Reserve(asset.m_materials.size());

        for (uint32_t i = 0; i < asset.m_materials.size(); ++i)
        {
            const glTF::Material& gltfMat = asset.m_materials[i];
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

                if (gltfMat.textures[ti] && gltfMat.textures[ti]->sampler != nullptr)
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

                pbrMat.mSamplerHandles[ti] = GET_SAM_HANDLE(texSampleDesc);

                if (gltfMat.textures[ti] == nullptr)
                    continue;

                std::filesystem::path imagePath = asset.m_basePath / gltfMat.textures[ti]->source->path;
                pbrMat.mTextures[ti] = GET_TEXFF(
                    imagePath, 
                    GetTextureFlag(ti, (gltfMat.alphaBlend | gltfMat.alphaTest) && ti == PBRMaterial::kBaseColor),
                    Graphics::kWhiteOpaque2D,
                    pbrMat.mTextureHandles + ti);
            }
        }

        TextureManager::GetInstance()->WaitLoading();

        LoadIBLTextures();
	}

    GeometryData BuildSubMesh(const glTF::Primitive& primitive, SubMesh& subMesh, const ePSOFlags meshPsoFlags)
    {
        ASSERT(primitive.attributes[glTF::Primitive::kPosition] != nullptr, "Must have POSITION");
        GeometryData geoData;
        uint32_t vertexCount = primitive.attributes[glTF::Primitive::kPosition]->count;
        geoData.vertexCount = vertexCount;

        // process index
        bool b32BitIndices;
        uint32_t maxIndex;
        uint32_t indexCount;
        std::unique_ptr<byte[]> newIndices;
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
            geoData.indexBufferSize = perIndexSize * indexCount;
            newIndices = std::make_unique<byte[]>(geoData.indexBufferSize);
            std::unique_ptr<byte[]> faceRemap = std::make_unique<byte[]>(nFaces * sizeof(uint32_t));
            if (b32BitIndices) // must be 32Bit
            {
                CheckHR(OptimizeFacesLRU((uint32_t*)primitive.indices->dataPtr, nFaces, (uint32_t*)faceRemap.get(), 64));
                CheckHR(ReorderIB((uint32_t*)primitive.indices->dataPtr, nFaces, (uint32_t*)faceRemap.get(), (uint32_t*)newIndices.get()));
            }
            else if (primitive.indices->componentType == glTF::Accessor::kUnsignedShort)
            {
                CheckHR(OptimizeFacesLRU((uint16_t*)primitive.indices->dataPtr, nFaces, (uint32_t*)faceRemap.get(), 64));
                CheckHR(ReorderIB((uint16_t*)primitive.indices->dataPtr, nFaces, (uint32_t*)faceRemap.get(), (uint16_t*)newIndices.get()));
            }
            else
            {
                CheckHR(OptimizeFacesLRU((uint32_t*)primitive.indices->dataPtr, nFaces, (uint32_t*)faceRemap.get(), 64));
                CheckHR(ReorderIB((uint32_t*)primitive.indices->dataPtr, nFaces, (uint32_t*)faceRemap.get(), (uint16_t*)newIndices.get()));
            }
        }
        else
        {
            WARN_IF(primitive.mode == D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, "Impossible primitive topology when lacking indices");

            indexCount = vertexCount * 3;
            maxIndex = indexCount - 1;
            if (indexCount > 0xFFFF)
            {
                b32BitIndices = true;
                geoData.indexBufferSize = 4 * indexCount;
                newIndices = std::make_unique<byte[]>(geoData.indexBufferSize);
                uint32_t* tmp = (uint32_t*)newIndices.get();
                for (uint32_t i = 0; i < indexCount; ++i)
                    tmp[i] = i;
            }
            else
            {
                b32BitIndices = false;
                geoData.indexBufferSize = 2 * indexCount;
                newIndices = std::make_unique<byte[]>(geoData.indexBufferSize);
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
        //const bool HasUV2 = primitive.attributes[glTF::Primitive::kTexcoord2] != nullptr;
        //const bool HasUV3 = primitive.attributes[glTF::Primitive::kTexcoord3] != nullptr;

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
        //if (HasUV2)
        //{
        //    InputElements.push_back({ "TEXCOORD", 2,
        //       AccessorFormat(*primitive.attributes[glTF::Primitive::kTexcoord2]),
        //       glTF::Primitive::kTexcoord2 });
        //}
        //if (HasUV3)
        //{
        //    InputElements.push_back({ "TEXCOORD", 3,
        //       AccessorFormat(*primitive.attributes[glTF::Primitive::kTexcoord3]),
        //       glTF::Primitive::kTexcoord3 });
        //}

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
            Scalar maxRadiusLSSq(kZero);
            AxisAlignedBox aabbLS = AxisAlignedBox(kZero);

            for (uint32_t v = 0; v < vertexCount/*maxIndex*/; ++v)
            {
                Vector3 positionLS = Vector3(position[v]);

                aabbLS.AddPoint(positionLS);

                maxRadiusLSSq = Max(maxRadiusLSSq, LengthSquare(sphereCenterLS - positionLS));
            }

            XMStoreFloat3((XMFLOAT3*)subMesh.bounds, sphereCenterLS);
            subMesh.bounds[3] = maxRadiusLSSq;
            XMStoreFloat3(&subMesh.minPos, aabbLS.GetMin());
            XMStoreFloat3(&subMesh.maxPos, aabbLS.GetMax());
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
        //if (HasUV2)
        //{
        //    texcoords[2].reset(new XMFLOAT2[vertexCount]);
        //    CheckHR(vbr.Read(texcoords[2].get(), "TEXCOORD", 2, vertexCount));
        //}
        //if (HasUV3)
        //{
        //    texcoords[3].reset(new XMFLOAT2[vertexCount]);
        //    CheckHR(vbr.Read(texcoords[3].get(), "TEXCOORD", 3, vertexCount));
        //}

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
        std::vector<D3D12_INPUT_ELEMENT_DESC> outputElements;

        subMesh.psoFlags = ePSOFlags::kHasPosition | ePSOFlags::kHasNormal;
        outputElements.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT });
        outputElements.push_back({ "NORMAL", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT });
        if (tangent.get())
        {
            outputElements.push_back({ "TANGENT", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT });
            subMesh.psoFlags |= ePSOFlags::kHasTangent;
        }
        if (texcoords[0].get() || meshPsoFlags & ePSOFlags::kHasUV0)
        {
            outputElements.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT });
            subMesh.psoFlags |= ePSOFlags::kHasUV0;
        }
        if (texcoords[1].get() || meshPsoFlags & ePSOFlags::kHasUV1)
        {
            outputElements.push_back({ "TEXCOORD", 1, DXGI_FORMAT_R16G16_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT });
            subMesh.psoFlags |= ePSOFlags::kHasUV1;
        }
        //if (texcoords[2].get())
        //{
        //    outputElements.push_back({ "TEXCOORD", 2, DXGI_FORMAT_R8G8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT });
        //    subMesh.psoFlags |= ePSOFlags::kHasUV2;
        //}
        //if (texcoords[3].get())
        //{
        //    outputElements.push_back({ "TEXCOORD", 3, DXGI_FORMAT_R8G8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT });
        //    subMesh.psoFlags |= ePSOFlags::kHasUV3;
        //}
        if (primitive.material->alphaBlend)
            subMesh.psoFlags |= ePSOFlags::kAlphaBlend;
        if (primitive.material->alphaTest)
            subMesh.psoFlags |= ePSOFlags::kAlphaTest;
        if (primitive.material->twoSided)
            subMesh.psoFlags |= ePSOFlags::kTwoSided;

        D3D12_INPUT_LAYOUT_DESC layout = { outputElements.data(), (uint32_t)outputElements.size() };

        VBWriter vbw;
        vbw.Initialize(layout);

        uint32_t offsets[D3D12_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT];
        uint32_t strides[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
        ComputeInputLayout(layout, offsets, strides);
        uint32_t stride = strides[0];

        geoData.vertexBufferSize = stride * vertexCount;
        geoData.VB = std::make_unique<byte[]>(geoData.vertexBufferSize);
        CheckHR(vbw.AddStream(geoData.VB.get(), vertexCount, 0, stride));

        vbw.Write(position.get(), "POSITION", 0, vertexCount);
        vbw.Write(normal.get(), "NORMAL", 0, vertexCount, true);
        if (tangent.get())
            CheckHR(vbw.Write(tangent.get(), "TANGENT", 0, vertexCount, true));
        if (texcoords[0].get())
            CheckHR(vbw.Write(texcoords[0].get(), "TEXCOORD", 0, vertexCount));
        if (texcoords[1].get())
            CheckHR(vbw.Write(texcoords[1].get(), "TEXCOORD", 1, vertexCount));
        //if (texcoords[2].get())
        //    CheckHR(vbw.Write(texcoords[2].get(), "TEXCOORD", 2, vertexCount));
        //if (texcoords[3].get())
        //    CheckHR(vbw.Write(texcoords[3].get(), "TEXCOORD", 3, vertexCount));

        // Now write a VB for positions only (or positions and UV when alpha testing)
        std::vector<D3D12_INPUT_ELEMENT_DESC> depthElements;
        depthElements.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT });
        if (primitive.material->alphaTest || meshPsoFlags & ePSOFlags::kAlphaTest)
        {
            depthElements.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT });
        }
        D3D12_INPUT_LAYOUT_DESC depthLayout = { depthElements.data(), (uint32_t)depthElements.size() };

        VBWriter dvbw;
        dvbw.Initialize(depthLayout);
        MemoryBarrier;
        ComputeInputLayout(depthLayout, offsets, strides);
        uint32_t depthStride = strides[0];

        geoData.depthVertexBufferSize = depthStride * vertexCount;
        geoData.depthVB = std::make_unique<byte[]>(geoData.depthVertexBufferSize);
        CheckHR(dvbw.AddStream(geoData.depthVB.get(), vertexCount, 0, depthStride));

        dvbw.Write(position.get(), "POSITION", 0, vertexCount);
        if (primitive.material->alphaTest && texcoords[0])
        {
            dvbw.Write(texcoords[0].get(), "TEXCOORD", 0, vertexCount);
        }

        ASSERT(primitive.material->index < 0x8000, "Only 15-bit material indices allowed");
        ASSERT(stride <= 0xFF && depthStride <= 0xFF);
        geoData.vertexStride = (uint8_t)stride;
        geoData.depthVertexStride = (uint8_t)depthStride;
        subMesh.index32 = b32BitIndices;
        subMesh.materialIdx = primitive.material->index;
        subMesh.indexCount = indexCount;
        subMesh.uniqueMaterialIdx = -1;

        return geoData;
    }

    Mesh BuildMesh(const glTF::Mesh& gltfMesh)
    {
        Mesh mesh;
        mesh.subMeshes = std::make_unique<SubMesh[]>(gltfMesh.primitives.size());
        mesh.subMeshCount = gltfMesh.primitives.size();

        /*
        //std::vector<std::future<GeometryData>> futureGeoData;
        //std::vector<GeometryData> futureGeoData1;
        //for (size_t pi = 0; pi < gltfMesh.primitives.size(); pi++)
        //{
        //    futureGeoData.emplace_back(Utility::gThreadPoolExecutor.Submit(
        //        &BuildSubMesh, std::cref(gltfMesh.primitives[pi]), std::ref(mesh.subMeshes[pi])));
        //}
        //futureGeoData1.resize(futureGeoData.size());

        //// calc all submesh size
        //uint32_t startIndex = 0;
        //uint32_t baseVertex = 0;
        //uint32_t totalIndexSize = 0;
        //uint32_t totalVertexSize = 0;
        //uint32_t totaldepthVertexSize = 0;

        //Math::AxisAlignedBox boundingBox;
        //Math::BoundingSphere boundingSphere;
        //for (size_t fi = 0; fi < futureGeoData.size(); fi++)
        //{
        //    const GeometryData& geoData = futureGeoData1.emplace_back(futureGeoData[fi].get());
        //    SubMesh& submesh = mesh.subMeshes[fi];
        //    submesh.baseVertex = baseVertex;
        //    submesh.startIndex = startIndex;
        //    baseVertex += geoData.vertexCount;
        //    startIndex += submesh.indexCount;
        //    totalIndexSize += geoData.indexBufferSize;
        //    totalVertexSize += geoData.vertexBufferSize;
        //    totaldepthVertexSize += geoData.depthVertexBufferSize;

        //    Math::AxisAlignedBox aabbSub(submesh.minPos, submesh.maxPos);
        //    Math::BoundingSphere shSub((const XMFLOAT4*)submesh.bounds);
        //    boundingBox.AddBoundingBox(aabbSub);
        //    boundingSphere.Union(shSub);
        //}
        //DirectX::XMStoreFloat4((XMFLOAT4*)mesh.bounds, (Vector4)boundingSphere);
        //DirectX::XMStoreFloat3(&mesh.minPos, (Vector4)boundingBox.GetMin());
        //DirectX::XMStoreFloat3(&mesh.maxPos, (Vector4)boundingBox.GetMax());

        // copy all buffers to a single buffer
        //mesh.sizeVB = totalVertexSize;
        //mesh.sizeDepthVB = totaldepthVertexSize;
        //mesh.sizeIB = totalIndexSize;
        //mesh.VB = std::make_unique<byte[]>(totalVertexSize);
        //mesh.DepthVB = std::make_unique<byte[]>(totaldepthVertexSize);
        //mesh.IB = std::make_unique<byte[]>(totalIndexSize);
        //uint32_t vertexBufferOffset = 0;
        //uint32_t indexBufferOffset = 0;
        //uint32_t depthVertexBufferOffset = 0;
        //for (size_t fi = 0; fi < futureGeoData.size(); fi++)
        //{
        //    const GeometryData& geoData = futureGeoData1[fi];
        //    CopyMemory(mesh.VB.get() + vertexBufferOffset, geoData.VB.get(), geoData.vertexBufferSize);
        //    CopyMemory(mesh.DepthVB.get() + depthVertexBufferOffset, geoData.depthVB.get(), geoData.depthVertexBufferSize);
        //    CopyMemory(mesh.IB.get() + indexBufferOffset, geoData.IB.get(), geoData.indexBufferSize);
        //    vertexBufferOffset += geoData.vertexBufferSize;
        //    depthVertexBufferOffset += geoData.depthVertexBufferSize;
        //    indexBufferOffset += geoData.indexBufferSize;
        //}
        */

        uint32_t startIndex = 0;
        uint32_t baseVertex = 0;
        uint32_t totalIndexSize = 0;
        uint32_t totalVertexSize = 0;
        uint32_t totaldepthVertexSize = 0;
        Math::AxisAlignedBox boundingBox(kZero);
        Math::BoundingSphere boundingSphere(kZero);
        std::vector<GeometryData> allGeoData;

        // preprocess mesh flags, each mesh has same vertex layout
        uint32_t meshflags = (ePSOFlags::kHasNormal | ePSOFlags::kHasPosition);
        for (size_t pi = 0; pi < gltfMesh.primitives.size(); pi++)
        {
            const bool hasUV0 = gltfMesh.primitives[pi].attributes[glTF::Primitive::kTexcoord0] != nullptr;
            const bool hasUV1 = gltfMesh.primitives[pi].attributes[glTF::Primitive::kTexcoord1] != nullptr;
            const bool alphaTest = gltfMesh.primitives[pi].material->alphaTest;
            meshflags |= hasUV0 ? ePSOFlags::kHasUV0 : 0;
            meshflags |= hasUV1 ? ePSOFlags::kHasUV1 : 0;
            meshflags |= alphaTest ? ePSOFlags::kAlphaTest : 0;
        }

        for (size_t pi = 0; pi < gltfMesh.primitives.size(); pi++)
        {
            GeometryData& geoData = allGeoData.emplace_back(BuildSubMesh(
                gltfMesh.primitives[pi], mesh.subMeshes[pi], (ePSOFlags) meshflags));

            Utility::PrintMessage("%d %d", mesh.depthVertexStride, geoData.depthVertexStride);
            ASSERT(mesh.vertexStride == 0 || mesh.vertexStride == geoData.vertexStride);
            ASSERT(mesh.depthVertexStride == 0 || mesh.depthVertexStride == geoData.depthVertexStride);
            mesh.vertexStride = geoData.vertexStride;
            mesh.depthVertexStride = geoData.depthVertexStride;

            SubMesh& submesh = mesh.subMeshes[pi];
            submesh.baseVertex = baseVertex;
            submesh.startIndex = startIndex;
            baseVertex += geoData.vertexCount;
            startIndex += submesh.indexCount;
            totalIndexSize += geoData.indexBufferSize;
            totalVertexSize += geoData.vertexBufferSize;
            totaldepthVertexSize += geoData.depthVertexBufferSize;

            Math::AxisAlignedBox aabbSub(submesh.minPos, submesh.maxPos);
            Math::BoundingSphere shSub((const XMFLOAT4*)submesh.bounds);
            boundingBox.AddBoundingBox(aabbSub);
            boundingSphere = boundingSphere.Union(shSub);
        }
        DirectX::XMStoreFloat4((XMFLOAT4*)mesh.bounds, (Vector4)boundingSphere);
        DirectX::XMStoreFloat3(&mesh.minPos, (Vector4)boundingBox.GetMin());
        DirectX::XMStoreFloat3(&mesh.maxPos, (Vector4)boundingBox.GetMax());

        // copy all buffers to a single buffer
        mesh.sizeVB = totalVertexSize;
        mesh.sizeDepthVB = totaldepthVertexSize;
        mesh.sizeIB = totalIndexSize;
        mesh.VB = std::make_unique<byte[]>(totalVertexSize);
        mesh.DepthVB = std::make_unique<byte[]>(totaldepthVertexSize);
        mesh.IB = std::make_unique<byte[]>(totalIndexSize);
        uint32_t vertexBufferOffset = 0;
        uint32_t indexBufferOffset = 0;
        uint32_t depthVertexBufferOffset = 0;
        for (size_t fi = 0; fi < allGeoData.size(); fi++)
        {
            const GeometryData& geoData = allGeoData[fi];
            CopyMemory(mesh.VB.get() + vertexBufferOffset, geoData.VB.get(), geoData.vertexBufferSize);
            CopyMemory(mesh.DepthVB.get() + depthVertexBufferOffset, geoData.depthVB.get(), geoData.depthVertexBufferSize);
            CopyMemory(mesh.IB.get() + indexBufferOffset, geoData.IB.get(), geoData.indexBufferSize);
            vertexBufferOffset += geoData.vertexBufferSize;
            depthVertexBufferOffset += geoData.depthVertexBufferSize;
            indexBufferOffset += geoData.indexBufferSize;
        }

        return mesh;
    }

    void BuildAllMeshes(const glTF::Asset& asset)
    {
        std::vector<std::future<Mesh>> meshBuildTasks;
        for (size_t i = 0; i < asset.m_meshes.size(); i++)
        {
            const glTF::Mesh& gltfMesh = asset.m_meshes[i];
            //Mesh& mesh = MeshManager::GetInstance()->AddUnInitializedMesh();

            meshBuildTasks.emplace_back(Utility::gThreadPoolExecutor.Submit(&BuildMesh, std::cref(gltfMesh)));
        }

        for (size_t i = 0; i < meshBuildTasks.size(); i++)
            MeshManager::GetInstance()->AddMesh(std::move(meshBuildTasks[i].get()));

        MeshManager::GetInstance()->UpdateMeshes();
    }

    void WalkGraph(
        std::vector<Model>& sceneModels,
        const std::vector<glTF::Node*>& siblings,
        uint32_t curIndex,
        const Math::Matrix4& xform
    )
    {
        using namespace Math;

        size_t numSiblings = siblings.size();
        for (size_t i = 0; i < numSiblings; ++i)
        {
            glTF::Node* curNode = siblings[i];
            Model& model = sceneModels[curNode->linearIdx];
            model.mHasChildren = false;
            model.mCurIndex = curNode->linearIdx;
            model.mParentIndex = curIndex;
            
            Math::Matrix4 modelXForm;
            if (curNode->hasMatrix)
            {
                CopyMemory((float*)&modelXForm, curNode->matrix, sizeof(curNode->matrix));
            }
            else
            {
                Quaternion rot;
                XMFLOAT3 scale;
                CopyMemory((float*)&rot, curNode->rotation, sizeof(curNode->rotation));
                CopyMemory((float*)&scale, curNode->scale, sizeof(curNode->scale));
                modelXForm = Matrix4(
                    Matrix3(rot) * Matrix3::MakeScale(scale),
                    Vector3(*(const XMFLOAT3*)curNode->translation)
                );
            }
            const AffineTransform& affineTrans = (const AffineTransform&)modelXForm;
            Math::Quaternion q(modelXForm);
            Math::UniformTransform localTrans(q, affineTrans.GetUniformScale(), affineTrans.GetTranslation());
            model.mLocalTrans = localTrans;

            const Matrix4 LocalXform = xform * modelXForm;

            if (!curNode->pointsToCamera && curNode->mesh != nullptr)
            {
                model.mMesh = GET_MESH(curNode->mesh->index);

                Scalar scaleXSqr = LengthSquare((Vector3)LocalXform.GetX());
                Scalar scaleYSqr = LengthSquare((Vector3)LocalXform.GetY());
                Scalar scaleZSqr = LengthSquare((Vector3)LocalXform.GetZ());
                Scalar sphereScale = Sqrt(Max(Max(scaleXSqr, scaleYSqr), scaleZSqr));

                Vector3 sphereCenter(*(Math::XMFLOAT3*)model.mMesh->bounds);
                sphereCenter = (Vector3)(LocalXform * sphereCenter);
                Scalar sphereRadius = sphereScale * model.mMesh->bounds[3];
                model.m_BSOS = Math::BoundingSphere(sphereCenter, sphereRadius);
                model.m_BBoxOS = AxisAlignedBox::CreateFromSphere(model.m_BSOS);
            }

            if (curNode->children.size() > 0)
            {
                model.mHasChildren = true;
                WalkGraph(sceneModels, curNode->children, model.mCurIndex, LocalXform);
            }

            // Are there more siblings?
            if (i + 1 < numSiblings)
            {
                model.mHasSiblings = true;
            }
        }
    }

    void BuildScene(Scene* scene, const glTF::Asset& asset)
    {
        scene->GetModels().resize(asset.m_nodes.size());
        scene->GetModelTranforms().resize(asset.m_nodes.size());

        const glTF::Scene* gltfScene = asset.m_scene; 
        // only one scene
        WalkGraph(scene->GetModels(), gltfScene->nodes, -1, Math::Matrix4(Math::kIdentity));
    }
};
