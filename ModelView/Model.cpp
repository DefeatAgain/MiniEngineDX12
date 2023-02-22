#include "Model.h"
#include "MeshRenderer.h"
#include "Mesh.h"

void Model::Render(MeshRenderer& sorter, const Math::AffineTransform& transform,
    D3D12_GPU_VIRTUAL_ADDRESS meshCBV) const
{
    const Math::Frustum& frustum = sorter.GetViewFrustum();
    const Math::AffineTransform& viewMat = (const  Math::AffineTransform&)sorter.GetViewMatrix();

    for (uint32_t i = 0; i < mMesh->subMeshCount; ++i)
    {
        const SubMesh& subMesh = mMesh->subMeshes[i];

        Math::BoundingSphere sphereLS((const XMFLOAT4*)subMesh.bounds);
        Math::BoundingSphere sphereWS = Math::BoundingSphere(transform * sphereLS.GetCenter(),
            sphereWS.GetRadius() * transform.GetUniformScale());
        Math::BoundingSphere sphereVS = Math::BoundingSphere(viewMat * sphereWS.GetCenter(), sphereWS.GetRadius());

        //if (frustum.IntersectSphere(sphereVS))
        //if (subMesh.psoFlags & ePSOFlags::kAlphaTest)
        {
            float distance = -sphereVS.GetCenter().GetZ() - sphereVS.GetRadius();
            sorter.AddMesh(subMesh, this, distance, meshCBV);
        }
    }
}
