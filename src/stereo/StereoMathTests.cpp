#include "stereo/D3D8DrawPolicy.h"
#include "stereo/D3D8FirstPersonArmPolicy.h"
#include "stereo/D3D8FrameCompositionPolicy.h"
#include "stereo/D3D8SemanticDrawPolicy.h"
#include "stereo/D3D8ShaderTransform.h"
#include "stereo/D3D8WeaponDrawPolicy.h"
#include "stereo/MainMenuOverlayLayout.h"
#include "stereo/StereoMath.h"
#include "stereo/TreeAngleSlicePolicy.h"
#include "stereo/UiPointerMath.h"
#include "stereo/WeaponMotionPolicy.h"
#include "stereo/WeaponPoseMath.h"
#include "client/D3D8RuntimePosePolicy.h"
#include "client/D3D8TrackingAnchor.h"
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

void TestCentreViewPose()
{
    constexpr std::string_view test = "centre view pose";
    constexpr float sinQuarterTurn = 0.38268343236F;
    constexpr float cosQuarterTurn = 0.92387953251F;
    const auto centre = bfvr::stereo::ComputeCentreViewPose(
        {
            {-0.032F, 1.60F, -0.10F},
            {0.0F, -sinQuarterTurn, 0.0F, cosQuarterTurn}},
        {
            {0.032F, 1.60F, -0.10F},
            {0.0F, sinQuarterTurn, 0.0F, cosQuarterTurn}});
    if (!centre.has_value())
    {
        Fail(test, "valid canted stereo poses were rejected");
        return;
    }
    ExpectNear(test, centre->position.x, 0.0F);
    ExpectNear(test, centre->position.y, 1.60F);
    ExpectNear(test, centre->position.z, -0.10F);
    ExpectNear(test, centre->orientation.x, 0.0F);
    ExpectNear(test, centre->orientation.y, 0.0F);
    ExpectNear(test, centre->orientation.z, 0.0F);
    ExpectNear(test, centre->orientation.w, 1.0F);

    // Antipodal quaternions encode the same orientation and must not average
    // to the invalid zero quaternion.
    const auto antipodal = bfvr::stereo::ComputeCentreViewPose(
        {{-0.032F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}},
        {{0.032F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, -1.0F}});
    if (!antipodal.has_value())
    {
        Fail(test, "equivalent antipodal eye orientations were rejected");
        return;
    }
    ExpectNear(test, antipodal->orientation.w, 1.0F);
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

bfvr::stereo::Matrix4 MultiplyTestMatrices(
    const bfvr::stereo::Matrix4& lhs,
    const bfvr::stereo::Matrix4& rhs)
{
    bfvr::stereo::Matrix4 result = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            for (std::size_t inner = 0; inner < 4; ++inner)
            {
                result.values[row][column] +=
                    lhs.values[row][inner] * rhs.values[inner][column];
            }
        }
    }
    return result;
}

void TestCurrentBodyFrameAndAbsoluteGripWeaponDelta()
{
    constexpr std::string_view test =
        "current body frame and absolute grip weapon delta";
    constexpr float rootHalf = 0.70710678118F;
    const bfvr::stereo::Pose currentHead = {
        {0.25F, 0.20F, -0.40F},
        {0.0F, rootHalf, 0.0F, rootHalf}};
    const auto currentHeadView =
        bfvr::stereo::MakeD3D8ViewFromOpenXRPose(currentHead);
    if (!currentHeadView.has_value())
    {
        Fail(test, "current LOCAL head View was rejected");
        return;
    }
    const bfvr::stereo::Matrix4 playerBodyView = {
        {{{0.0F, 0.0F, -1.0F, 0.0F},
          {0.0F, 1.0F, 0.0F, 0.0F},
          {1.0F, 0.0F, 0.0F, 0.0F},
          {8.0F, -1.0F, 4.0F, 1.0F}}}};
    const auto recoveredBodyView = bfvr::stereo::MakeD3D8CurrentBodyView(
        MultiplyTestMatrices(playerBodyView, *currentHeadView),
        currentHead,
        1.0F);
    if (!recoveredBodyView.has_value())
    {
        Fail(test, "current body frame was rejected");
        return;
    }
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            ExpectNear(
                test,
                recoveredBodyView->values[row][column],
                playerBodyView.values[row][column]);
        }
    }

    const bfvr::stereo::Pose currentGrip = {
        {0.41F, -0.18F, -0.57F},
        {rootHalf, 0.0F, 0.0F, rootHalf}};
    const bfvr::stereo::Matrix4 targetBodyDelta = {
        {{{0.0F, 0.0F, -1.0F, 0.0F},
          {0.0F, 1.0F, 0.0F, 0.0F},
          {1.0F, 0.0F, 0.0F, 0.0F},
          {0.13F, -0.08F, 0.24F, 1.0F}}}};
    const auto attachment =
        bfvr::stereo::MakeD3D8AbsoluteGripToWeaponAttachment(
            currentGrip,
            targetBodyDelta,
            1.0F);
    const auto reconstructed = attachment.has_value()
        ? bfvr::stereo::MakeD3D8AbsoluteGripWeaponDelta(
            *attachment,
            currentGrip,
            1.0F)
        : std::nullopt;
    if (!reconstructed.has_value())
    {
        Fail(test, "absolute grip attachment was rejected");
        return;
    }
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            ExpectNear(
                test,
                reconstructed->values[row][column],
                targetBodyDelta.values[row][column]);
        }
    }
}

