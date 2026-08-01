#include "stereo/QuickMenuInteraction.h"

#include "stereo/UiPointerMath.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace
{
using bfvr::stereo::Pose;
using bfvr::stereo::Quaternion;
using bfvr::stereo::Vec3;

constexpr float kHandForwardOffsetMeters = 0.16F;
constexpr float kHandUpOffsetMeters = 0.09F;
constexpr float kPositionDeadzoneMeters = 0.018F;
constexpr float kOrientationDeadzoneRadians = 0.035F;
constexpr float kPositionHalfLifeSeconds = 0.070F;
constexpr float kOrientationHalfLifeSeconds = 0.095F;
constexpr float kMaximumFrameSeconds = 0.100F;
constexpr float kSmallValue = 0.000001F;

bool IsFinite(float value) noexcept
{
    return std::isfinite(value);
}

bool IsFinite(const Vec3& value) noexcept
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

bool IsFinite(const Quaternion& value) noexcept
{
    return IsFinite(value.x) && IsFinite(value.y) &&
        IsFinite(value.z) && IsFinite(value.w);
}

Vec3 Add(const Vec3& lhs, const Vec3& rhs) noexcept
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

Vec3 Subtract(const Vec3& lhs, const Vec3& rhs) noexcept
{
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vec3 Scale(const Vec3& value, float scalar) noexcept
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

float Dot(const Vec3& lhs, const Vec3& rhs) noexcept
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Vec3 Cross(const Vec3& lhs, const Vec3& rhs) noexcept
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x};
}

float Length(const Vec3& value) noexcept
{
    return std::sqrt(std::max(Dot(value, value), 0.0F));
}

std::optional<Vec3> Normalize(const Vec3& value) noexcept
{
    const float length = Length(value);
    if (!IsFinite(value) || !IsFinite(length) || length <= kSmallValue)
    {
        return std::nullopt;
    }
    return Scale(value, 1.0F / length);
}

std::optional<Quaternion> Normalize(const Quaternion& value) noexcept
{
    const float lengthSquared = value.x * value.x + value.y * value.y +
        value.z * value.z + value.w * value.w;
    if (!IsFinite(value) || !IsFinite(lengthSquared) ||
        lengthSquared <= kSmallValue)
    {
        return std::nullopt;
    }
    const float reciprocal = 1.0F / std::sqrt(lengthSquared);
    return Quaternion{
        value.x * reciprocal,
        value.y * reciprocal,
        value.z * reciprocal,
        value.w * reciprocal};
}

Vec3 Rotate(const Quaternion& orientation, const Vec3& value) noexcept
{
    const Vec3 axis{orientation.x, orientation.y, orientation.z};
    const Vec3 doubledCross = Scale(Cross(axis, value), 2.0F);
    return Add(
        value,
        Add(
            Scale(doubledCross, orientation.w),
            Cross(axis, doubledCross)));
}

Quaternion QuaternionFromBasis(
    const Vec3& right,
    const Vec3& up,
    const Vec3& forward) noexcept
{
    const float m00 = right.x;
    const float m01 = up.x;
    const float m02 = forward.x;
    const float m10 = right.y;
    const float m11 = up.y;
    const float m12 = forward.y;
    const float m20 = right.z;
    const float m21 = up.z;
    const float m22 = forward.z;
    const float trace = m00 + m11 + m22;
    Quaternion result = {};
    if (trace > 0.0F)
    {
        const float scale = std::sqrt(trace + 1.0F) * 2.0F;
        result.w = 0.25F * scale;
        result.x = (m21 - m12) / scale;
        result.y = (m02 - m20) / scale;
        result.z = (m10 - m01) / scale;
    }
    else if (m00 > m11 && m00 > m22)
    {
        const float scale = std::sqrt(1.0F + m00 - m11 - m22) * 2.0F;
        result.w = (m21 - m12) / scale;
        result.x = 0.25F * scale;
        result.y = (m01 + m10) / scale;
        result.z = (m02 + m20) / scale;
    }
    else if (m11 > m22)
    {
        const float scale = std::sqrt(1.0F + m11 - m00 - m22) * 2.0F;
        result.w = (m02 - m20) / scale;
        result.x = (m01 + m10) / scale;
        result.y = 0.25F * scale;
        result.z = (m12 + m21) / scale;
    }
    else
    {
        const float scale = std::sqrt(1.0F + m22 - m00 - m11) * 2.0F;
        result.w = (m10 - m01) / scale;
        result.x = (m02 + m20) / scale;
        result.y = (m12 + m21) / scale;
        result.z = 0.25F * scale;
    }
    return Normalize(result).value_or(Quaternion{});
}

