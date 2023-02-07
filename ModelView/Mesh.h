#pragma once
#include "Math/VectorMath.h"
#include "Math/BoundingBox.h"
#include "GpuBuffer.h"

class CommandList;

struct SubMesh
{
    float bounds[4];     // A bounding sphere
    Math::XMFLOAT3 minPos;
    Math::XMFLOAT3 maxPos;

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
    uint8_t vertexStride;
    uint8_t depthVertexStride;
    uint16_t uniqueMaterialIdx;
};


struct Mesh
{
    std::unique_ptr<byte[]> VB;
    std::unique_ptr<byte[]> DepthVB;
    std::unique_ptr<byte[]> IB;
    std::unique_ptr<SubMesh[]> subMeshes;

    float bounds[4];     // A bounding sphere
    Math::XMFLOAT3 minPos;
    Math::XMFLOAT3 maxPos;

    uint32_t sizeVB;
    uint32_t sizeDepthVB;
    uint32_t sizeIB;

    uint32_t vbOffset;      // BufferLocation - Buffer.GpuVirtualAddress
    uint32_t vbDepthOffset; // BufferLocation - Buffer.GpuVirtualAddress
    uint32_t ibOffset;      // BufferLocation - Buffer.GpuVirtualAddress
    uint32_t meshIndex;
    uint32_t subMeshCount;
};

class MeshManager : public Singleton<MeshManager>, public Graphics::CopyContext
{
    USE_SINGLETON;

    enum eBufferType
    {
        kVertexBuffer,
        kDepthVertexBuffer,
        kIndexBuffer,
        kNumBufferTypes
    };
private:
    MeshManager() :
        mNeedUpdate(false), 
        mVertexBufferOffset(0), 
        mIndexBufferOffset(0), 
        mDepthVertexBufferOffset(0),
        mCurrentResientSize(0)
    {}
public:
    ~MeshManager() {}

    Mesh& AddUnInitializedMesh();

    void UpdateMeshes();

    D3D12_GPU_VIRTUAL_ADDRESS GetVBVirtualAddr() { return mGpuBuffer[kVertexBuffer].GetGpuVirtualAddress(); }
    D3D12_GPU_VIRTUAL_ADDRESS GetDepthVBVirtualAddr() { return mGpuBuffer[kDepthVertexBuffer].GetGpuVirtualAddress(); }
    D3D12_GPU_VIRTUAL_ADDRESS GetIBVirtualAddr() { return mGpuBuffer[kIndexBuffer].GetGpuVirtualAddress(); }

    const Mesh* GetMesh(size_t index) const { return &mAllMeshs[index]; }
private:
    void ReserveBuffer(uint32_t vertexBufferSize, uint32_t depthVertexBufferSize, uint32_t indexBufferSize);

    CommandList* UpdateMeshBufferTask(CommandList* commandList, UploadBuffer cpuBuffer[kNumBufferTypes]);
    CommandList* ReserveMeshBufferTask(CommandList* commandList, GpuBuffer newBuffer[kNumBufferTypes]);
private:
    bool mNeedUpdate;
    uint32_t mVertexBufferOffset;
    uint32_t mIndexBufferOffset;
    uint32_t mDepthVertexBufferOffset;
    GpuBuffer mGpuBuffer[kNumBufferTypes];

    size_t mCurrentResientSize;
    std::vector<Mesh> mAllMeshs;
};

#define GET_MESH(index) MeshManager::GetInstance()->GetMesh(index)
#define GET_MESH_VB MeshManager::GetInstance()->GetVBVirtualAddr()
#define GET_MESH_DepthVB MeshManager::GetInstance()->GetDepthVBVirtualAddr()
#define GET_MESH_IB MeshManager::GetInstance()->GetIBVirtualAddr()