void TestD3D8RuntimeLocalOriginPosePolicy()
{
    constexpr std::string_view test = "runtime LOCAL pose policy";
    const bfvr::D3D8RuntimeView origin =
        bfvr::MakeD3D8OpenXRLocalOrigin();
    ExpectNear(test, origin.positionX, 0.0F);
    ExpectNear(test, origin.positionY, 0.0F);
    ExpectNear(test, origin.positionZ, 0.0F);
    ExpectNear(test, origin.orientationX, 0.0F);
    ExpectNear(test, origin.orientationY, 0.0F);
    ExpectNear(test, origin.orientationZ, 0.0F);
    ExpectNear(test, origin.orientationW, 1.0F);

    constexpr float rootHalf = 0.70710678118F;
    bfvr::D3D8RuntimeView weirdSpawnHead = {};
    weirdSpawnHead.positionX = -0.91F;
    weirdSpawnHead.positionY = 0.23F;
    weirdSpawnHead.positionZ = 1.47F;
    weirdSpawnHead.orientationX = rootHalf;
    weirdSpawnHead.orientationY = 0.0F;
    weirdSpawnHead.orientationZ = 0.0F;
    weirdSpawnHead.orientationW = rootHalf;
    const bfvr::D3D8RuntimeFramePosePolicy weirdTracked =
        bfvr::MakeD3D8RuntimeFramePosePolicy(weirdSpawnHead, true);

    bfvr::D3D8RuntimeView normalHead = {};
    normalHead.positionX = 0.06F;
    normalHead.positionY = 0.02F;
    normalHead.positionZ = -0.31F;
    normalHead.orientationY = rootHalf;
    normalHead.orientationW = rootHalf;
    const bfvr::D3D8RuntimeFramePosePolicy normalTracked =
        bfvr::MakeD3D8RuntimeFramePosePolicy(normalHead, true);

    // The later tracked frame must never inherit the arbitrary first pose.
    ExpectNear(test, weirdTracked.renderViewReference.positionX, 0.0F);
    ExpectNear(test, weirdTracked.renderViewReference.orientationW, 1.0F);
    ExpectNear(test, normalTracked.renderViewReference.positionX, 0.0F);
    ExpectNear(test, normalTracked.renderViewReference.positionY, 0.0F);
    ExpectNear(test, normalTracked.renderViewReference.positionZ, 0.0F);
    ExpectNear(test, normalTracked.renderViewReference.orientationX, 0.0F);
    ExpectNear(test, normalTracked.renderViewReference.orientationY, 0.0F);
    ExpectNear(test, normalTracked.renderViewReference.orientationZ, 0.0F);
    ExpectNear(test, normalTracked.renderViewReference.orientationW, 1.0F);
    ExpectNear(test, normalTracked.EyeReference(false).positionZ, 0.0F);
    ExpectNear(test, normalTracked.EyeReference(true).positionZ, normalHead.positionZ);
    ExpectNear(
        test,
        normalTracked.EyeReference(true).orientationY,
        normalHead.orientationY);

    bfvr::stereo::Matrix4 sourceCamera = {};
    for (std::size_t index = 0; index < 4; ++index)
    {
        sourceCamera.values[index][index] = 1.0F;
    }
    const auto trackedCamera =
        bfvr::stereo::ComposeRuntimeHeadWithD3D8Camera(
            sourceCamera,
            {{
                normalTracked.renderViewReference.positionX,
                normalTracked.renderViewReference.positionY,
                normalTracked.renderViewReference.positionZ},
             {
                normalTracked.renderViewReference.orientationX,
                normalTracked.renderViewReference.orientationY,
                normalTracked.renderViewReference.orientationZ,
                normalTracked.renderViewReference.orientationW}},
            {{normalHead.positionX, normalHead.positionY, normalHead.positionZ},
             {
                normalHead.orientationX,
                normalHead.orientationY,
                normalHead.orientationZ,
                normalHead.orientationW}},
            1.0F);
    if (!trackedCamera.has_value())
    {
        Fail(test, "tracked LOCAL camera pose was rejected");
        return;
    }
    const bfvr::stereo::Vec4 trackedOrigin =
        bfvr::stereo::TransformRowVector(
            {0.0F, 0.0F, 0.0F, 1.0F},
            *trackedCamera);
    if (std::fabs(trackedOrigin.x) < 0.00001F &&
        std::fabs(trackedOrigin.y) < 0.00001F &&
        std::fabs(trackedOrigin.z) < 0.00001F)
    {
        Fail(test, "tracked head translation produced no camera movement");
    }

    const bfvr::D3D8RuntimeFramePosePolicy untracked =
        bfvr::MakeD3D8RuntimeFramePosePolicy(normalHead, false);
    ExpectNear(test, untracked.renderViewReference.positionX, normalHead.positionX);
    ExpectNear(test, untracked.renderViewReference.positionY, normalHead.positionY);
    ExpectNear(test, untracked.renderViewReference.positionZ, normalHead.positionZ);
    ExpectNear(
        test,
        untracked.renderViewReference.orientationY,
        normalHead.orientationY);
    ExpectNear(
        test,
        untracked.EyeReference(false).positionZ,
        normalHead.positionZ);
    const auto untrackedCamera =
        bfvr::stereo::ComposeRuntimeHeadWithD3D8Camera(
            sourceCamera,
            {{
                untracked.renderViewReference.positionX,
                untracked.renderViewReference.positionY,
                untracked.renderViewReference.positionZ},
             {
                untracked.renderViewReference.orientationX,
                untracked.renderViewReference.orientationY,
                untracked.renderViewReference.orientationZ,
                untracked.renderViewReference.orientationW}},
            {{normalHead.positionX, normalHead.positionY, normalHead.positionZ},
             {
                normalHead.orientationX,
                normalHead.orientationY,
                normalHead.orientationZ,
                normalHead.orientationW}},
            1.0F);
    if (!untrackedCamera.has_value())
    {
        Fail(test, "untracked no-delta camera pose was rejected");
        return;
    }
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            ExpectNear(
                test,
                untrackedCamera->values[row][column],
                sourceCamera.values[row][column]);
        }
    }
}