std::optional<Quaternion> MakeHeadFacingOrientation(
    const Vec3& panelPosition,
    const Vec3& headPosition) noexcept
{
    const auto forward = Normalize(Subtract(headPosition, panelPosition));
    if (!forward.has_value())
    {
        return std::nullopt;
    }
    Vec3 referenceUp{0.0F, 1.0F, 0.0F};
    auto right = Normalize(Cross(referenceUp, *forward));
    if (!right.has_value())
    {
        referenceUp = {0.0F, 0.0F, -1.0F};
        right = Normalize(Cross(referenceUp, *forward));
    }
    if (!right.has_value())
    {
        return std::nullopt;
    }
    const auto up = Normalize(Cross(*forward, *right));
    return up.has_value()
        ? std::optional<Quaternion>(
              QuaternionFromBasis(*right, *up, *forward))
        : std::nullopt;
}

std::optional<Pose> MakeTargetPanelPose(
    const Pose& head,
    const Pose& grip) noexcept
{
    const auto headOrientation = Normalize(head.orientation);
    if (!headOrientation.has_value() || !IsFinite(head.position) ||
        !IsFinite(grip.position))
    {
        return std::nullopt;
    }
    const Vec3 rawForward = Rotate(
        *headOrientation,
        {0.0F, 0.0F, -1.0F});
    const auto horizontalForward = Normalize(
        Vec3{rawForward.x, 0.0F, rawForward.z});
    if (!horizontalForward.has_value())
    {
        return std::nullopt;
    }
    Pose target = {};
    target.position = Add(
        grip.position,
        Add(
            Scale(*horizontalForward, kHandForwardOffsetMeters),
            {0.0F, kHandUpOffsetMeters, 0.0F}));
    const auto orientation = MakeHeadFacingOrientation(
        target.position,
        head.position);
    if (!orientation.has_value())
    {
        return std::nullopt;
    }
    target.orientation = *orientation;
    return target;
}

Quaternion Slerp(
    const Quaternion& sourceValue,
    const Quaternion& targetValue,
    float amount) noexcept
{
    const auto source = Normalize(sourceValue);
    const auto targetNormalized = Normalize(targetValue);
    if (!source.has_value() || !targetNormalized.has_value())
    {
        return {};
    }
    Quaternion target = *targetNormalized;
    float dot = source->x * target.x + source->y * target.y +
        source->z * target.z + source->w * target.w;
    if (dot < 0.0F)
    {
        target = {-target.x, -target.y, -target.z, -target.w};
        dot = -dot;
    }
    dot = std::clamp(dot, -1.0F, 1.0F);
    const float clampedAmount = std::clamp(amount, 0.0F, 1.0F);
    if (dot > 0.9995F)
    {
        return Normalize(Quaternion{
            source->x + (target.x - source->x) * clampedAmount,
            source->y + (target.y - source->y) * clampedAmount,
            source->z + (target.z - source->z) * clampedAmount,
            source->w + (target.w - source->w) * clampedAmount})
            .value_or(*source);
    }
    const float angle = std::acos(dot);
    const float sine = std::sin(angle);
    if (sine <= kSmallValue)
    {
        return *source;
    }
    const float sourceWeight =
        std::sin((1.0F - clampedAmount) * angle) / sine;
    const float targetWeight = std::sin(clampedAmount * angle) / sine;
    return {
        source->x * sourceWeight + target.x * targetWeight,
        source->y * sourceWeight + target.y * targetWeight,
        source->z * sourceWeight + target.z * targetWeight,
        source->w * sourceWeight + target.w * targetWeight};
}

float ExponentialFollow(float elapsedSeconds, float halfLifeSeconds) noexcept
{
    constexpr float kNaturalLogTwo = 0.6931471805599453F;
    return 1.0F - std::exp(
        -kNaturalLogTwo * elapsedSeconds / halfLifeSeconds);
}

float QuaternionAngle(
    const Quaternion& lhsValue,
    const Quaternion& rhsValue) noexcept
{
    const auto lhs = Normalize(lhsValue);
    const auto rhs = Normalize(rhsValue);
    if (!lhs.has_value() || !rhs.has_value())
    {
        return 0.0F;
    }
    const float dot = std::fabs(
        lhs->x * rhs->x + lhs->y * rhs->y +
        lhs->z * rhs->z + lhs->w * rhs->w);
    return 2.0F * std::acos(std::clamp(dot, 0.0F, 1.0F));
}

