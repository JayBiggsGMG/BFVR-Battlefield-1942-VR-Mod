#pragma once

#include "settings/UserSettings.h"
#include "stereo/QuickMenuInteraction.h"
#include "stereo/UiPointerMath.h"

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
    AutoCalibrateStandingHeight,
    RecenterForward,
    ComfortVignetteEnabled,
    OffHandGripPrevious,
    OffHandGripNext,
    HandCrosshairPrevious,
    HandCrosshairNext,
    MountedCrosshairPrevious,
    MountedCrosshairNext,
    InvertFlightPitch,
    InvertTurretPitch,
    InvertTurretYaw,
    ControllerHapticsEnabled,
    SniperScopeSmoothingEnabled,
    FxaaEnabled,
    FxaaSharpening,
    AmbientOcclusionEnabled,
    AmbientOcclusionRadius,
    AmbientOcclusionStrength,
    BloomEnabled,
    BloomThreshold,
    BloomIntensity,
    WaterReflectionsEnabled
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
    ForwardRecenterFailed
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
constexpr std::array<float, 3> kSettingsMenuControlsSelectorRowCentersPixels = {
    165.0F, 330.0F, 420.0F};
constexpr std::array<float, 5> kSettingsMenuControlsToggleRowCentersPixels = {
    555.0F, 620.0F, 685.0F, 750.0F, 815.0F};
constexpr float kSettingsMenuSelectorLeftArrowCenterPixels = 585.0F;
constexpr float kSettingsMenuSelectorRightArrowCenterPixels = 910.0F;
constexpr std::array<float, 9> kSettingsMenuGraphicsRowCentersPixels = {
    125.0F, 210.0F, 295.0F, 380.0F,
    465.0F, 550.0F, 635.0F, 720.0F, 805.0F};
constexpr std::array<std::uint32_t,
    static_cast<std::size_t>(SettingsMenuTab::Count)>
    kSettingsMenuPageCounts = {2, 1, 1};

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

private:
    void Activate(SettingsMenuSelection selection) noexcept;
    [[nodiscard]] std::uint32_t PageCount() const noexcept;
    void SetTurnSpeedFromPointer(float pointerU) noexcept;
    void SetHeightAdjustmentFromPointer(float pointerU) noexcept;
    void SetGraphicsSliderFromPointer(
        SettingsMenuSelection selection,
        float pointerU) noexcept;

    UiMenuAnchorTracker anchor_ = {};
    Pose panelPose_ = {};
    SettingsMenuTab tab_ = SettingsMenuTab::VrSettings;
    SettingsMenuSelection hovered_ = SettingsMenuSelection::None;
    SettingsMenuSelection pressed_ = SettingsMenuSelection::None;
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