void TestViewSpaceWeaponPose()
{
    constexpr std::string_view test = "view-space weapon pose";
    const bfvr::stereo::Pose referenceHead = {
        {0.0F, 1.60F, 0.0F},
        {0.0F, 0.0F, 0.0F, 1.0F}};
    const bfvr::stereo::Pose referenceGrip = {
        {0.20F, 1.30F, -0.45F},
        {0.0F, 0.0F, 0.0F, 1.0F}};

    // Physical head movement shared by the controller must not move a
    // view-model. Only controller motion relative to the HMD is visual input.
    const bfvr::stereo::Pose movedHead = {
        {0.0F, 1.60F, -0.30F},
        {0.0F, 0.0F, 0.0F, 1.0F}};
    const bfvr::stereo::Pose movedGrip = {
        {0.20F, 1.30F, -0.75F},
        {0.0F, 0.0F, 0.0F, 1.0F}};
    const auto headCancelled = bfvr::stereo::MakeD3D8ViewSpaceWeaponDelta(
        referenceHead,
        referenceGrip,
        movedHead,
        movedGrip,
        1.0F,
        1.0F);
    if (!headCancelled.has_value())
    {
        Fail(test, "shared head/controller movement was rejected");
        return;
    }
    ExpectNear(test, headCancelled->values[3][0], 0.0F);
    ExpectNear(test, headCancelled->values[3][1], 0.0F);
    ExpectNear(test, headCancelled->values[3][2], 0.0F);

    // A shared rigid movement includes orientation, not just position. Rotate
    // and translate both the HMD and grip together in LOCAL space; their
    // head-relative relationship and therefore the weapon delta stay identity.
    constexpr float rootHalf = 0.70710678118F;
    const bfvr::stereo::Pose rigidlyMovedHead = {
        {1.0F, 2.0F, 3.0F},
        {0.0F, rootHalf, 0.0F, rootHalf}};
    const bfvr::stereo::Pose rigidlyMovedGrip = {
        {0.55F, 1.70F, 2.80F},
        {0.0F, rootHalf, 0.0F, rootHalf}};
    const auto rigidHeadCancelled =
        bfvr::stereo::MakeD3D8ViewSpaceWeaponDelta(
            referenceHead,
            referenceGrip,
            rigidlyMovedHead,
            rigidlyMovedGrip,
            1.0F,
            1.0F);
    if (!rigidHeadCancelled.has_value())
    {
        Fail(test, "shared rigid HMD/controller movement was rejected");
        return;
    }
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            ExpectNear(
                test,
                rigidHeadCancelled->values[row][column],
                row == column ? 1.0F : 0.0F);
        }
    }

    const bfvr::stereo::Pose currentGrip = {
        {0.30F, 1.30F, -0.65F},
        {0.0F, 0.0F, 0.0F, 1.0F}};
    const auto delta = bfvr::stereo::MakeD3D8ViewSpaceWeaponDelta(
        referenceHead,
        referenceGrip,
        referenceHead,
        currentGrip,
        10.0F,
        1.0F);
    if (!delta.has_value())
    {
        Fail(test, "valid controller translation was rejected");
        return;
    }
    ExpectNear(test, delta->values[3][0], 1.0F);
    ExpectNear(test, delta->values[3][1], 0.0F);
    ExpectNear(test, delta->values[3][2], 2.0F);

    const bfvr::stereo::Pose rotatedGrip = {
        referenceGrip.position,
        {0.0F, rootHalf, 0.0F, rootHalf}};
    const auto rotationDelta = bfvr::stereo::MakeD3D8ViewSpaceWeaponDelta(
        referenceHead,
        referenceGrip,
        referenceHead,
        rotatedGrip,
        1.0F,
        1.0F);
    if (!rotationDelta.has_value())
    {
        Fail(test, "valid controller rotation was rejected");
        return;
    }
    const bfvr::stereo::Vec4 rotatedForward =
        bfvr::stereo::TransformRowVector(
            {0.0F, 0.0F, 1.0F, 0.0F},
            *rotationDelta);
    ExpectNear(test, rotatedForward.x, -1.0F);
    ExpectNear(test, rotatedForward.y, 0.0F);
    ExpectNear(test, rotatedForward.z, 0.0F);
    // Rotating a controller in place requires a compensating translation in
    // the post-multiplied delta. That conjugation keeps the tracked grip fixed
    // while rotating attached geometry around it. A rotation-only delta with
    // a zero translation would instead rotate this already-positioned grip
    // around the HMD/view origin.
    const bfvr::stereo::Vec4 gripPivot = {
        referenceGrip.position.x - referenceHead.position.x,
        referenceGrip.position.y - referenceHead.position.y,
        -(referenceGrip.position.z - referenceHead.position.z),
        1.0F};
    const bfvr::stereo::Vec4 transformedGripPivot =
        bfvr::stereo::TransformRowVector(gripPivot, *rotationDelta);
    ExpectNear(test, transformedGripPivot.x, gripPivot.x);
    ExpectNear(test, transformedGripPivot.y, gripPivot.y);
    ExpectNear(test, transformedGripPivot.z, gripPivot.z);
    ExpectNear(test, transformedGripPivot.w, 1.0F);

    const bfvr::stereo::Vec4 markerFromGrip = {
        gripPivot.x,
        gripPivot.y,
        gripPivot.z + 1.0F,
        1.0F};
    const bfvr::stereo::Vec4 transformedMarker =
        bfvr::stereo::TransformRowVector(markerFromGrip, *rotationDelta);
    ExpectNear(test, transformedMarker.x, gripPivot.x - 1.0F);
    ExpectNear(test, transformedMarker.y, gripPivot.y);
    ExpectNear(test, transformedMarker.z, gripPivot.z);
    ExpectNear(test, transformedMarker.w, 1.0F);

    const auto projection = bfvr::stereo::MakeD3D8ProjectionFromFovTangents(
        {-1.0F, 1.0F, 1.0F, -1.0F},
        0.1F,
        100.0F);
    if (!projection.has_value())
    {
        Fail(test, "test projection could not be built");
        return;
    }
    const auto adjustedWvp = bfvr::stereo::ApplyViewSpaceWeaponDeltaToD3D8Wvp(
        *projection,
        *projection,
        *delta);
    if (!adjustedWvp.has_value())
    {
        Fail(test, "valid view-space WVP adjustment was rejected");
        return;
    }
    const bfvr::stereo::Vec4 vertex = {0.0F, 0.0F, 2.0F, 1.0F};
    const bfvr::stereo::Vec4 expected = bfvr::stereo::TransformRowVector(
        bfvr::stereo::TransformRowVector(vertex, *delta),
        *projection);
    const bfvr::stereo::Vec4 actual =
        bfvr::stereo::TransformRowVector(vertex, *adjustedWvp);
    ExpectNear(test, actual.x, expected.x);
    ExpectNear(test, actual.y, expected.y);
    ExpectNear(test, actual.z, expected.z);
    ExpectNear(test, actual.w, expected.w);

    const bfvr::stereo::Matrix4 sourceWorld = {
        {{{1.0F, 0.0F, 0.0F, 0.0F},
          {0.0F, 1.0F, 0.0F, 0.0F},
          {0.0F, 0.0F, 1.0F, 0.0F},
          {3.0F, -2.0F, 5.0F, 1.0F}}}};
    const auto sourceView = bfvr::stereo::MakeD3D8ViewFromOpenXRPose(
        {{4.0F, 1.0F, -2.0F}, {0.0F, rootHalf, 0.0F, rootHalf}});
    if (!sourceView.has_value())
    {
        Fail(test, "test fixed-function view could not be built");
        return;
    }
    const auto adjustedWorld =
        bfvr::stereo::ApplyViewSpaceWeaponDeltaToD3D8World(
            sourceWorld,
            *sourceView,
            *delta);
    if (!adjustedWorld.has_value())
    {
        Fail(test, "valid view-space World adjustment was rejected");
        return;
    }
    const bfvr::stereo::Vec4 worldActual = bfvr::stereo::TransformRowVector(
        bfvr::stereo::TransformRowVector(vertex, *adjustedWorld),
        *sourceView);
    const bfvr::stereo::Vec4 worldExpected = bfvr::stereo::TransformRowVector(
        bfvr::stereo::TransformRowVector(
            bfvr::stereo::TransformRowVector(vertex, sourceWorld),
            *sourceView),
        *delta);
    ExpectNear(test, worldActual.x, worldExpected.x);
    ExpectNear(test, worldActual.y, worldExpected.y);
    ExpectNear(test, worldActual.z, worldExpected.z);
    ExpectNear(test, worldActual.w, worldExpected.w);

    // Stereo replay must insert the centre-HMD controller delta before the
    // residual eye transform. Building the temporary World from sourceView,
    // then applying the eye residual, is algebraically identical to
    // World * sourceView * delta * residualEye. Building it from the full
    // eye View would reverse the last two factors and give each eye a
    // different attachment pivot when the controller rotates.
    const bfvr::stereo::Matrix4 residualEyeView = {
        {{{1.0F, 0.0F, 0.0F, 0.0F},
          {0.0F, 1.0F, 0.0F, 0.0F},
          {0.0F, 0.0F, 1.0F, 0.0F},
          {0.032F, 0.0F, 0.0F, 1.0F}}}};
    const auto adjustedRotationWorld =
        bfvr::stereo::ApplyViewSpaceWeaponDeltaToD3D8World(
            sourceWorld,
            *sourceView,
            *rotationDelta);
    if (!adjustedRotationWorld.has_value())
    {
        Fail(test, "rotated stereo replay World adjustment was rejected");
        return;
    }

    // Conversion to a World attachment is a same-frame transaction shared by
    // the original draw and both eyes.
    const auto calibrationWorldAttachment =
        bfvr::stereo::MakeD3D8WorldSpaceWeaponDelta(
            *sourceView,
            *rotationDelta);
    if (!calibrationWorldAttachment.has_value())
    {
        Fail(test, "valid calibration World attachment was rejected");
        return;
    }
    const auto recoveredCalibrationOffset =
        bfvr::stereo::MakeD3D8CalibrationViewWeaponOffset(
            *sourceView,
            *calibrationWorldAttachment);
    if (!recoveredCalibrationOffset.has_value())
    {
        Fail(test, "valid calibration World attachment could not be recovered");
        return;
    }
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            ExpectNear(
                test,
                recoveredCalibrationOffset->values[row][column],
                rotationDelta->values[row][column]);
        }
    }
    const auto calibrationAnchoredWorld =
        bfvr::stereo::ApplyWorldSpaceWeaponDeltaToD3D8World(
            sourceWorld,
            *calibrationWorldAttachment);
    if (!calibrationAnchoredWorld.has_value())
    {
        Fail(test, "valid fixed World attachment was rejected");
        return;
    }
    const bfvr::stereo::Vec4 calibrationAnchoredActual =
        bfvr::stereo::TransformRowVector(vertex, *calibrationAnchoredWorld);
    const bfvr::stereo::Vec4 calibrationAnchoredExpected =
        bfvr::stereo::TransformRowVector(vertex, *adjustedRotationWorld);
    ExpectNear(
        test,
        calibrationAnchoredActual.x,
        calibrationAnchoredExpected.x);
    ExpectNear(
        test,
        calibrationAnchoredActual.y,
        calibrationAnchoredExpected.y);
    ExpectNear(
        test,
        calibrationAnchoredActual.z,
        calibrationAnchoredExpected.z);
    ExpectNear(
        test,
        calibrationAnchoredActual.w,
        calibrationAnchoredExpected.w);

    const bfvr::stereo::Vec4 stereoReplayActual =
        bfvr::stereo::TransformRowVector(
            bfvr::stereo::TransformRowVector(
                bfvr::stereo::TransformRowVector(
                    vertex,
                    *adjustedRotationWorld),
                *sourceView),
            residualEyeView);
    const bfvr::stereo::Vec4 stereoReplayExpected =
        bfvr::stereo::TransformRowVector(
            bfvr::stereo::TransformRowVector(
                bfvr::stereo::TransformRowVector(
                    bfvr::stereo::TransformRowVector(
                        vertex,
                        sourceWorld),
                    *sourceView),
                *rotationDelta),
            residualEyeView);
    ExpectNear(test, stereoReplayActual.x, stereoReplayExpected.x);
    ExpectNear(test, stereoReplayActual.y, stereoReplayExpected.y);
    ExpectNear(test, stereoReplayActual.z, stereoReplayExpected.z);
    ExpectNear(test, stereoReplayActual.w, stereoReplayExpected.w);

    // Prove this fixture actually distinguishes the old order. Translation
    // deltas commute with the residual eye translation and gave a false sense
    // of coverage; a grip rotation does not.
    const bfvr::stereo::Vec4 oldEyeThenGripOrder =
        bfvr::stereo::TransformRowVector(
            bfvr::stereo::TransformRowVector(
                bfvr::stereo::TransformRowVector(
                    bfvr::stereo::TransformRowVector(
                        vertex,
                        sourceWorld),
                    *sourceView),
                residualEyeView),
            *rotationDelta);
    if (std::fabs(oldEyeThenGripOrder.x - stereoReplayExpected.x) <
            0.001F &&
        std::fabs(oldEyeThenGripOrder.y - stereoReplayExpected.y) <
            0.001F &&
        std::fabs(oldEyeThenGripOrder.z - stereoReplayExpected.z) <
            0.001F)
    {
        Fail(test, "stereo-order fixture does not distinguish the old order");
    }

    // A development calibration persists the actual controller attachment A,
    // not the target delta D from one session. Reconstructing with the commit
    // grip must return D exactly, and the same physical head-relative grip in
    // a translated/rotated tracking session must return the same D.
    const auto portableAttachment =
        bfvr::stereo::MakeD3D8ControllerToWeaponAttachment(
            referenceHead,
            referenceGrip,
            *rotationDelta,
            1.0F);
    if (!portableAttachment.has_value())
    {
        Fail(test, "portable controller attachment was rejected");
        return;
    }
    const auto reconstructedAtCommit =
        bfvr::stereo::MakeD3D8AttachedWeaponViewDelta(
            *portableAttachment,
            referenceHead,
            referenceGrip,
            1.0F);
    const auto reconstructedInNewSession =
        bfvr::stereo::MakeD3D8AttachedWeaponViewDelta(
            *portableAttachment,
            rigidlyMovedHead,
            rigidlyMovedGrip,
            1.0F);
    if (!reconstructedAtCommit.has_value() ||
        !reconstructedInNewSession.has_value())
    {
        Fail(test, "portable controller attachment could not be reconstructed");
        return;
    }
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            ExpectNear(
                test,
                reconstructedAtCommit->values[row][column],
                rotationDelta->values[row][column]);
            ExpectNear(
                test,
                reconstructedInNewSession->values[row][column],
                rotationDelta->values[row][column]);
        }
    }

    const auto capturedAndMoved =
        bfvr::stereo::ComposeD3D8ViewSpaceWeaponDeltas(
            *rotationDelta,
            *delta);
    if (!capturedAndMoved.has_value())
    {
        Fail(test, "valid captured weapon offset did not compose");
        return;
    }
    const bfvr::stereo::Vec4 composedActual =
        bfvr::stereo::TransformRowVector(vertex, *capturedAndMoved);
    const bfvr::stereo::Vec4 composedExpected =
        bfvr::stereo::TransformRowVector(
            bfvr::stereo::TransformRowVector(vertex, *rotationDelta),
            *delta);
    ExpectNear(test, composedActual.x, composedExpected.x);
    ExpectNear(test, composedActual.y, composedExpected.y);
    ExpectNear(test, composedActual.z, composedExpected.z);
    ExpectNear(test, composedActual.w, composedExpected.w);

    if (bfvr::stereo::MakeD3D8ViewSpaceWeaponDelta(
            referenceHead,
            referenceGrip,
            referenceHead,
            currentGrip,
            1.0F,
            0.0F)
            .has_value())
    {
        Fail(test, "invalid translation limit did not fail closed");
    }
    bfvr::stereo::Matrix4 singularProjection = {};
    if (bfvr::stereo::ApplyViewSpaceWeaponDeltaToD3D8Wvp(
            *projection,
            singularProjection,
            *delta)
            .has_value())
    {
        Fail(test, "singular projection did not fail closed");
    }
    if (bfvr::stereo::ApplyViewSpaceWeaponDeltaToD3D8World(
            sourceWorld,
            singularProjection,
            *delta)
            .has_value())
    {
        Fail(test, "singular View did not fail closed");
    }
}

