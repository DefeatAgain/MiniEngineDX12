#pragma once
#include "Math/VectorMath.h"
#include "Math/BoundingBox.h"
#include "GpuBuffer.h"

struct Mesh;
class MeshRenderer;
class Scene;


// as gltf Node
class Model
{
    friend class Scene;
public:
    Model() {}
	~Model() {}

    void Render(MeshRenderer& renderer, const Math::AffineTransform& transform,
        D3D12_GPU_VIRTUAL_ADDRESS meshCBV) const;

    const Mesh* GetMesh() const { return mMesh; }
    Math::BoundingSphere GetWorldBoundingSphere() const;
private:
    Math::XMFLOAT3 mPosition;
    Math::XMFLOAT4 mRotation;
    Math::XMFLOAT3 mScale;

    uint32_t mParentIndex;
    uint32_t mCurIndex;

    Math::BoundingSphere m_BSLS;         // local space bounds
    //Math::BoundingSphere m_BSOS;        // object space bounds
    Math::AxisAlignedBox m_BBoxLS;       // local space AABB
    //Math::AxisAlignedBox m_BBoxOS;       // object space AABB

    // These variables can be present by index
	const Mesh* mMesh;
    const Scene* mScene;
    bool mHasChildren;
    bool mHasSiblings;
};
