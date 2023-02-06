#include "Model.h"
#include "MeshRenderer.h"
#include "Mesh.h"

void Model::Render(MeshRenderer& sorter, const Math::UniformTransform& transform,
    D3D12_GPU_VIRTUAL_ADDRESS meshCBV) const
{
    const Math::Frustum& frustum = sorter.GetViewFrustum();
    const Math::AffineTransform transformAffine(transform);
    const Math::AffineTransform& viewMat = (const  Math::AffineTransform&)sorter.GetViewMatrix();
    const Math::AffineTransform& objectMat = (const  Math::AffineTransform&)mXform;

    for (uint32_t i = 0; i < mMesh->subMeshCount; ++i)
    {
        const SubMesh& mesh = mMesh->subMeshes[i];

        Math::BoundingSphere sphereLS((const XMFLOAT4*)mesh.bounds);
        Math::BoundingSphere sphereWS = Math::BoundingSphere(transformAffine * objectMat * sphereLS.GetCenter(), 
            sphereWS.GetRadius() * transform.GetScale());
        Math::BoundingSphere sphereVS = Math::BoundingSphere(viewMat * sphereWS.GetCenter(), sphereWS.GetRadius());

        if (frustum.IntersectSphere(sphereVS))
        {
            float distance = -sphereVS.GetCenter().GetZ() - sphereVS.GetRadius();
            sorter.AddMesh(mesh, this, distance, meshCBV);
        }
    }
}
