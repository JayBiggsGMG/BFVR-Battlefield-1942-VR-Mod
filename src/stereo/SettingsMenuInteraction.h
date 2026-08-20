#pragma once

#include "settings/UserSettings.h"
#include "stereo/QuickMenuInteraction.h"
#include "stereo/UiPointerMath.h"
#include "stereo/UiPointerSmoothing.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace bfvr::stereo
{

enum class SettingsMenuTab : std::uint32_t
{
    VrSettings = 0,
    Controls,
    GraphicsAudio,
    Count
};

enum class SettingsMenuSelection : std::uint32_t
{
    None = 0,
    TabVrSettings,
    TabControls,
    TabGraphicsAudio,
    ControllerLayout,
    ArrowLeft,
    ArrowRight,
    Save,
    Cancel,
    ResetDefaults,
    PlayModePrevious,
    PlayModeNext,
    ArtificialTurnPrevious,
    ArtificialTurnNext,
    SnapAnglePrevious,
    SnapAngleNext,
    MovementDirectionPrevious,
    MovementDirectionNext,
    InfantryTurnSpeed,
    VrHeightAdjustment,
    RightHandPositionX,
    RightHandPositionY,
    RightHandPositionZ,
    LeftHandPositionX,
    LeftHandPositionY,
    LeftHandPositionZ,
    ResetHandPositions,
    AutoCalibrateStandingHeight,
    RecenterForward,
    ComfortVignetteEnabled,
    ShowPrevious,
    ShowNext,
    DeathCameraComfortEnabled,
    MenuPointerSmoothingEnabled,
    CrosshairColorPrevious,
    CrosshairColorNext,
    CrosshairOpacity,
    OffHandGripPrevious,
    OffHandGripNext,
    HandCrosshairPrevious,
    HandCrosshairNext,
    MountedCrosshairPrevious,
    MountedCrosshairNext,
    PointerItemCrosshairPrevious,
    PointerItemCrosshairNext,
    InvertFlightPitch,
    AircraftPitchWithRoll,
    SwapAircraftSticks,
    InvertTurretPitch,
    InvertTurretYaw,
    ControllerHapticsEnabled,
    SniperScopeSmoothingEnabled,
    VehicleMotionAimSensitivity,
    FxaaEnabled,
    FxaaSharpening,
    AmbientOcclusionEnabled,
    AmbientOcclusionRadius,
    AmbientOcclusionStrength,
    BloomEnabled,
    BloomThreshold,
    BloomIntensity,
    WaterReflectionsEnabled,
    ColorProfilePrevious,
    ColorProfileNext,
    ColorExposure,
    ColorContrast,
    ColorSaturation,
    ResetColorSettings,
    KillSoundEnabled
};

enum class SettingsMenuCommand : std::uint32_t
{
    None = 0,
    Save,
    Cancel,
    ResetDefaults,
    RecenterForward
};

enum class SettingsMenuStatus : std::uint32_t
{
    SettingsLoaded = 0,
    SettingsNotSaved,
    SettingsSaved,
    SettingsSavedRestartRequired,
    DefaultsRestored,
    DefaultsLoaded,
    InvalidConfigDefaultsLoaded,
    ConfigReadFailed,
    SaveFailed,
    StandingHeightCalibrated,
    StandingHeightUnavailable,
    StandingModeRequired,
    ForwardRecentered,
    ForwardRecenterFailed,
    HandPositionsReset,
    ColorSettingsReset
};

constexpr std::uint32_t kSettingsMenuTextureSize = 1024;
// Owner headset tuning: the authored square is 80% of the native Deploy menu
// width while retaining essentially the same depth and follow policy.
constexpr float kSettingsMenuNativeWidthScale = 0.80F;
// The Settings pointer reuses QM_Cursor.png at half its initial menu-relative
// physical size; its authored top-left hotspot remains unchanged.
constexpr float kSettingsMenuCursorScale = 0.50F;
// Keep the Settings surface clearly in front of the native Deploy/Spawn UI
// while preserving essentially the same apparent depth and scale.
constexpr float kSettingsMenuForwardOffsetMeters = 0.04F;
constexpr float kSettingsMenuFollowStartRadians = 0.610865238F;
constexpr float kSettingsMenuFollowRadiansPerSecond = 1.570796327F;
constexpr float kSettingsMenuControlColumnPixels = 560.0F;
constexpr float kSettingsMenuSliderRightPixels = 816.0F;
constexpr float kSettingsMenuNumberBoxLeftPixels = 832.0F;
constexpr std::array<float, 4> kSettingsMenuVrPageOneRowCentersPixels = {
    165.0F, 315.0F, 465.0F, 650.0F};
constexpr std::array<float, 4> kSettingsMenuVrPageTwoRowCentersPixels = {
    135.0F, 300.0F, 515.0F, 700.0F};
constexpr std::array<float, 3> kSettingsMenuVrPageThreeRowCentersPixels = {
    180.0F, 370.0F, 610.0F};
constexpr std::array<float, 6> kSettingsMenuVrPageFourRowCentersPixels = {
    125.0F, 230.0F, 335.0F, 440.0F, 545.0F, 650.0F};
constexpr float kSettingsMenuHandResetCenterPixels = 770.0F;
constexpr float kSettingsMenuControlsGripRowCenterPixels = 150.0F;
constexpr std::array<float, 3>
    kSettingsMenuControlsCrosshairRowCentersPixels = {
        205.0F, 335.0F, 465.0F};
constexpr float kSettingsMenuControlsCrosshairColorRowCenterPixels = 595.0F;
constexpr float kSettingsMenuControlsCrosshairOpacityRowCenterPixels = 725.0F;
constexpr float kSettingsMenuControlsVehicleAimSliderCenterPixels = 390.0F;
constexpr std::array<float, 7> kSettingsMenuControlsToggleRowCentersPixels = {
    335.0F, 410.0F, 485.0F, 560.0F, 635.0F, 710.0F, 785.0F};
constexpr float kSettingsMenuSelectorLeftArrowCenterPixels = 585.0F;
constexpr float kSettingsMenuSelectorRightArrowCenterPixels = 910.0F;
constexpr std::array<float, 9> kSettingsMenuGraphicsRowCentersPixels = {
    125.0F, 210.0F, 295.0F, 380.0F,
    465.0F, 550.0F, 635.0F, 720.0F, 805.0F};
constexpr std::array<float, 5> kSettingsMenuColorRowCentersPixels = {
    160.0F, 315.0F, 470.0F, 625.0F, 795.0F};
constexpr float kSettingsMenuAudioRowCenterPixels = 310.0F;
constexpr std::array<std::uint32_t,
    static_cast<std::size_t>(SettingsMenuTab::Count)>
    kSettingsMenuPageCounts = {4, 3, 3};

struct SettingsMenuSnapshot
{
    bool active = false;
    bool visible = false;
    bool pointerVisible = false;
    bool controllerLayoutVisible = false;
    bool arrowLeftVisible = false;
    bool arrowRightVisible = false;
    float pointerU = 0.0F;
    float pointerV = 0.0F;
    float widthMeters = 0.0F;
    float heightMeters = 0.0F;
    Pose panelPose = {};
    SettingsMenuTab tab = SettingsMenuTab::VrSettings;
    SettingsMenuSelection hovered = SettingsMenuSelection::None;
    std::uint32_t page = 0;
    settings::UserSettingsValues values = {};
    SettingsMenuStatus status = SettingsMenuStatus::SettingsLoaded;
};

// Persistent controller-ray interaction for the owner-authored Settings menu.
// Its panel uses the exact yaw-only edge-follow policy of the native Deploy /
// Spawn UI and never inherits either controller's orientation.
class SettingsMenuInteraction
{
public:
    void Configure(float widthMeters, float distanceMeters) noexcept;
    void Open() noexcept;
    void SetValues(const settings::UserSettingsValues& values) noexcept;
    void SetStatus(SettingsMenuStatus status) noexcept;
    void Update(const QuickMenuFrameInput& input) noexcept;
    void Reset() noexcept;
    void ResetTrackingAnchor() noexcept;

    [[nodiscard]] SettingsMenuSnapshot Snapshot() const noexcept;
    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] SettingsMenuCommand TakeCommand() noexcept;
    [[nodiscard]] bool TakeValuesChanged() noexcept;
    [[nodiscard]] SettingsMenuSelection
    TakeMenuSoundActivation() noexcept;

private:
    void Activate(SettingsMenuSelection selection) noexcept;
    [[nodiscard]] std::uint32_t PageCount() const noexcept;
    void SetTurnSpeedFromPointer(float pointerU) noexcept;
    void SetVehicleMotionAimSensitivityFromPointer(float pointerU) noexcept;
    void SetHeightAdjustmentFromPointer(float pointerU) noexcept;
    void SetHandPositionFromPointer(
        SettingsMenuSelection selection,
        float pointerU) noexcept;
    void SetCrosshairOpacityFromPointer(float pointerU) noexcept;
    void SetGraphicsSliderFromPointer(
        SettingsMenuSelection selection,
        float pointerU) noexcept;

    UiMenuAnchorTracker anchor_ = {};
    UiPointerSmoother pointerSmoother_ = {};
    Pose panelPose_ = {};
    SettingsMenuTab tab_ = SettingsMenuTab::VrSettings;
    SettingsMenuSelection hovered_ = SettingsMenuSelection::None;
    SettingsMenuSelection pressed_ = SettingsMenuSelection::None;
    SettingsMenuSelection menuSoundActivation_ =
        SettingsMenuSelection::None;
    SettingsMenuCommand command_ = SettingsMenuCommand::None;
    settings::UserSettingsValues values_ = {};
    SettingsMenuStatus status_ = SettingsMenuStatus::SettingsLoaded;
    std::uint32_t page_ = 0;
    float pointerU_ = 0.0F;
    float pointerV_ = 0.0F;
    float widthMeters_ = 1.6F;
    float distanceMeters_ = 1.5F;
    bool active_ = false;
    bool poseValid_ = false;
    bool pointerVisible_ = false;
    bool controllerLayoutVisible_ = false;
    bool primaryWasHeld_ = false;
    bool sliderDragging_ = false;
    bool valuesChanged_ = false;
    bool standingHeightValid_ = false;
    float standingHeightMeters_ = 0.0F;
};

[[nodiscard]] SettingsMenuSelection SettingsMenuSelectionAt(
    float normalizedX,
    float normalizedY,
    bool arrowLeftVisible,
    bool arrowRightVisible,
    SettingsMenuTab tab = SettingsMenuTab::VrSettings,
    bool controllerLayoutVisible = false,
    std::uint32_t page = 0,
    settings::ArtificialTurnMode turnMode =
        settings::ArtificialTurnMode::Smooth) noexcept;

[[nodiscard]] const wchar_t* SettingsMenuSelectionName(
    SettingsMenuSelection selection) noexcept;

[[nodiscard]] Pose MakeSettingsMenuCursorPose(
    const Pose& panelPose,
    float panelWidthMeters,
    float panelHeightMeters,
    float pointerU,
    float pointerV,
    float cursorWidthMeters,
    float cursorHeightMeters) noexcept;

} // namespace bfvr::stereo
