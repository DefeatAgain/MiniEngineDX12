#pragma once
#include "Math/VectorMath.h"
#include "Math/BoundingBox.h"
#include "GpuBuffer.h"

struct Mesh;
class MeshRenderer;

// as gltf Node
class Model
{
public:
    Model() {}
	~Model() {}

    void Render(MeshRenderer& sorter, const Math::AffineTransform& transform,
        D3D12_GPU_VIRTUAL_ADDRESS meshCBV) const;

    Math::UniformTransform mLocalTrans;
    //Math::XMFLOAT3 position;
    //Math::Quaternion rotation;
    //Math::XMFLOAT3 scale;

    uint32_t mParentIndex;
    uint32_t mCurIndex;

    //Math::BoundingSphere m_BSLS;         // local space bounds
    Math::BoundingSphere m_BSOS;        // object space bounds
    //Math::AxisAlignedBox m_BBoxLS;       // local space AABB
    Math::AxisAlignedBox m_BBoxOS;       // object space AABB

	const Mesh* mMesh;
    bool mHasChildren;
    bool mHasSiblings;
};
