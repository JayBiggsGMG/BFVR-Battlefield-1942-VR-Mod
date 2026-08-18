#pragma once

#include "stereo/StereoMath.h"

#include <cstddef>
#include <cstdint>

namespace bfvr::stereo
{

enum class QuickMenuSelection : std::uint32_t
{
    None = 0,
    MainMenu,
    Deploy,
    Weapon1,
    Weapon2,
    Weapon3,
    Weapon4,
    Weapon5,
    Weapon6,
    CameraF9,
    CameraF10,
    CameraF11,
    CameraF12,
    RadioRoger,
    RadioNegative,
    RadioGoGoGo,
    ToggleHud,
    MountedCameraDecouple,
    SwapKit,
    VrSettings,
    Count
};

constexpr std::size_t kQuickMenuSelectionCount =
    static_cast<std::size_t>(QuickMenuSelection::Count);
constexpr std::uint32_t kQuickMenuTextureSize = 512;
// Owner-requested 30% reduction from the initial 0.38 m square panel.
constexpr float kQuickMenuWidthMeters = 0.266F;
constexpr float kQuickMenuHeightMeters = 0.266F;
constexpr std::uint32_t kQuickMenuUtilityTextureWidth = 512;
constexpr std::uint32_t kQuickMenuUtilityTextureHeight = 64;
constexpr std::size_t kQuickMenuUtilityButtonCount = 3;
constexpr std::size_t kQuickMenuUtilityVisualCount = 8;
constexpr std::uint32_t kQuickMenuCommandTextureWidth = 86;
constexpr std::uint32_t kQuickMenuCommandTextureHeight = 425;
constexpr std::size_t kQuickMenuCommandButtonCount = 4;
constexpr std::size_t kQuickMenuCommandVisualCount = 5;
constexpr float kQuickMenuCommandWidthMeters =
    kQuickMenuWidthMeters *
    static_cast<float>(kQuickMenuCommandTextureWidth) /
    static_cast<float>(kQuickMenuTextureSize);
constexpr float kQuickMenuCommandHeightMeters =
    kQuickMenuHeightMeters *
    static_cast<float>(kQuickMenuCommandTextureHeight) /
    static_cast<float>(kQuickMenuTextureSize);
constexpr float kQuickMenuUtilityGapMeters = 0.010F;
constexpr float kQuickMenuUtilityHeightMeters =
    kQuickMenuWidthMeters *
    static_cast<float>(kQuickMenuUtilityTextureHeight) /
    static_cast<float>(kQuickMenuUtilityTextureWidth);
// The panel was originally 0.16 m ahead of the right grip. Moving the whole
// assembly another 0.33 m along the HMD's horizontal forward direction keeps
// its pose, interaction ray, cursor, strip, and desktop mirror synchronized.
constexpr float kQuickMenuHandForwardOffsetMeters = 0.49F;
constexpr float kQuickMenuCursorHotspotX = 0.0F;
constexpr float kQuickMenuCursorHotspotY = 0.0F;

struct QuickMenuFrameInput
{
    std::int64_t predictedDisplayTime = 0;
    bool sessionFocused = false;
    bool shouldRender = false;
    bool headTracked = false;
    bool rightGripTracked = false;
    bool rightAimTracked = false;
    bool rightPrimaryHeld = false;
    Pose headPose = {};
    Pose rightGripPose = {};
    Pose rightAimPose = {};
    bool standingHeightValid = false;
    float standingHeightMeters = 0.0F;
};

struct QuickMenuInteractionSnapshot
{
    bool visible = false;
    bool pointerVisible = false;
    bool pointerOnUtilityStrip = false;
    bool pointerOnCommandColumn = false;
    float pointerU = 0.0F;
    float pointerV = 0.0F;
    Pose panelPose = {};
    QuickMenuSelection hovered = QuickMenuSelection::None;
};

// Right-controller A is a dedicated hold action. The panel follows a
// dead-zoned, smoothed target near the right grip, but never inherits wrist
// orientation. Instead it remains roll-free and faces the current HMD pose.
class QuickMenuInteraction
{
public:
    void Update(const QuickMenuFrameInput& input) noexcept;
    void Reset() noexcept;

    [[nodiscard]] QuickMenuInteractionSnapshot Snapshot() const noexcept;
    [[nodiscard]] QuickMenuSelection TakeReleasedSelection() noexcept;

private:
    void Cancel(bool blockUntilRelease) noexcept;

    Pose panelPose_ = {};
    std::int64_t lastDisplayTime_ = 0;
    QuickMenuSelection hovered_ = QuickMenuSelection::None;
    QuickMenuSelection released_ = QuickMenuSelection::None;
    float pointerU_ = 0.0F;
    float pointerV_ = 0.0F;
    bool visible_ = false;
    bool pointerVisible_ = false;
    bool pointerOnUtilityStrip_ = false;
    bool pointerOnCommandColumn_ = false;
    bool blockedUntilRelease_ = false;
};

[[nodiscard]] QuickMenuSelection QuickMenuSelectionAt(
    float normalizedX,
    float normalizedY) noexcept;

[[nodiscard]] QuickMenuSelection QuickMenuUtilitySelectionAt(
    float normalizedX,
    float normalizedY) noexcept;

[[nodiscard]] QuickMenuSelection QuickMenuCommandSelectionAt(
    float normalizedX,
    float normalizedY) noexcept;

[[nodiscard]] bool IsQuickMenuUtilitySelection(
    QuickMenuSelection selection) noexcept;

[[nodiscard]] bool IsQuickMenuCommandSelection(
    QuickMenuSelection selection) noexcept;

[[nodiscard]] const wchar_t* QuickMenuSelectionName(
    QuickMenuSelection selection) noexcept;

// Places the cursor texture so its authored top-left corner, rather than its
// centre, coincides with the ray hit point on the panel.
[[nodiscard]] Pose MakeQuickMenuCursorPose(
    const Pose& panelPose,
    float pointerU,
    float pointerV,
    float cursorWidthMeters,
    float cursorHeightMeters) noexcept;

[[nodiscard]] Pose MakeQuickMenuUtilityPose(
    const Pose& panelPose) noexcept;

[[nodiscard]] Pose MakeQuickMenuCommandPose(
    const Pose& panelPose) noexcept;

[[nodiscard]] Pose MakeQuickMenuUtilityCursorPose(
    const Pose& panelPose,
    float pointerU,
    float pointerV,
    float cursorWidthMeters,
    float cursorHeightMeters) noexcept;

[[nodiscard]] Pose MakeQuickMenuCommandCursorPose(
    const Pose& panelPose,
    float pointerU,
    float pointerV,
    float cursorWidthMeters,
    float cursorHeightMeters) noexcept;

} // namespace bfvr::stereo
