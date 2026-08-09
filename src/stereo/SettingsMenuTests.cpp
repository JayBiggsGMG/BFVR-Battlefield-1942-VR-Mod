#include "client/SettingsMenuArt.h"
#include "stereo/SettingsMenuInteraction.h"

#include <windows.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace
{
using bfvr::stereo::Pose;
using bfvr::stereo::Quaternion;
using bfvr::stereo::SettingsMenuSelection;
using bfvr::stereo::SettingsMenuTab;
using bfvr::stereo::SettingsMenuCommand;
using bfvr::stereo::Vec3;

bool NearlyEqual(float lhs, float rhs, float tolerance = 0.0001F)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

Vec3 Add(const Vec3& lhs, const Vec3& rhs)
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

Vec3 Scale(const Vec3& value, float amount)
{
    return {value.x * amount, value.y * amount, value.z * amount};
}

Vec3 Cross(const Vec3& lhs, const Vec3& rhs)
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x};
}

Vec3 Rotate(const Quaternion& orientation, const Vec3& value)
{
    const Vec3 axis{orientation.x, orientation.y, orientation.z};
    const Vec3 doubledCross = Scale(Cross(axis, value), 2.0F);
    return Add(
        value,
        Add(
            Scale(doubledCross, orientation.w),
            Cross(axis, doubledCross)));
}

bool TestBounds()
{
    using bfvr::stereo::SettingsMenuSelectionAt;
    return
        SettingsMenuSelectionAt(0.16F, 0.05F, false, false) ==
            SettingsMenuSelection::TabVrSettings &&
        SettingsMenuSelectionAt(0.50F, 0.05F, false, false) ==
            SettingsMenuSelection::TabControls &&
        SettingsMenuSelectionAt(0.84F, 0.05F, false, false) ==
            SettingsMenuSelection::TabGraphicsAudio &&
        SettingsMenuSelectionAt(0.82F, 0.87F, false, false) ==
            SettingsMenuSelection::ControllerLayout &&
        SettingsMenuSelectionAt(0.59F, 0.87F, false, false) ==
            SettingsMenuSelection::None &&
        SettingsMenuSelectionAt(0.59F, 0.87F, true, false) ==
            SettingsMenuSelection::ArrowLeft &&
        SettingsMenuSelectionAt(0.66F, 0.87F, false, true) ==
            SettingsMenuSelection::ArrowRight &&
        SettingsMenuSelectionAt(0.16F, 0.97F, false, false) ==
            SettingsMenuSelection::Save &&
        SettingsMenuSelectionAt(0.50F, 0.97F, false, false) ==
            SettingsMenuSelection::Cancel &&
        SettingsMenuSelectionAt(0.84F, 0.97F, false, false) ==
            SettingsMenuSelection::ResetDefaults &&
        SettingsMenuSelectionAt(0.64F, 0.454F, false, false) ==
            SettingsMenuSelection::InfantryTurnSpeed &&
        SettingsMenuSelectionAt(
            0.889F, 0.161F, false, true,
            SettingsMenuTab::VrSettings, false, 0) ==
            SettingsMenuSelection::PlayModeNext &&
        SettingsMenuSelectionAt(
            0.70F, 0.293F, true, false,
            SettingsMenuTab::VrSettings, false, 1) ==
            SettingsMenuSelection::VrHeightAdjustment &&
        SettingsMenuSelectionAt(
            0.57F, 0.132F, true, false,
            SettingsMenuTab::VrSettings, false, 1) ==
            SettingsMenuSelection::ComfortVignetteEnabled &&
        SettingsMenuSelectionAt(
            0.889F,
            0.161F,
            false,
            false,
            SettingsMenuTab::Controls,
            false) == SettingsMenuSelection::OffHandGripNext &&
        SettingsMenuSelectionAt(
            0.57F,
            0.571F,
            false,
            false,
            SettingsMenuTab::Controls,
            false) == SettingsMenuSelection::InvertFlightPitch &&
        SettingsMenuSelectionAt(
            0.889F,
            0.322F,
            false,
            false,
            SettingsMenuTab::Controls,
            false) == SettingsMenuSelection::HandCrosshairNext &&
        SettingsMenuSelectionAt(
            0.57F,
            0.571F,
            false,
            false,
            SettingsMenuTab::Controls,
            true) == SettingsMenuSelection::None &&
        SettingsMenuSelectionAt(
            0.57F,
            0.122F,
            false,
            false,
            SettingsMenuTab::GraphicsAudio,
            false) == SettingsMenuSelection::FxaaEnabled &&
        SettingsMenuSelectionAt(
            0.70F,
            0.205F,
            false,
            false,
            SettingsMenuTab::GraphicsAudio,
            false) == SettingsMenuSelection::FxaaSharpening &&
        SettingsMenuSelectionAt(
            0.70F,
            0.371F,
            false,
            false,
            SettingsMenuTab::GraphicsAudio,
            false) == SettingsMenuSelection::AmbientOcclusionRadius &&
        SettingsMenuSelectionAt(
            0.70F,
            0.728F,
            false,
            false,
            SettingsMenuTab::GraphicsAudio,
            true) == SettingsMenuSelection::None &&
        SettingsMenuSelectionAt(-0.1F, 0.5F, false, false) ==
            SettingsMenuSelection::None;
}

