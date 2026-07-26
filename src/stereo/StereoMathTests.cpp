#include "stereo/D3D8DrawPolicy.h"
#include "stereo/D3D8FrameCompositionPolicy.h"
#include "stereo/D3D8SemanticDrawPolicy.h"
#include "stereo/D3D8ShaderTransform.h"
#include "stereo/StereoMath.h"
#include "stereo/TreeAngleSlicePolicy.h"
#include "client/D3D8StereoProbeRecords.h"
#include "client/D3D8SpriteShaderTransform.h"
#include "client/D3D8StereoShaderTransform.h"
#include "client/D3D8TreeSpriteShaderTransform.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
int g_failures = 0;

struct FakeShaderConstantDevice
{
    bfvr::d3d8probe::D3DMatrix constants = {};
};

struct FakeSpriteShaderConstantDevice
{
    float registers[16][4] = {};
};

HRESULT WINAPI SetFakeShaderConstants(
    void* device,
    DWORD firstRegister,
    const void* data,
    DWORD registerCount)
{
    if (device == nullptr ||
        data == nullptr ||
        firstRegister != 0 ||
        registerCount != 4)
    {
        return E_INVALIDARG;
    }
    std::memcpy(
        &static_cast<FakeShaderConstantDevice*>(device)->constants,
        data,
        sizeof(bfvr::d3d8probe::D3DMatrix));
    return S_OK;
}

HRESULT WINAPI GetFakeShaderConstants(
    void* device,
    DWORD firstRegister,
    void* data,
    DWORD registerCount)
{
    if (device == nullptr ||
        data == nullptr ||
        firstRegister != 0 ||
        registerCount != 4)
    {
        return E_INVALIDARG;
    }
    std::memcpy(
        data,
        &static_cast<FakeShaderConstantDevice*>(device)->constants,
        sizeof(bfvr::d3d8probe::D3DMatrix));
    return S_OK;
}

HRESULT WINAPI SetFakeSpriteShaderConstants(
    void* device,
    DWORD firstRegister,
    const void* data,
    DWORD registerCount)
{
    if (device == nullptr ||
        data == nullptr ||
        firstRegister + registerCount > 16)
    {
        return E_INVALIDARG;
    }
    std::memcpy(
        &static_cast<FakeSpriteShaderConstantDevice*>(device)
             ->registers[firstRegister][0],
        data,
        registerCount * sizeof(float) * 4);
    return S_OK;
}

HRESULT WINAPI GetFakeSpriteShaderConstants(
    void* device,
    DWORD firstRegister,
    void* data,
    DWORD registerCount)
{
    if (device == nullptr ||
        data == nullptr ||
        firstRegister + registerCount > 16)
    {
        return E_INVALIDARG;
    }
    std::memcpy(
        data,
        &static_cast<FakeSpriteShaderConstantDevice*>(device)
             ->registers[firstRegister][0],
        registerCount * sizeof(float) * 4);
    return S_OK;
}

void Fail(std::string_view test, std::string_view detail)
{
    ++g_failures;
    std::cerr << "FAIL " << test << ": " << detail << '\n';
}

void ExpectNear(std::string_view test, float actual, float expected, float tolerance = 0.00001F)
{
    if (std::fabs(actual - expected) > tolerance)
    {
        std::cerr << "FAIL " << test << ": expected " << expected << ", got " << actual << '\n';
        ++g_failures;
    }
}

void TestIdentityEyeOffsets()
{
    constexpr std::string_view test = "identity eye offsets";
    const bfvr::stereo::Pose head{{1.0F, 2.0F, 3.0F}, {0.0F, 0.0F, 0.0F, 1.0F}};
    const auto eyes = bfvr::stereo::ComputeEyePoses(head, 0.064F);
    if (!eyes.has_value())
    {
        Fail(test, "valid head pose was rejected");
        return;
    }

    ExpectNear(test, eyes->left.position.x, 0.968F);
    ExpectNear(test, eyes->left.position.y, 2.0F);
    ExpectNear(test, eyes->left.position.z, 3.0F);
    ExpectNear(test, eyes->right.position.x, 1.032F);
    ExpectNear(test, eyes->right.position.y, 2.0F);
    ExpectNear(test, eyes->right.position.z, 3.0F);
}

void TestRotatedEyeOffsets()
{
    constexpr std::string_view test = "rotated eye offsets";
    constexpr float rootHalf = 0.70710678118F;
    const bfvr::stereo::Pose head{{0.0F, 0.0F, 0.0F}, {0.0F, rootHalf, 0.0F, rootHalf}};
    const auto eyes = bfvr::stereo::ComputeEyePoses(head, 0.064F);
    if (!eyes.has_value())
    {
        Fail(test, "valid rotated head pose was rejected");
        return;
    }

    // A +90 degree OpenXR yaw rotates head-local left (-X) toward world +Z.
    ExpectNear(test, eyes->left.position.x, 0.0F);
    ExpectNear(test, eyes->left.position.z, 0.032F);
    ExpectNear(test, eyes->right.position.x, 0.0F);
    ExpectNear(test, eyes->right.position.z, -0.032F);
}

void TestCoordinateAndViewConversion()
{
    constexpr std::string_view test = "OpenXR to D3D8 view conversion";
    const bfvr::stereo::Vec3 converted = bfvr::stereo::OpenXRToD3D8Coordinates({1.0F, 2.0F, -3.0F});
    ExpectNear(test, converted.x, 1.0F);
    ExpectNear(test, converted.y, 2.0F);
    ExpectNear(test, converted.z, 3.0F);

    const bfvr::stereo::Pose eye{{1.0F, 2.0F, 3.0F}, {0.0F, 0.0F, 0.0F, 1.0F}};
    const auto view = bfvr::stereo::MakeD3D8ViewFromOpenXRPose(eye);
    if (!view.has_value())
    {
        Fail(test, "valid eye pose was rejected");
        return;
    }

    ExpectNear(test, view->values[3][0], -1.0F);
    ExpectNear(test, view->values[3][1], -2.0F);
    ExpectNear(test, view->values[3][2], 3.0F);
    const bfvr::stereo::Vec4 cameraOrigin =
        bfvr::stereo::TransformRowVector({1.0F, 2.0F, -3.0F, 1.0F}, *view);
    ExpectNear(test, cameraOrigin.x, 0.0F);
    ExpectNear(test, cameraOrigin.y, 0.0F);
    ExpectNear(test, cameraOrigin.z, 0.0F);
    ExpectNear(test, cameraOrigin.w, 1.0F);

    // One metre in OpenXR's forward direction (-Z) becomes one metre in
    // D3D8's forward direction (+Z) after view conversion.
    const bfvr::stereo::Vec4 forwardPoint =
        bfvr::stereo::TransformRowVector({1.0F, 2.0F, -2.0F, 1.0F}, *view);
    ExpectNear(test, forwardPoint.z, 1.0F);
}

