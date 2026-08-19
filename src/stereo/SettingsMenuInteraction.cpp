#include "stereo/SettingsMenuInteraction.h"

#include <algorithm>
#include <cmath>

namespace
{
using bfvr::stereo::Pose;
using bfvr::stereo::Quaternion;
using bfvr::stereo::Vec3;

bool IsFinite(float value) noexcept
{
    return std::isfinite(value);
}

Vec3 Add(const Vec3& lhs, const Vec3& rhs) noexcept
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

Vec3 Scale(const Vec3& value, float amount) noexcept
{
    return {value.x * amount, value.y * amount, value.z * amount};
}

Vec3 Cross(const Vec3& lhs, const Vec3& rhs) noexcept
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x};
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

bool IsHeadTrackingValid(
    const bfvr::stereo::QuickMenuFrameInput& input) noexcept
{
    return input.predictedDisplayTime > 0 && input.shouldRender &&
        input.headTracked;
}

bool IsPointerTrackingValid(
    const bfvr::stereo::QuickMenuFrameInput& input) noexcept
{
    return input.sessionFocused && input.shouldRender &&
        input.rightAimTracked;
}

bool IsSliderSelection(
    bfvr::stereo::SettingsMenuSelection selection) noexcept
{
    using bfvr::stereo::SettingsMenuSelection;
    return selection == SettingsMenuSelection::InfantryTurnSpeed ||
        selection == SettingsMenuSelection::VehicleMotionAimSensitivity ||
        selection == SettingsMenuSelection::VrHeightAdjustment ||
        selection == SettingsMenuSelection::CrosshairOpacity ||
        selection == SettingsMenuSelection::FxaaSharpening ||
        selection == SettingsMenuSelection::AmbientOcclusionRadius ||
        selection == SettingsMenuSelection::AmbientOcclusionStrength ||
        selection == SettingsMenuSelection::BloomThreshold ||
        selection == SettingsMenuSelection::BloomIntensity ||
        selection == SettingsMenuSelection::ColorExposure ||
        selection == SettingsMenuSelection::ColorContrast ||
        selection == SettingsMenuSelection::ColorSaturation;
}
} // namespace

