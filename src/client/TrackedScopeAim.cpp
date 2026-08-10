#include "client/TrackedScopeAim.h"

#include "client/ControllerInputCache.h"
#include "presenter/SharedPresentationProtocol.h"
#include "stereo/WeaponPoseMath.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <optional>

namespace
{
using Matrix4 = bfvr::stereo::Matrix4;

constexpr DWORD kRightControllerHand = 1;
constexpr float kBf1942WorldUnitsPerMeter = 1.0F;

bool IsFinite(const Matrix4& matrix) noexcept
{
    for (const auto& row : matrix.values)
    {
        for (const float value : row)
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }
    }
    return true;
}

bool HasTrackedGrip(
    const bfvr::D3D8RuntimeControllerHand& hand) noexcept
{
    constexpr DWORD kRequiredFlags =
        bfvr::shared::kControllerHandFlagGripActive |
        bfvr::shared::kControllerHandFlagGripPositionValid |
        bfvr::shared::kControllerHandFlagGripOrientationValid |
        bfvr::shared::kControllerHandFlagGripPositionTracked |
        bfvr::shared::kControllerHandFlagGripOrientationTracked;
    return (hand.flags & kRequiredFlags) == kRequiredFlags;
}

bool HasTrackedAim(
    const bfvr::D3D8RuntimeControllerHand& hand) noexcept
{
    constexpr DWORD kRequiredFlags =
        bfvr::shared::kControllerHandFlagAimActive |
        bfvr::shared::kControllerHandFlagAimPositionValid |
        bfvr::shared::kControllerHandFlagAimOrientationValid |
        bfvr::shared::kControllerHandFlagAimPositionTracked |
        bfvr::shared::kControllerHandFlagAimOrientationTracked;
    return (hand.flags & kRequiredFlags) == kRequiredFlags;
}

Matrix4 IdentityMatrix() noexcept
{
    Matrix4 result = {};
    for (std::size_t index = 0; index < 4; ++index)
    {
        result.values[index][index] = 1.0F;
    }
    return result;
}

Matrix4 Multiply(const Matrix4& left, const Matrix4& right) noexcept
{
    Matrix4 result = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            for (std::size_t inner = 0; inner < 4; ++inner)
            {
                result.values[row][column] +=
                    left.values[row][inner] * right.values[inner][column];
            }
        }
    }
    return result;
}

std::optional<Matrix4> ReadObjectTransform(const void* object) noexcept
{
    using GetTransformationFn = const Matrix4*(__thiscall*)(void*);
    if (object == nullptr)
    {
        return std::nullopt;
    }
    __try
    {
        const auto* const objectBytes = static_cast<const std::byte*>(object);
        const void* const vtable = *reinterpret_cast<void* const*>(objectBytes);
        const void* const target = vtable == nullptr
            ? nullptr
            : *reinterpret_cast<void* const*>(
                static_cast<const std::byte*>(vtable) + 0x3C);
        const auto getter = reinterpret_cast<GetTransformationFn>(
            const_cast<void*>(target));
        const Matrix4* const matrix = getter == nullptr
            ? nullptr
            : getter(const_cast<void*>(object));
        if (matrix == nullptr)
        {
            return std::nullopt;
        }
        Matrix4 copy = {};
        std::memcpy(&copy, matrix, sizeof(copy));
        return IsFinite(copy)
            ? std::optional<Matrix4>(copy)
            : std::nullopt;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return std::nullopt;
    }
}
} // namespace

namespace bfvr
{

bool ReadFreshTrackedScopeAim(
    const void* expectedSoldier,
    TrackedScopeAimSample& result,
    DWORD maximumAgeMs) noexcept
{
    result = {};
    if (expectedSoldier == nullptr)
    {
        return false;
    }

    D3D8RuntimeControllerSample controller = {};
    LONG generation = 0;
    if (!ReadFreshAcceptedControllerInput(
            controller,
            generation,
            maximumAgeMs))
    {
        return false;
    }
    const D3D8RuntimeControllerHand& right =
        controller.hands[kRightControllerHand];
    if (!HasTrackedGrip(right) || !HasTrackedAim(right))
    {
        return false;
    }

    const stereo::Pose controllerWeaponPose = {
        {
            right.gripPose.positionX,
            right.gripPose.positionY,
            right.gripPose.positionZ},
        {
            right.aimPose.orientationX,
            right.aimPose.orientationY,
            right.aimPose.orientationZ,
            right.aimPose.orientationW}};
    const auto controllerGunLocal =
        stereo::MakeD3D8AbsoluteGripWeaponDelta(
            IdentityMatrix(),
            controllerWeaponPose,
            kBf1942WorldUnitsPerMeter);
    const auto soldierWorld = ReadObjectTransform(expectedSoldier);
    if (!controllerGunLocal.has_value() || !soldierWorld.has_value())
    {
        return false;
    }
    const Matrix4 controllerGunWorld = Multiply(
        *controllerGunLocal,
        *soldierWorld);
    if (!IsFinite(controllerGunWorld))
    {
        return false;
    }

    result.controllerGunWorld = controllerGunWorld;
    result.soldier = expectedSoldier;
    result.controllerGeneration = generation;
    result.predictedDisplayTime = controller.predictedDisplayTime;
    result.sessionFocused = controller.sessionFocused;

    const D3D8RuntimeControllerHand& left = controller.hands[0];
    result.leftSqueezeValue = left.squeezeValue;
    result.leftSqueezeActive =
        (left.flags & shared::kControllerHandFlagSqueezeActive) != 0;
    if (HasTrackedGrip(left))
    {
        const stereo::Pose leftGripPose = {
            {
                left.gripPose.positionX,
                left.gripPose.positionY,
                left.gripPose.positionZ},
            {
                left.gripPose.orientationX,
                left.gripPose.orientationY,
                left.gripPose.orientationZ,
                left.gripPose.orientationW}};
        const auto leftGripLocal =
            stereo::MakeD3D8AbsoluteGripWeaponDelta(
                IdentityMatrix(),
                leftGripPose,
                kBf1942WorldUnitsPerMeter);
        if (leftGripLocal.has_value())
        {
            const Matrix4 leftGripWorld = Multiply(
                *leftGripLocal,
                *soldierWorld);
            if (IsFinite(leftGripWorld))
            {
                result.leftGripWorld = leftGripWorld;
                result.leftGripTracked = true;
            }
        }
    }
    return true;
}

} // namespace bfvr