void TestYawedViewConversion()
{
    constexpr std::string_view test = "yawed D3D8 view conversion";
    constexpr float rootHalf = 0.70710678118F;
    const bfvr::stereo::Pose eye{{0.0F, 0.0F, 0.0F}, {0.0F, rootHalf, 0.0F, rootHalf}};
    const auto view = bfvr::stereo::MakeD3D8ViewFromOpenXRPose(eye);
    if (!view.has_value())
    {
        Fail(test, "valid yawed eye pose was rejected");
        return;
    }

    // After a +90 degree yaw, the point physically in front of the eye is
    // OpenXR (-X). It must land on D3D8's positive view-Z axis.
    const bfvr::stereo::Vec4 frontOfEye =
        bfvr::stereo::TransformRowVector({-1.0F, 0.0F, 0.0F, 1.0F}, *view);
    ExpectNear(test, frontOfEye.x, 0.0F);
    ExpectNear(test, frontOfEye.y, 0.0F);
    ExpectNear(test, frontOfEye.z, 1.0F);
}

void TestProjectionConversion()
{
    constexpr std::string_view test = "asymmetric D3D8 projection conversion";
    const bfvr::stereo::FovTangents fov{-1.0F, 2.0F, 3.0F, -1.0F};
    const auto projection = bfvr::stereo::MakeD3D8ProjectionFromFovTangents(fov, 0.5F, 10.0F);
    if (!projection.has_value())
    {
        Fail(test, "valid asymmetric FOV was rejected");
        return;
    }

    ExpectNear(test, projection->values[0][0], 2.0F / 3.0F);
    ExpectNear(test, projection->values[1][1], 0.5F);
    ExpectNear(test, projection->values[2][0], -1.0F / 3.0F);
    ExpectNear(test, projection->values[2][1], -0.5F);
    ExpectNear(test, projection->values[2][2], 10.0F / 9.5F);
    ExpectNear(test, projection->values[2][3], 1.0F);
    ExpectNear(test, projection->values[3][2], -5.0F / 9.5F);

    const bfvr::stereo::Vec4 leftNear =
        bfvr::stereo::TransformRowVector({-0.5F, 0.0F, 0.5F, 1.0F}, *projection);
    const bfvr::stereo::Vec4 rightNear =
        bfvr::stereo::TransformRowVector({1.0F, 0.0F, 0.5F, 1.0F}, *projection);
    const bfvr::stereo::Vec4 topNear =
        bfvr::stereo::TransformRowVector({0.0F, 1.5F, 0.5F, 1.0F}, *projection);
    const bfvr::stereo::Vec4 bottomNear =
        bfvr::stereo::TransformRowVector({0.0F, -0.5F, 0.5F, 1.0F}, *projection);
    const bfvr::stereo::Vec4 nearPoint =
        bfvr::stereo::TransformRowVector({0.0F, 0.0F, 0.5F, 1.0F}, *projection);
    const bfvr::stereo::Vec4 farPoint =
        bfvr::stereo::TransformRowVector({0.0F, 0.0F, 10.0F, 1.0F}, *projection);

    ExpectNear(test, leftNear.x / leftNear.w, -1.0F);
    ExpectNear(test, rightNear.x / rightNear.w, 1.0F);
    ExpectNear(test, topNear.y / topNear.w, 1.0F);
    ExpectNear(test, bottomNear.y / bottomNear.w, -1.0F);
    ExpectNear(test, nearPoint.z / nearPoint.w, 0.0F);
    ExpectNear(test, farPoint.z / farPoint.w, 1.0F);
}

void TestInvalidInputRejection()
{
    constexpr std::string_view test = "invalid input rejection";
    const bfvr::stereo::Pose zeroQuaternion{{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 0.0F}};
    if (bfvr::stereo::ComputeEyePoses(zeroQuaternion, 0.064F).has_value())
    {
        Fail(test, "zero quaternion was accepted");
    }
    if (bfvr::stereo::ComputeEyePoses({}, -0.001F).has_value())
    {
        Fail(test, "negative IPD was accepted");
    }
    if (bfvr::stereo::MakeD3D8ProjectionFromFovTangents({-1.0F, -1.0F, 1.0F, -1.0F}, 0.1F, 10.0F).has_value())
    {
        Fail(test, "degenerate horizontal FOV was accepted");
    }
    if (bfvr::stereo::MakeD3D8ProjectionFromFovTangents({-1.0F, 1.0F, 1.0F, -1.0F}, 1.0F, 1.0F).has_value())
    {
        Fail(test, "zero depth interval was accepted");
    }
}

void TestDiagnosticD3D8StereoPair()
{
    constexpr std::string_view test = "diagnostic D3D8 stereo pair";
    bfvr::stereo::Matrix4 view = {};
    bfvr::stereo::Matrix4 projection = {};
    for (int index = 0; index < 4; ++index)
    {
        view.values[index][index] = 1.0F;
        projection.values[index][index] = 1.0F;
    }
    view.values[3][0] = 12.0F;
    projection.values[0][0] = 2.0F;
    projection.values[2][0] = 0.25F;

    const auto pair =
        bfvr::stereo::MakeDiagnosticD3D8StereoPair(view, projection, 0.032F, 10.0F);
    if (!pair.has_value())
    {
        Fail(test, "valid source transforms were rejected");
        return;
    }

    ExpectNear(test, pair->leftView.values[3][0], 12.032F);
    ExpectNear(test, pair->rightView.values[3][0], 11.968F);
    ExpectNear(test, pair->leftProjection.values[2][0], 0.2436F);
    ExpectNear(test, pair->rightProjection.values[2][0], 0.2564F);
    ExpectNear(test, view.values[3][0], 12.0F);
    ExpectNear(test, projection.values[2][0], 0.25F);

    if (bfvr::stereo::MakeDiagnosticD3D8StereoPair(view, projection, 0.0F, 10.0F).has_value())
    {
        Fail(test, "zero eye offset was accepted");
    }
    if (bfvr::stereo::MakeDiagnosticD3D8StereoPair(view, projection, 0.032F, 0.0F).has_value())
    {
        Fail(test, "zero convergence distance was accepted");
    }
}

