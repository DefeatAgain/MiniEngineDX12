//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  James Stanard 
//

#include "Camera.h"
#include "Utils/DebugUtils.h"

#include <cmath>

using namespace Math;

namespace Graphics
{
    bool gReversedZ = true;
}

void BaseCamera::SetLookDirection(Vector3 forward, Vector3 up)
{
    // Given, but ensure normalization
    Scalar forwardLenSq = LengthSquare(forward);
    forward = Select(forward * RecipSqrt(forwardLenSq), -Vector3(kZUnitVector), forwardLenSq < Scalar(0.000001f));

    // Deduce a valid, orthogonal right vector
    Vector3 right = Cross(forward, up);
    Scalar rightLenSq = LengthSquare(right);
    if (Abs(1.0f - Abs(forward.GetY())) < 0.000001f)
        right = Select(right * RecipSqrt(rightLenSq), Quaternion(Vector3(kZUnitVector), XM_PIDIV2) * forward, rightLenSq < Scalar(0.000001f));
    else
        right = Select(right * RecipSqrt(rightLenSq), Quaternion(Vector3(kYUnitVector), -XM_PIDIV2) * forward, rightLenSq < Scalar(0.000001f));

    // Compute actual up vector
    up = Cross(right, forward);

    // Finish constructing basis
    m_Basis = Matrix3(right, up, -forward);
    m_CameraToWorld.SetRotation(Quaternion(m_Basis));
}

void BaseCamera::Update()
{
    m_PreviousViewProjMatrix = m_ViewProjMatrix;

    m_ViewMatrix = Matrix4(~m_CameraToWorld);
    m_ViewProjMatrix = m_ProjMatrix * m_ViewMatrix;
    m_ReprojectMatrix = m_PreviousViewProjMatrix * Invert(GetViewProjMatrix());

    m_FrustumVS = Frustum(m_ProjMatrix);
    m_FrustumWS = m_CameraToWorld * m_FrustumVS;
}


void Camera::UpdateProjMatrix(void)
{
    float Y = 1.0f / std::tanf(m_VerticalFOV * 0.5f);
    float X = Y / m_AspectRatio;

    float Q1, Q2;

    // ReverseZ puts far plane at Z=0 and near plane at Z=1.  This is never a bad idea, and it's
    // actually a great idea with F32 depth buffers to redistribute precision more evenly across
    // the entire range.  It requires clearing Z to 0.0f and using a GREATER variant depth test.
    // Some care must also be done to properly reconstruct linear W in a pixel shader from hyperbolic Z.
    //if (m_ReverseZ)
    if (Graphics::gReversedZ)
    {
        if (m_InfiniteZ)
        {
            Q1 = 0.0f;
            Q2 = m_NearClip;
        }
        else
        {
            Q1 = m_NearClip / (m_FarClip - m_NearClip);
            Q2 = Q1 * m_FarClip;
        }
    }
    else
    {
        if (m_InfiniteZ)
        {
            Q1 = -1.0f;
            Q2 = -m_NearClip;
        }
        else
        {
            Q1 = m_FarClip / (m_NearClip - m_FarClip);
            Q2 = Q1 * m_NearClip;
        }
    }

    SetProjMatrix(Matrix4(
        Vector4(X, 0.0f, 0.0f, 0.0f),
        Vector4(0.0f, Y, 0.0f, 0.0f),
        Vector4(0.0f, 0.0f, Q1, -1.0f),
        Vector4(0.0f, 0.0f, Q2, 0.0f)
    ));
}

void ShadowCamera::UpdateMatrix(Math::Vector3 LightDirection, Math::Vector3 ShadowCenter, Math::Vector3 ShadowBounds)
{
    SetLookDirection(LightDirection, Vector3(kYUnitVector));

    // Converts world units to texel units so we can quantize the camera position to whole texel units
    Vector3 RcpDimensions = Recip(ShadowBounds);

    SetPosition(ShadowCenter - LightDirection * ShadowBounds.GetZ() * 0.5f);

    if (Graphics::gReversedZ)
    {
        Matrix4 projMat = Matrix4::MakeScale(Vector3(2.0f, 2.0f, 1.0f) * RcpDimensions);
        projMat.SetW(Vector4(0.0f, 0.0f, 1.0f, 1.0f));
        SetProjMatrix(projMat);
    }
    else
    {
        SetProjMatrix(Matrix4::MakeScale(Vector3(2.0f, 2.0f, -1.0f) * RcpDimensions));
    }

    Update();

    // Transform from clip space to texture space
    mShadowMatrix = Matrix4(AffineTransform(Matrix3::MakeScale(0.5f, -0.5f, 1.0f), Vector3(0.5f, 0.5f, 0.0f))) * m_ViewProjMatrix;
}