void TestD3D8WeaponDrawPolicy()
{
    constexpr std::string_view test = "D3D8 weapon draw policy";
    bfvr::stereo::WeaponDrawPolicyInput input = {};
    input.indexedDraw = true;
    input.rendererRoute = bfvr::stereo::WeaponRendererRoute::GenericMesh;
    input.vertexShaderOrFvf = 0x112U;
    input.zEnabled = true;
    input.firstPersonProjection = true;
    input.worldKnown = true;
    input.viewKnown = true;
    for (std::size_t axis = 0; axis < 4; ++axis)
    {
        input.world.values[axis][axis] = 1.0F;
        input.view.values[axis][axis] = 1.0F;
    }
    input.world.values[3][0] = 10.5F;
    input.world.values[3][1] = 20.0F;
    input.world.values[3][2] = 29.5F;
    input.view.values[3][0] = -10.0F;
    input.view.values[3][1] = -20.0F;
    input.view.values[3][2] = -30.0F;

    if (bfvr::stereo::ClassifyWeaponDraw(input, 2.0F) !=
        bfvr::stereo::WeaponDrawDisposition::SharedFixedFunctionWeaponCandidate)
    {
        Fail(test, "shared fixed-function candidate was rejected");
    }

    input.alphaBlendEnabled = true;
    if (bfvr::stereo::ClassifyWeaponDraw(input, 2.0F) !=
        bfvr::stereo::WeaponDrawDisposition::Unclassified)
    {
        Fail(test, "alpha-enabled draw was classified as a weapon");
    }
    input.alphaBlendEnabled = false;
    input.rendererRoute = bfvr::stereo::WeaponRendererRoute::AnimatedMesh;
    if (bfvr::stereo::ClassifyWeaponDraw(input, 2.0F) !=
        bfvr::stereo::WeaponDrawDisposition::Unclassified)
    {
        Fail(test, "animated route was classified as a static weapon");
    }
    input.rendererRoute = bfvr::stereo::WeaponRendererRoute::GenericMesh;
    input.world.values[3][0] = 11.0F;
    input.world.values[3][2] = 30.0F;
    if (bfvr::stereo::ClassifyWeaponDraw(input, 2.0F) !=
        bfvr::stereo::WeaponDrawDisposition::Unclassified)
    {
        Fail(test, "nearby lateral world/vehicle mesh was classified as a weapon");
    }
    input.world.values[3][0] = 30.0F;
    if (bfvr::stereo::ClassifyWeaponDraw(input, 2.0F) !=
        bfvr::stereo::WeaponDrawDisposition::Unclassified)
    {
        Fail(test, "distant generic mesh was classified as a weapon");
    }
    input.world.values[3][0] = 10.5F;
    input.view.values[0][0] = 2.0F;
    if (bfvr::stereo::ClassifyWeaponDraw(input, 2.0F) !=
        bfvr::stereo::WeaponDrawDisposition::Unclassified)
    {
        Fail(test, "non-rigid View was classified as a weapon");
    }
}

void TestViewModelPerspectiveCorrection()
{
    constexpr std::string_view test =
        "viewmodel perspective correction before grip";
    const auto ordinaryProjection =
        bfvr::stereo::MakeD3D8ProjectionFromFovTangents(
            {-1.0F, 1.0F, 0.5625F, -0.5625F},
            0.1F,
            1000.0F);
    const auto viewModelProjection =
        bfvr::stereo::MakeD3D8ProjectionFromFovTangents(
            {-0.5F, 0.5F, 0.28125F, -0.28125F},
            0.01F,
            10.0F);
    if (!ordinaryProjection.has_value() ||
        !viewModelProjection.has_value())
    {
        Fail(test, "test projections could not be built");
        return;
    }

    const auto correction =
        bfvr::stereo::MakeD3D8ViewModelPerspectiveCorrection(
            *viewModelProjection,
            *ordinaryProjection);
    if (!correction.has_value())
    {
        Fail(test, "valid projection pair was rejected");
        return;
    }
    ExpectNear(test, correction->values[0][0], 2.0F);
    ExpectNear(test, correction->values[1][1], 2.0F);
    ExpectNear(test, correction->values[2][2], 1.0F);
    ExpectNear(test, correction->values[3][3], 1.0F);

    bfvr::stereo::Matrix4 gripAttachment = {};
    for (std::size_t index = 0; index < 4; ++index)
    {
        gripAttachment.values[index][index] = 1.0F;
    }
    gripAttachment.values[3][0] = 0.25F;
    gripAttachment.values[3][1] = -0.15F;
    gripAttachment.values[3][2] = 0.40F;
    const auto correctedAttachment =
        bfvr::stereo::ComposeD3D8ViewSpaceWeaponDeltas(
            *correction,
            gripAttachment);
    if (!correctedAttachment.has_value())
    {
        Fail(test, "perspective and grip transforms did not compose");
        return;
    }

    // The perspective morph changes base geometry, but because it precedes
    // the rigid grip attachment it cannot attenuate or amplify physical
    // controller translation.
    ExpectNear(test, correctedAttachment->values[0][0], 2.0F);
    ExpectNear(test, correctedAttachment->values[1][1], 2.0F);
    ExpectNear(test, correctedAttachment->values[3][0], 0.25F);
    ExpectNear(test, correctedAttachment->values[3][1], -0.15F);
    ExpectNear(test, correctedAttachment->values[3][2], 0.40F);

    bfvr::stereo::Matrix4 invalidProjection = {};
    if (bfvr::stereo::MakeD3D8ViewModelPerspectiveCorrection(
            invalidProjection,
            *ordinaryProjection)
            .has_value())
    {
        Fail(test, "singular viewmodel projection was accepted");
    }
}