void TestRuntimeFovD3D8StereoPair()
{
    constexpr std::string_view test = "runtime FOV D3D8 stereo pair";
    bfvr::stereo::Matrix4 view = {};
    for (int index = 0; index < 4; ++index)
    {
        view.values[index][index] = 1.0F;
    }
    view.values[3][0] = 12.0F;
    const bfvr::stereo::FovTangents sourceFov{-1.0F, 1.0F, 1.0F, -1.0F};
    const auto sourceProjection =
        bfvr::stereo::MakeD3D8ProjectionFromFovTangents(
            sourceFov,
            0.1F,
            100.0F);
    const bfvr::stereo::Pose leftEye{
        {-0.032F, 1.7F, 0.0F},
        {0.0F, 0.0F, 0.0F, 1.0F}};
    const bfvr::stereo::Pose rightEye{
        {0.032F, 1.7F, 0.0F},
        {0.0F, 0.0F, 0.0F, 1.0F}};
    const bfvr::stereo::FovTangents leftFov{-1.2F, 0.9F, 1.1F, -1.0F};
    const bfvr::stereo::FovTangents rightFov{-0.9F, 1.2F, 1.1F, -1.0F};
    const auto pair = sourceProjection.has_value()
        ? bfvr::stereo::MakeRuntimeFovD3D8StereoPair(
            view,
            *sourceProjection,
            leftEye,
            rightEye,
            leftFov,
            rightFov,
            1.0F)
        : std::nullopt;
    if (!pair.has_value())
    {
        Fail(test, "valid runtime eye pair was rejected");
        return;
    }

    ExpectNear(test, pair->leftView.values[3][0], 12.032F);
    ExpectNear(test, pair->rightView.values[3][0], 11.968F);
    const auto expectedLeft =
        bfvr::stereo::MakeD3D8ProjectionFromFovTangents(
            leftFov,
            0.1F,
            100.0F);
    const auto expectedRight =
        bfvr::stereo::MakeD3D8ProjectionFromFovTangents(
            rightFov,
            0.1F,
            100.0F);
    ExpectNear(
        test,
        pair->leftProjection.values[2][0],
        expectedLeft->values[2][0]);
    ExpectNear(
        test,
        pair->rightProjection.values[2][0],
        expectedRight->values[2][0]);
    if (bfvr::stereo::MakeRuntimeFovD3D8StereoPair(
            view,
            *sourceProjection,
            leftEye,
            leftEye,
            leftFov,
            rightFov,
            1.0F).has_value())
    {
        Fail(test, "co-located runtime eyes were accepted");
    }
}

void TestRuntimePoseD3D8StereoPair()
{
    constexpr std::string_view test = "runtime pose D3D8 stereo pair";
    bfvr::stereo::Matrix4 view = {};
    for (int index = 0; index < 4; ++index)
    {
        view.values[index][index] = 1.0F;
    }
    const bfvr::stereo::FovTangents fov{-1.0F, 1.0F, 1.0F, -1.0F};
    const auto projection =
        bfvr::stereo::MakeD3D8ProjectionFromFovTangents(
            fov,
            0.1F,
            100.0F);
    const bfvr::stereo::Pose referenceHead{
        {1.0F, 2.0F, 3.0F},
        {0.0F, 0.0F, 0.0F, 1.0F}};
    const auto neutralEyes =
        bfvr::stereo::ComputeEyePoses(referenceHead, 0.064F);
    const auto neutralPair =
        projection.has_value() && neutralEyes.has_value()
        ? bfvr::stereo::MakeRuntimePoseD3D8StereoPair(
            view,
            *projection,
            referenceHead,
            neutralEyes->left,
            neutralEyes->right,
            fov,
            fov,
            1.0F)
        : std::nullopt;
    if (!neutralPair.has_value())
    {
        Fail(test, "neutral runtime pose pair was rejected");
        return;
    }
    ExpectNear(test, neutralPair->leftView.values[3][0], 0.032F);
    ExpectNear(test, neutralPair->rightView.values[3][0], -0.032F);

    constexpr float rootHalf = 0.70710678118F;
    const bfvr::stereo::Pose movedHead{
        {1.0F, 2.0F, 2.75F},
        {0.0F, rootHalf, 0.0F, rootHalf}};
    const auto movedEyes =
        bfvr::stereo::ComputeEyePoses(movedHead, 0.064F);
    const auto movedPair =
        movedEyes.has_value()
        ? bfvr::stereo::MakeRuntimePoseD3D8StereoPair(
            view,
            *projection,
            referenceHead,
            movedEyes->left,
            movedEyes->right,
            fov,
            fov,
            1.0F)
        : std::nullopt;
    if (!movedPair.has_value())
    {
        Fail(test, "translated and yawed runtime pose pair was rejected");
        return;
    }

    // Relative to the neutral pose, +90 degree yaw turns physical forward
    // toward OpenXR -X. These baseline-space points are one metre in front
    // of each current eye and must land on each D3D8 eye's +Z axis.
    const bfvr::stereo::Vec4 leftFront =
        bfvr::stereo::TransformRowVector(
            {-1.0F, 0.0F, 0.218F, 1.0F},
            movedPair->leftView);
    const bfvr::stereo::Vec4 rightFront =
        bfvr::stereo::TransformRowVector(
            {-1.0F, 0.0F, 0.282F, 1.0F},
            movedPair->rightView);
    ExpectNear(test, leftFront.x, 0.0F);
    ExpectNear(test, leftFront.y, 0.0F);
    ExpectNear(test, leftFront.z, 1.0F);
    ExpectNear(test, rightFront.x, 0.0F);
    ExpectNear(test, rightFront.y, 0.0F);
    ExpectNear(test, rightFront.z, 1.0F);
}

void TestRuntimeHeadCameraComposition()
{
    constexpr std::string_view test = "runtime head camera composition";
    bfvr::stereo::Matrix4 sourceCamera = {};
    for (int index = 0; index < 4; ++index)
    {
        sourceCamera.values[index][index] = 1.0F;
    }
    sourceCamera.values[3][0] = 10.0F;
    sourceCamera.values[3][1] = 20.0F;
    sourceCamera.values[3][2] = 30.0F;
    const bfvr::stereo::Pose referenceHead{
        {1.0F, 2.0F, 3.0F},
        {0.0F, 0.0F, 0.0F, 1.0F}};
    const auto neutral =
        bfvr::stereo::ComposeRuntimeHeadWithD3D8Camera(
            sourceCamera,
            referenceHead,
            referenceHead,
            1.0F);
    if (!neutral.has_value())
    {
        Fail(test, "neutral head pose was rejected");
        return;
    }
    ExpectNear(test, neutral->values[3][0], 10.0F);
    ExpectNear(test, neutral->values[3][1], 20.0F);
    ExpectNear(test, neutral->values[3][2], 30.0F);

    constexpr float rootHalf = 0.70710678118F;
    const bfvr::stereo::Pose movedHead{
        {1.0F, 2.0F, 2.75F},
        {0.0F, rootHalf, 0.0F, rootHalf}};
    const auto moved =
        bfvr::stereo::ComposeRuntimeHeadWithD3D8Camera(
            sourceCamera,
            referenceHead,
            movedHead,
            1.0F);
    if (!moved.has_value())
    {
        Fail(test, "translated and yawed head pose was rejected");
        return;
    }
    const auto cameraOrigin =
        bfvr::stereo::TransformRowVector(
            {0.0F, 0.0F, 0.0F, 1.0F},
            *moved);
    const auto cameraForward =
        bfvr::stereo::TransformRowVector(
            {0.0F, 0.0F, 1.0F, 1.0F},
            *moved);
    ExpectNear(test, cameraOrigin.x, 10.0F);
    ExpectNear(test, cameraOrigin.y, 20.0F);
    ExpectNear(test, cameraOrigin.z, 30.25F);
    ExpectNear(test, cameraForward.x, 9.0F);
    ExpectNear(test, cameraForward.y, 20.0F);
    ExpectNear(test, cameraForward.z, 30.25F);
}