void ShadowCamera::UpdateMatrix(Math::Vector3 lightDirection, const Math::Frustum& mainCameraFrustumW, float cameraNearstZOffset,
    uint32_t bufferWidth, uint32_t bufferHeight, uint32_t bufferPrecision)
{
    SetLookDirection(lightDirection, Vector3(kYUnitVector));
    SetPosition(Vector3(kZero));

    Math::Frustum frustumLightView = ~GetRotation() * mainCameraFrustumW;
    Math::AxisAlignedBox tempBox(kZero);
    for (size_t i = 0; i < Math::Frustum::kNumConers; i++)
    {
        tempBox.AddPoint(frustumLightView.GetFrustumCorner((Math::Frustum::CornerID)i));
    }
    Vector3 lookMin(tempBox.GetMin().GetX(), tempBox.GetMin().GetY(), tempBox.GetMax().GetZ());
    Vector3 lookMax(tempBox.GetMax().GetX(), tempBox.GetMax().GetY(), tempBox.GetMin().GetZ());
    Vector3 shadowCenter = (Vector3(lookMax.GetX(), lookMax.GetY(), lookMin.GetZ()) + lookMin) * 0.5f;
    float additionZ = cameraNearstZOffset - Abs(shadowCenter.GetZ());
    if (additionZ < 0.0f)
        additionZ = 0.0f;

    // Can not deal all situation of objects out of light camera
    //Math::BoundingSphere sceneBoundsLS(~GetRotation() * sceneBounds.GetCenter(), sceneBounds.GetRadius());
    //if (sceneBoundsLS.Contains(shadowCenter))
    //{
    //    float pointToShadowCenter = (shadowCenter - sceneBoundsLS.GetCenter()).GetZ();
    //    if (std::abs(pointToShadowCenter) < 1e-6)
    //        additionZ = sceneBoundsLS.GetRadius();
    //    else if (pointToShadowCenter < 0.0f)
    //        additionZ = pointToShadowCenter + sceneBoundsLS.GetRadius();
    //    else
    //        additionZ = sceneBoundsLS.GetRadius() - pointToShadowCenter;
    //}

    shadowCenter.SetZ(additionZ + shadowCenter.GetZ());
    Scalar farDist = Length(frustumLightView.GetFrustumCorner(Frustum::kFarLowerLeft) -
                            frustumLightView.GetFrustumCorner(Frustum::kFarUpperRight));
    Scalar crossDist = Length(frustumLightView.GetFrustumCorner(Frustum::kNearLowerLeft) - 
                              frustumLightView.GetFrustumCorner(Frustum::kFarUpperRight));
    Scalar maxDist = Select(crossDist, farDist, farDist > crossDist);
    // width and height must be equal and not be changed
    Vector3 shadowBounds = Vector3(maxDist, maxDist, tempBox.GetDimensions().GetZ() + std::abs(additionZ));
    Vector3 RcpDimensions = Recip(shadowBounds);
    // Converts world units to texel units so we can quantize the camera position to whole texel units
    Vector3 QuantizeScale = Vector3((float)bufferWidth, (float)bufferHeight, (float)((1 << bufferPrecision) - 1)) * RcpDimensions;

    //
    // Recenter the camera at the quantized position
    //
    // Scale to texel units, truncate fractional part, and scale back to world units
    shadowCenter = Floor(shadowCenter * QuantizeScale) / QuantizeScale;
    // Transform back into world space
    shadowCenter = GetRotation() * shadowCenter;

    SetPosition(shadowCenter);

    // copy from previous
    if (Graphics::gReversedZ)
    {
        Matrix4 projMat = Matrix4::MakeScale(Vector3(2.0f, 2.0f, 1.0f) * RcpDimensions);
        projMat.SetW(Vector4(0.0f, 0.0f, 1.0f, 1.0f));
        SetProjMatrix(projMat);
    }
    else
    {
        SetProjMatrix(Matrix4::MakeScale(Vector3(2.0f, 2.0f, -1.0f) * RcpDimensions));
    }

    Update();

    // Transform from clip space to texture space
    mShadowMatrix = Matrix4(AffineTransform(Matrix3::MakeScale(0.5f, -0.5f, 1.0f), Vector3(0.5f, 0.5f, 0.0f))) * m_ViewProjMatrix;
}

void ShadowCamera::GetDivideCSMCameras(std::vector<ShadowCamera>& shadowCameras, const float zDivides[], uint32_t numDivides, 
    uint32_t maxNumDivides, Math::Vector3 lightDirection, const Math::Camera& mainCamera, float cameraNearstZOffset,
    uint32_t bufferWidth, uint32_t bufferHeight, uint32_t bufferPrecision)
{
    ASSERT(numDivides > 0);

    std::vector<Math::Camera> allDividesCamera(numDivides + 1, mainCamera);
    shadowCameras.resize(numDivides + 1);

    float* zDivides_0 = (float*)alloca(maxNumDivides * 4);
    GetDivideCSMZRange(zDivides_0, mainCamera, zDivides, numDivides, maxNumDivides);
    for (uint32_t i = 0; i < numDivides + 1; i++)
    {
        float nearZ = i == 0 ? mainCamera.GetNearClip() : zDivides_0[i - 1];
        float farZ = i < numDivides ? zDivides_0[i] : mainCamera.GetFarClip();
        allDividesCamera[i].SetZRange(nearZ, farZ);
        allDividesCamera[i].Update();

        shadowCameras[i].UpdateMatrix(lightDirection, allDividesCamera[i].GetWorldSpaceFrustum(), cameraNearstZOffset,
            bufferWidth, bufferHeight, bufferPrecision);
    }
}

void ShadowCamera::GetDivideCSMZRange(float* CSMZDivides, const Math::Camera& mainCamera,
    const float* zDivides, uint32_t numDivides, uint32_t maxNumDivides)
{
    float viewDist = mainCamera.GetFarClip() - mainCamera.GetNearClip();
    for (size_t i = 0; i < maxNumDivides; i++)
        CSMZDivides[i] = i < numDivides ? viewDist * zDivides[i] : FLT_MAX;
}

void ShadowCamera::GetDivideCSMZRange(Math::Vector3& CSMZDivides, const Math::Camera& mainCamera,
    const float* zDivides, uint32_t numDivides, uint32_t maxNumDivides)
{
    ASSERT(maxNumDivides < 4);
    float* zDivides_0 = (float*)alloca(maxNumDivides * 4);
    GetDivideCSMZRange(zDivides_0, mainCamera, zDivides, numDivides, maxNumDivides);
    CSMZDivides = Vector3(XMLoadFloat3((const XMFLOAT3*)zDivides_0));
}
