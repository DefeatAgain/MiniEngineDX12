#include "Model.h"
#include "MeshRenderer.h"
#include "Mesh.h"
#include "Scene.h"

void Model::Render(MeshRenderer& renderer, const Math::AffineTransform& transform, D3D12_GPU_VIRTUAL_ADDRESS meshCBV) const
{
    for (size_t passIndex = 0; passIndex < renderer.GetPassCount(); passIndex++)
    {
        const Math::Frustum& frustum = renderer.GetViewFrustum(passIndex);
        const Math::AffineTransform& viewMat = (const Math::AffineTransform&)renderer.GetViewMatrix(passIndex);

        for (uint32_t i = 0; i < mMesh->subMeshCount; ++i)
        {
            const SubMesh& subMesh = mMesh->subMeshes[i];

            Math::BoundingSphere sphereLS((const XMFLOAT4*)subMesh.bounds);
            Math::BoundingSphere sphereWS(transform * sphereLS.GetCenter(), sphereLS.GetRadius() * transform.GetUniformScale());
            Math::BoundingSphere sphereVS = Math::BoundingSphere(viewMat * sphereWS.GetCenter(), sphereWS.GetRadius());

            if (frustum.IntersectSphere(sphereVS))
            {
                float distance = -sphereVS.GetCenter().GetZ() - sphereVS.GetRadius();
                renderer.AddMesh(passIndex, subMesh, this, distance, meshCBV);
            }
        }
    }
}

Math::BoundingSphere Model::GetWorldBoundingSphere() const
{
    const Math::AffineTransform& trans = mScene->GetModelTranform(mCurIndex);
    return Math::BoundingSphere(trans * m_BSLS.GetCenter(), trans.GetUniformScale() * m_BSLS.GetRadius());
}