void TestD3D8DrawPolicy()
{
    constexpr std::string_view test = "D3D8 draw policy";
    const auto perspective = bfvr::stereo::MakeD3D8ProjectionFromFovTangents(
        {-1.0F, 1.0F, 1.0F, -1.0F},
        0.1F,
        1000.0F);
    if (!perspective.has_value())
    {
        Fail(test, "test perspective could not be built");
        return;
    }

    using bfvr::stereo::D3D8DrawPolicy;
    if (bfvr::stereo::ClassifyD3D8DrawPolicy(0x112, *perspective) !=
        D3D8DrawPolicy::StereoPerspective)
    {
        Fail(test, "ordinary perspective FVF was not classified as stereo");
    }
    if (bfvr::stereo::ClassifyD3D8DrawPolicy(0x144, *perspective) !=
        D3D8DrawPolicy::MonoPretransformed)
    {
        Fail(test, "observed XYZRHW FVF was not classified as monoscopic");
    }
    if (bfvr::stereo::ClassifyD3D8DrawPolicy(0x80000004, *perspective) !=
        D3D8DrawPolicy::StereoPerspective)
    {
        Fail(test, "shader handle was mistaken for an XYZRHW FVF");
    }

    bfvr::stereo::Matrix4 orthographic = {};
    for (int index = 0; index < 4; ++index)
    {
        orthographic.values[index][index] = 1.0F;
    }
    if (bfvr::stereo::ClassifyD3D8DrawPolicy(0x112, orthographic) !=
        D3D8DrawPolicy::MonoNonPerspective)
    {
        Fail(test, "non-perspective projection was not classified as monoscopic");
    }
    if (!bfvr::stereo::UsesStereoTransforms(D3D8DrawPolicy::StereoPerspective) ||
        bfvr::stereo::UsesStereoTransforms(D3D8DrawPolicy::MonoPretransformed) ||
        bfvr::stereo::UsesStereoTransforms(D3D8DrawPolicy::MonoNonPerspective))
    {
        Fail(test, "stereo-transform policy mapping is inconsistent");
    }
}

void TestBF1942SemanticDrawPolicy()
{
    constexpr std::string_view test = "BF1942 semantic draw policy";
    using bfvr::stereo::D3D8SemanticDrawClass;
    bfvr::stereo::BF1942D3D8DrawSignature signature = {
        0x0066800A,
        0x0062B83F,
        true,
        true,
        4,
        2,
        0x112,
        0,
        1,
        0,
        0,
        0};
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::SkyboxCubeFace)
    {
        Fail(test, "exact profiled skybox cube-face signature was rejected");
    }

    signature.primitiveCount = 3;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Unclassified)
    {
        Fail(test, "different primitive count did not fail closed");
    }
    signature.primitiveCount = 2;
    signature.alphaBlendEnable = 1;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Unclassified)
    {
        Fail(test, "different render state did not fail closed");
    }
    signature.alphaBlendEnable = 0;
    signature.rendererReturnAddress = 0x0062B840;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Unclassified)
    {
        Fail(test, "different renderer return did not fail closed");
    }

    signature = {
        0x00667EF4,
        0x0064D84C,
        false,
        true,
        4,
        4,
        0x2C2,
        1,
        0,
        1,
        1,
        0};
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::BillboardBatch)
    {
        Fail(test, "exact profiled billboard-batch signature was rejected");
    }
    signature.lighting = 1;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Unclassified)
    {
        Fail(test, "different billboard lighting state did not fail closed");
    }

    signature = {
        0x0066800A,
        0x0067C997,
        true,
        true,
        4,
        200,
        0x252,
        1,
        0,
        1,
        1,
        1};
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::TreeMeshAlphaBlock)
    {
        Fail(test, "exact profiled TreeMesh alpha-block signature was rejected");
    }
    signature.zWriteEnable = 1;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Unclassified)
    {
        Fail(test, "different TreeMesh depth-write state did not fail closed");
    }

    signature = {
        0x0066800A,
        0x0067C997,
        true,
        true,
        4,
        121,
        0x8B737890,
        1,
        0,
        1,
        1,
        1,
        0,
        0xC3A0389DCCB0E1B0ULL,
        2256,
        10};
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::TreeMeshProgrammableSprite)
    {
        Fail(test, "exact translated TreeMesh sprite signature was rejected");
    }
    signature.originalVertexShaderCreationOrdinal = 9;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::TreeMeshProgrammableSprite)
    {
        Fail(test, "runtime-variable TreeMesh sprite ordinal was rejected");
    }
    signature.originalVertexShaderCreationOrdinal = 10;
    signature.originalVertexShaderByteCount = 2252;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Unclassified)
    {
        Fail(test, "different TreeMesh sprite byte count did not fail closed");
    }

    signature = {
        0x0066800A,
        0x005AF40F,
        true,
        true,
        4,
        2959,
        0x17,
        1,
        1,
        0,
        1,
        1,
        0};
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::AnimatedMeshSkinning)
    {
        Fail(test, "exact profiled animated-mesh skinning signature was rejected");
    }
    signature.primitiveType = 5;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::AnimatedMeshSkinning)
    {
        Fail(test, "live animated-mesh triangle-strip signature was rejected");
    }
    signature.primitiveType = 6;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Unclassified)
    {
        Fail(test, "unobserved animated-mesh primitive type did not fail closed");
    }
    signature.primitiveType = 4;
    signature.vertexShaderOrFvf = 0x18;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Unclassified)
    {
        Fail(test, "different animated-mesh shader handle did not fail closed");
    }
    signature.vertexShaderOrFvf = 0x8B737890;
    signature.originalVertexShaderHash = 0x64F6D635ADDB7328ULL;
    signature.originalVertexShaderByteCount = 1836;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::AnimatedMeshSkinning)
    {
        Fail(test, "observed translated skinning-shader identity was rejected");
    }
    signature.originalVertexShaderByteCount = 1832;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Unclassified)
    {
        Fail(test, "translated skinning-shader byte count did not fail closed");
    }

    signature = {
        0x0066800A,
        0x0062E8BE,
        true,
        true,
        4,
        178,
        0x8B737890,
        1,
        0,
        1,
        1,
        0,
        0,
        0xEE5AA9145B53F1BEULL,
        3056,
        5};
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::TranslucentSprite)
    {
        Fail(test, "exact translated translucent-sprite signature was rejected");
    }
    signature.originalVertexShaderHash = 0;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Unclassified)
    {
        Fail(test, "sprite draw without original shader identity did not fail closed");
    }
    signature.originalVertexShaderHash = 0xEE5AA9145B53F1BEULL;
    signature.originalVertexShaderCreationOrdinal = 4;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Unclassified)
    {
        Fail(test, "different sprite creation ordinal did not fail closed");
    }
    signature.originalVertexShaderCreationOrdinal = 5;
    signature.rendererReturnAddress = 0x0062E8BF;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Unclassified)
    {
        Fail(test, "different sprite renderer return did not fail closed");
    }

    signature = {
        0x00667EF4,
        0x0065D140,
        false,
        false,
        4,
        28,
        0x144,
        0,
        1,
        1,
        0,
        0};
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Ref2FontGlyphBatch)
    {
        Fail(test, "exact profiled Ref2 font signature was rejected");
    }
    signature.wrapperReturnAddress = 0x00667EF5;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Unclassified)
    {
        Fail(test, "different Ref2 font wrapper return did not fail closed");
    }

    signature = {
        0x00667DFD,
        0x00664CF6,
        false,
        false,
        4,
        6,
        0x142,
        0,
        0,
        1,
        0,
        0};
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Ref2MenuQuad)
    {
        Fail(test, "exact profiled Ref2 menu batch signature was rejected");
    }
    signature.rendererReturnAddress = 0x00666018;
    signature.primitiveType = 6;
    signature.zEnable = 1;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Ref2MenuQuad)
    {
        Fail(test, "exact profiled Ref2 clipped menu signature was rejected");
    }
    signature.primitiveType = 4;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Unclassified)
    {
        Fail(test, "different Ref2 menu primitive type did not fail closed");
    }
    signature.rendererReturnAddress = 0x007EBFF6;
    signature.producerReturnAddress = 0x00664CF6;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Ref2MenuQuad)
    {
        Fail(test, "exact nested Ref2 menu flush signature was rejected");
    }
    signature.producerReturnAddress = 0x00664CF7;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Unclassified)
    {
        Fail(test, "different nested Ref2 producer return did not fail closed");
    }

    signature = {
        0x0066800A,
        0x00665098,
        true,
        false,
        4,
        900,
        0x142,
        1,
        1,
        0,
        0,
        0};
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Ref2MenuQuad)
    {
        Fail(test, "exact profiled Ref2 cached-menu signature was rejected");
    }
    signature.fogEnable = 1;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Unclassified)
    {
        Fail(test, "different Ref2 cached-menu state did not fail closed");
    }
}