void TestWeaponMotionTracker()
{
    constexpr std::string_view test = "weapon motion tracker";
    bfvr::stereo::WeaponMotionTracker tracker;
    bfvr::stereo::WeaponMotionTrackingInput input = {};
    input.gripTrackingValid = true;
    input.predictedDisplayTime = 100;
    input.head = {{0.0F, 1.60F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}};
    input.grip = {{0.20F, 1.30F, -0.45F}, {0.0F, 0.0F, 0.0F, 1.0F}};
    input.worldUnitsPerMeter = 1.0F;
    if (tracker.Update(input).has_value() || !tracker.IsCalibrated())
    {
        Fail(test, "first valid pose did not calibrate without an offset");
        return;
    }

    input.predictedDisplayTime = 101;
    input.grip.position.x += 0.10F;
    const auto moved = tracker.Update(input);
    if (!moved.has_value())
    {
        Fail(test, "valid tracked grip translation was rejected");
        return;
    }
    ExpectNear(test, moved->values[3][0], 0.10F);
    ExpectNear(test, moved->values[3][1], 0.0F);
    ExpectNear(test, moved->values[3][2], 0.0F);

    // The tracker retains its calibration-time HMD basis. A later HMD move
    // with an unchanged LOCAL grip must preserve the controller-only delta.
    input.predictedDisplayTime = 102;
    input.head = {
        {0.25F, 1.80F, -0.40F},
        {0.0F, 0.70710678118F, 0.0F, 0.70710678118F}};
    const auto headOnly = tracker.Update(input);
    if (!headOnly.has_value())
    {
        Fail(test, "head-only sample was rejected");
        return;
    }
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            const float expected = row == 3 && column == 0
                ? 0.10F
                : row == column ? 1.0F : 0.0F;
            ExpectNear(test, headOnly->values[row][column], expected);
        }
    }

    // Finite tracked reach is unrestricted; there is no viewmodel leash.
    for (int step = 0; step < 10; ++step)
    {
        input.predictedDisplayTime += 1;
        input.grip.position.x += 0.15F;
        if (!tracker.Update(input).has_value())
        {
            Fail(test, "finite arm extension was restricted");
            return;
        }
    }

    input.gripTrackingValid = false;
    if (tracker.Update(input).has_value() || tracker.IsCalibrated())
    {
        Fail(test, "invalid tracking did not reset calibration");
        return;
    }

    input.gripTrackingValid = true;
    input.predictedDisplayTime = 102;
    if (tracker.Update(input).has_value())
    {
        Fail(test, "tracking recovery did not recalibrate in place");
        return;
    }

    input.predictedDisplayTime = 101;
    input.grip.position.x += 0.05F;
    if (tracker.Update(input).has_value())
    {
        Fail(test, "predicted-time reversal was applied as motion");
        return;
    }

    input.predictedDisplayTime = 103;
    input.grip.position.x += 1.0F;
    if (!tracker.Update(input).has_value())
    {
        Fail(test, "large finite controller translation was restricted");
    }
}

void TestUiPointerMapping()
{
    constexpr std::string_view test = "OpenXR aim to BF1942 UI canvas";
    constexpr float quadWidth = 1.6F;
    constexpr float quadHeight = quadWidth * 2016.0F / 1872.0F;
    const bfvr::stereo::Pose quad = {
        {0.0F, 0.0F, -1.5F},
        {0.0F, 0.0F, 0.0F, 1.0F}};

    const auto center =
        bfvr::stereo::MapOpenXRAimPoseToAspectFitUiCanvas(
            {{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}},
            quad,
            quadWidth,
            quadHeight,
            1872,
            2016,
            1920,
            1080,
            800,
            600);
    if (!center.has_value())
    {
        Fail(test, "center aim ray did not hit the UI canvas");
        return;
    }
    ExpectNear(test, center->normalizedX, 0.5F);
    ExpectNear(test, center->normalizedY, 0.5F);
    ExpectNear(test, center->pixelX, 400.0F);
    ExpectNear(test, center->pixelY, 300.0F);

    // BFVR aspect-fits a 1920x1080 render target into the 1872x2016 OpenXR
    // texture, while BF1942's menu projection and native input remain on an
    // 800x600 logical canvas. The mapper must unpad the raster before applying
    // the separate logical coordinate scale.
    const auto upperRight =
        bfvr::stereo::MapOpenXRAimPoseToAspectFitUiCanvas(
            {{0.4F, 0.225F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}},
            quad,
            quadWidth,
            quadHeight,
            1872,
            2016,
            1920,
            1080,
            800,
            600);
    if (!upperRight.has_value())
    {
        Fail(test, "valid aim ray inside aspect-fitted content was rejected");
        return;
    }
    ExpectNear(test, upperRight->normalizedX, 0.75F);
    ExpectNear(test, upperRight->normalizedY, 0.25F);
    ExpectNear(test, upperRight->pixelX, 600.0F);
    ExpectNear(test, upperRight->pixelY, 150.0F);

    if (bfvr::stereo::MapOpenXRAimPoseToAspectFitUiCanvas(
            {{0.0F, 0.70F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}},
            quad,
            quadWidth,
            quadHeight,
            1872,
            2016,
            1920,
            1080,
            800,
            600)
            .has_value())
    {
        Fail(test, "transparent aspect-fit padding produced a canvas point");
    }

    // A ray facing away from the panel intersects its plane only behind the
    // controller and must never become a cursor coordinate.
    if (bfvr::stereo::MapOpenXRAimPoseToAspectFitUiCanvas(
            {{0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}},
            quad,
            quadWidth,
            quadHeight,
            1872,
            2016,
            1920,
            1080,
            800,
            600)
            .has_value())
    {
        Fail(test, "back-facing aim ray produced a canvas point");
    }

    // A controller behind the panel can intersect the same mathematical plane,
    // but a one-sided menu must not be interactive through its back face.
    if (bfvr::stereo::MapOpenXRAimPoseToAspectFitUiCanvas(
            {{0.0F, 0.0F, -2.0F}, {0.0F, 1.0F, 0.0F, 0.0F}},
            quad,
            quadWidth,
            quadHeight,
            1872,
            2016,
            1920,
            1080,
            800,
            600)
            .has_value())
    {
        Fail(test, "back-face UI ray produced a canvas point");
    }

    constexpr float halfSine45 = 0.70710678118F;
    const bfvr::stereo::Pose movedHead = {{2.0F, 1.0F, -3.0F},
        {0.0F, halfSine45, 0.0F, halfSine45}};
    const bfvr::stereo::Pose movedAim = movedHead;
    const auto relativeAim = bfvr::stereo::MakePoseRelativeToReference(
        movedHead, movedAim);
    if (!relativeAim.has_value())
        return Fail(test, "valid head-relative controller pose was rejected");
    const auto movedCenter = bfvr::stereo::MapOpenXRAimPoseToAspectFitUiCanvas(
        *relativeAim, quad, quadWidth, quadHeight, 1872, 2016, 1920, 1080,
        800, 600);
    if (!movedCenter.has_value())
        return Fail(test, "head-relative ray missed the VIEW-space UI");
    ExpectNear(test, movedCenter->pixelX, 400.0F);
    ExpectNear(test, movedCenter->pixelY, 300.0F);
}