void AimAt(
    bfvr::stereo::QuickMenuFrameInput& input,
    const Pose& panel,
    float panelWidth,
    float u,
    float v)
{
    const Vec3 local = {
        (u - 0.5F) * panelWidth,
        (0.5F - v) * panelWidth,
        0.75F};
    input.rightAimPose.position = Add(
        panel.position,
        Rotate(panel.orientation, local));
    input.rightAimPose.orientation = panel.orientation;
}

void Click(
    bfvr::stereo::SettingsMenuInteraction& interaction,
    bfvr::stereo::QuickMenuFrameInput& input)
{
    input.predictedDisplayTime += 11'111'111;
    input.rightPrimaryHeld = true;
    interaction.Update(input);
    input.predictedDisplayTime += 11'111'111;
    input.rightPrimaryHeld = false;
    interaction.Update(input);
}

bool TestInteractionAndPlacement()
{
    bfvr::stereo::SettingsMenuInteraction interaction;
    constexpr float nativeDistance = 1.50F;
    constexpr float settingsDistance = nativeDistance -
        bfvr::stereo::kSettingsMenuForwardOffsetMeters;
    constexpr float settingsWidth = 1.60F *
        bfvr::stereo::kSettingsMenuNativeWidthScale;
    interaction.Configure(settingsWidth, settingsDistance);
    interaction.Open();
    bfvr::stereo::QuickMenuFrameInput input = {};
    input.predictedDisplayTime = 1'000'000'000;
    input.sessionFocused = true;
    input.shouldRender = true;
    input.headTracked = true;
    input.rightAimTracked = true;
    input.headPose.position = {0.0F, 1.70F, 0.0F};
    input.headPose.orientation.w = 1.0F;
    input.rightAimPose.orientation.w = 1.0F;
    input.rightAimPose.position = input.headPose.position;
    interaction.Update(input);
    auto state = interaction.Snapshot();
    if (!state.active || !state.visible ||
        state.tab != SettingsMenuTab::VrSettings || state.page != 0 ||
        state.arrowLeftVisible || !state.arrowRightVisible ||
        !NearlyEqual(state.widthMeters, settingsWidth) ||
        !NearlyEqual(
            settingsWidth / 1.60F,
            bfvr::stereo::kSettingsMenuNativeWidthScale) ||
        !NearlyEqual(bfvr::stereo::kSettingsMenuCursorScale, 0.50F) ||
        !NearlyEqual(state.panelPose.position.z, -settingsDistance) ||
        !NearlyEqual(
            nativeDistance + state.panelPose.position.z,
            bfvr::stereo::kSettingsMenuForwardOffsetMeters))
    {
        return false;
    }

    AimAt(input, state.panelPose, state.widthMeters, 0.82F, 0.87F);
    input.predictedDisplayTime += 11'111'111;
    interaction.Update(input);
    if (interaction.Snapshot().hovered !=
        SettingsMenuSelection::ControllerLayout)
    {
        return false;
    }
    Click(interaction, input);
    if (!interaction.Snapshot().controllerLayoutVisible)
    {
        return false;
    }

    // The Controller Layout overlay is modal over the settings body.
    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.64F, 0.454F);
    input.predictedDisplayTime += 11'111'111;
    interaction.Update(input);
    if (interaction.Snapshot().hovered != SettingsMenuSelection::None)
    {
        return false;
    }
    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.82F, 0.87F);
    input.predictedDisplayTime += 11'111'111;
    interaction.Update(input);
    Click(interaction, input);
    if (interaction.Snapshot().controllerLayoutVisible)
    {
        return false;
    }

    // Clicking the bar sets a snapped value; holding and moving drags it.
    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.64F, 0.454F);
    input.predictedDisplayTime += 11'111'111;
    interaction.Update(input);
    Click(interaction, input);
    const std::uint32_t clickedTurnSpeed =
        interaction.Snapshot().values.infantryTurnSpeedPercent;
    if (clickedTurnSpeed ==
            bfvr::settings::kDefaultInfantryTurnSpeedPercent ||
        !interaction.TakeValuesChanged() ||
        interaction.Snapshot().status !=
            bfvr::stereo::SettingsMenuStatus::SettingsNotSaved)
    {
        return false;
    }
    AimAt(input, state.panelPose, state.widthMeters, 0.56F, 0.454F);
    input.predictedDisplayTime += 11'111'111;
    input.rightPrimaryHeld = true;
    interaction.Update(input);
    AimAt(input, state.panelPose, state.widthMeters, 0.79F, 0.454F);
    input.predictedDisplayTime += 11'111'111;
    interaction.Update(input);
    input.predictedDisplayTime += 11'111'111;
    input.rightPrimaryHeld = false;
    interaction.Update(input);
    if (interaction.Snapshot().values.infantryTurnSpeedPercent <=
            clickedTurnSpeed ||
        !interaction.TakeValuesChanged())
    {
        return false;
    }

    // Page 1 selectors stage play mode, conditional snap angle, and the
    // infantry-only movement basis. Smooth's slider is replaced by angle
    // arrows as soon as Snap is selected.
    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.889F, 0.161F);
    Click(interaction, input);
    if (interaction.Snapshot().values.playMode !=
            bfvr::settings::PlayMode::Standing ||
        !interaction.TakeValuesChanged())
    {
        return false;
    }
    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.571F, 0.308F);
    Click(interaction, input);
    if (interaction.Snapshot().values.artificialTurnMode !=
            bfvr::settings::ArtificialTurnMode::Snap ||
        !interaction.TakeValuesChanged())
    {
        return false;
    }
    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.889F, 0.454F);
    Click(interaction, input);
    if (interaction.Snapshot().values.snapTurnAngleDegrees != 60 ||
        !interaction.TakeValuesChanged())
    {
        return false;
    }
    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.889F, 0.635F);
    Click(interaction, input);
    if (interaction.Snapshot().values.movementDirection !=
            bfvr::settings::MovementDirection::Head ||
        !interaction.TakeValuesChanged())
    {
        return false;
    }

    // Page 2 exposes the comfort toggle, keeps manual trim separate from the
    // measured physical floor-to-eye height, and retains immediate recenter.
    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.66F, 0.87F);
    Click(interaction, input);
    state = interaction.Snapshot();
    if (state.page != 1 || !state.arrowLeftVisible || state.arrowRightVisible)
    {
        return false;
    }
    const bool originalComfortVignette = state.values.comfortVignetteEnabled;
    AimAt(input, state.panelPose, state.widthMeters, 0.57F, 0.132F);
    Click(interaction, input);
    if (interaction.Snapshot().values.comfortVignetteEnabled ==
            originalComfortVignette ||
        !interaction.TakeValuesChanged())
    {
        return false;
    }
    state = interaction.Snapshot();
    input.standingHeightValid = true;
    input.standingHeightMeters = 1.82F;
    AimAt(input, state.panelPose, state.widthMeters, 0.60F, 0.503F);
    Click(interaction, input);
    if (interaction.Snapshot().values.standingEyeHeightCentimeters != 182 ||
        interaction.Snapshot().values.vrHeightAdjustmentCentimeters != 0 ||
        !interaction.TakeValuesChanged() ||
        interaction.Snapshot().status !=
            bfvr::stereo::SettingsMenuStatus::StandingHeightCalibrated)
    {
        return false;
    }
    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.60F, 0.684F);
    Click(interaction, input);
    if (interaction.TakeCommand() != SettingsMenuCommand::RecenterForward ||
        interaction.Snapshot().status !=
            bfvr::stereo::SettingsMenuStatus::ForwardRecentered)
    {
        return false;
    }
    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.59F, 0.87F);
    Click(interaction, input);
    if (interaction.Snapshot().page != 0)
    {
        return false;
    }

    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.50F, 0.05F);
    input.predictedDisplayTime += 11'111'111;
    interaction.Update(input);
    Click(interaction, input);
    if (interaction.Snapshot().tab != SettingsMenuTab::Controls)
    {
        return false;
    }
    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.889F, 0.161F);
    input.predictedDisplayTime += 11'111'111;
    interaction.Update(input);
    Click(interaction, input);
    if (interaction.Snapshot().values.offHandGripStyle !=
            bfvr::settings::OffHandGripStyle::Toggle ||
        !interaction.TakeValuesChanged())
    {
        return false;
    }
    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.889F, 0.322F);
    input.predictedDisplayTime += 11'111'111;
    interaction.Update(input);
    Click(interaction, input);
    if (interaction.Snapshot().values.handWeaponCrosshair !=
            bfvr::settings::WorldCrosshairMode::HitMarkerOnly ||
        !interaction.TakeValuesChanged())
    {
        return false;
    }
    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.57F, 0.571F);
    input.predictedDisplayTime += 11'111'111;
    interaction.Update(input);
    Click(interaction, input);
    if (!interaction.Snapshot().values.invertFlightPitch ||
        !interaction.TakeValuesChanged() ||
        interaction.Snapshot().status !=
            bfvr::stereo::SettingsMenuStatus::SettingsNotSaved)
    {
        return false;
    }
    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.57F, 0.757F);
    input.predictedDisplayTime += 11'111'111;
    interaction.Update(input);
    Click(interaction, input);
    if (interaction.Snapshot().values.controllerHapticsEnabled ||
        !interaction.TakeValuesChanged())
    {
        return false;
    }

    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.84F, 0.05F);
    input.predictedDisplayTime += 11'111'111;
    interaction.Update(input);
    Click(interaction, input);
    if (interaction.Snapshot().tab != SettingsMenuTab::GraphicsAudio)
    {
        return false;
    }
    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.57F, 0.142F);
    input.predictedDisplayTime += 11'111'111;
    interaction.Update(input);
    Click(interaction, input);
    if (interaction.Snapshot().values.fxaaEnabled ||
        !interaction.TakeValuesChanged())
    {
        return false;
    }
    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.75F, 0.205F);
    input.predictedDisplayTime += 11'111'111;
    interaction.Update(input);
    Click(interaction, input);
    if (interaction.Snapshot().values.fxaaSharpeningPercent ==
            bfvr::settings::kDefaultFxaaSharpeningPercent ||
        !interaction.TakeValuesChanged())
    {
        return false;
    }
    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.75F, 0.371F);
    input.predictedDisplayTime += 11'111'111;
    interaction.Update(input);
    Click(interaction, input);
    if (interaction.Snapshot().values.ambientOcclusionRadiusCentimeters ==
            bfvr::settings::kDefaultAmbientOcclusionRadiusCentimeters ||
        !interaction.TakeValuesChanged())
    {
        return false;
    }
    interaction.SetStatus(
        bfvr::stereo::SettingsMenuStatus::SettingsSaved);
    if (interaction.Snapshot().status !=
        bfvr::stereo::SettingsMenuStatus::SettingsSaved)
    {
        return false;
    }

    // Save and Reset issue commands without closing the menu.
    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.16F, 0.97F);
    input.predictedDisplayTime += 11'111'111;
    interaction.Update(input);
    Click(interaction, input);
    if (!interaction.IsActive() ||
        interaction.TakeCommand() != SettingsMenuCommand::Save ||
        interaction.TakeCommand() != SettingsMenuCommand::None)
    {
        return false;
    }
    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.84F, 0.97F);
    input.predictedDisplayTime += 11'111'111;
    interaction.Update(input);
    Click(interaction, input);
    if (!interaction.IsActive() ||
        interaction.TakeCommand() != SettingsMenuCommand::ResetDefaults)
    {
        return false;
    }
    state = interaction.Snapshot();
    AimAt(input, state.panelPose, state.widthMeters, 0.50F, 0.97F);
    input.predictedDisplayTime += 11'111'111;
    interaction.Update(input);
    Click(interaction, input);
    return interaction.TakeCommand() == SettingsMenuCommand::Cancel &&
        !interaction.IsActive() &&
        !interaction.Snapshot().visible;
}