void TestD3D8FrameCompositionPolicy()
{
    constexpr std::string_view test = "D3D8 frame composition policy";
    using bfvr::stereo::D3D8FrameCompositionLayer;
    using bfvr::stereo::D3D8SemanticDrawClass;
    if (bfvr::stereo::SelectD3D8FrameCompositionLayer(
            D3D8SemanticDrawClass::Ref2MenuQuad) !=
        D3D8FrameCompositionLayer::Ref2Ui)
    {
        Fail(test, "Ref2 menu quad was not routed to the UI layer");
    }
    if (bfvr::stereo::SelectD3D8FrameCompositionLayer(
            D3D8SemanticDrawClass::Ref2FontGlyphBatch) !=
        D3D8FrameCompositionLayer::Ref2Ui)
    {
        Fail(test, "Ref2 font batch was not routed to the UI layer");
    }
    if (bfvr::stereo::SelectD3D8FrameCompositionLayer(
            D3D8SemanticDrawClass::SkyboxCubeFace) !=
            D3D8FrameCompositionLayer::WorldEyes ||
        bfvr::stereo::SelectD3D8FrameCompositionLayer(
            D3D8SemanticDrawClass::BillboardBatch) !=
            D3D8FrameCompositionLayer::WorldEyes ||
        bfvr::stereo::SelectD3D8FrameCompositionLayer(
            D3D8SemanticDrawClass::TreeMeshAlphaBlock) !=
            D3D8FrameCompositionLayer::WorldEyes ||
        bfvr::stereo::SelectD3D8FrameCompositionLayer(
            D3D8SemanticDrawClass::TreeMeshProgrammableSprite) !=
            D3D8FrameCompositionLayer::WorldEyes ||
        bfvr::stereo::SelectD3D8FrameCompositionLayer(
            D3D8SemanticDrawClass::AnimatedMeshSkinning) !=
            D3D8FrameCompositionLayer::WorldEyes ||
        bfvr::stereo::SelectD3D8FrameCompositionLayer(
            D3D8SemanticDrawClass::TranslucentSprite) !=
            D3D8FrameCompositionLayer::WorldEyes ||
        bfvr::stereo::SelectD3D8FrameCompositionLayer(
            D3D8SemanticDrawClass::Unclassified) !=
            D3D8FrameCompositionLayer::WorldEyes)
    {
        Fail(test, "non-UI semantic class escaped the world-eye layer");
    }
}

void TestD3D8SkinningShaderConstants()
{
    constexpr std::string_view test = "D3D8 skinning shader constants";
    bfvr::stereo::Matrix4 world = {};
    world.values = {{
        {{2.0F, 0.0F, 0.0F, 0.0F}},
        {{0.0F, 3.0F, 0.0F, 0.0F}},
        {{0.0F, 0.0F, 4.0F, 0.0F}},
        {{5.0F, 6.0F, 7.0F, 1.0F}}}};
    bfvr::stereo::Matrix4 view = {};
    view.values = {{
        {{1.0F, 0.0F, 0.0F, 0.0F}},
        {{0.0F, 1.0F, 0.0F, 0.0F}},
        {{0.0F, 0.0F, 1.0F, 0.0F}},
        {{-1.0F, -2.0F, -3.0F, 1.0F}}}};
    bfvr::stereo::Matrix4 projection = {};
    projection.values = {{
        {{10.0F, 0.0F, 0.0F, 0.0F}},
        {{0.0F, 20.0F, 0.0F, 0.0F}},
        {{0.0F, 0.0F, 30.0F, 0.0F}},
        {{0.0F, 0.0F, 0.0F, 1.0F}}}};

    const auto constants = bfvr::stereo::MakeD3D8SkinningShaderConstants(
        world,
        view,
        projection);
    if (!constants.has_value())
    {
        Fail(test, "finite transforms were rejected");
        return;
    }
    const float expected[4][4] = {
        {20.0F, 0.0F, 0.0F, 40.0F},
        {0.0F, 60.0F, 0.0F, 80.0F},
        {0.0F, 0.0F, 120.0F, 120.0F},
        {0.0F, 0.0F, 0.0F, 1.0F}};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            ExpectNear(
                test,
                constants->values[row][column],
                expected[row][column]);
        }
    }

    world.values[0][0] = std::numeric_limits<float>::quiet_NaN();
    if (bfvr::stereo::MakeD3D8SkinningShaderConstants(
            world,
            view,
            projection).has_value())
    {
        Fail(test, "non-finite world transform was accepted");
    }
}

