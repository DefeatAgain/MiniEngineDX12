#include "Mesh.h"
#include "CommandList.h"
#include "CommandQueue.h"

Mesh& MeshManager::AddMesh()
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

    // calc all mesh size
    for (size_t i = mAddedSize; i < mAllMeshs.size(); i++, mAddedSize++)
    {
        Mesh& mesh = mAllMeshs[i];
        totalVertexSize += mesh.sizeVB;
        totaldepthVertexSize += mesh.sizeDepthVB;
        totalIndexSize += mesh.sizeIB;
    }

    ReserveBuffer(totalVertexSize + mVertexBufferOffset, totaldepthVertexSize + mDepthVertexBufferOffset, totalIndexSize + mIndexBufferOffset);

    mCpuVB.Create(L"VB Cpu", totalVertexSize);
    mCpuDepthVB.Create(L"DepthVB Cpu", totaldepthVertexSize);
    mCpuIB.Create(L"IB Cpu", totalIndexSize);

    // copy to upload buffer
    size_t curVertexBufferOffset = 0;
    size_t curDepthVertexBufferOffset = 0;
    size_t curIndexBufferOffset = 0;
    for (size_t i = mAddedSize; i < mAllMeshs.size(); i++, mAddedSize++)
    {
        Mesh& mesh = mAllMeshs[i];
        CopyMemory((uint8_t*)mCpuVB.Map() + curVertexBufferOffset, mesh.VB.get(), mesh.sizeVB);
        CopyMemory((uint8_t*)mCpuDepthVB.Map() + curDepthVertexBufferOffset, mesh.DepthVB.get(), mesh.sizeDepthVB);
        CopyMemory((uint8_t*)mCpuIB.Map() + curIndexBufferOffset, mesh.IB.get(), mesh.sizeIB);

        mesh.vbOffset = mVertexBufferOffset + curVertexBufferOffset;
        mesh.vbDepthOffset = mDepthVertexBufferOffset + curDepthVertexBufferOffset;
        mesh.ibOffset = mIndexBufferOffset + curIndexBufferOffset;
        curVertexBufferOffset += mesh.sizeVB;
        curDepthVertexBufferOffset += mesh.sizeDepthVB;
        curIndexBufferOffset += mesh.sizeIB;
    }
    
    PushGraphicsTaskSync(&MeshManager::UpdateMeshBufferTask, this);

    mIndexBufferOffset += curIndexBufferOffset;
    mVertexBufferOffset += curVertexBufferOffset;
    mDepthVertexBufferOffset += curDepthVertexBufferOffset;

    mCpuVB.Destroy();
    mCpuDepthVB.Destroy();
    mCpuIB.Destroy();
}

void MeshManager::ReserveBuffer(size_t vertexBufferSize, size_t depthVertexBufferSize, size_t indexBufferSize)
{
    if (mGpuVB[mCurrentUsesGpuBuffer].GetBufferSize() >= vertexBufferSize)
        return;

    uint8_t nextSelectBuffer = mCurrentUsesGpuBuffer ^ 1;
    mGpuVB[nextSelectBuffer].Create(L"VB Gpu", vertexBufferSize + vertexBufferSize / 2, 1);
    mGpuDepthVB[nextSelectBuffer].Create(L"DepthVB Gpu", depthVertexBufferSize + depthVertexBufferSize / 2, 1);
    mGpuIB[nextSelectBuffer].Create(L"IB Gpu", indexBufferSize + indexBufferSize / 2, 1);

    CommandQueueManager::GetInstance()->GetGraphicsQueue().WaitForIdle();
    PushGraphicsTaskSync(&MeshManager::ReserveMeshBufferTask, this);

    mGpuVB[mCurrentUsesGpuBuffer].Destroy();
    mGpuIB[mCurrentUsesGpuBuffer].Destroy();
    mGpuDepthVB[mCurrentUsesGpuBuffer].Destroy();
    mCurrentUsesGpuBuffer = nextSelectBuffer;
}

CommandList* MeshManager::UpdateMeshBufferTask(CommandList* commandList)
{
    CopyCommandList& copyList = commandList->GetCopyCommandList().Begin(L"Update mesh buffer");
    copyList.CopyBufferRegion(mGpuVB[mCurrentUsesGpuBuffer], mVertexBufferOffset, mCpuVB, 0, mCpuVB.GetBufferSize());
    copyList.CopyBufferRegion(mGpuIB[mCurrentUsesGpuBuffer], mIndexBufferOffset, mCpuIB, 0, mCpuIB.GetBufferSize());
    copyList.CopyBufferRegion(mGpuDepthVB[mCurrentUsesGpuBuffer], mDepthVertexBufferOffset, mCpuDepthVB, 0 , mCpuDepthVB.GetBufferSize());
    return commandList;
}

CommandList* MeshManager::ReserveMeshBufferTask(CommandList* commandList)
{
    CopyCommandList& copyList = commandList->GetCopyCommandList().Begin(L"Reserve mesh buffer");
    uint8_t nextSelectBuffer = mCurrentUsesGpuBuffer ^ 1;
    copyList.CopyBuffer(mGpuVB[nextSelectBuffer], mGpuVB[mCurrentUsesGpuBuffer]);
    copyList.CopyBuffer(mGpuIB[nextSelectBuffer], mGpuIB[mCurrentUsesGpuBuffer]);
    copyList.CopyBuffer(mGpuDepthVB[nextSelectBuffer], mGpuDepthVB[mCurrentUsesGpuBuffer]);
    return commandList;
}