bool IsTrackedInputValid(
    const bfvr::stereo::QuickMenuFrameInput& input) noexcept
{
    return input.predictedDisplayTime > 0 && input.sessionFocused &&
        input.shouldRender && input.headTracked && input.rightGripTracked &&
        input.rightAimTracked && IsFinite(input.headPose.position) &&
        IsFinite(input.headPose.orientation) &&
        IsFinite(input.rightGripPose.position) &&
        IsFinite(input.rightAimPose.position) &&
        IsFinite(input.rightAimPose.orientation);
}
} // namespace

namespace bfvr::stereo
{

QuickMenuSelection QuickMenuSelectionAt(
    float normalizedX,
    float normalizedY) noexcept
{
    if (!IsFinite(normalizedX) || !IsFinite(normalizedY) ||
        normalizedX < 0.0F || normalizedX > 1.0F ||
        normalizedY < 0.0F || normalizedY > 1.0F)
    {
        return QuickMenuSelection::None;
    }
    constexpr float topRowBottom = 96.0F / 512.0F;
    if (normalizedY < topRowBottom)
    {
        return normalizedX < 0.5F
            ? QuickMenuSelection::MainMenu
            : QuickMenuSelection::Deploy;
    }

    const float lowerY = std::clamp(
        (normalizedY - topRowBottom) / (1.0F - topRowBottom),
        0.0F,
        1.0F);
    constexpr float cameraColumnLeft = 420.0F / 512.0F;
    if (normalizedX >= cameraColumnLeft)
    {
        const int row = std::min(static_cast<int>(lowerY * 4.0F), 3);
        return static_cast<QuickMenuSelection>(
            static_cast<std::uint32_t>(QuickMenuSelection::CameraF9) + row);
    }

    constexpr float numberColumnSplit = 210.0F / 512.0F;
    const int row = std::min(static_cast<int>(lowerY * 3.0F), 2);
    const int column = normalizedX < numberColumnSplit ? 0 : 1;
    return static_cast<QuickMenuSelection>(
        static_cast<std::uint32_t>(QuickMenuSelection::Weapon1) +
        row * 2 + column);
}

const wchar_t* QuickMenuSelectionName(
    QuickMenuSelection selection) noexcept
{
    switch (selection)
    {
    case QuickMenuSelection::MainMenu: return L"Escape / main menu";
    case QuickMenuSelection::Deploy: return L"Enter / deploy menu";
    case QuickMenuSelection::Weapon1: return L"1 / weapon-seat slot";
    case QuickMenuSelection::Weapon2: return L"2 / weapon-seat slot";
    case QuickMenuSelection::Weapon3: return L"3 / weapon-seat slot";
    case QuickMenuSelection::Weapon4: return L"4 / weapon-seat slot";
    case QuickMenuSelection::Weapon5: return L"5 / weapon-seat slot";
    case QuickMenuSelection::Weapon6: return L"6 / weapon-seat slot";
    case QuickMenuSelection::CameraF9: return L"F9 / camera";
    case QuickMenuSelection::CameraF10: return L"F10 / camera";
    case QuickMenuSelection::CameraF11: return L"F11 / camera";
    case QuickMenuSelection::CameraF12: return L"F12 / camera";
    default: return L"none";
    }
}

Pose MakeQuickMenuCursorPose(
    const Pose& panelPose,
    float pointerU,
    float pointerV,
    float cursorWidthMeters,
    float cursorHeightMeters) noexcept
{
    const float clampedU = std::clamp(pointerU, 0.0F, 1.0F);
    const float clampedV = std::clamp(pointerV, 0.0F, 1.0F);
    const Vec3 localOffset = {
        (clampedU - 0.5F) * kQuickMenuWidthMeters +
            cursorWidthMeters * (0.5F - kQuickMenuCursorHotspotX),
        (0.5F - clampedV) * kQuickMenuHeightMeters -
            cursorHeightMeters * (0.5F - kQuickMenuCursorHotspotY),
        0.001F};
    Pose result = panelPose;
    result.position = Add(
        panelPose.position,
        Rotate(panelPose.orientation, localOffset));
    return result;
}

void QuickMenuInteraction::Update(
    const QuickMenuFrameInput& input) noexcept
{
    const bool trackingValid = IsTrackedInputValid(input);
    if (!trackingValid)
    {
        Cancel(
            visible_ || blockedUntilRelease_ ||
            input.rightPrimaryHeld);
        return;
    }
    if (!input.rightPrimaryHeld)
    {
        if (visible_ && !blockedUntilRelease_)
        {
            released_ = hovered_;
        }
        Cancel(false);
        return;
    }
    if (blockedUntilRelease_)
    {
        return;
    }
    const auto target = MakeTargetPanelPose(
        input.headPose,
        input.rightGripPose);
    if (!target.has_value())
    {
        Cancel(true);
        return;
    }
    if (!visible_)
    {
        panelPose_ = *target;
        visible_ = true;
    }
    else
    {
        const float elapsedSeconds = std::clamp(
            static_cast<float>(
                input.predictedDisplayTime - lastDisplayTime_) *
                0.000000001F,
            0.0F,
            kMaximumFrameSeconds);
        const Vec3 positionDelta = Subtract(
            target->position,
            panelPose_.position);
        const float distance = Length(positionDelta);
        if (elapsedSeconds > 0.0F && distance > kPositionDeadzoneMeters)
        {
            const float deadzoneScale =
                1.0F - kPositionDeadzoneMeters / distance;
            const float follow = ExponentialFollow(
                elapsedSeconds,
                kPositionHalfLifeSeconds);
            panelPose_.position = Add(
                panelPose_.position,
                Scale(positionDelta, deadzoneScale * follow));
        }

        const auto facing = MakeHeadFacingOrientation(
            panelPose_.position,
            input.headPose.position);
        if (!facing.has_value())
        {
            Cancel(true);
            return;
        }
        const float orientationAngle = QuaternionAngle(
            panelPose_.orientation,
            *facing);
        if (elapsedSeconds > 0.0F &&
            orientationAngle > kOrientationDeadzoneRadians)
        {
            const float deadzoneScale =
                1.0F - kOrientationDeadzoneRadians / orientationAngle;
            const float follow = ExponentialFollow(
                elapsedSeconds,
                kOrientationHalfLifeSeconds);
            const Quaternion smoothed = Slerp(
                panelPose_.orientation,
                *facing,
                deadzoneScale * follow);
            const Vec3 smoothedForward = Rotate(
                smoothed,
                {0.0F, 0.0F, 1.0F});
            const auto rollFree = MakeHeadFacingOrientation(
                panelPose_.position,
                Add(panelPose_.position, smoothedForward));
            panelPose_.orientation = rollFree.value_or(*facing);
        }
    }
    lastDisplayTime_ = input.predictedDisplayTime;

    const auto hit = MapOpenXRAimPoseToAspectFitUiCanvas(
        input.rightAimPose,
        panelPose_,
        kQuickMenuWidthMeters,
        kQuickMenuHeightMeters,
        kQuickMenuTextureSize,
        kQuickMenuTextureSize,
        kQuickMenuTextureSize,
        kQuickMenuTextureSize,
        kQuickMenuTextureSize,
        kQuickMenuTextureSize);
    pointerVisible_ = hit.has_value();
    hovered_ = hit.has_value()
        ? QuickMenuSelectionAt(hit->normalizedX, hit->normalizedY)
        : QuickMenuSelection::None;
    if (hit.has_value())
    {
        pointerU_ = hit->normalizedX;
        pointerV_ = hit->normalizedY;
    }
}

void QuickMenuInteraction::Reset() noexcept
{
    panelPose_ = {};
    lastDisplayTime_ = 0;
    hovered_ = QuickMenuSelection::None;
    released_ = QuickMenuSelection::None;
    pointerU_ = 0.0F;
    pointerV_ = 0.0F;
    visible_ = false;
    pointerVisible_ = false;
    blockedUntilRelease_ = false;
}

QuickMenuInteractionSnapshot QuickMenuInteraction::Snapshot() const noexcept
{
    QuickMenuInteractionSnapshot result = {};
    result.visible = visible_;
    result.pointerVisible = visible_ && pointerVisible_;
    result.pointerU = pointerU_;
    result.pointerV = pointerV_;
    result.panelPose = panelPose_;
    result.hovered = visible_ ? hovered_ : QuickMenuSelection::None;
    return result;
}

QuickMenuSelection QuickMenuInteraction::TakeReleasedSelection() noexcept
{
    const QuickMenuSelection result = released_;
    released_ = QuickMenuSelection::None;
    return result;
}

void QuickMenuInteraction::Cancel(bool blockUntilRelease) noexcept
{
    visible_ = false;
    pointerVisible_ = false;
    hovered_ = QuickMenuSelection::None;
    lastDisplayTime_ = 0;
    blockedUntilRelease_ = blockUntilRelease;
}

} // namespace bfvr::stereo