void TestD3D8SpriteShaderConstants()
{
    constexpr std::string_view test = "D3D8 sprite shader constants";
    bfvr::stereo::Matrix4 view = {};
    view.values = {{
        {{0.0F, 0.0F, 1.0F, 0.0F}},
        {{0.0F, 1.0F, 0.0F, 0.0F}},
        {{-1.0F, 0.0F, 0.0F, 0.0F}},
        {{3.0F, -2.0F, 5.0F, 1.0F}}}};
    bfvr::stereo::Matrix4 projection = {};
    projection.values = {{
        {{2.0F, 0.0F, 0.0F, 0.0F}},
        {{0.0F, 3.0F, 0.0F, 0.0F}},
        {{0.25F, -0.5F, 1.1F, 1.0F}},
        {{0.0F, 0.0F, -0.1F, 0.0F}}}};
    const auto constants =
        bfvr::stereo::MakeD3D8SpriteShaderConstants(
            view,
            projection,
            7.0F);
    if (!constants.has_value())
    {
        Fail(test, "finite sprite transforms were rejected");
        return;
    }
    ExpectNear(test, constants->rotationOnlyView.values[0][2], -1.0F);
    ExpectNear(test, constants->rotationOnlyView.values[2][0], 1.0F);
    ExpectNear(test, constants->rotationOnlyView.values[0][3], 0.0F);
    ExpectNear(test, constants->projection.values[0][2], 0.25F);
    ExpectNear(test, constants->projection.values[1][2], -0.5F);
    ExpectNear(test, constants->cameraPosition.x, -5.0F);
    ExpectNear(test, constants->cameraPosition.y, 2.0F);
    ExpectNear(test, constants->cameraPosition.z, 3.0F);
    ExpectNear(test, constants->cameraPosition.w, 7.0F);
}

void TestD3D8TreeSpriteShaderConstants()
{
    constexpr std::string_view test = "D3D8 tree sprite shader constants";
    bfvr::stereo::Matrix4 world = {};
    bfvr::stereo::Matrix4 view = {};
    bfvr::stereo::Matrix4 projection = {};
    for (std::size_t index = 0; index < 4; ++index)
    {
        world.values[index][index] = 1.0F;
        view.values[index][index] = 1.0F;
        projection.values[index][index] = 1.0F;
    }
    world.values[3][0] = 5.0F;
    world.values[3][1] = 6.0F;
    world.values[3][2] = 7.0F;
    view.values[3][0] = -1.0F;
    view.values[3][1] = -2.0F;
    view.values[3][2] = -3.0F;
    projection.values[2][0] = -0.25F;

    const auto constants =
        bfvr::stereo::MakeD3D8TreeSpriteShaderConstants(
            world,
            view,
            projection);
    if (!constants.has_value())
    {
        Fail(test, "finite tree sprite transforms were rejected");
        return;
    }
    ExpectNear(test, constants->worldView.values[0][3], 4.0F);
    ExpectNear(test, constants->worldView.values[1][3], 4.0F);
    ExpectNear(test, constants->worldView.values[2][3], 4.0F);
    ExpectNear(test, constants->projection.values[0][2], -0.25F);

    world.values[0][0] = std::numeric_limits<float>::infinity();
    if (bfvr::stereo::MakeD3D8TreeSpriteShaderConstants(
            world,
            view,
            projection).has_value())
    {
        Fail(test, "non-finite tree world transform was accepted");
    }
}

void TestD3D8SkinningShaderOverrideLifecycle()
{
    constexpr std::string_view test = "D3D8 skinning shader override lifecycle";
    bfvr::d3d8probe::D3DMatrix identity = {};
    for (std::size_t index = 0; index < 4; ++index)
    {
        identity.values[index][index] = 1.0F;
    }
    bfvr::d3d8probe::D3DMatrix leftView = identity;
    bfvr::d3d8probe::D3DMatrix rightView = identity;
    leftView.values[3][0] = -0.032F;
    rightView.values[3][0] = 0.032F;
    FakeShaderConstantDevice device = {identity};
    const bfvr::d3d8probe::D3D8VertexShaderConstantApi api = {
        &SetFakeShaderConstants,
        &GetFakeShaderConstants};
    bfvr::d3d8probe::D3D8SkinningShaderTransformState state = {};
    const auto prepareResult =
        bfvr::d3d8probe::PrepareD3D8SkinningShaderTransforms(
            api,
            &device,
            identity,
            identity,
            identity,
            leftView,
            identity,
            rightView,
            identity,
            state);
    if (prepareResult !=
        bfvr::d3d8probe::SkinningShaderPrepareResult::Prepared)
    {
        Fail(test, "valid source constants were rejected");
        return;
    }
    if (FAILED(bfvr::d3d8probe::ApplyD3D8SkinningShaderEye(
            api,
            &device,
            state,
            0)))
    {
        Fail(test, "left-eye constants were not applied");
        return;
    }
    ExpectNear(test, device.constants.values[0][3], -0.032F);
    if (!bfvr::d3d8probe::RestoreAndVerifyD3D8SkinningShaderConstants(
            api,
            &device,
            state) ||
        std::memcmp(&device.constants, &identity, sizeof(identity)) != 0)
    {
        Fail(test, "source constants were not restored exactly");
    }

    device.constants.values[0][0] = 2.0F;
    if (bfvr::d3d8probe::PrepareD3D8SkinningShaderTransforms(
            api,
            &device,
            identity,
            identity,
            identity,
            leftView,
            identity,
            rightView,
            identity,
            state) !=
        bfvr::d3d8probe::SkinningShaderPrepareResult::SourceConstantsMismatch)
    {
        Fail(test, "mismatched engine constants did not fail closed");
    }
}