bool TestArt(const wchar_t* directory)
{
    bfvr::SettingsMenuArt art;
    if (!art.InitializeFromDirectory(directory, nullptr, nullptr) ||
        !art.IsReady())
    {
        return false;
    }
    bfvr::stereo::SettingsMenuSnapshot state = {};
    state.active = true;
    state.visible = true;
    std::vector<std::uint32_t> base;
    UINT width = 0;
    UINT height = 0;
    if (!art.Compose(state, base, width, height) ||
        width != bfvr::stereo::kSettingsMenuTextureSize ||
        height != bfvr::stereo::kSettingsMenuTextureSize || base.empty())
    {
        return false;
    }
    std::vector<std::uint32_t> cachedBase;
    UINT cachedWidth = 0;
    UINT cachedHeight = 0;
    if (!art.Compose(state, cachedBase, cachedWidth, cachedHeight) ||
        cachedWidth != width || cachedHeight != height || cachedBase != base)
    {
        // Reusing cached text rasters must be pixel-identical to the initial
        // GDI composition; this is a performance cache, not a visual policy.
        return false;
    }
    auto differs = [&](const bfvr::stereo::SettingsMenuSnapshot& variant) {
        std::vector<std::uint32_t> pixels;
        UINT variantWidth = 0;
        UINT variantHeight = 0;
        return art.Compose(
                   variant,
                   pixels,
                   variantWidth,
                   variantHeight) &&
            variantWidth == width && variantHeight == height &&
            pixels != base;
    };
    auto variant = state;
    variant.tab = SettingsMenuTab::Controls;
    if (!differs(variant))
    {
        return false;
    }
    variant.hovered = SettingsMenuSelection::HandCrosshairNext;
    if (!differs(variant))
    {
        return false;
    }
    variant = state;
    variant.values.infantryTurnSpeedPercent = 200;
    if (!differs(variant))
    {
        return false;
    }
    variant = state;
    variant.tab = SettingsMenuTab::Controls;
    variant.values.offHandGripStyle =
        bfvr::settings::OffHandGripStyle::Toggle;
    variant.values.handWeaponCrosshair =
        bfvr::settings::WorldCrosshairMode::HitMarkerOnly;
    variant.values.invertFlightPitch = true;
    variant.values.controllerHapticsEnabled = false;
    if (!differs(variant))
    {
        return false;
    }
    variant = state;
    variant.tab = SettingsMenuTab::GraphicsAudio;
    if (!differs(variant))
    {
        return false;
    }
    variant.values.fxaaEnabled = false;
    variant.values.fxaaSharpeningPercent = 80;
    variant.values.ambientOcclusionRadiusCentimeters = 120;
    if (!differs(variant))
    {
        return false;
    }
    variant = state;
    variant.controllerLayoutVisible = true;
    if (!differs(variant))
    {
        return false;
    }
    variant = state;
    variant.status = bfvr::stereo::SettingsMenuStatus::DefaultsRestored;
    if (!differs(variant))
    {
        return false;
    }
    variant = state;
    variant.status = bfvr::stereo::SettingsMenuStatus::
        SettingsSavedRestartRequired;
    if (!differs(variant))
    {
        return false;
    }
    variant = state;
    variant.hovered = SettingsMenuSelection::Cancel;
    if (!differs(variant))
    {
        return false;
    }
    variant = state;
    variant.arrowLeftVisible = true;
    variant.arrowRightVisible = true;
    return differs(variant);
}

