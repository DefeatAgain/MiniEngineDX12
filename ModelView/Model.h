#pragma once
#include "Math/VectorMath.h"
#include "Math/BoundingBox.h"
#include "GpuBuffer.h"

class Material;


struct SubMesh
{
    Math::BoundingSphere m_BSLS;         // local space bounds
    Math::BoundingSphere m_BSOS;  // object space bounds
    Math::AxisAlignedBox m_BBoxLS;       // local space AABB
    Math::AxisAlignedBox m_BBoxOS;       // object space AABB

    uint32_t startIndex;  // Offset to first index in index buffer 
    uint32_t baseVertex;  // Offset to first vertex in vertex buffer
    uint32_t indexCount;
    union
    {
        uint32_t hash;
        struct {
            uint32_t psoFlags : 16;
            uint32_t index32 : 1;
            uint32_t materialIdx : 15;  // mesh
        };
    };
    uint16_t vertexStride;
    uint16_t uniqueMaterialIdx;
};


struct Mesh
{
    GpuBuffer VB;
    GpuBuffer depthVB;
    GpuBuffer IB;

    uint32_t primCount;
    float    bounds[4];     // A bounding sphere
    //uint32_t vbOffset;      // BufferLocation - Buffer.GpuVirtualAddress
    //uint32_t vbDepthOffset; // BufferLocation - Buffer.GpuVirtualAddress
    //uint32_t ibOffset;      // BufferLocation - Buffer.GpuVirtualAddress
    //uint8_t  vbStride;      // StrideInBytes
    //uint8_t  ibFormat;      // DXGI_FORMAT

    std::vector<SubMesh> subMeshes;

    static std::vector<Mesh> sAllMeshs;
};


// as gltf Node
class Model
{
public:
    Model();
	~Model() { Destroy(); }

	void Destroy();
private:
    Math::XMFLOAT3 position;
    Math::Quaternion rotation;
    Math::XMFLOAT3 scale;

	Mesh* mMesh;
	Math::BoundingSphere mBoundingSphere; // Object-space bounding sphere
	Math::AxisAlignedBox mBoundingBox;
    std::vector<UploadBuffer> mConstantBuffer;
};