void TestD3D8SpriteShaderOverrideLifecycle()
{
    constexpr std::string_view test = "D3D8 sprite shader override lifecycle";
    bfvr::d3d8probe::D3DMatrix sourceView = {};
    bfvr::d3d8probe::D3DMatrix projection = {};
    for (std::size_t index = 0; index < 4; ++index)
    {
        sourceView.values[index][index] = 1.0F;
        projection.values[index][index] = 1.0F;
    }
    sourceView.values[3][0] = -1.0F;
    sourceView.values[3][1] = -2.0F;
    sourceView.values[3][2] = -3.0F;
    bfvr::d3d8probe::D3DMatrix leftView = sourceView;
    bfvr::d3d8probe::D3DMatrix rightView = sourceView;
    leftView.values[3][0] += 0.032F;
    rightView.values[3][0] -= 0.032F;
    bfvr::d3d8probe::D3DMatrix leftProjection = projection;
    bfvr::d3d8probe::D3DMatrix rightProjection = projection;
    leftProjection.values[2][0] = -0.1F;
    rightProjection.values[2][0] = 0.1F;

    bfvr::stereo::Matrix4 sourceViewMatrix = {};
    bfvr::stereo::Matrix4 projectionMatrix = {};
    std::memcpy(&sourceViewMatrix, &sourceView, sizeof(sourceViewMatrix));
    std::memcpy(&projectionMatrix, &projection, sizeof(projectionMatrix));
    const auto sourceConstants =
        bfvr::stereo::MakeD3D8SpriteShaderConstants(
            sourceViewMatrix,
            projectionMatrix,
            1.0F);
    if (!sourceConstants.has_value())
    {
        Fail(test, "source constants could not be built");
        return;
    }

    FakeSpriteShaderConstantDevice device = {};
    std::memcpy(
        &device.registers[0][0],
        &sourceConstants->rotationOnlyView,
        sizeof(sourceConstants->rotationOnlyView));
    std::memcpy(
        &device.registers[4][0],
        &sourceConstants->projection,
        sizeof(sourceConstants->projection));
    device.registers[8][0] = 11.0F;
    device.registers[8][1] = 12.0F;
    device.registers[8][2] = 13.0F;
    device.registers[8][3] = 14.0F;
    device.registers[9][0] = sourceConstants->cameraPosition.x;
    device.registers[9][1] = sourceConstants->cameraPosition.y;
    device.registers[9][2] = sourceConstants->cameraPosition.z;
    device.registers[9][3] = sourceConstants->cameraPosition.w;
    const FakeSpriteShaderConstantDevice original = device;

    const bfvr::d3d8probe::D3D8VertexShaderConstantApi api = {
        &SetFakeSpriteShaderConstants,
        &GetFakeSpriteShaderConstants};
    bfvr::d3d8probe::D3D8SpriteShaderTransformState state = {};
    const auto result =
        bfvr::d3d8probe::PrepareD3D8SpriteShaderTransforms(
            api,
            &device,
            sourceView,
            projection,
            leftView,
            leftProjection,
            rightView,
            rightProjection,
            state);
    if (result != bfvr::d3d8probe::SpriteShaderPrepareResult::Prepared ||
        FAILED(bfvr::d3d8probe::ApplyD3D8SpriteShaderEye(
            api,
            &device,
            state,
            0)))
    {
        Fail(test, "valid sprite constants were not prepared and applied");
        return;
    }
    ExpectNear(test, device.registers[4][2], -0.1F);
    ExpectNear(test, device.registers[9][0], 0.968F);
    ExpectNear(test, device.registers[8][0], 11.0F);
    if (!bfvr::d3d8probe::RestoreAndVerifyD3D8SpriteShaderConstants(
            api,
            &device,
            state) ||
        std::memcmp(&device, &original, sizeof(device)) != 0)
    {
        Fail(test, "sprite registers c0-c9 were not restored exactly");
    }

    device.registers[9][0] += 1.0F;
    if (bfvr::d3d8probe::PrepareD3D8SpriteShaderTransforms(
            api,
            &device,
            sourceView,
            projection,
            leftView,
            leftProjection,
            rightView,
            rightProjection,
            state) !=
        bfvr::d3d8probe::SpriteShaderPrepareResult::SourceConstantsMismatch)
    {
        Fail(test, "mismatched sprite camera constant did not fail closed");
    }
}

void TestD3D8TreeSpriteShaderOverrideLifecycle()
{
    constexpr std::string_view test =
        "D3D8 tree sprite shader override lifecycle";
    bfvr::d3d8probe::D3DMatrix world = {};
    bfvr::d3d8probe::D3DMatrix sourceView = {};
    bfvr::d3d8probe::D3DMatrix projection = {};
    for (std::size_t index = 0; index < 4; ++index)
    {
        world.values[index][index] = 1.0F;
        sourceView.values[index][index] = 1.0F;
        projection.values[index][index] = 1.0F;
    }
    world.values[3][0] = 5.0F;
    sourceView.values[3][0] = -1.0F;
    bfvr::d3d8probe::D3DMatrix leftView = sourceView;
    bfvr::d3d8probe::D3DMatrix rightView = sourceView;
    leftView.values[3][0] += 0.032F;
    rightView.values[3][0] -= 0.032F;
    bfvr::d3d8probe::D3DMatrix leftProjection = projection;
    bfvr::d3d8probe::D3DMatrix rightProjection = projection;
    leftProjection.values[2][0] = -0.1F;
    rightProjection.values[2][0] = 0.1F;

    bfvr::stereo::Matrix4 worldMatrix = {};
    bfvr::stereo::Matrix4 sourceViewMatrix = {};
    bfvr::stereo::Matrix4 projectionMatrix = {};
    std::memcpy(&worldMatrix, &world, sizeof(worldMatrix));
    std::memcpy(
        &sourceViewMatrix,
        &sourceView,
        sizeof(sourceViewMatrix));
    std::memcpy(
        &projectionMatrix,
        &projection,
        sizeof(projectionMatrix));
    const auto sourceConstants =
        bfvr::stereo::MakeD3D8TreeSpriteShaderConstants(
            worldMatrix,
            sourceViewMatrix,
            projectionMatrix);
    if (!sourceConstants.has_value())
    {
        Fail(test, "source tree constants could not be built");
        return;
    }

    FakeSpriteShaderConstantDevice device = {};
    std::memcpy(
        &device.registers[0][0],
        &sourceConstants->worldView,
        sizeof(sourceConstants->worldView));
    std::memcpy(
        &device.registers[4][0],
        &sourceConstants->projection,
        sizeof(sourceConstants->projection));
    device.registers[8][0] = 41.0F;
    device.registers[8][1] = 42.0F;
    device.registers[8][2] = 43.0F;
    device.registers[8][3] = 44.0F;
    const FakeSpriteShaderConstantDevice original = device;

    const bfvr::d3d8probe::D3D8VertexShaderConstantApi api = {
        &SetFakeSpriteShaderConstants,
        &GetFakeSpriteShaderConstants};
    bfvr::d3d8probe::D3D8TreeSpriteShaderTransformState state = {};
    const auto result =
        bfvr::d3d8probe::PrepareD3D8TreeSpriteShaderTransforms(
            api,
            &device,
            world,
            sourceView,
            projection,
            leftView,
            leftProjection,
            rightView,
            rightProjection,
            state);
    if (result !=
            bfvr::d3d8probe::TreeSpriteShaderPrepareResult::Prepared ||
        FAILED(bfvr::d3d8probe::ApplyD3D8TreeSpriteShaderEye(
            api,
            &device,
            state,
            0)))
    {
        Fail(test, "valid tree sprite constants were not prepared and applied");
        return;
    }
    ExpectNear(test, device.registers[0][3], 4.032F);
    ExpectNear(test, device.registers[4][2], -0.1F);
    ExpectNear(test, device.registers[8][0], 41.0F);
    if (!bfvr::d3d8probe::RestoreAndVerifyD3D8TreeSpriteShaderConstants(
            api,
            &device,
            state) ||
        std::memcmp(&device, &original, sizeof(device)) != 0)
    {
        Fail(test, "tree sprite registers c0-c7 were not restored exactly");
    }

    device.registers[0][0] += 1.0F;
    if (bfvr::d3d8probe::PrepareD3D8TreeSpriteShaderTransforms(
            api,
            &device,
            world,
            sourceView,
            projection,
            leftView,
            leftProjection,
            rightView,
            rightProjection,
            state) !=
        bfvr::d3d8probe::TreeSpriteShaderPrepareResult::SourceConstantsMismatch)
    {
        Fail(test, "mismatched tree sprite constants did not fail closed");
    }
}