bool WriteCaptureBitmap(
    const std::wstring& path,
    const std::vector<std::uint32_t>& premultiplied,
    UINT width,
    UINT height)
{
    std::vector<std::uint32_t> opaque = premultiplied;
    for (std::uint32_t& pixel : opaque)
    {
        pixel |= 0xFF000000U;
    }
    BITMAPFILEHEADER fileHeader = {};
    BITMAPINFOHEADER infoHeader = {};
    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(infoHeader);
    fileHeader.bfSize = fileHeader.bfOffBits +
        static_cast<DWORD>(opaque.size() * sizeof(std::uint32_t));
    infoHeader.biSize = sizeof(infoHeader);
    infoHeader.biWidth = static_cast<LONG>(width);
    infoHeader.biHeight = -static_cast<LONG>(height);
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 32;
    infoHeader.biCompression = BI_RGB;
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    DWORD written = 0;
    bool success = WriteFile(
        file,
        &fileHeader,
        sizeof(fileHeader),
        &written,
        nullptr) != FALSE && written == sizeof(fileHeader);
    success = success && WriteFile(
        file,
        &infoHeader,
        sizeof(infoHeader),
        &written,
        nullptr) != FALSE && written == sizeof(infoHeader);
    const DWORD pixelBytes = static_cast<DWORD>(
        opaque.size() * sizeof(std::uint32_t));
    success = success && WriteFile(
        file,
        opaque.data(),
        pixelBytes,
        &written,
        nullptr) != FALSE && written == pixelBytes;
    CloseHandle(file);
    return success;
}