namespace bfvr::stereo
{

SettingsMenuSelection SettingsMenuSelectionAt(
    float normalizedX,
    float normalizedY,
    bool arrowLeftVisible,
    bool arrowRightVisible,
    SettingsMenuTab tab,
    bool controllerLayoutVisible,
    std::uint32_t page,
    settings::ArtificialTurnMode turnMode) noexcept
{
    if (!IsFinite(normalizedX) || !IsFinite(normalizedY) ||
        normalizedX < 0.0F || normalizedX > 1.0F ||
        normalizedY < 0.0F || normalizedY > 1.0F)
    {
        return SettingsMenuSelection::None;
    }
    const float pixelX = normalizedX * kSettingsMenuTextureSize;
    const float pixelY = normalizedY * kSettingsMenuTextureSize;
    if (pixelY >= 16.0F && pixelY <= 88.0F)
    {
        if (pixelX <= 346.0F)
        {
            return SettingsMenuSelection::TabVrSettings;
        }
        if (pixelX <= 678.0F)
        {
            return SettingsMenuSelection::TabControls;
        }
        return SettingsMenuSelection::TabGraphicsAudio;
    }
    if (pixelY >= 849.0F && pixelY <= 932.0F)
    {
        if (arrowLeftVisible && pixelX >= 563.0F && pixelX <= 643.0F)
        {
            return SettingsMenuSelection::ArrowLeft;
        }
        if (arrowRightVisible && pixelX >= 635.0F && pixelX <= 711.0F)
        {
            return SettingsMenuSelection::ArrowRight;
        }
        if (pixelX >= 696.0F && pixelX <= 992.0F)
        {
            return SettingsMenuSelection::ControllerLayout;
        }
    }
    if (pixelY >= 941.0F && pixelY <= 1024.0F)
    {
        if (pixelX <= 346.0F)
        {
            return SettingsMenuSelection::Save;
        }
        if (pixelX <= 681.0F)
        {
            return SettingsMenuSelection::Cancel;
        }
        return SettingsMenuSelection::ResetDefaults;
    }
    if (controllerLayoutVisible)
    {
        return SettingsMenuSelection::None;
    }
    if (tab == SettingsMenuTab::VrSettings && page == 0)
    {
        const auto selectorAt = [&](float centerY,
                                    SettingsMenuSelection previous) {
            if (pixelY < centerY - 38.0F || pixelY > centerY + 38.0F)
            {
                return SettingsMenuSelection::None;
            }
            if (pixelX >= kSettingsMenuSelectorLeftArrowCenterPixels - 40.0F &&
                pixelX <= kSettingsMenuSelectorLeftArrowCenterPixels + 40.0F)
            {
                return previous;
            }
            if (pixelX >= kSettingsMenuSelectorRightArrowCenterPixels - 40.0F &&
                pixelX <= kSettingsMenuSelectorRightArrowCenterPixels + 40.0F)
            {
                return static_cast<SettingsMenuSelection>(
                    static_cast<std::uint32_t>(previous) + 1U);
            }
            return SettingsMenuSelection::None;
        };
        SettingsMenuSelection selection = selectorAt(
            kSettingsMenuVrPageOneRowCentersPixels[0],
            SettingsMenuSelection::PlayModePrevious);
        if (selection != SettingsMenuSelection::None) return selection;
        selection = selectorAt(
            kSettingsMenuVrPageOneRowCentersPixels[1],
            SettingsMenuSelection::ArtificialTurnPrevious);
        if (selection != SettingsMenuSelection::None) return selection;
        selection = selectorAt(
            kSettingsMenuVrPageOneRowCentersPixels[3],
            SettingsMenuSelection::MovementDirectionPrevious);
        if (selection != SettingsMenuSelection::None) return selection;
        if (pixelY >= kSettingsMenuVrPageOneRowCentersPixels[2] - 38.0F &&
            pixelY <= kSettingsMenuVrPageOneRowCentersPixels[2] + 38.0F)
        {
            if (turnMode == settings::ArtificialTurnMode::Snap)
            {
                return selectorAt(
                    kSettingsMenuVrPageOneRowCentersPixels[2],
                    SettingsMenuSelection::SnapAnglePrevious);
            }
            if (turnMode == settings::ArtificialTurnMode::Smooth &&
                pixelX >= kSettingsMenuControlColumnPixels &&
                pixelX <= kSettingsMenuSliderRightPixels)
            {
                return SettingsMenuSelection::InfantryTurnSpeed;
            }
        }
    }
    if (tab == SettingsMenuTab::VrSettings && page == 1)
    {
        if (pixelX >= kSettingsMenuControlColumnPixels &&
            pixelX <= kSettingsMenuControlColumnPixels + 72.0F &&
            pixelY >= kSettingsMenuVrPageTwoRowCentersPixels[0] - 38.0F &&
            pixelY <= kSettingsMenuVrPageTwoRowCentersPixels[0] + 38.0F)
        {
            return SettingsMenuSelection::ComfortVignetteEnabled;
        }
        if (pixelX >= kSettingsMenuControlColumnPixels &&
            pixelX <= kSettingsMenuControlColumnPixels + 72.0F &&
            pixelY >= kSettingsMenuVrPageTwoRowCentersPixels[1] - 38.0F &&
            pixelY <= kSettingsMenuVrPageTwoRowCentersPixels[1] + 38.0F)
        {
            return SettingsMenuSelection::DeathCameraComfortEnabled;
        }
        const float showCenterY = kSettingsMenuVrPageTwoRowCentersPixels[2];
        if (pixelY >= showCenterY - 38.0F &&
            pixelY <= showCenterY + 38.0F)
        {
            if (pixelX >= kSettingsMenuSelectorLeftArrowCenterPixels - 40.0F &&
                pixelX <= kSettingsMenuSelectorLeftArrowCenterPixels + 40.0F)
            {
                return SettingsMenuSelection::ShowPrevious;
            }
            if (pixelX >= kSettingsMenuSelectorRightArrowCenterPixels - 40.0F &&
                pixelX <= kSettingsMenuSelectorRightArrowCenterPixels + 40.0F)
            {
                return SettingsMenuSelection::ShowNext;
            }
        }
        if (pixelX >= kSettingsMenuControlColumnPixels &&
            pixelX <= kSettingsMenuControlColumnPixels + 72.0F &&
            pixelY >= kSettingsMenuVrPageTwoRowCentersPixels[3] - 38.0F &&
            pixelY <= kSettingsMenuVrPageTwoRowCentersPixels[3] + 38.0F)
        {
            return SettingsMenuSelection::MenuPointerSmoothingEnabled;
        }
    }
    if (tab == SettingsMenuTab::VrSettings && page == 2)
    {
        if (pixelX >= kSettingsMenuControlColumnPixels &&
            pixelX <= kSettingsMenuSliderRightPixels &&
            pixelY >= kSettingsMenuVrPageThreeRowCentersPixels[0] - 38.0F &&
            pixelY <= kSettingsMenuVrPageThreeRowCentersPixels[0] + 38.0F)
        {
            return SettingsMenuSelection::VrHeightAdjustment;
        }
        if (pixelX >= 350.0F && pixelX <= 930.0F &&
            pixelY >= kSettingsMenuVrPageThreeRowCentersPixels[1] - 42.0F &&
            pixelY <= kSettingsMenuVrPageThreeRowCentersPixels[1] + 42.0F)
        {
            return SettingsMenuSelection::AutoCalibrateStandingHeight;
        }
        if (pixelX >= 350.0F && pixelX <= 930.0F &&
            pixelY >= kSettingsMenuVrPageThreeRowCentersPixels[2] - 42.0F &&
            pixelY <= kSettingsMenuVrPageThreeRowCentersPixels[2] + 42.0F)
        {
            return SettingsMenuSelection::RecenterForward;
        }
    }
    if (tab == SettingsMenuTab::Controls && page == 0)
    {
        const float gripY = kSettingsMenuControlsGripRowCenterPixels;
        if (pixelY >= gripY - 38.0F && pixelY <= gripY + 38.0F)
        {
            if (pixelX >= kSettingsMenuSelectorLeftArrowCenterPixels - 40.0F &&
                pixelX <= kSettingsMenuSelectorLeftArrowCenterPixels + 40.0F)
            {
                return SettingsMenuSelection::OffHandGripPrevious;
            }
            if (pixelX >= kSettingsMenuSelectorRightArrowCenterPixels - 40.0F &&
                pixelX <= kSettingsMenuSelectorRightArrowCenterPixels + 40.0F)
            {
                return SettingsMenuSelection::OffHandGripNext;
            }
        }
        for (std::size_t index = 0;
             index < kSettingsMenuControlsToggleRowCentersPixels.size();
             ++index)
        {
            if (pixelX >= kSettingsMenuControlColumnPixels &&
                pixelX <= kSettingsMenuControlColumnPixels + 72.0F &&
                pixelY >= kSettingsMenuControlsToggleRowCentersPixels[index] - 26.0F &&
                pixelY <= kSettingsMenuControlsToggleRowCentersPixels[index] + 26.0F)
            {
                return static_cast<SettingsMenuSelection>(
                    static_cast<std::uint32_t>(
                        SettingsMenuSelection::InvertFlightPitch) +
                    static_cast<std::uint32_t>(index));
            }
        }
    }
    if (tab == SettingsMenuTab::Controls && page == 1)
    {
        for (std::size_t index = 0;
             index < kSettingsMenuControlsCrosshairRowCentersPixels.size();
             ++index)
        {
            const float centerY =
                kSettingsMenuControlsCrosshairRowCentersPixels[index];
            if (pixelY < centerY - 38.0F || pixelY > centerY + 38.0F)
            {
                continue;
            }
            const SettingsMenuSelection previous = static_cast<
                SettingsMenuSelection>(
                    static_cast<std::uint32_t>(
                        SettingsMenuSelection::HandCrosshairPrevious) +
                    static_cast<std::uint32_t>(index) * 2U);
            if (pixelX >= kSettingsMenuSelectorLeftArrowCenterPixels - 40.0F &&
                pixelX <= kSettingsMenuSelectorLeftArrowCenterPixels + 40.0F)
            {
                return previous;
            }
            if (pixelX >= kSettingsMenuSelectorRightArrowCenterPixels - 40.0F &&
                pixelX <= kSettingsMenuSelectorRightArrowCenterPixels + 40.0F)
            {
                return static_cast<SettingsMenuSelection>(
                    static_cast<std::uint32_t>(previous) + 1U);
            }
        }
        const float colorY =
            kSettingsMenuControlsCrosshairColorRowCenterPixels;
        if (pixelY >= colorY - 38.0F && pixelY <= colorY + 38.0F)
        {
            if (pixelX >= kSettingsMenuSelectorLeftArrowCenterPixels - 40.0F &&
                pixelX <= kSettingsMenuSelectorLeftArrowCenterPixels + 40.0F)
            {
                return SettingsMenuSelection::CrosshairColorPrevious;
            }
            if (pixelX >= kSettingsMenuSelectorRightArrowCenterPixels - 40.0F &&
                pixelX <= kSettingsMenuSelectorRightArrowCenterPixels + 40.0F)
            {
                return SettingsMenuSelection::CrosshairColorNext;
            }
        }
        const float opacityY =
            kSettingsMenuControlsCrosshairOpacityRowCenterPixels;
        if (pixelX >= kSettingsMenuControlColumnPixels &&
            pixelX <= kSettingsMenuSliderRightPixels &&
            pixelY >= opacityY - 38.0F && pixelY <= opacityY + 38.0F)
        {
            return SettingsMenuSelection::CrosshairOpacity;
        }
    }
    if (tab == SettingsMenuTab::Controls && page == 2)
    {
        const float centerY =
            kSettingsMenuControlsVehicleAimSliderCenterPixels;
        if (pixelX >= kSettingsMenuControlColumnPixels &&
            pixelX <= kSettingsMenuSliderRightPixels &&
            pixelY >= centerY - 38.0F &&
            pixelY <= centerY + 38.0F)
        {
            return SettingsMenuSelection::VehicleMotionAimSensitivity;
        }
    }
    if (tab == SettingsMenuTab::GraphicsAudio && page == 0)
    {
        for (std::size_t index = 0;
             index < kSettingsMenuGraphicsRowCentersPixels.size();
             ++index)
        {
            const float centerY =
                kSettingsMenuGraphicsRowCentersPixels[index];
            const bool toggle =
                index == 0 || index == 2 || index == 5 || index == 8;
            const float right = toggle
                ? kSettingsMenuControlColumnPixels + 72.0F
                : kSettingsMenuSliderRightPixels;
            if (pixelX >= kSettingsMenuControlColumnPixels &&
                pixelX <= right &&
                pixelY >= centerY - 38.0F &&
                pixelY <= centerY + 38.0F)
            {
                return static_cast<SettingsMenuSelection>(
                    static_cast<std::uint32_t>(
                        SettingsMenuSelection::FxaaEnabled) +
                    static_cast<std::uint32_t>(index));
            }
        }
    }
    if (tab == SettingsMenuTab::GraphicsAudio && page == 1)
    {
        const float profileY = kSettingsMenuColorRowCentersPixels[0];
        if (pixelY >= profileY - 38.0F && pixelY <= profileY + 38.0F)
        {
            if (pixelX >= kSettingsMenuSelectorLeftArrowCenterPixels - 40.0F &&
                pixelX <= kSettingsMenuSelectorLeftArrowCenterPixels + 40.0F)
            {
                return SettingsMenuSelection::ColorProfilePrevious;
            }
            if (pixelX >= kSettingsMenuSelectorRightArrowCenterPixels - 40.0F &&
                pixelX <= kSettingsMenuSelectorRightArrowCenterPixels + 40.0F)
            {
                return SettingsMenuSelection::ColorProfileNext;
            }
        }
        for (std::size_t index = 1; index <= 3; ++index)
        {
            if (pixelX >= kSettingsMenuControlColumnPixels &&
                pixelX <= kSettingsMenuSliderRightPixels &&
                pixelY >= kSettingsMenuColorRowCentersPixels[index] - 38.0F &&
                pixelY <= kSettingsMenuColorRowCentersPixels[index] + 38.0F)
            {
                return static_cast<SettingsMenuSelection>(
                    static_cast<std::uint32_t>(
                        SettingsMenuSelection::ColorExposure) +
                    static_cast<std::uint32_t>(index - 1));
            }
        }
        if (pixelX >= 350.0F && pixelX <= 930.0F &&
            pixelY >= kSettingsMenuColorRowCentersPixels[4] - 42.0F &&
            pixelY <= kSettingsMenuColorRowCentersPixels[4] + 42.0F)
        {
            return SettingsMenuSelection::ResetColorSettings;
        }
    }
    if (tab == SettingsMenuTab::GraphicsAudio && page == 2 &&
        pixelX >= kSettingsMenuControlColumnPixels &&
        pixelX <= kSettingsMenuControlColumnPixels + 72.0F &&
        pixelY >= kSettingsMenuAudioRowCenterPixels - 38.0F &&
        pixelY <= kSettingsMenuAudioRowCenterPixels + 38.0F)
    {
        return SettingsMenuSelection::KillSoundEnabled;
    }
    return SettingsMenuSelection::None;
}

const wchar_t* SettingsMenuSelectionName(
    SettingsMenuSelection selection) noexcept
{
    switch (selection)
    {
    case SettingsMenuSelection::TabVrSettings: return L"VR Settings tab";
    case SettingsMenuSelection::TabControls: return L"Controls tab";
    case SettingsMenuSelection::TabGraphicsAudio:
        return L"Graphics / Audio tab";
    case SettingsMenuSelection::ControllerLayout:
        return L"Controller Layout toggle";
    case SettingsMenuSelection::ArrowLeft: return L"previous page";
    case SettingsMenuSelection::ArrowRight: return L"next page";
    case SettingsMenuSelection::Save: return L"Save";
    case SettingsMenuSelection::Cancel: return L"Cancel";
    case SettingsMenuSelection::ResetDefaults:
        return L"Reset to Defaults";
    case SettingsMenuSelection::InfantryTurnSpeed:
        return L"Turn Speed slider";
    case SettingsMenuSelection::PlayModePrevious: return L"previous Play Mode";
    case SettingsMenuSelection::PlayModeNext: return L"next Play Mode";
    case SettingsMenuSelection::ArtificialTurnPrevious:
        return L"previous Artificial Turning mode";
    case SettingsMenuSelection::ArtificialTurnNext:
        return L"next Artificial Turning mode";
    case SettingsMenuSelection::SnapAnglePrevious:
        return L"previous Snap Angle";
    case SettingsMenuSelection::SnapAngleNext: return L"next Snap Angle";
    case SettingsMenuSelection::MovementDirectionPrevious:
        return L"previous Movement Direction";
    case SettingsMenuSelection::MovementDirectionNext:
        return L"next Movement Direction";
    case SettingsMenuSelection::VrHeightAdjustment:
        return L"Manual Height Adjustment slider";
    case SettingsMenuSelection::AutoCalibrateStandingHeight:
        return L"Auto-Calibrate Standing Height";
    case SettingsMenuSelection::RecenterForward: return L"Recenter Forward";
    case SettingsMenuSelection::ComfortVignetteEnabled:
        return L"Comfort Vignette";
    case SettingsMenuSelection::ShowPrevious:
        return L"previous Show mode";
    case SettingsMenuSelection::ShowNext: return L"next Show mode";
    case SettingsMenuSelection::DeathCameraComfortEnabled:
        return L"Death Camera Comfort";
    case SettingsMenuSelection::MenuPointerSmoothingEnabled:
        return L"Menu Pointer Smoothing";
    case SettingsMenuSelection::CrosshairColorPrevious:
        return L"previous 3D Crosshair Color";
    case SettingsMenuSelection::CrosshairColorNext:
        return L"next 3D Crosshair Color";
    case SettingsMenuSelection::CrosshairOpacity:
        return L"3D Crosshair Opacity";
    case SettingsMenuSelection::OffHandGripPrevious:
        return L"previous Off-hand Grip Style";
    case SettingsMenuSelection::OffHandGripNext:
        return L"next Off-hand Grip Style";
    case SettingsMenuSelection::HandCrosshairPrevious:
        return L"previous Hand Weapons crosshair mode";
    case SettingsMenuSelection::HandCrosshairNext:
        return L"next Hand Weapons crosshair mode";
    case SettingsMenuSelection::MountedCrosshairPrevious:
        return L"previous Mounted Guns crosshair mode";
    case SettingsMenuSelection::MountedCrosshairNext:
        return L"next Mounted Guns crosshair mode";
    case SettingsMenuSelection::PointerItemCrosshairPrevious:
        return L"previous Knives / Throwables / Gadgets crosshair mode";
    case SettingsMenuSelection::PointerItemCrosshairNext:
        return L"next Knives / Throwables / Gadgets crosshair mode";
    case SettingsMenuSelection::InvertFlightPitch:
        return L"Flight Pitch inversion";
    case SettingsMenuSelection::InvertTurretPitch:
        return L"Turret Pitch inversion";
    case SettingsMenuSelection::InvertTurretYaw:
        return L"Turret Yaw inversion";
    case SettingsMenuSelection::ControllerHapticsEnabled:
        return L"Controller Haptics";
    case SettingsMenuSelection::SniperScopeSmoothingEnabled:
        return L"Sniper Aim Smoothing";
    case SettingsMenuSelection::VehicleMotionAimSensitivity:
        return L"Vehicle Turret Motion Sensitivity slider";
    case SettingsMenuSelection::AircraftPitchWithRoll:
        return L"Aircraft Pitch and Roll on Same Stick";
    case SettingsMenuSelection::SwapAircraftSticks:
        return L"Swap Aircraft Sticks";
    case SettingsMenuSelection::FxaaEnabled: return L"FXAA";
    case SettingsMenuSelection::FxaaSharpening:
        return L"FXAA Sharpening slider";
    case SettingsMenuSelection::AmbientOcclusionEnabled:
        return L"Ambient Occlusion";
    case SettingsMenuSelection::AmbientOcclusionRadius:
        return L"Ambient Occlusion Radius slider";
    case SettingsMenuSelection::AmbientOcclusionStrength:
        return L"Ambient Occlusion Strength slider";
    case SettingsMenuSelection::BloomEnabled: return L"Bloom";
    case SettingsMenuSelection::BloomThreshold:
        return L"Bloom Threshold slider";
    case SettingsMenuSelection::BloomIntensity:
        return L"Bloom Intensity slider";
    case SettingsMenuSelection::WaterReflectionsEnabled:
        return L"Water SSR";
    case SettingsMenuSelection::ColorProfilePrevious:
        return L"previous Color Profile";
    case SettingsMenuSelection::ColorProfileNext:
        return L"next Color Profile";
    case SettingsMenuSelection::ColorExposure:
        return L"Exposure slider";
    case SettingsMenuSelection::ColorContrast:
        return L"Contrast slider";
    case SettingsMenuSelection::ColorSaturation:
        return L"Saturation slider";
    case SettingsMenuSelection::ResetColorSettings:
        return L"Reset Color Settings";
    case SettingsMenuSelection::KillSoundEnabled:
        return L"Kill Sound";
    default: return L"none";
    }
}

Pose MakeSettingsMenuCursorPose(
    const Pose& panelPose,
    float panelWidthMeters,
    float panelHeightMeters,
    float pointerU,
    float pointerV,
    float cursorWidthMeters,
    float cursorHeightMeters) noexcept
{
    const float clampedU = std::clamp(pointerU, 0.0F, 1.0F);
    const float clampedV = std::clamp(pointerV, 0.0F, 1.0F);
    const Vec3 localOffset = {
        (clampedU - 0.5F) * panelWidthMeters + cursorWidthMeters * 0.5F,
        (0.5F - clampedV) * panelHeightMeters - cursorHeightMeters * 0.5F,
        0.001F};
    Pose result = panelPose;
    result.position = Add(
        panelPose.position,
        Rotate(panelPose.orientation, localOffset));
    return result;
}

void SettingsMenuInteraction::Configure(
    float widthMeters,
    float distanceMeters) noexcept
{
    if (IsFinite(widthMeters) && widthMeters > 0.0F)
    {
        widthMeters_ = widthMeters;
    }
    if (IsFinite(distanceMeters) && distanceMeters > 0.0F)
    {
        distanceMeters_ = distanceMeters;
    }
}

void SettingsMenuInteraction::Open() noexcept
{
    Reset();
    active_ = true;
}

void SettingsMenuInteraction::SetValues(
    const settings::UserSettingsValues& values) noexcept
{
    values_ = values;
    values_.infantryTurnSpeedPercent = std::clamp(
        values_.infantryTurnSpeedPercent,
        settings::kMinimumInfantryTurnSpeedPercent,
        settings::kMaximumInfantryTurnSpeedPercent);
    values_.infantryTurnSpeedPercent =
        ((values_.infantryTurnSpeedPercent +
          settings::kInfantryTurnSpeedStepPercent / 2U) /
        settings::kInfantryTurnSpeedStepPercent) *
        settings::kInfantryTurnSpeedStepPercent;
    const auto snap = [](std::uint32_t value,
                         std::uint32_t minimum,
                         std::uint32_t maximum,
                         std::uint32_t step) {
        const std::uint32_t clamped = std::clamp(value, minimum, maximum);
        return minimum +
            ((clamped - minimum + step / 2U) / step) * step;
    };
    values_.snapTurnAngleDegrees = snap(
        values_.snapTurnAngleDegrees,
        settings::kMinimumSnapTurnAngleDegrees,
        settings::kMaximumSnapTurnAngleDegrees,
        settings::kSnapTurnAngleStepDegrees);
    values_.crosshairOpacityPercent = snap(
        values_.crosshairOpacityPercent,
        settings::kMinimumCrosshairOpacityPercent,
        settings::kMaximumCrosshairOpacityPercent,
        settings::kCrosshairOpacityStepPercent);
    values_.vrHeightAdjustmentCentimeters = std::clamp(
        values_.vrHeightAdjustmentCentimeters,
        settings::kMinimumVrHeightAdjustmentCentimeters,
        settings::kMaximumVrHeightAdjustmentCentimeters);
    values_.ambientOcclusionRadiusCentimeters = snap(
        values_.ambientOcclusionRadiusCentimeters,
        settings::kMinimumAmbientOcclusionRadiusCentimeters,
        settings::kMaximumAmbientOcclusionRadiusCentimeters,
        settings::kAmbientOcclusionRadiusStepCentimeters);
    values_.ambientOcclusionStrengthPercent = snap(
        values_.ambientOcclusionStrengthPercent,
        settings::kMinimumAmbientOcclusionStrengthPercent,
        settings::kMaximumAmbientOcclusionStrengthPercent,
        settings::kAmbientOcclusionStrengthStepPercent);
    values_.bloomThresholdPercent = snap(
        values_.bloomThresholdPercent,
        settings::kMinimumBloomThresholdPercent,
        settings::kMaximumBloomThresholdPercent,
        settings::kBloomThresholdStepPercent);
    values_.bloomIntensityPercent = snap(
        values_.bloomIntensityPercent,
        settings::kMinimumBloomIntensityPercent,
        settings::kMaximumBloomIntensityPercent,
        settings::kBloomIntensityStepPercent);
    values_.colorExposureTenthsEv = std::clamp(
        values_.colorExposureTenthsEv,
        settings::kMinimumColorExposureTenthsEv,
        settings::kMaximumColorExposureTenthsEv);
    values_.colorContrastPercent = std::clamp(
        values_.colorContrastPercent,
        settings::kMinimumColorContrastPercent,
        settings::kMaximumColorContrastPercent);
    values_.colorSaturationPercent = std::clamp(
        values_.colorSaturationPercent,
        settings::kMinimumColorSaturationPercent,
        settings::kMaximumColorSaturationPercent);
}

void SettingsMenuInteraction::SetStatus(SettingsMenuStatus status) noexcept
{
    status_ = status;
}

void SettingsMenuInteraction::Update(
    const QuickMenuFrameInput& input) noexcept
{
    if (!active_)
    {
        return;
    }
    standingHeightValid_ = input.standingHeightValid &&
        IsFinite(input.standingHeightMeters) &&
        input.standingHeightMeters > 0.5F &&
        input.standingHeightMeters < 2.5F;
    standingHeightMeters_ = standingHeightValid_
        ? input.standingHeightMeters
        : 0.0F;
    if (IsHeadTrackingValid(input) &&
        UpdateUiMenuAnchor(
            anchor_,
            input.headPose,
            input.predictedDisplayTime,
            kSettingsMenuFollowStartRadians,
            kSettingsMenuFollowRadiansPerSecond))
    {
        panelPose_ = anchor_.anchor;
        panelPose_.position = Add(
            anchor_.anchor.position,
            Rotate(
                anchor_.anchor.orientation,
                {0.0F, 0.0F, -distanceMeters_}));
        poseValid_ = true;
    }

    pointerVisible_ = false;
    hovered_ = SettingsMenuSelection::None;
    if (poseValid_ && IsPointerTrackingValid(input))
    {
        const auto hit = MapOpenXRAimPoseToAspectFitUiCanvas(
            input.rightAimPose,
            panelPose_,
            widthMeters_,
            widthMeters_,
            kSettingsMenuTextureSize,
            kSettingsMenuTextureSize,
            kSettingsMenuTextureSize,
            kSettingsMenuTextureSize,
            kSettingsMenuTextureSize,
            kSettingsMenuTextureSize);
        if (hit.has_value())
        {
            pointerVisible_ = true;
            const UiPointerPoint filtered = pointerSmoother_.Update(
                hit->normalizedX,
                hit->normalizedY,
                input.predictedDisplayTime,
                values_.menuPointerSmoothingEnabled);
            pointerU_ = filtered.x;
            pointerV_ = filtered.y;
            hovered_ = SettingsMenuSelectionAt(
                pointerU_,
                pointerV_,
                page_ > 0,
                page_ + 1 < PageCount(),
                tab_,
                controllerLayoutVisible_,
                page_,
                values_.artificialTurnMode);
        }
    }

    if (input.rightPrimaryHeld && !primaryWasHeld_)
    {
        pressed_ = hovered_;
        sliderDragging_ = IsSliderSelection(hovered_);
        if (sliderDragging_)
        {
            menuSoundActivation_ = pressed_;
            if (pressed_ == SettingsMenuSelection::InfantryTurnSpeed)
            {
                SetTurnSpeedFromPointer(pointerU_);
            }
            else if (pressed_ ==
                     SettingsMenuSelection::VehicleMotionAimSensitivity)
            {
                SetVehicleMotionAimSensitivityFromPointer(pointerU_);
            }
            else if (pressed_ == SettingsMenuSelection::VrHeightAdjustment)
            {
                SetHeightAdjustmentFromPointer(pointerU_);
            }
            else if (pressed_ == SettingsMenuSelection::CrosshairOpacity)
            {
                SetCrosshairOpacityFromPointer(pointerU_);
            }
            else
            {
                SetGraphicsSliderFromPointer(pressed_, pointerU_);
            }
        }
    }
    else if (input.rightPrimaryHeld && sliderDragging_ && pointerVisible_)
    {
        if (pressed_ == SettingsMenuSelection::InfantryTurnSpeed)
        {
            SetTurnSpeedFromPointer(pointerU_);
        }
        else if (pressed_ ==
                 SettingsMenuSelection::VehicleMotionAimSensitivity)
        {
            SetVehicleMotionAimSensitivityFromPointer(pointerU_);
        }
        else if (pressed_ == SettingsMenuSelection::VrHeightAdjustment)
        {
            SetHeightAdjustmentFromPointer(pointerU_);
        }
        else if (pressed_ == SettingsMenuSelection::CrosshairOpacity)
        {
            SetCrosshairOpacityFromPointer(pointerU_);
        }
        else
        {
            SetGraphicsSliderFromPointer(pressed_, pointerU_);
        }
    }
    else if (!input.rightPrimaryHeld && primaryWasHeld_)
    {
        if (!sliderDragging_ && pressed_ != SettingsMenuSelection::None &&
            pressed_ == hovered_)
        {
            Activate(pressed_);
        }
        sliderDragging_ = false;
        pressed_ = SettingsMenuSelection::None;
    }
    primaryWasHeld_ = input.rightPrimaryHeld;
    if (!pointerVisible_)
    {
        pointerSmoother_.Reset();
    }
}

void SettingsMenuInteraction::Reset() noexcept
{
    ResetUiMenuAnchor(anchor_);
    pointerSmoother_.Reset();
    panelPose_ = {};
    tab_ = SettingsMenuTab::VrSettings;
    hovered_ = SettingsMenuSelection::None;
    pressed_ = SettingsMenuSelection::None;
    menuSoundActivation_ = SettingsMenuSelection::None;
    command_ = SettingsMenuCommand::None;
    values_ = {};
    status_ = SettingsMenuStatus::SettingsLoaded;
    page_ = 0;
    pointerU_ = 0.0F;
    pointerV_ = 0.0F;
    active_ = false;
    poseValid_ = false;
    pointerVisible_ = false;
    controllerLayoutVisible_ = false;
    primaryWasHeld_ = false;
    sliderDragging_ = false;
    valuesChanged_ = false;
    standingHeightValid_ = false;
    standingHeightMeters_ = 0.0F;
}

void SettingsMenuInteraction::ResetTrackingAnchor() noexcept
{
    ResetUiMenuAnchor(anchor_);
    pointerSmoother_.Reset();
    panelPose_ = {};
    poseValid_ = false;
    pointerVisible_ = false;
    hovered_ = SettingsMenuSelection::None;
    pressed_ = SettingsMenuSelection::None;
    menuSoundActivation_ = SettingsMenuSelection::None;
    primaryWasHeld_ = false;
    sliderDragging_ = false;
}

SettingsMenuSnapshot SettingsMenuInteraction::Snapshot() const noexcept
{
    SettingsMenuSnapshot result = {};
    result.active = active_;
    result.visible = active_ && poseValid_;
    result.pointerVisible = result.visible && pointerVisible_;
    result.controllerLayoutVisible = controllerLayoutVisible_;
    result.arrowLeftVisible = page_ > 0;
    result.arrowRightVisible = page_ + 1 < PageCount();
    result.pointerU = pointerU_;
    result.pointerV = pointerV_;
    result.widthMeters = widthMeters_;
    result.heightMeters = widthMeters_;
    result.panelPose = panelPose_;
    result.tab = tab_;
    result.hovered = result.visible
        ? hovered_
        : SettingsMenuSelection::None;
    result.page = page_;
    result.values = values_;
    result.status = status_;
    return result;
}

bool SettingsMenuInteraction::IsActive() const noexcept
{
    return active_;
}

SettingsMenuCommand SettingsMenuInteraction::TakeCommand() noexcept
{
    const SettingsMenuCommand result = command_;
    command_ = SettingsMenuCommand::None;
    return result;
}

bool SettingsMenuInteraction::TakeValuesChanged() noexcept
{
    const bool result = valuesChanged_;
    valuesChanged_ = false;
    return result;
}

SettingsMenuSelection
SettingsMenuInteraction::TakeMenuSoundActivation() noexcept
{
    const SettingsMenuSelection result = menuSoundActivation_;
    menuSoundActivation_ = SettingsMenuSelection::None;
    return result;
}

void SettingsMenuInteraction::Activate(
    SettingsMenuSelection selection) noexcept
{
    menuSoundActivation_ = selection;
    switch (selection)
    {
    case SettingsMenuSelection::TabVrSettings:
        tab_ = SettingsMenuTab::VrSettings;
        page_ = 0;
        break;
    case SettingsMenuSelection::TabControls:
        tab_ = SettingsMenuTab::Controls;
        page_ = 0;
        break;
    case SettingsMenuSelection::TabGraphicsAudio:
        tab_ = SettingsMenuTab::GraphicsAudio;
        page_ = 0;
        break;
    case SettingsMenuSelection::ControllerLayout:
        controllerLayoutVisible_ = !controllerLayoutVisible_;
        sliderDragging_ = false;
        break;
    case SettingsMenuSelection::ArrowLeft:
        if (page_ > 0)
        {
            --page_;
        }
        break;
    case SettingsMenuSelection::ArrowRight:
        if (page_ + 1 < PageCount())
        {
            ++page_;
        }
        break;
    case SettingsMenuSelection::Cancel:
        command_ = SettingsMenuCommand::Cancel;
        active_ = false;
        pointerVisible_ = false;
        hovered_ = SettingsMenuSelection::None;
        break;
    case SettingsMenuSelection::Save:
        command_ = SettingsMenuCommand::Save;
        break;
    case SettingsMenuSelection::ResetDefaults:
        command_ = SettingsMenuCommand::ResetDefaults;
        break;
    case SettingsMenuSelection::PlayModePrevious:
    case SettingsMenuSelection::PlayModeNext:
        values_.playMode = values_.playMode == settings::PlayMode::Seated
            ? settings::PlayMode::Standing
            : settings::PlayMode::Seated;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    case SettingsMenuSelection::ArtificialTurnPrevious:
    case SettingsMenuSelection::ArtificialTurnNext:
    {
        constexpr int modeCount = 2;
        const int direction =
            selection == SettingsMenuSelection::ArtificialTurnPrevious
            ? -1
            : 1;
        const int current = static_cast<int>(values_.artificialTurnMode);
        values_.artificialTurnMode =
            static_cast<settings::ArtificialTurnMode>(
                (current + direction + modeCount) % modeCount);
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    }
    case SettingsMenuSelection::SnapAnglePrevious:
    case SettingsMenuSelection::SnapAngleNext:
    {
        const int direction =
            selection == SettingsMenuSelection::SnapAnglePrevious ? -1 : 1;
        const int next = static_cast<int>(values_.snapTurnAngleDegrees) +
            direction * static_cast<int>(settings::kSnapTurnAngleStepDegrees);
        values_.snapTurnAngleDegrees = static_cast<std::uint32_t>(std::clamp(
            next,
            static_cast<int>(settings::kMinimumSnapTurnAngleDegrees),
            static_cast<int>(settings::kMaximumSnapTurnAngleDegrees)));
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    }
    case SettingsMenuSelection::MovementDirectionPrevious:
    case SettingsMenuSelection::MovementDirectionNext:
    {
        constexpr int modeCount = 3;
        const int direction =
            selection == SettingsMenuSelection::MovementDirectionPrevious
            ? -1
            : 1;
        const int current = static_cast<int>(values_.movementDirection);
        values_.movementDirection = static_cast<settings::MovementDirection>(
            (current + direction + modeCount) % modeCount);
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    }
    case SettingsMenuSelection::AutoCalibrateStandingHeight:
        if (values_.playMode != settings::PlayMode::Standing)
        {
            status_ = SettingsMenuStatus::StandingModeRequired;
            break;
        }
        if (!standingHeightValid_)
        {
            status_ = SettingsMenuStatus::StandingHeightUnavailable;
            break;
        }
        values_.standingEyeHeightCentimeters = std::clamp(
            static_cast<std::uint32_t>(std::lround(
                standingHeightMeters_ * 100.0F)),
            settings::kMinimumStandingEyeHeightCentimeters,
            settings::kMaximumStandingEyeHeightCentimeters);
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::StandingHeightCalibrated;
        break;
    case SettingsMenuSelection::RecenterForward:
        command_ = SettingsMenuCommand::RecenterForward;
        status_ = SettingsMenuStatus::ForwardRecentered;
        break;
    case SettingsMenuSelection::ComfortVignetteEnabled:
        values_.comfortVignetteEnabled = !values_.comfortVignetteEnabled;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    case SettingsMenuSelection::ShowPrevious:
    case SettingsMenuSelection::ShowNext:
    {
        constexpr int modeCount = 3;
        const int direction = selection == SettingsMenuSelection::ShowPrevious
            ? -1
            : 1;
        const int current = static_cast<int>(values_.firstPersonVisibility);
        values_.firstPersonVisibility =
            static_cast<settings::FirstPersonVisibility>(
                (current + direction + modeCount) % modeCount);
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    }
    case SettingsMenuSelection::DeathCameraComfortEnabled:
        values_.deathCameraComfortEnabled =
            !values_.deathCameraComfortEnabled;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    case SettingsMenuSelection::MenuPointerSmoothingEnabled:
        values_.menuPointerSmoothingEnabled =
            !values_.menuPointerSmoothingEnabled;
        pointerSmoother_.Reset();
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    case SettingsMenuSelection::CrosshairColorPrevious:
    case SettingsMenuSelection::CrosshairColorNext:
    {
        constexpr int colorCount = 8;
        const int direction =
            selection == SettingsMenuSelection::CrosshairColorPrevious
            ? -1
            : 1;
        const int current = static_cast<int>(values_.crosshairColor);
        values_.crosshairColor = static_cast<settings::CrosshairColor>(
            (current + direction + colorCount) % colorCount);
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    }
    case SettingsMenuSelection::OffHandGripPrevious:
    case SettingsMenuSelection::OffHandGripNext:
        values_.offHandGripStyle =
            values_.offHandGripStyle == settings::OffHandGripStyle::Hold
            ? settings::OffHandGripStyle::Toggle
            : settings::OffHandGripStyle::Hold;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    case SettingsMenuSelection::HandCrosshairPrevious:
    case SettingsMenuSelection::HandCrosshairNext:
    case SettingsMenuSelection::MountedCrosshairPrevious:
    case SettingsMenuSelection::MountedCrosshairNext:
    case SettingsMenuSelection::PointerItemCrosshairPrevious:
    case SettingsMenuSelection::PointerItemCrosshairNext:
    {
        settings::WorldCrosshairMode* mode =
            selection == SettingsMenuSelection::HandCrosshairPrevious ||
                selection == SettingsMenuSelection::HandCrosshairNext
            ? &values_.handWeaponCrosshair
            : selection == SettingsMenuSelection::MountedCrosshairPrevious ||
                selection == SettingsMenuSelection::MountedCrosshairNext
            ? &values_.mountedWeaponCrosshair
            : &values_.pointerItemCrosshair;
        const int direction =
            selection == SettingsMenuSelection::HandCrosshairPrevious ||
                selection == SettingsMenuSelection::MountedCrosshairPrevious ||
                selection == SettingsMenuSelection::PointerItemCrosshairPrevious
            ? -1
            : 1;
        constexpr int modeCount = 3;
        const int current = static_cast<int>(*mode);
        *mode = static_cast<settings::WorldCrosshairMode>(
            (current + direction + modeCount) % modeCount);
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    }
    case SettingsMenuSelection::InvertFlightPitch:
        values_.invertFlightPitch = !values_.invertFlightPitch;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    case SettingsMenuSelection::InvertTurretPitch:
        values_.invertTurretPitch = !values_.invertTurretPitch;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    case SettingsMenuSelection::InvertTurretYaw:
        values_.invertTurretYaw = !values_.invertTurretYaw;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    case SettingsMenuSelection::ControllerHapticsEnabled:
        values_.controllerHapticsEnabled =
            !values_.controllerHapticsEnabled;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    case SettingsMenuSelection::SniperScopeSmoothingEnabled:
        values_.sniperScopeSmoothingEnabled =
            !values_.sniperScopeSmoothingEnabled;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    case SettingsMenuSelection::AircraftPitchWithRoll:
        values_.aircraftPitchWithRoll = !values_.aircraftPitchWithRoll;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    case SettingsMenuSelection::SwapAircraftSticks:
        values_.swapAircraftSticks = !values_.swapAircraftSticks;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    case SettingsMenuSelection::FxaaEnabled:
        values_.fxaaEnabled = !values_.fxaaEnabled;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    case SettingsMenuSelection::AmbientOcclusionEnabled:
        values_.ambientOcclusionEnabled =
            !values_.ambientOcclusionEnabled;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    case SettingsMenuSelection::BloomEnabled:
        values_.bloomEnabled = !values_.bloomEnabled;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    case SettingsMenuSelection::WaterReflectionsEnabled:
        values_.waterReflectionsEnabled = !values_.waterReflectionsEnabled;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    case SettingsMenuSelection::KillSoundEnabled:
        values_.killSoundEnabled = !values_.killSoundEnabled;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    case SettingsMenuSelection::ColorProfilePrevious:
    case SettingsMenuSelection::ColorProfileNext:
    {
        constexpr int profileCount = 3;
        const int direction =
            selection == SettingsMenuSelection::ColorProfilePrevious
            ? -1
            : 1;
        const int current = static_cast<int>(values_.colorProfile);
        values_.colorProfile = static_cast<settings::ColorProfile>(
            (current + direction + profileCount) % profileCount);
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
        break;
    }
    case SettingsMenuSelection::ResetColorSettings:
        values_.colorProfile = settings::ColorProfile::Original;
        values_.colorExposureTenthsEv =
            settings::kDefaultColorExposureTenthsEv;
        values_.colorContrastPercent =
            settings::kDefaultColorContrastPercent;
        values_.colorSaturationPercent =
            settings::kDefaultColorSaturationPercent;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::ColorSettingsReset;
        break;
    case SettingsMenuSelection::InfantryTurnSpeed:
    case SettingsMenuSelection::VehicleMotionAimSensitivity:
    case SettingsMenuSelection::VrHeightAdjustment:
    case SettingsMenuSelection::CrosshairOpacity:
    case SettingsMenuSelection::FxaaSharpening:
    case SettingsMenuSelection::AmbientOcclusionRadius:
    case SettingsMenuSelection::AmbientOcclusionStrength:
    case SettingsMenuSelection::BloomThreshold:
    case SettingsMenuSelection::BloomIntensity:
    case SettingsMenuSelection::ColorExposure:
    case SettingsMenuSelection::ColorContrast:
    case SettingsMenuSelection::ColorSaturation:
        break;
    case SettingsMenuSelection::None:
    default:
        break;
    }
}

void SettingsMenuInteraction::SetCrosshairOpacityFromPointer(
    float pointerU) noexcept
{
    const float pixelX = std::clamp(pointerU, 0.0F, 1.0F) *
        kSettingsMenuTextureSize;
    const float normalized = std::clamp(
        (pixelX - kSettingsMenuControlColumnPixels) /
            (kSettingsMenuSliderRightPixels -
             kSettingsMenuControlColumnPixels),
        0.0F,
        1.0F);
    const float unsnapped = static_cast<float>(
        settings::kMinimumCrosshairOpacityPercent) +
        normalized * static_cast<float>(
            settings::kMaximumCrosshairOpacityPercent -
            settings::kMinimumCrosshairOpacityPercent);
    const auto steps = static_cast<std::uint32_t>(std::lround(
        (unsnapped - settings::kMinimumCrosshairOpacityPercent) /
        static_cast<float>(settings::kCrosshairOpacityStepPercent)));
    const std::uint32_t selected = std::clamp(
        settings::kMinimumCrosshairOpacityPercent +
            steps * settings::kCrosshairOpacityStepPercent,
        settings::kMinimumCrosshairOpacityPercent,
        settings::kMaximumCrosshairOpacityPercent);
    if (values_.crosshairOpacityPercent != selected)
    {
        values_.crosshairOpacityPercent = selected;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
    }
}

void SettingsMenuInteraction::SetVehicleMotionAimSensitivityFromPointer(
    float pointerU) noexcept
{
    const float pixelX = std::clamp(pointerU, 0.0F, 1.0F) *
        kSettingsMenuTextureSize;
    const float normalized = std::clamp(
        (pixelX - kSettingsMenuControlColumnPixels) /
            (kSettingsMenuSliderRightPixels -
             kSettingsMenuControlColumnPixels),
        0.0F,
        1.0F);
    const float unsnapped = static_cast<float>(
        settings::kMinimumVehicleMotionAimSensitivityPercent) +
        normalized * static_cast<float>(
            settings::kMaximumVehicleMotionAimSensitivityPercent -
            settings::kMinimumVehicleMotionAimSensitivityPercent);
    const auto steps = static_cast<std::uint32_t>(std::lround(
        unsnapped / static_cast<float>(
            settings::kVehicleMotionAimSensitivityStepPercent)));
    const std::uint32_t selected = std::clamp(
        steps * settings::kVehicleMotionAimSensitivityStepPercent,
        settings::kMinimumVehicleMotionAimSensitivityPercent,
        settings::kMaximumVehicleMotionAimSensitivityPercent);
    if (selected != values_.vehicleMotionAimSensitivityPercent)
    {
        values_.vehicleMotionAimSensitivityPercent = selected;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
    }
}

void SettingsMenuInteraction::SetHeightAdjustmentFromPointer(
    float pointerU) noexcept
{
    const float pixelX = std::clamp(pointerU, 0.0F, 1.0F) *
        kSettingsMenuTextureSize;
    const float normalized = std::clamp(
        (pixelX - kSettingsMenuControlColumnPixels) /
            (kSettingsMenuSliderRightPixels -
             kSettingsMenuControlColumnPixels),
        0.0F,
        1.0F);
    const float unsnapped = static_cast<float>(
        settings::kMinimumVrHeightAdjustmentCentimeters) +
        normalized * static_cast<float>(
            settings::kMaximumVrHeightAdjustmentCentimeters -
            settings::kMinimumVrHeightAdjustmentCentimeters);
    const std::int32_t selected = std::clamp(
        static_cast<std::int32_t>(std::lround(unsnapped)),
        settings::kMinimumVrHeightAdjustmentCentimeters,
        settings::kMaximumVrHeightAdjustmentCentimeters);
    if (selected != values_.vrHeightAdjustmentCentimeters)
    {
        values_.vrHeightAdjustmentCentimeters = selected;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
    }
}

void SettingsMenuInteraction::SetGraphicsSliderFromPointer(
    SettingsMenuSelection selection,
    float pointerU) noexcept
{
    const float pixelX = std::clamp(pointerU, 0.0F, 1.0F) *
        kSettingsMenuTextureSize;
    const float normalized = std::clamp(
        (pixelX - kSettingsMenuControlColumnPixels) /
            (kSettingsMenuSliderRightPixels -
             kSettingsMenuControlColumnPixels),
        0.0F,
        1.0F);
    std::int32_t* signedDestination = nullptr;
    std::int32_t signedMinimum = 0;
    std::int32_t signedMaximum = 0;
    std::int32_t signedStep = 1;
    switch (selection)
    {
    case SettingsMenuSelection::ColorExposure:
        signedDestination = &values_.colorExposureTenthsEv;
        signedMinimum = settings::kMinimumColorExposureTenthsEv;
        signedMaximum = settings::kMaximumColorExposureTenthsEv;
        signedStep = settings::kColorExposureStepTenthsEv;
        break;
    case SettingsMenuSelection::ColorContrast:
        signedDestination = &values_.colorContrastPercent;
        signedMinimum = settings::kMinimumColorContrastPercent;
        signedMaximum = settings::kMaximumColorContrastPercent;
        signedStep = settings::kColorContrastStepPercent;
        break;
    case SettingsMenuSelection::ColorSaturation:
        signedDestination = &values_.colorSaturationPercent;
        signedMinimum = settings::kMinimumColorSaturationPercent;
        signedMaximum = settings::kMaximumColorSaturationPercent;
        signedStep = settings::kColorSaturationStepPercent;
        break;
    default:
        break;
    }
    if (signedDestination != nullptr)
    {
        const float unsnapped = static_cast<float>(signedMinimum) +
            normalized * static_cast<float>(signedMaximum - signedMinimum);
        const std::int32_t selected = std::clamp(
            signedMinimum + static_cast<std::int32_t>(std::lround(
                (unsnapped - static_cast<float>(signedMinimum)) /
                static_cast<float>(signedStep))) * signedStep,
            signedMinimum,
            signedMaximum);
        if (*signedDestination != selected)
        {
            *signedDestination = selected;
            valuesChanged_ = true;
            status_ = SettingsMenuStatus::SettingsNotSaved;
        }
        return;
    }
    std::uint32_t* destination = nullptr;
    std::uint32_t minimum = 0;
    std::uint32_t maximum = 0;
    std::uint32_t step = 1;
    switch (selection)
    {
    case SettingsMenuSelection::FxaaSharpening:
        destination = &values_.fxaaSharpeningPercent;
        minimum = settings::kMinimumFxaaSharpeningPercent;
        maximum = settings::kMaximumFxaaSharpeningPercent;
        step = settings::kFxaaSharpeningStepPercent;
        break;
    case SettingsMenuSelection::AmbientOcclusionRadius:
        destination = &values_.ambientOcclusionRadiusCentimeters;
        minimum = settings::kMinimumAmbientOcclusionRadiusCentimeters;
        maximum = settings::kMaximumAmbientOcclusionRadiusCentimeters;
        step = settings::kAmbientOcclusionRadiusStepCentimeters;
        break;
    case SettingsMenuSelection::AmbientOcclusionStrength:
        destination = &values_.ambientOcclusionStrengthPercent;
        minimum = settings::kMinimumAmbientOcclusionStrengthPercent;
        maximum = settings::kMaximumAmbientOcclusionStrengthPercent;
        step = settings::kAmbientOcclusionStrengthStepPercent;
        break;
    case SettingsMenuSelection::BloomThreshold:
        destination = &values_.bloomThresholdPercent;
        minimum = settings::kMinimumBloomThresholdPercent;
        maximum = settings::kMaximumBloomThresholdPercent;
        step = settings::kBloomThresholdStepPercent;
        break;
    case SettingsMenuSelection::BloomIntensity:
        destination = &values_.bloomIntensityPercent;
        minimum = settings::kMinimumBloomIntensityPercent;
        maximum = settings::kMaximumBloomIntensityPercent;
        step = settings::kBloomIntensityStepPercent;
        break;
    default:
        return;
    }
    const float unsnapped = static_cast<float>(minimum) +
        normalized * static_cast<float>(maximum - minimum);
    const std::uint32_t selected = std::clamp(
        minimum + static_cast<std::uint32_t>(std::lround(
            (unsnapped - static_cast<float>(minimum)) /
            static_cast<float>(step))) * step,
        minimum,
        maximum);
    if (*destination != selected)
    {
        *destination = selected;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
    }
}

void SettingsMenuInteraction::SetTurnSpeedFromPointer(
    float pointerU) noexcept
{
    const float pixelX = std::clamp(pointerU, 0.0F, 1.0F) *
        kSettingsMenuTextureSize;
    const float normalized = std::clamp(
        (pixelX - kSettingsMenuControlColumnPixels) /
            (kSettingsMenuSliderRightPixels -
             kSettingsMenuControlColumnPixels),
        0.0F,
        1.0F);
    const float unsnapped =
        static_cast<float>(settings::kMinimumInfantryTurnSpeedPercent) +
        normalized * static_cast<float>(
            settings::kMaximumInfantryTurnSpeedPercent -
            settings::kMinimumInfantryTurnSpeedPercent);
    const auto steps = static_cast<std::uint32_t>(std::lround(
        unsnapped /
        static_cast<float>(settings::kInfantryTurnSpeedStepPercent)));
    const std::uint32_t selected = std::clamp(
        steps * settings::kInfantryTurnSpeedStepPercent,
        settings::kMinimumInfantryTurnSpeedPercent,
        settings::kMaximumInfantryTurnSpeedPercent);
    if (selected != values_.infantryTurnSpeedPercent)
    {
        values_.infantryTurnSpeedPercent = selected;
        valuesChanged_ = true;
        status_ = SettingsMenuStatus::SettingsNotSaved;
    }
}

std::uint32_t SettingsMenuInteraction::PageCount() const noexcept
{
    const std::size_t index = static_cast<std::size_t>(tab_);
    return index < kSettingsMenuPageCounts.size()
        ? kSettingsMenuPageCounts[index]
        : 1U;
}

} // namespace bfvr::stereo