void TestBF1942TreeAngleSlicePolicy()
{
    constexpr std::string_view test = "BF1942 tree angle slice policy";
    const bfvr::stereo::BF1942TreeAngleSliceContext context = {
        {0.0F, 0.0F, 0.0F},
        1.0F,
        0.0F,
        0.0F,
        1.0F,
        4,
        1};
    const auto centre =
        bfvr::stereo::SelectBF1942TreeAngleSlice(
            context,
            {1.0F, 0.0F, 0.0F});
    const auto quarterTurn =
        bfvr::stereo::SelectBF1942TreeAngleSlice(
            context,
            {0.0F, 0.0F, 1.0F});
    const auto opposite =
        bfvr::stereo::SelectBF1942TreeAngleSlice(
            context,
            {-1.0F, 0.0F, 0.0F});
    if (centre != 1U || quarterTurn != 0U || opposite != 3U)
    {
        Fail(test, "camera directions selected the wrong retail angle slice");
    }

    const auto remapped =
        bfvr::stereo::RemapBF1942TreeAngleSliceStartIndex(
            106,
            2,
            1,
            3);
    if (remapped != 118U)
    {
        Fail(test, "eye remap did not preserve the angle-zero index base");
    }
    if (bfvr::stereo::RemapBF1942TreeAngleSliceStartIndex(
            5,
            2,
            1,
            0)
            .has_value() ||
        bfvr::stereo::SelectBF1942TreeAngleSlice(
            context,
            context.origin)
            .has_value())
    {
        Fail(test, "invalid slice inputs did not fail closed");
    }
    auto zeroAxis = context;
    zeroAxis.angleAxisX = 0.0F;
    zeroAxis.angleAxisZ = 0.0F;
    if (bfvr::stereo::SelectBF1942TreeAngleSlice(
            zeroAxis,
            {1.0F, 0.0F, 0.0F})
            .has_value())
    {
        Fail(test, "zero orientation axis did not fail closed");
    }
}

void TestStereoFrameResourceReuseReset()
{
    constexpr std::string_view test = "stereo frame resource reuse reset";
    bfvr::d3d8probe::StereoFrameRecord record = {};
    record.ownedColor[0] = reinterpret_cast<void*>(1);
    record.ownedColor[1] = reinterpret_cast<void*>(2);
    record.ownedDepth[0] = reinterpret_cast<void*>(3);
    record.ownedDepth[1] = reinterpret_cast<void*>(4);
    record.menuColor = reinterpret_cast<void*>(5);
    record.menuDepth = reinterpret_cast<void*>(6);
    record.reusableReadback[0] = reinterpret_cast<void*>(7);
    record.reusableReadback[1] = reinterpret_cast<void*>(8);
    record.reusableReadback[2] = reinterpret_cast<void*>(9);
    record.colorDescription = {21, 1, 1, 0, 0, 0, 1872, 2016};
    record.depthDescription = {75, 1, 2, 0, 0, 0, 1872, 2016};
    record.menuColorDescription = {21, 1, 1, 0, 0, 0, 1920, 1080};
    record.reusableReadbackDescription[0] =
        {21, 1, 0, 1, 0, 0, 1872, 2016};
    record.reusableReadbackDescription[1] =
        record.reusableReadbackDescription[0];
    record.reusableReadbackDescription[2] =
        {21, 1, 0, 1, 0, 0, 1920, 1080};
    record.resourcesReady = 1;
    record.mirroredDraws = 400;
    record.restoreFailures = 2;
    record.allRestorationsExact = FALSE;
    bfvr::d3d8probe::ResetStereoFrameRecordForResourceReuse(record);
    if (record.ownedColor[0] != reinterpret_cast<void*>(1) ||
        record.ownedColor[1] != reinterpret_cast<void*>(2) ||
        record.ownedDepth[0] != reinterpret_cast<void*>(3) ||
        record.ownedDepth[1] != reinterpret_cast<void*>(4) ||
        record.menuColor != reinterpret_cast<void*>(5) ||
        record.menuDepth != reinterpret_cast<void*>(6) ||
        record.reusableReadback[0] != reinterpret_cast<void*>(7) ||
        record.reusableReadback[1] != reinterpret_cast<void*>(8) ||
        record.reusableReadback[2] != reinterpret_cast<void*>(9))
    {
        Fail(test, "owned resource identity was not preserved");
    }
    if (record.colorDescription.width != 1872 ||
        record.menuColorDescription.width != 1920 ||
        record.reusableReadbackDescription[0].width != 1872 ||
        record.reusableReadbackDescription[2].width != 1920 ||
        record.resourcesReady != 0 ||
        record.mirroredDraws != 0 ||
        record.restoreFailures != 0 ||
        record.allRestorationsExact != TRUE)
    {
        Fail(test, "per-frame state was not reset");
    }
}
}

int main()
{
    TestIdentityEyeOffsets();
    TestRotatedEyeOffsets();
    TestCoordinateAndViewConversion();
    TestYawedViewConversion();
    TestProjectionConversion();
    TestInvalidInputRejection();
    TestDiagnosticD3D8StereoPair();
    TestRuntimeFovD3D8StereoPair();
    TestRuntimePoseD3D8StereoPair();
    TestRuntimeHeadCameraComposition();
    TestD3D8SkinningShaderConstants();
    TestD3D8SpriteShaderConstants();
    TestD3D8TreeSpriteShaderConstants();
    TestD3D8SkinningShaderOverrideLifecycle();
    TestD3D8SpriteShaderOverrideLifecycle();
    TestD3D8TreeSpriteShaderOverrideLifecycle();
    TestBF1942TreeAngleSlicePolicy();
    TestD3D8DrawPolicy();
    TestBF1942SemanticDrawPolicy();
    TestD3D8FrameCompositionPolicy();
    TestStereoFrameResourceReuseReset();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " stereo-math assertion(s) failed.\n";
        return 1;
    }

    std::cout << "BFVR stereo math tests passed.\n";
    return 0;
}