bool CaptureArt(const wchar_t* assetDirectory, const wchar_t* outputDirectory)
{
    bfvr::SettingsMenuArt art;
    if (!art.InitializeFromDirectory(assetDirectory, nullptr, nullptr))
    {
        return false;
    }
    auto capture = [&](SettingsMenuTab tab,
                       const wchar_t* name,
                       bool overlay,
                       bool checked,
                       std::uint32_t page = 0) {
        bfvr::stereo::SettingsMenuSnapshot state = {};
        state.active = true;
        state.visible = true;
        state.tab = tab;
        state.page = page;
        state.arrowLeftVisible = page > 0;
        state.arrowRightVisible =
            tab == SettingsMenuTab::VrSettings && page == 0;
        state.controllerLayoutVisible = overlay;
        state.values.invertFlightPitch = checked;
        state.values.invertTurretPitch = checked;
        state.values.invertTurretYaw = checked;
        if (checked)
        {
            state.values.offHandGripStyle =
                bfvr::settings::OffHandGripStyle::Toggle;
            state.values.handWeaponCrosshair =
                bfvr::settings::WorldCrosshairMode::HitMarkerOnly;
            state.values.mountedWeaponCrosshair =
                bfvr::settings::WorldCrosshairMode::HitMarkerOnly;
        }
        std::vector<std::uint32_t> pixels;
        UINT width = 0;
        UINT height = 0;
        std::wstring path = outputDirectory;
        if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
        {
            path.push_back(L'\\');
        }
        path += name;
        return art.Compose(state, pixels, width, height) &&
            WriteCaptureBitmap(path, pixels, width, height);
    };
    return capture(
               SettingsMenuTab::VrSettings,
               L"Settings-VR.bmp",
               false,
               false) &&
        capture(
            SettingsMenuTab::VrSettings,
            L"Settings-VR-Page2.bmp",
            false,
            false,
            1) &&
        capture(
            SettingsMenuTab::Controls,
            L"Settings-Controls.bmp",
            false,
            false) &&
        capture(
            SettingsMenuTab::Controls,
            L"Settings-Controls-Checked.bmp",
            false,
            true) &&
        capture(
            SettingsMenuTab::GraphicsAudio,
            L"Settings-Graphics-Audio.bmp",
            false,
            false) &&
        capture(
            SettingsMenuTab::Controls,
            L"Settings-Controls-Overlay.bmp",
            true,
            true);
}
} // namespace

int wmain(int argumentCount, wchar_t** arguments)
{
    if ((argumentCount != 2 && argumentCount != 3) || arguments[1] == nullptr)
    {
        std::cerr << "Expected one SettingsMenu asset-directory argument.\n";
        return 1;
    }
    if (!TestBounds())
    {
        std::cerr << "VR Settings artwork-bound interaction test failed.\n";
        return 1;
    }
    if (!TestInteractionAndPlacement())
    {
        std::cerr << "VR Settings persistent interaction/placement test failed.\n";
        return 1;
    }
    if (!TestArt(arguments[1]))
    {
        std::cerr << "VR Settings authored layer-stack test failed.\n";
        return 1;
    }
    if (argumentCount == 3 &&
        (arguments[2] == nullptr || !CaptureArt(arguments[1], arguments[2])))
    {
        std::cerr << "VR Settings diagnostic capture failed.\n";
        return 1;
    }
    std::cout << "BFVR Settings Menu tests passed.\n";
    return 0;
}
