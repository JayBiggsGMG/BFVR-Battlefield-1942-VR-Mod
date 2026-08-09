#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace bfvr::settings
{

constexpr std::uint32_t kUserSettingsSchemaVersion = 1;
constexpr std::uint32_t kMinimumInfantryTurnSpeedPercent = 50;
constexpr std::uint32_t kMaximumInfantryTurnSpeedPercent = 300;
constexpr std::uint32_t kInfantryTurnSpeedStepPercent = 10;
constexpr std::uint32_t kDefaultInfantryTurnSpeedPercent = 100;
constexpr std::uint32_t kMinimumSnapTurnAngleDegrees = 15;
constexpr std::uint32_t kMaximumSnapTurnAngleDegrees = 90;
constexpr std::uint32_t kSnapTurnAngleStepDegrees = 15;
constexpr std::uint32_t kDefaultSnapTurnAngleDegrees = 45;
constexpr std::int32_t kMinimumVrHeightAdjustmentCentimeters = -30;
constexpr std::int32_t kMaximumVrHeightAdjustmentCentimeters = 30;
constexpr std::int32_t kVrHeightAdjustmentStepCentimeters = 1;
constexpr std::int32_t kDefaultVrHeightAdjustmentCentimeters = 0;
constexpr std::uint32_t kMinimumStandingEyeHeightCentimeters = 50;
constexpr std::uint32_t kMaximumStandingEyeHeightCentimeters = 250;
constexpr std::uint32_t kDefaultStandingEyeHeightCentimeters = 170;
constexpr std::uint32_t kMinimumAmbientOcclusionRadiusCentimeters = 10;
constexpr std::uint32_t kMaximumAmbientOcclusionRadiusCentimeters = 150;
constexpr std::uint32_t kAmbientOcclusionRadiusStepCentimeters = 5;
constexpr std::uint32_t kDefaultAmbientOcclusionRadiusCentimeters = 60;
constexpr std::uint32_t kMinimumAmbientOcclusionStrengthPercent = 0;
constexpr std::uint32_t kMaximumAmbientOcclusionStrengthPercent = 100;
constexpr std::uint32_t kAmbientOcclusionStrengthStepPercent = 5;
constexpr std::uint32_t kDefaultAmbientOcclusionStrengthPercent = 100;
constexpr std::uint32_t kMinimumFxaaSharpeningPercent = 0;
constexpr std::uint32_t kMaximumFxaaSharpeningPercent = 100;
constexpr std::uint32_t kFxaaSharpeningStepPercent = 5;
constexpr std::uint32_t kDefaultFxaaSharpeningPercent = 25;
constexpr std::uint32_t kMinimumBloomThresholdPercent = 5;
constexpr std::uint32_t kMaximumBloomThresholdPercent = 95;
constexpr std::uint32_t kBloomThresholdStepPercent = 5;
constexpr std::uint32_t kDefaultBloomThresholdPercent = 75;
constexpr std::uint32_t kMinimumBloomIntensityPercent = 0;
constexpr std::uint32_t kMaximumBloomIntensityPercent = 100;
constexpr std::uint32_t kBloomIntensityStepPercent = 5;
constexpr std::uint32_t kDefaultBloomIntensityPercent = 25;

enum class OffHandGripStyle : std::uint32_t
{
    Hold = 0,
    Toggle
};

enum class WorldCrosshairMode : std::uint32_t
{
    Off = 0,
    On,
    HitMarkerOnly
};

enum class PlayMode : std::uint32_t
{
    Seated = 0,
    Standing
};

enum class ArtificialTurnMode : std::uint32_t
{
    Snap = 0,
    Smooth
};

enum class MovementDirection : std::uint32_t
{
    Character = 0,
    Head,
    OffHandController
};

struct UserSettingsValues
{
    std::uint32_t infantryTurnSpeedPercent =
        kDefaultInfantryTurnSpeedPercent;
    PlayMode playMode = PlayMode::Seated;
    ArtificialTurnMode artificialTurnMode = ArtificialTurnMode::Smooth;
    std::uint32_t snapTurnAngleDegrees = kDefaultSnapTurnAngleDegrees;
    MovementDirection movementDirection = MovementDirection::Character;
    std::int32_t vrHeightAdjustmentCentimeters =
        kDefaultVrHeightAdjustmentCentimeters;
    std::uint32_t standingEyeHeightCentimeters =
        kDefaultStandingEyeHeightCentimeters;
    bool comfortVignetteEnabled = true;
    bool invertFlightPitch = false;
    bool invertTurretPitch = false;
    bool invertTurretYaw = false;
    bool controllerHapticsEnabled = true;
    OffHandGripStyle offHandGripStyle = OffHandGripStyle::Hold;
    WorldCrosshairMode handWeaponCrosshair = WorldCrosshairMode::On;
    WorldCrosshairMode mountedWeaponCrosshair = WorldCrosshairMode::On;
    bool fxaaEnabled = true;
    std::uint32_t fxaaSharpeningPercent = kDefaultFxaaSharpeningPercent;
    bool ambientOcclusionEnabled = true;
    std::uint32_t ambientOcclusionRadiusCentimeters =
        kDefaultAmbientOcclusionRadiusCentimeters;
    std::uint32_t ambientOcclusionStrengthPercent =
        kDefaultAmbientOcclusionStrengthPercent;
    bool waterReflectionsEnabled = true;
    bool bloomEnabled = true;
    std::uint32_t bloomThresholdPercent = kDefaultBloomThresholdPercent;
    std::uint32_t bloomIntensityPercent = kDefaultBloomIntensityPercent;

    [[nodiscard]] bool operator==(const UserSettingsValues&) const = default;
};

using UserSettingValidator = bool (*)(std::string_view value) noexcept;

struct UserSettingSeed
{
    std::string key;
    std::string defaultValue;
    // Each line is emitted as a comment immediately above this setting. It
    // should explain behavior, tradeoffs, units/range, and accepted values so
    // UserConfig.txt remains a complete manual-editing reference.
    std::vector<std::string> documentation;
    UserSettingValidator validator = nullptr;
};

using UserSettingsSchema = std::vector<UserSettingSeed>;

// This is the single production default seed. Actual user-facing keys will be
// added here as their controls are implemented; the persistence/session layer
// does not need to change when the schema grows.
[[nodiscard]] UserSettingsSchema SeededUserSettingsSchema();

struct UserSettings
{
    std::map<std::string, std::string> values;

    [[nodiscard]] bool operator==(const UserSettings&) const = default;
};

[[nodiscard]] UserSettingsValues DecodeUserSettings(
    const UserSettings& settings) noexcept;
void EncodeUserSettings(
    const UserSettingsValues& values,
    UserSettings& settings);

// Resource-negotiated settings cannot be applied to an active producer /
// presenter pair. This pure comparison owns the menu's restart-required rule.
[[nodiscard]] bool UserSettingsRequireRestart(
    const UserSettingsValues& startup,
    const UserSettingsValues& saved) noexcept;

// Manual trim is deliberately independent of Seated/Standing placement.
// Standing floor mapping is performed from simultaneous LOCAL and STAGE poses
// by the x86 context anchor, not folded into this persisted adjustment.
[[nodiscard]] float ComputeManualHeightAdjustmentMeters(
    const UserSettingsValues& values) noexcept;

enum class UserSettingsLoadStatus : std::uint32_t
{
    Loaded = 0,
    LoadedCompletedSeed,
    MissingUsedDefaults,
    MissingCreatedDefaults,
    InvalidUsedDefaults,
    IoErrorUsedDefaults
};

struct UserSettingsLoadResult
{
    UserSettings settings;
    UserSettingsLoadStatus status =
        UserSettingsLoadStatus::IoErrorUsedDefaults;
};

// Versioned, human-readable, atomic UserConfig.txt persistence. Only keys in
// the current seed schema are accepted or written; unknown future/old keys are
// ignored until their owning implementation exists.
class UserSettingsStore
{
public:
    bool Initialize(
        const std::wstring& path,
        UserSettingsSchema schema = SeededUserSettingsSchema());

    [[nodiscard]] UserSettings Defaults() const;
    [[nodiscard]] UserSettingsLoadResult Load() const;
    [[nodiscard]] UserSettingsLoadResult LoadOrCreateDefaults() const;
    [[nodiscard]] bool Save(const UserSettings& settings) const;
    [[nodiscard]] bool IsReady() const noexcept;
    [[nodiscard]] const std::wstring& Path() const noexcept;

private:
    [[nodiscard]] const UserSettingSeed* FindSeed(
        std::string_view key) const noexcept;
    [[nodiscard]] bool IsValid(
        const UserSettingSeed& seed,
        std::string_view value) const noexcept;

    std::wstring path_;
    UserSettingsSchema schema_;
    bool ready_ = false;
};

// Owns one menu-open transaction. Reset mutates only working values, Save is
// the only disk write, and Cancel discards the transaction. A later Begin
// always reloads the last saved file (or the seed if it does not exist).
class UserSettingsSession
{
public:
    UserSettingsLoadStatus Begin(const UserSettingsStore& store);
    void ResetToDefaults();
    [[nodiscard]] bool Save();
    void Cancel() noexcept;

    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] const UserSettings& Working() const noexcept;
    [[nodiscard]] UserSettings& Working() noexcept;

private:
    const UserSettingsStore* store_ = nullptr;
    UserSettings working_;
    bool active_ = false;
};

