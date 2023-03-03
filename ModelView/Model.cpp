#include "Model.h"
#include "MeshRenderer.h"
#include "Mesh.h"
#include "Scene.h"

void Model::Render(MeshRenderer& renderer, const Math::AffineTransform& transform,
    D3D12_GPU_VIRTUAL_ADDRESS meshCBV) const
{
    const Math::Frustum& frustum = renderer.GetViewFrustum();
    const Math::AffineTransform& viewMat = (const  Math::AffineTransform&)renderer.GetViewMatrix();

    for (uint32_t i = 0; i < mMesh->subMeshCount; ++i)
    {
        const SubMesh& subMesh = mMesh->subMeshes[i];

        Math::BoundingSphere sphereLS((const XMFLOAT4*)subMesh.bounds);
        Math::BoundingSphere sphereWS(transform * sphereLS.GetCenter(), sphereLS.GetRadius() * transform.GetUniformScale());
        Math::BoundingSphere sphereVS = Math::BoundingSphere(viewMat * sphereWS.GetCenter(), sphereWS.GetRadius());

        //if (subMesh.psoFlags & ePSOFlags::kAlphaTest)
        if (frustum.IntersectSphere(sphereVS))
        {
            float distance = -sphereVS.GetCenter().GetZ() - sphereVS.GetRadius();
            renderer.AddMesh(subMesh, this, distance, meshCBV);
        }
    }
}

Math::BoundingSphere Model::GetWorldBoundingSphere() const
{
    const Math::AffineTransform& trans = mScene->GetModelTranform(mCurIndex);
    return Math::BoundingSphere(trans * m_BSLS.GetCenter(), trans.GetUniformScale() * m_BSLS.GetRadius());
}
