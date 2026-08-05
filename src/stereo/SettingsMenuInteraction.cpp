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
        selection == SettingsMenuSelection::AmbientOcclusionRadius ||
        selection == SettingsMenuSelection::AmbientOcclusionStrength ||
        selection == SettingsMenuSelection::BloomThreshold ||
        selection == SettingsMenuSelection::BloomIntensity;
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
    bool controllerLayoutVisible) noexcept
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
    if (tab == SettingsMenuTab::VrSettings &&
        pixelX >= kSettingsMenuControlColumnPixels &&
        pixelX <= kSettingsMenuSliderRightPixels &&
        pixelY >= kSettingsMenuVrRowCenterPixels - 38.0F &&
        pixelY <= kSettingsMenuVrRowCenterPixels + 38.0F)
    {
        return SettingsMenuSelection::InfantryTurnSpeed;
    }
    if (tab == SettingsMenuTab::Controls)
    {
        for (std::size_t index = 0;
             index < kSettingsMenuControlsSelectorRowCentersPixels.size();
             ++index)
        {
            const float centerY =
                kSettingsMenuControlsSelectorRowCentersPixels[index];
            if (pixelY < centerY - 38.0F || pixelY > centerY + 38.0F)
            {
                continue;
            }
            const SettingsMenuSelection first = static_cast<
                SettingsMenuSelection>(
                    static_cast<std::uint32_t>(
                        SettingsMenuSelection::OffHandGripPrevious) +
                    static_cast<std::uint32_t>(index) * 2U);
            if (pixelX >= kSettingsMenuSelectorLeftArrowCenterPixels - 40.0F &&
                pixelX <= kSettingsMenuSelectorLeftArrowCenterPixels + 40.0F)
            {
                return first;
            }
            if (pixelX >= kSettingsMenuSelectorRightArrowCenterPixels - 40.0F &&
                pixelX <= kSettingsMenuSelectorRightArrowCenterPixels + 40.0F)
            {
                return static_cast<SettingsMenuSelection>(
                    static_cast<std::uint32_t>(first) + 1U);
            }
        }
        for (std::size_t index = 0;
             index < kSettingsMenuControlsToggleRowCentersPixels.size();
             ++index)
        {
            if (pixelX >= kSettingsMenuControlColumnPixels &&
                pixelX <= kSettingsMenuControlColumnPixels + 72.0F &&
                pixelY >= kSettingsMenuControlsToggleRowCentersPixels[index] - 38.0F &&
                pixelY <= kSettingsMenuControlsToggleRowCentersPixels[index] + 38.0F)
            {
                return static_cast<SettingsMenuSelection>(
                    static_cast<std::uint32_t>(
                        SettingsMenuSelection::InvertFlightPitch) +
                    static_cast<std::uint32_t>(index));
            }
        }
    }
    if (tab == SettingsMenuTab::GraphicsAudio)
    {
        for (std::size_t index = 0;
             index < kSettingsMenuGraphicsRowCentersPixels.size();
             ++index)
        {
            const float centerY =
                kSettingsMenuGraphicsRowCentersPixels[index];
            const bool toggle = index == 0 || index == 1 || index == 4;
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
    case SettingsMenuSelection::InvertFlightPitch:
        return L"Flight Pitch inversion";
    case SettingsMenuSelection::InvertTurretPitch:
        return L"Turret Pitch inversion";
    case SettingsMenuSelection::InvertTurretYaw:
        return L"Turret Yaw inversion";
    case SettingsMenuSelection::FxaaEnabled: return L"FXAA";
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
            pointerU_ = hit->normalizedX;
            pointerV_ = hit->normalizedY;
            hovered_ = SettingsMenuSelectionAt(
                pointerU_,
                pointerV_,
                page_ > 0,
                page_ + 1 < PageCount(),
                tab_,
                controllerLayoutVisible_);
        }
    }

    if (input.rightPrimaryHeld && !primaryWasHeld_)
    {
        pressed_ = hovered_;
        sliderDragging_ = IsSliderSelection(hovered_);
        if (sliderDragging_)
        {
            if (pressed_ == SettingsMenuSelection::InfantryTurnSpeed)
            {
                SetTurnSpeedFromPointer(pointerU_);
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
}

void SettingsMenuInteraction::Reset() noexcept
{
    ResetUiMenuAnchor(anchor_);
    panelPose_ = {};
    tab_ = SettingsMenuTab::VrSettings;
    hovered_ = SettingsMenuSelection::None;
    pressed_ = SettingsMenuSelection::None;
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

void SettingsMenuInteraction::Activate(
    SettingsMenuSelection selection) noexcept
{
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
    {
        settings::WorldCrosshairMode* mode =
            selection == SettingsMenuSelection::HandCrosshairPrevious ||
                selection == SettingsMenuSelection::HandCrosshairNext
            ? &values_.handWeaponCrosshair
            : &values_.mountedWeaponCrosshair;
        const int direction =
            selection == SettingsMenuSelection::HandCrosshairPrevious ||
                selection == SettingsMenuSelection::MountedCrosshairPrevious
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
    case SettingsMenuSelection::InfantryTurnSpeed:
    case SettingsMenuSelection::AmbientOcclusionRadius:
    case SettingsMenuSelection::AmbientOcclusionStrength:
    case SettingsMenuSelection::BloomThreshold:
    case SettingsMenuSelection::BloomIntensity:
        break;
    case SettingsMenuSelection::None:
    default:
        break;
    }
}

void SettingsMenuInteraction::SetGraphicsSliderFromPointer(
    SettingsMenuSelection selection,
    float pointerU) noexcept
{
    std::uint32_t* destination = nullptr;
    std::uint32_t minimum = 0;
    std::uint32_t maximum = 0;
    std::uint32_t step = 1;
    switch (selection)
    {
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
    const float pixelX = std::clamp(pointerU, 0.0F, 1.0F) *
        kSettingsMenuTextureSize;
    const float normalized = std::clamp(
        (pixelX - kSettingsMenuControlColumnPixels) /
            (kSettingsMenuSliderRightPixels -
             kSettingsMenuControlColumnPixels),
        0.0F,
        1.0F);
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