// Per-process startup owner. BFVRClient and BFVRPresenter each initialize this
// against the same file. Missing files are atomically seed-created; invalid
// files are never overwritten implicitly, but runtime safely uses defaults.
class UserSettingsRuntime
{
public:
    UserSettingsLoadStatus Initialize(const wchar_t* payloadDirectory);
    UserSettingsLoadStatus Reload();
    // Performs a metadata check and reloads only when another process or a
    // manual editor has changed, replaced, created, or deleted the file.
    [[nodiscard]] bool ReloadIfChanged();
    [[nodiscard]] bool Commit(const UserSettings& settings);

    [[nodiscard]] bool IsReady() const noexcept;
    [[nodiscard]] const UserSettings& Current() const noexcept;
    [[nodiscard]] const UserSettingsStore& Store() const noexcept;

private:
    UserSettingsStore store_;
    UserSettings current_;
    std::uint64_t observedWriteTime_ = 0;
    std::uint64_t observedFileSize_ = 0;
    bool observedFileExists_ = false;
    bool ready_ = false;
};

[[nodiscard]] UserSettingsRuntime& ProcessUserSettingsRuntime();

// Normal launcher contract: BFVR_USER_CONFIG_PATH names BFVR\UserConfig.txt.
// The fallbacks keep standalone probes and direct presenter runs deterministic.
[[nodiscard]] std::wstring ResolveUserSettingsPath(
    const wchar_t* payloadDirectory);

[[nodiscard]] const wchar_t* UserSettingsLoadStatusName(
    UserSettingsLoadStatus status) noexcept;

} // namespace bfvr::settings