void TestUiMenuYawAnchorFollow()
{
    constexpr std::string_view test = "yaw-only menu anchor follow";
    constexpr float rootHalf = 0.70710678118F;
    const bfvr::stereo::Pose openingHead = {
        {1.0F, 1.60F, -2.0F},
        {0.0F, rootHalf, 0.0F, rootHalf}};
    const auto yawOnly = bfvr::stereo::MakeYawOnlyUiAnchor(openingHead);
    if (!yawOnly.has_value())
    {
        Fail(test, "valid opening head pose was rejected");
        return;
    }
    ExpectNear(test, yawOnly->position.x, 1.0F);
    ExpectNear(test, yawOnly->position.y, 1.60F);
    ExpectNear(test, yawOnly->position.z, -2.0F);
    ExpectNear(test, yawOnly->orientation.x, 0.0F);
    ExpectNear(test, yawOnly->orientation.y, rootHalf);
    ExpectNear(test, yawOnly->orientation.z, 0.0F);
    ExpectNear(test, yawOnly->orientation.w, rootHalf);

    bfvr::stereo::UiMenuAnchorTracker tracker = {};
    const bfvr::stereo::Pose forwardHead = {
        {0.0F, 1.60F, 0.0F},
        {0.0F, 0.0F, 0.0F, 1.0F}};
    if (!bfvr::stereo::UpdateUiMenuAnchor(
            tracker,
            forwardHead,
            1000000000,
            0.50F,
            1.0F))
    {
        Fail(test, "opening anchor was not accepted");
        return;
    }

    const bfvr::stereo::Pose farRightHead = {
        {3.0F, 1.60F, 4.0F},
        {0.0F, rootHalf, 0.0F, rootHalf}};
    if (!bfvr::stereo::UpdateUiMenuAnchor(
            tracker,
            farRightHead,
            1100000000,
            0.50F,
            1.0F))
    {
        Fail(test, "follow update was rejected");
        return;
    }
    // A 90-degree look-away over 0.1 seconds moves at the configured
    // 1 radian/sec cap, not by the full yaw delta.
    ExpectNear(test, tracker.anchor.orientation.y, std::sin(0.05F));
    ExpectNear(test, tracker.anchor.orientation.w, std::cos(0.05F));
    ExpectNear(test, tracker.anchor.position.x, 3.0F);
    ExpectNear(test, tracker.anchor.position.z, 4.0F);

    const bfvr::stereo::Pose nearHead = {
        {8.0F, 1.60F, 9.0F},
        {0.0F, std::sin(0.15F), 0.0F, std::cos(0.15F)}};
    if (!bfvr::stereo::UpdateUiMenuAnchor(
            tracker,
            nearHead,
            1200000000,
            0.50F,
            1.0F))
    {
        Fail(test, "in-view update was rejected");
        return;
    }
    // Inside the dead zone, the panel remains genuinely world-locked.
    ExpectNear(test, tracker.anchor.position.x, 3.0F);
    ExpectNear(test, tracker.anchor.position.z, 4.0F);

    bfvr::stereo::ResetUiMenuAnchor(tracker);
    if (tracker.valid || tracker.lastPredictedDisplayTime != 0)
    {
        Fail(test, "menu close did not reset the anchor tracker");
    }
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
        0x00654571,
        true,
        true,
        4,
        8704,
        0x8B737890,
        1,
        0,
        1,
        1,
        0};
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::WaterSurface)
    {
        Fail(test, "observed water alpha pass was rejected");
    }
    if (bfvr::stereo::ShouldUseBF1942StereoStableWaterReflection(
            D3D8SemanticDrawClass::WaterSurface,
            0,
            false) ||
        !bfvr::stereo::ShouldUseBF1942StereoStableWaterReflection(
            D3D8SemanticDrawClass::WaterSurface,
            1,
            false) ||
        bfvr::stereo::ShouldUseBF1942StereoStableWaterReflection(
            D3D8SemanticDrawClass::WaterSurface,
            1,
            true) ||
        bfvr::stereo::ShouldUseBF1942StereoStableWaterReflection(
            D3D8SemanticDrawClass::Unclassified,
            1,
            false))
    {
        Fail(test, "stereo-stable reflection escaped the exact additive water pass");
    }
    signature.rendererReturnAddress = 0x00654572;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Unclassified)
    {
        Fail(test, "different water return anchor did not fail closed");
    }
    signature.rendererReturnAddress = 0x00654571;
    signature.lighting = 1;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Unclassified)
    {
        Fail(test, "different water lighting state did not fail closed");
    }
    const bool exactFirstPersonArm = bfvr::stereo::IsBF1942FirstPersonArmDraw(
        D3D8SemanticDrawClass::AnimatedMeshSkinning, true, 2.25F, 3.75F);
    const bool ordinarySoldier = bfvr::stereo::IsBF1942FirstPersonArmDraw(
        D3D8SemanticDrawClass::AnimatedMeshSkinning, true, 1.60F, 2.84F);
    const bool unclassifiedViewmodel = bfvr::stereo::IsBF1942FirstPersonArmDraw(
        D3D8SemanticDrawClass::Unclassified, true, 2.25F, 3.75F);
    if (!exactFirstPersonArm)
    {
        Fail(test, "exact animated first-person projection was not suppressible");
    }
    if (ordinarySoldier)
    {
        Fail(test, "ordinary-world animated soldier was suppressible");
    }
    if (unclassifiedViewmodel)
    {
        Fail(test, "unclassified first-person draw was suppressible");
    }
    if (!bfvr::stereo::ShouldSuppressBF1942FirstPersonArmDraw(
            true,
            exactFirstPersonArm,
            false))
    {
        Fail(test, "default native-arm policy did not suppress the exact draw");
    }
    if (bfvr::stereo::ShouldSuppressBF1942FirstPersonArmDraw(
            true,
            exactFirstPersonArm,
            true))
    {
        Fail(test, "enabled native-arm policy still suppressed the exact draw");
    }
    if (bfvr::stereo::ShouldSuppressBF1942FirstPersonArmDraw(
            true,
            ordinarySoldier,
            true) ||
        bfvr::stereo::ShouldSuppressBF1942FirstPersonArmDraw(
            false,
            exactFirstPersonArm,
            false))
    {
        Fail(test, "native-arm policy broadened beyond presentation exact-arm draws");
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
    signature.zWriteEnable = 0;
    signature.alphaBlendEnable = 0;
    signature.fogEnable = 1;
    signature.lighting = 1;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Ref2FontGlyphBatch)
    {
        Fail(test, "authored alpha-test Ref2 font variant was rejected");
    }
    signature.zEnable = 1;
    if (bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature) !=
        D3D8SemanticDrawClass::Unclassified)
    {
        Fail(test, "depth-enabled Ref2 font near miss did not fail closed");
    }
    signature.zEnable = 0;
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
    using bfvr::stereo::D3D8DrawPolicy;
    using bfvr::stereo::D3D8FrameCompositionLayer;
    using bfvr::stereo::D3D8SemanticDrawClass;
    if (bfvr::stereo::SelectD3D8FrameCompositionLayer(
            D3D8SemanticDrawClass::Ref2MenuQuad,
            D3D8DrawPolicy::MonoNonPerspective) !=
        D3D8FrameCompositionLayer::Ref2Ui)
    {
        Fail(test, "Ref2 menu quad was not routed to the UI layer");
    }
    if (bfvr::stereo::SelectD3D8FrameCompositionLayer(
            D3D8SemanticDrawClass::Ref2FontGlyphBatch,
            D3D8DrawPolicy::MonoPretransformed) !=
        D3D8FrameCompositionLayer::Ref2Ui)
    {
        Fail(test, "Ref2 font batch was not routed to the UI layer");
    }
    if (bfvr::stereo::SelectD3D8FrameCompositionLayer(
            D3D8SemanticDrawClass::SkyboxCubeFace,
            D3D8DrawPolicy::StereoPerspective) !=
            D3D8FrameCompositionLayer::WorldEyes ||
        bfvr::stereo::SelectD3D8FrameCompositionLayer(
            D3D8SemanticDrawClass::BillboardBatch,
            D3D8DrawPolicy::StereoPerspective) !=
            D3D8FrameCompositionLayer::WorldEyes ||
        bfvr::stereo::SelectD3D8FrameCompositionLayer(
            D3D8SemanticDrawClass::TreeMeshAlphaBlock,
            D3D8DrawPolicy::StereoPerspective) !=
            D3D8FrameCompositionLayer::WorldEyes ||
        bfvr::stereo::SelectD3D8FrameCompositionLayer(
            D3D8SemanticDrawClass::TreeMeshProgrammableSprite,
            D3D8DrawPolicy::StereoPerspective) !=
            D3D8FrameCompositionLayer::WorldEyes ||
        bfvr::stereo::SelectD3D8FrameCompositionLayer(
            D3D8SemanticDrawClass::AnimatedMeshSkinning,
            D3D8DrawPolicy::StereoPerspective) !=
            D3D8FrameCompositionLayer::WorldEyes ||
        bfvr::stereo::SelectD3D8FrameCompositionLayer(
            D3D8SemanticDrawClass::WaterSurface,
            D3D8DrawPolicy::StereoPerspective) !=
            D3D8FrameCompositionLayer::WorldEyes ||
        bfvr::stereo::SelectD3D8FrameCompositionLayer(
            D3D8SemanticDrawClass::TranslucentSprite,
            D3D8DrawPolicy::StereoPerspective) !=
            D3D8FrameCompositionLayer::WorldEyes ||
        bfvr::stereo::SelectD3D8FrameCompositionLayer(
            D3D8SemanticDrawClass::Unclassified,
            D3D8DrawPolicy::StereoPerspective) !=
            D3D8FrameCompositionLayer::WorldEyes)
    {
        Fail(test, "non-UI semantic class escaped the world-eye layer");
    }
    if (bfvr::stereo::SelectD3D8FrameCompositionLayer(
            D3D8SemanticDrawClass::Unclassified,
            D3D8DrawPolicy::MonoPretransformed) !=
            D3D8FrameCompositionLayer::Ref2Ui ||
        bfvr::stereo::SelectD3D8FrameCompositionLayer(
            D3D8SemanticDrawClass::Unclassified,
            D3D8DrawPolicy::MonoNonPerspective) !=
            D3D8FrameCompositionLayer::Ref2Ui)
    {
        Fail(test, "unclassified monoscopic draw escaped the UI layer");
    }

    bfvr::stereo::D3D8FrameCompletionFacts standalone = {};
    standalone.hasMirroredDraws = true;
    standalone.stateRestorationExact = true;
    standalone.worldEyesHaveColor = true;
    standalone.eyeImagesDiffer = true;
    standalone.layerPartitionExact = true;
    standalone.uiLayerHasContent = true;
    if (bfvr::stereo::IsD3D8FrameCompositionComplete(standalone) == false)
    {
        Fail(test, "complete standalone stereo/UI frame was rejected");
    }

    standalone.worldEyesHaveColor = false;
    standalone.presentationMode = true;
    standalone.frameTransferred = true;
    standalone.eyeImagesDiffer = false;
    if (bfvr::stereo::IsD3D8FrameCompositionComplete(standalone) == false)
    {
        Fail(test, "transferred UI-only presentation frame was rejected");
    }

    standalone.frameTransferred = false;
    if (bfvr::stereo::IsD3D8FrameCompositionComplete(standalone))
    {
        Fail(test, "untransferred UI-only presentation frame was accepted");
    }

    standalone.frameTransferred = true;
    standalone.stateRestorationExact = false;
    if (bfvr::stereo::IsD3D8FrameCompositionComplete(standalone))
    {
        Fail(test, "state-corrupt UI-only presentation frame was accepted");
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
    if (!bfvr::d3d8probe::RestoreD3D8SkinningShaderConstants(
            api,
            &device,
            state) ||
        !bfvr::d3d8probe::VerifyD3D8SkinningShaderConstants(
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
    if (!bfvr::d3d8probe::RestoreD3D8SpriteShaderConstants(
            api,
            &device,
            state) ||
        !bfvr::d3d8probe::VerifyD3D8SpriteShaderConstants(
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
    if (!bfvr::d3d8probe::RestoreD3D8TreeSpriteShaderConstants(
            api,
            &device,
            state) ||
        !bfvr::d3d8probe::VerifyD3D8TreeSpriteShaderConstants(
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
    record.depthExport[0] = reinterpret_cast<void*>(10);
    record.depthExport[1] = reinterpret_cast<void*>(11);
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
    record.allRestorationsAccepted = FALSE;
    bfvr::d3d8probe::ResetStereoFrameRecordForResourceReuse(record);
    if (record.ownedColor[0] != reinterpret_cast<void*>(1) ||
        record.ownedColor[1] != reinterpret_cast<void*>(2) ||
        record.ownedDepth[0] != reinterpret_cast<void*>(3) ||
        record.ownedDepth[1] != reinterpret_cast<void*>(4) ||
        record.depthExport[0] != reinterpret_cast<void*>(10) ||
        record.depthExport[1] != reinterpret_cast<void*>(11) ||
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
        record.allRestorationsAccepted != TRUE)
    {
        Fail(test, "per-frame state was not reset");
    }
}

void TestMainMenuOverlayLayout()
{
    constexpr std::string_view test = "main menu overlay layout";
    constexpr bfvr::stereo::UiCanvasRect rect =
        bfvr::stereo::BackToGameButtonRect();
    constexpr float tolerance = 0.001F;
    const auto differs = [](float left, float right)
    {
        return std::abs(left - right) > tolerance;
    };
    if (differs(rect.left, 344.96F) || differs(rect.top, 542.08F) ||
        differs(rect.right, 455.04F) || differs(rect.bottom, 576.0F) ||
        differs(rect.right - rect.left, 110.08F) ||
        differs(rect.bottom - rect.top, 33.92F))
    {
        Fail(test, "button did not match the reference-image size and 24-pixel bottom margin");
    }
    constexpr bfvr::stereo::UiCanvasRect referenceMapped =
        bfvr::stereo::MapUiCanvasRectThroughAspectFit(
            rect,
            1920.0F,
            1080.0F,
            1920.0F,
            1080.0F);
    constexpr float visibleBorderWidth =
        (referenceMapped.right - referenceMapped.left) * 246.0F / 256.0F;
    constexpr float visibleBorderHeight =
        (referenceMapped.bottom - referenceMapped.top) * 50.0F / 64.0F;
    if (differs(visibleBorderWidth, 253.872F) ||
        differs(visibleBorderHeight, 47.7F) ||
        referenceMapped.left < 800.0F)
    {
        Fail(test, "reference-image border size or copyright-text clearance regressed");
    }
    if (!bfvr::stereo::IsInsideUiCanvasRect(rect, rect.left, rect.top) ||
        !bfvr::stereo::IsInsideUiCanvasRect(
            rect,
            rect.right - 0.01F,
            rect.bottom - 0.01F) ||
        bfvr::stereo::IsInsideUiCanvasRect(
            rect,
            rect.left - 0.01F,
            rect.top) ||
        bfvr::stereo::IsInsideUiCanvasRect(
            rect,
            rect.right,
            rect.bottom - 1.0F) ||
        bfvr::stereo::IsInsideUiCanvasRect(
            rect,
            (rect.left + rect.right) * 0.5F,
            rect.bottom))
    {
        Fail(test, "button hit-test boundaries did not match rendering");
    }

    bool pressed = false;
    if (!bfvr::stereo::ConsumeUiButtonPressEdge(
            true,
            true,
            true,
            pressed) ||
        bfvr::stereo::ConsumeUiButtonPressEdge(
            true,
            true,
            true,
            pressed))
    {
        Fail(test, "one press did not produce exactly one activation edge");
    }
    static_cast<void>(bfvr::stereo::ConsumeUiButtonPressEdge(
        true,
        true,
        false,
        pressed));
    if (bfvr::stereo::ConsumeUiButtonPressEdge(
            true,
            false,
            true,
            pressed) ||
        bfvr::stereo::ConsumeUiButtonPressEdge(
            true,
            true,
            true,
            pressed))
    {
        Fail(test, "a press begun outside activated after moving inside");
    }
}

void TestContextTrackingAnchors()
{
    constexpr std::string_view test = "context tracking anchors";
    bfvr::D3D8TrackingAnchor anchor = {};
    bfvr::D3D8RuntimeView head = {};
    head.positionX = 2.0F;
    head.positionY = 0.35F;
    head.positionZ = 3.0F;
    anchor.Update(
        head,
        true,
        {bfvr::D3D8TrackingContextKind::Infantry, 0x1000},
        1'000'000'000,
        0,
        false,
        true,
        1.10F,
        1.70F,
        0.10F);
    const auto infantryReference = anchor.ReferenceHead({});
    ExpectNear(test, infantryReference.positionX, 2.0F);
    ExpectNear(test, infantryReference.positionY, 0.25F);
    ExpectNear(test, infantryReference.positionZ, 3.0F);
    const auto rebasedInfantryHead = anchor.RebaseView(head);
    ExpectNear(test, rebasedInfantryHead.positionX, 0.0F);
    ExpectNear(test, rebasedInfantryHead.positionY, 0.10F);
    ExpectNear(test, rebasedInfantryHead.positionZ, 0.0F);

    // Switching to Standing while still physically seated must map the live
    // STAGE height to the existing BF1942 eye camera, not add 1.10 m on top.
    anchor.Update(
        head,
        true,
        {bfvr::D3D8TrackingContextKind::Infantry, 0x1000},
        2'000'000'000,
        0,
        true,
        true,
        1.10F,
        1.70F,
        0.0F);
    ExpectNear(test, anchor.ReferenceHead({}).positionY, 0.95F);
    ExpectNear(test, anchor.RebaseView(head).positionY, -0.60F);

    // Standing up changes LOCAL and STAGE head Y by the same amount, leaving
    // the derived LOCAL floor/reference fixed and producing zero extra height
    // at BF1942's nominal 1.70-m standing eye camera.
    head.positionY = 0.95F;
    anchor.Update(
        head,
        true,
        {bfvr::D3D8TrackingContextKind::Infantry, 0x1000},
        3'000'000'000,
        0,
        true,
        true,
        1.70F,
        1.70F,
        0.0F);
    ExpectNear(test, anchor.ReferenceHead({}).positionY, 0.95F);
    ExpectNear(test, anchor.RebaseView(head).positionY, 0.0F);

    anchor.Update(
        head,
        true,
        {bfvr::D3D8TrackingContextKind::Infantry, 0x1000},
        4'000'000'000,
        0,
        true,
        true,
        1.70F,
        1.70F,
        -0.30F);
    ExpectNear(test, anchor.RebaseView(head).positionY, -0.30F);

    // A user may select Seated while still standing and only then sit down.
    // The transition follows that one deliberate posture change and settles
    // without requiring another Standing/Seated toggle.
    anchor.Update(
        head,
        true,
        {bfvr::D3D8TrackingContextKind::Infantry, 0x1000},
        5'000'000'000,
        0,
        false,
        true,
        1.70F,
        1.70F,
        0.0F);
    ExpectNear(test, anchor.RebaseView(head).positionY, 0.0F);
    head.positionY = 0.45F;
    anchor.Update(
        head,
        true,
        {bfvr::D3D8TrackingContextKind::Infantry, 0x1000},
        6'000'000'000,
        0,
        false,
        true,
        1.20F,
        1.70F,
        0.0F);
    ExpectNear(test, anchor.RebaseView(head).positionY, 0.0F);
    anchor.Update(
        head,
        true,
        {bfvr::D3D8TrackingContextKind::Infantry, 0x1000},
        6'800'000'000,
        0,
        false,
        true,
        1.20F,
        1.70F,
        0.0F);
    head.positionY = 0.40F;
    anchor.Update(
        head,
        true,
        {bfvr::D3D8TrackingContextKind::Infantry, 0x1000},
        7'000'000'000,
        0,
        false,
        true,
        1.15F,
        1.70F,
        0.0F);
    ExpectNear(test, anchor.RebaseView(head).positionY, -0.05F);

    head.positionX = 8.0F;
    head.positionY = 1.20F;
    head.positionZ = -4.0F;
    anchor.Update(
        head,
        true,
        {bfvr::D3D8TrackingContextKind::Seat, 0x2000},
        8'000'000'000,
        0,
        false,
        true,
        1.20F,
        1.70F,
        0.30F);
    anchor.Update(
        head,
        true,
        {bfvr::D3D8TrackingContextKind::Seat, 0x2000},
        8'100'000'000,
        0,
        false,
        true,
        1.20F,
        1.70F,
        0.30F);
    anchor.Update(
        head,
        true,
        {bfvr::D3D8TrackingContextKind::Seat, 0x2000},
        8'200'000'000,
        0,
        false,
        true,
        1.20F,
        1.70F,
        0.30F);
    const auto seatReference = anchor.ReferenceHead({});
    ExpectNear(test, seatReference.positionX, 8.0F);
    ExpectNear(test, seatReference.positionY, 1.20F);
    ExpectNear(test, seatReference.positionZ, -4.0F);
    const auto rebasedSeatHead = anchor.RebaseView(head);
    ExpectNear(test, rebasedSeatHead.positionY, 0.0F);

    head.positionX = 8.25F;
    head.positionY = 1.30F;
    head.positionZ = -3.80F;
    anchor.Update(
        head,
        true,
        {bfvr::D3D8TrackingContextKind::Seat, 0x2000},
        9'000'000'000,
        7,
        false,
        true,
        1.30F,
        1.70F,
        -0.30F);
    const auto recenteredSeat = anchor.ReferenceHead({});
    ExpectNear(test, recenteredSeat.positionX, 8.25F);
    ExpectNear(test, recenteredSeat.positionY, 1.30F);
    ExpectNear(test, recenteredSeat.positionZ, -3.80F);

    // Returning to Infantry in Seated mode captures the current posture as
    // neutral even if the prior session/context was Standing.
    head = {};
    head.positionY = 0.40F;
    anchor.Update(
        head,
        true,
        {bfvr::D3D8TrackingContextKind::Infantry, 0x3000},
        10'000'000'000,
        7,
        false,
        true,
        1.15F,
        1.70F,
        0.0F);
    anchor.Update(
        head,
        true,
        {bfvr::D3D8TrackingContextKind::Infantry, 0x3000},
        10'100'000'000,
        7,
        false,
        true,
        1.15F,
        1.70F,
        0.0F);
    anchor.Update(
        head,
        true,
        {bfvr::D3D8TrackingContextKind::Infantry, 0x3000},
        10'200'000'000,
        7,
        false,
        true,
        1.15F,
        1.70F,
        0.0F);
    ExpectNear(test, anchor.RebaseView(head).positionY, 0.0F);
}
}

int main()
{
    TestIdentityEyeOffsets();
    TestCentreViewPose();
    TestRotatedEyeOffsets();
    TestCoordinateAndViewConversion();
    TestYawedViewConversion();
    TestProjectionConversion();
    TestInvalidInputRejection();
    TestDiagnosticD3D8StereoPair();
    TestRuntimeFovD3D8StereoPair();
    TestRuntimePoseD3D8StereoPair();
    TestRuntimeHeadCameraComposition();
    TestCurrentBodyFrameAndAbsoluteGripWeaponDelta();
    TestD3D8RuntimeLocalOriginPosePolicy();
    TestContextTrackingAnchors();
    TestViewSpaceWeaponPose();
    TestViewModelPerspectiveCorrection();
    TestD3D8WeaponDrawPolicy();
    TestWeaponMotionTracker();
    TestUiPointerMapping();
    TestUiMenuYawAnchorFollow();
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
    TestMainMenuOverlayLayout();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " stereo-math assertion(s) failed.\n";
        return 1;
    }

    std::cout << "BFVR stereo math tests passed.\n";
    return 0;
}
