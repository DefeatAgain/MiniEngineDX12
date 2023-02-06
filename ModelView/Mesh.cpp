#include "Mesh.h"
#include "CommandList.h"
#include "CommandQueue.h"

enum eGpuBufferUpdateFlags
{
    kUpdateVB = 0x1,
    kUpdateDepthVB = 0x2,
    kUpdateIB = 0x4
};

Mesh& MeshManager::AddUnInitializedMesh()
{
    mNeedUpdate = true;

    Mesh& mesh = mAllMeshs.emplace_back();
    mesh.meshIndex = mAllMeshs.size() - 1;
    return mesh;
}

void MeshManager::UpdateMeshes()
{
    if (!mNeedUpdate)
        return;
    mNeedUpdate = false;

    size_t totalIndexSize = 0;
    size_t totalVertexSize = 0;
    size_t totaldepthVertexSize = 0;
    size_t curMeshIndex = mCurrentResientSize;

    // calc all mesh size
    for (size_t i = mCurrentResientSize; i < mAllMeshs.size(); i++, mCurrentResientSize++)
    {
        Mesh& mesh = mAllMeshs[i];
        totalVertexSize += mesh.sizeVB;
        totaldepthVertexSize += mesh.sizeDepthVB;
        totalIndexSize += mesh.sizeIB;
    }

    ReserveBuffer(totalVertexSize + mVertexBufferOffset, totaldepthVertexSize + mDepthVertexBufferOffset, totalIndexSize + mIndexBufferOffset);

    UploadBuffer cpuBuffer[kNumBufferTypes];
    cpuBuffer[kVertexBuffer].Create(L"VB Cpu", totalVertexSize);
    cpuBuffer[kDepthVertexBuffer].Create(L"DepthVB Cpu", totaldepthVertexSize);
    cpuBuffer[kIndexBuffer].Create(L"IB Cpu", totalIndexSize);

    // copy to upload buffer
    size_t curVertexBufferOffset = 0;
    size_t curDepthVertexBufferOffset = 0;
    size_t curIndexBufferOffset = 0;
    for (size_t i = curMeshIndex; i < mAllMeshs.size(); i++, curMeshIndex++)
    {
        Mesh& mesh = mAllMeshs[i];
        CopyMemory((uint8_t*)cpuBuffer[kVertexBuffer].Map() + curVertexBufferOffset, mesh.VB.get(), mesh.sizeVB);
        CopyMemory((uint8_t*)cpuBuffer[kDepthVertexBuffer].Map() + curDepthVertexBufferOffset, mesh.DepthVB.get(), mesh.sizeDepthVB);
        CopyMemory((uint8_t*)cpuBuffer[kIndexBuffer].Map() + curIndexBufferOffset, mesh.IB.get(), mesh.sizeIB);

        mesh.vbOffset = mVertexBufferOffset + curVertexBufferOffset;
        mesh.vbDepthOffset = mDepthVertexBufferOffset + curDepthVertexBufferOffset;
        mesh.ibOffset = mIndexBufferOffset + curIndexBufferOffset;
        curVertexBufferOffset += mesh.sizeVB;
        curDepthVertexBufferOffset += mesh.sizeDepthVB;
        curIndexBufferOffset += mesh.sizeIB;
    }
    
    PushGraphicsTaskSync(&MeshManager::UpdateMeshBufferTask, this, cpuBuffer);

    mIndexBufferOffset += curIndexBufferOffset;
    mVertexBufferOffset += curVertexBufferOffset;
    mDepthVertexBufferOffset += curDepthVertexBufferOffset;
}

void MeshManager::ReserveBuffer(size_t vertexBufferSize, size_t depthVertexBufferSize, size_t indexBufferSize)
{
    bool needReserve = false;
    GpuBuffer newGpuBuffer[kNumBufferTypes];

    if (mGpuBuffer[kVertexBuffer].GetBufferSize() < vertexBufferSize)
    {
        newGpuBuffer[kVertexBuffer].Create(L"VB Gpu", vertexBufferSize + vertexBufferSize / 2, 1);
        needReserve = true;
    }

    if (mGpuBuffer[kDepthVertexBuffer].GetBufferSize() < depthVertexBufferSize)
    {
        newGpuBuffer[kDepthVertexBuffer].Create(L"DepthVB Gpu", depthVertexBufferSize + depthVertexBufferSize / 2, 1);
        needReserve = true;
    }

    if (mGpuBuffer[kIndexBuffer].GetBufferSize() < indexBufferSize)
    {
        newGpuBuffer[kIndexBuffer].Create(L"IB Gpu", indexBufferSize + indexBufferSize / 2, 1);
        needReserve = true;
    }

    if (!needReserve)
        return;

    CommandQueueManager::GetInstance()->GetGraphicsQueue().WaitForIdle();
    PushGraphicsTaskSync(&MeshManager::ReserveMeshBufferTask, this);

    if (newGpuBuffer[kVertexBuffer].GetBufferSize() != 0)
        mGpuBuffer[kVertexBuffer] = newGpuBuffer[kVertexBuffer];
    if (newGpuBuffer[kIndexBuffer].GetBufferSize() != 0)
        mGpuBuffer[kIndexBuffer] = newGpuBuffer[kIndexBuffer];
    if (newGpuBuffer[kDepthVertexBuffer].GetBufferSize() != 0)
        mGpuBuffer[kDepthVertexBuffer] = newGpuBuffer[kDepthVertexBuffer];
}

CommandList* MeshManager::UpdateMeshBufferTask(CommandList* commandList, UploadBuffer cpuBuffer[kNumBufferTypes])
{
    CopyCommandList& copyList = commandList->GetCopyCommandList().Begin(L"Update mesh buffer");
    copyList.CopyBufferRegion(mGpuBuffer[kVertexBuffer], mVertexBufferOffset, cpuBuffer[kVertexBuffer], 0, 
        cpuBuffer[kVertexBuffer].GetBufferSize());
    copyList.CopyBufferRegion(mGpuBuffer[kIndexBuffer], mIndexBufferOffset, cpuBuffer[kIndexBuffer], 0, 
        cpuBuffer[kIndexBuffer].GetBufferSize());
    copyList.CopyBufferRegion(mGpuBuffer[kDepthVertexBuffer], mDepthVertexBufferOffset, cpuBuffer[kDepthVertexBuffer], 0 , 
        cpuBuffer[kDepthVertexBuffer].GetBufferSize());

    return commandList;
}

CommandList* MeshManager::ReserveMeshBufferTask(CommandList* commandList, GpuBuffer newBuffer[kNumBufferTypes])
{
    CopyCommandList& copyList = commandList->GetCopyCommandList().Begin(L"Reserve mesh buffer");
    if (newBuffer[kVertexBuffer].GetBufferSize() != 0)
        copyList.CopyBuffer(mGpuBuffer[kVertexBuffer], newBuffer[kVertexBuffer]);
    if (newBuffer[kIndexBuffer].GetBufferSize() != 0)
        copyList.CopyBuffer(mGpuBuffer[kIndexBuffer], newBuffer[kIndexBuffer]);
    if (newBuffer[kDepthVertexBuffer].GetBufferSize() != 0)
        copyList.CopyBuffer(mGpuBuffer[kDepthVertexBuffer], newBuffer[kDepthVertexBuffer]);

    return commandList;
}
