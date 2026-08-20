#include "settings/UserSettings.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <string_view>

namespace
{
constexpr std::uint64_t kMaximumUserConfigBytes = 1024U * 1024U;
constexpr wchar_t kUserConfigEnvironmentName[] = L"BFVR_USER_CONFIG_PATH";
constexpr wchar_t kUserConfigFileName[] = L"UserConfig.txt";
constexpr std::string_view kInfantryTurnSpeedKey =
    "infantry_turn_speed_percent";
constexpr std::string_view kPlayModeKey = "play_mode";
constexpr std::string_view kArtificialTurnModeKey = "artificial_turning";
constexpr std::string_view kSnapTurnAngleKey = "snap_turn_angle_degrees";
constexpr std::string_view kMovementDirectionKey = "movement_direction";
constexpr std::string_view kVrHeightAdjustmentKey =
    "manual_height_adjustment_centimeters";
constexpr std::string_view kRightHandPositionXKey =
    "right_hand_position_x_centimeters";
constexpr std::string_view kRightHandPositionYKey =
    "right_hand_position_y_centimeters";
constexpr std::string_view kRightHandPositionZKey =
    "right_hand_position_z_centimeters";
constexpr std::string_view kLeftHandPositionXKey =
    "left_hand_position_x_centimeters";
constexpr std::string_view kLeftHandPositionYKey =
    "left_hand_position_y_centimeters";
constexpr std::string_view kLeftHandPositionZKey =
    "left_hand_position_z_centimeters";
constexpr std::string_view kStandingEyeHeightKey =
    "standing_eye_height_centimeters";
constexpr std::string_view kComfortVignetteEnabledKey =
    "comfort_vignette_enabled";
constexpr std::string_view kDeathCameraComfortEnabledKey =
    "death_camera_comfort_enabled";
constexpr std::string_view kShowArmsKey = "show_arms";
constexpr std::string_view kInvertFlightPitchKey = "invert_flight_pitch";
constexpr std::string_view kAircraftPitchWithRollKey =
    "aircraft_pitch_with_roll";
constexpr std::string_view kSwapAircraftSticksKey =
    "swap_aircraft_sticks";
constexpr std::string_view kInvertTurretPitchKey = "invert_turret_pitch";
constexpr std::string_view kInvertTurretYawKey = "invert_turret_yaw";
constexpr std::string_view kVehicleMotionAimSensitivityKey =
    "vehicle_motion_aim_sensitivity_percent";
constexpr std::string_view kControllerHapticsEnabledKey =
    "controller_haptics_enabled";
constexpr std::string_view kKillSoundEnabledKey = "kill_sound_enabled";
constexpr std::string_view kSniperScopeSmoothingEnabledKey =
    "sniper_scope_smoothing_enabled";
constexpr std::string_view kMenuPointerSmoothingEnabledKey =
    "menu_pointer_smoothing_enabled";
constexpr std::string_view kOffHandGripStyleKey = "off_hand_grip_style";
constexpr std::string_view kHandWeaponCrosshairKey =
    "hand_weapon_3d_crosshair";
constexpr std::string_view kMountedWeaponCrosshairKey =
    "mounted_weapon_3d_crosshair";
constexpr std::string_view kPointerItemCrosshairKey =
    "knife_throwable_gadget_3d_crosshair";
constexpr std::string_view kCrosshairColorKey = "3d_crosshair_color";
constexpr std::string_view kCrosshairOpacityKey =
    "3d_crosshair_opacity_percent";
constexpr std::string_view kFxaaEnabledKey = "fxaa_enabled";
constexpr std::string_view kFxaaSharpeningKey =
    "fxaa_sharpening_percent";
constexpr std::string_view kAmbientOcclusionEnabledKey =
    "ambient_occlusion_enabled";
constexpr std::string_view kAmbientOcclusionRadiusKey =
    "ambient_occlusion_radius_centimeters";
constexpr std::string_view kAmbientOcclusionStrengthKey =
    "ambient_occlusion_strength_percent";
constexpr std::string_view kWaterReflectionsEnabledKey =
    "water_reflections_enabled";
constexpr std::string_view kBloomEnabledKey = "bloom_enabled";
constexpr std::string_view kBloomThresholdKey = "bloom_threshold_percent";
constexpr std::string_view kBloomIntensityKey = "bloom_intensity_percent";
constexpr std::string_view kColorProfileKey = "color_profile";
constexpr std::string_view kColorExposureKey = "color_exposure_ev";
constexpr std::string_view kColorContrastKey = "color_contrast_percent";
constexpr std::string_view kColorSaturationKey = "color_saturation_percent";

std::string_view Trim(std::string_view value) noexcept
{
    constexpr std::string_view whitespace = " \t\r\n";
    const std::size_t first = value.find_first_not_of(whitespace);
    if (first == std::string_view::npos)
    {
        return {};
    }
    const std::size_t last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

bool IsValidKey(std::string_view key) noexcept
{
    if (key.empty())
    {
        return false;
    }
    return std::all_of(
        key.begin(),
        key.end(),
        [](char value) {
            return (value >= 'a' && value <= 'z') ||
                (value >= '0' && value <= '9') || value == '_';
        });
}

bool IsDirectory(const std::wstring& path) noexcept
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool IsBoolean(std::string_view value) noexcept
{
    return value == "true" || value == "false";
}

bool IsOffHandGripStyle(std::string_view value) noexcept
{
    return value == "hold" || value == "toggle";
}

bool IsFirstPersonVisibility(std::string_view value) noexcept
{
    // true/false are accepted as a one-save migration path for development
    // configurations written before the three-state selector existed.
    return value == "arms_and_hands" || value == "hands_only" ||
        value == "no_hands_or_arms" || value == "true" || value == "false";
}

bool IsWorldCrosshairMode(std::string_view value) noexcept
{
    return value == "off" || value == "on" ||
        value == "hit_marker_only";
}

bool IsCrosshairColor(std::string_view value) noexcept
{
    return value == "white" || value == "green" || value == "blue" ||
        value == "purple" || value == "red" || value == "pink" ||
        value == "orange" || value == "yellow" || value == "magenta";
}

bool IsColorProfile(std::string_view value) noexcept
{
    return value == "original" || value == "filmic" || value == "vibrant";
}

bool IsPlayMode(std::string_view value) noexcept
{
    return value == "seated" || value == "standing";
}

bool IsArtificialTurnMode(std::string_view value) noexcept
{
    // Accept the retired "off" spelling only for migration. Decode maps it
    // to snap, and the next Save rewrites the supported value.
    return value == "off" || value == "snap" || value == "smooth";
}

bool IsMovementDirection(std::string_view value) noexcept
{
    return value == "character" || value == "head" ||
        value == "off_hand_controller";
}

bool IsInfantryTurnSpeed(std::string_view value) noexcept
{
    std::uint32_t percent = 0;
    const auto parsed = std::from_chars(
        value.data(),
        value.data() + value.size(),
        percent);
    return parsed.ec == std::errc{} &&
        parsed.ptr == value.data() + value.size() &&
        percent >= bfvr::settings::kMinimumInfantryTurnSpeedPercent &&
        percent <= bfvr::settings::kMaximumInfantryTurnSpeedPercent &&
        percent % bfvr::settings::kInfantryTurnSpeedStepPercent == 0;
}

bool IsVehicleMotionAimSensitivity(std::string_view value) noexcept
{
    std::uint32_t percent = 0;
    const auto parsed = std::from_chars(
        value.data(),
        value.data() + value.size(),
        percent);
    return parsed.ec == std::errc{} &&
        parsed.ptr == value.data() + value.size() &&
        percent >=
            bfvr::settings::kMinimumVehicleMotionAimSensitivityPercent &&
        percent <=
            bfvr::settings::kMaximumVehicleMotionAimSensitivityPercent &&
        percent %
            bfvr::settings::kVehicleMotionAimSensitivityStepPercent == 0;
}

bool IsSnapTurnAngle(std::string_view value) noexcept
{
    std::uint32_t degrees = 0;
    const auto parsed = std::from_chars(
        value.data(), value.data() + value.size(), degrees);
    return parsed.ec == std::errc{} &&
        parsed.ptr == value.data() + value.size() &&
        degrees >= bfvr::settings::kMinimumSnapTurnAngleDegrees &&
        degrees <= bfvr::settings::kMaximumSnapTurnAngleDegrees &&
        (degrees - bfvr::settings::kMinimumSnapTurnAngleDegrees) %
                bfvr::settings::kSnapTurnAngleStepDegrees ==
            0;
}

bool IsVrHeightAdjustment(std::string_view value) noexcept
{
    std::int32_t centimeters = 0;
    const auto parsed = std::from_chars(
        value.data(), value.data() + value.size(), centimeters);
    return parsed.ec == std::errc{} &&
        parsed.ptr == value.data() + value.size() &&
        centimeters >=
            bfvr::settings::kMinimumVrHeightAdjustmentCentimeters &&
        centimeters <=
            bfvr::settings::kMaximumVrHeightAdjustmentCentimeters &&
        (centimeters -
         bfvr::settings::kMinimumVrHeightAdjustmentCentimeters) %
                bfvr::settings::kVrHeightAdjustmentStepCentimeters ==
            0;
}

bool IsStandingEyeHeight(std::string_view value) noexcept
{
    std::uint32_t centimeters = 0;
    const auto parsed = std::from_chars(
        value.data(), value.data() + value.size(), centimeters);
    return parsed.ec == std::errc{} &&
        parsed.ptr == value.data() + value.size() &&
        centimeters >= bfvr::settings::kMinimumStandingEyeHeightCentimeters &&
        centimeters <= bfvr::settings::kMaximumStandingEyeHeightCentimeters;
}

bool IsUnsignedInRangeStep(
    std::string_view value,
    std::uint32_t minimum,
    std::uint32_t maximum,
    std::uint32_t step) noexcept
{
    std::uint32_t parsedValue = 0;
    const auto parsed = std::from_chars(
        value.data(),
        value.data() + value.size(),
        parsedValue);
    return parsed.ec == std::errc{} &&
        parsed.ptr == value.data() + value.size() &&
        parsedValue >= minimum && parsedValue <= maximum &&
        step != 0 && (parsedValue - minimum) % step == 0;
}

bool IsSignedInRangeStep(
    std::string_view value,
    std::int32_t minimum,
    std::int32_t maximum,
    std::int32_t step) noexcept
{
    std::int32_t parsedValue = 0;
    const auto parsed = std::from_chars(
        value.data(),
        value.data() + value.size(),
        parsedValue);
    return parsed.ec == std::errc{} &&
        parsed.ptr == value.data() + value.size() &&
        parsedValue >= minimum && parsedValue <= maximum && step > 0 &&
        (parsedValue - minimum) % step == 0;
}

bool ParseFixedTenths(
    std::string_view value,
    std::int32_t& tenths) noexcept
{
    tenths = 0;
    if (value.empty())
    {
        return false;
    }
    bool negative = false;
    std::size_t offset = 0;
    if (value.front() == '-' || value.front() == '+')
    {
        negative = value.front() == '-';
        offset = 1;
    }
    if (offset >= value.size())
    {
        return false;
    }
    const std::size_t dot = value.find('.', offset);
    const std::string_view wholeText = dot == std::string_view::npos
        ? value.substr(offset)
        : value.substr(offset, dot - offset);
    if (wholeText.empty() ||
        (dot != std::string_view::npos && dot + 2 != value.size()))
    {
        return false;
    }
    std::int32_t whole = 0;
    const auto parsed = std::from_chars(
        wholeText.data(),
        wholeText.data() + wholeText.size(),
        whole);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != wholeText.data() + wholeText.size())
    {
        return false;
    }
    std::int32_t fraction = 0;
    if (dot != std::string_view::npos)
    {
        const char digit = value[dot + 1];
        if (digit < '0' || digit > '9')
        {
            return false;
        }
        fraction = digit - '0';
    }
    if (whole > (std::numeric_limits<std::int32_t>::max() - fraction) / 10)
    {
        return false;
    }
    tenths = whole * 10 + fraction;
    if (negative)
    {
        tenths = -tenths;
    }
    return true;
}

std::string EncodeFixedTenths(std::int32_t value)
{
    const bool negative = value < 0;
    const std::int32_t magnitude = negative ? -value : value;
    std::string result = negative ? "-" : "";
    result += std::to_string(magnitude / 10);
    result.push_back('.');
    result += std::to_string(magnitude % 10);
    return result;
}

bool IsColorExposure(std::string_view value) noexcept
{
    std::int32_t tenths = 0;
    return ParseFixedTenths(value, tenths) &&
        tenths >= bfvr::settings::kMinimumColorExposureTenthsEv &&
        tenths <= bfvr::settings::kMaximumColorExposureTenthsEv &&
        (tenths - bfvr::settings::kMinimumColorExposureTenthsEv) %
                bfvr::settings::kColorExposureStepTenthsEv == 0;
}

bool IsColorContrast(std::string_view value) noexcept
{
    return IsSignedInRangeStep(
        value,
        bfvr::settings::kMinimumColorContrastPercent,
        bfvr::settings::kMaximumColorContrastPercent,
        bfvr::settings::kColorContrastStepPercent);
}

bool IsColorSaturation(std::string_view value) noexcept
{
    return IsSignedInRangeStep(
        value,
        bfvr::settings::kMinimumColorSaturationPercent,
        bfvr::settings::kMaximumColorSaturationPercent,
        bfvr::settings::kColorSaturationStepPercent);
}

bool IsHandPosition(std::string_view value) noexcept
{
    std::int32_t centimeters = 0;
    const auto parsed = std::from_chars(
        value.data(), value.data() + value.size(), centimeters);
    return parsed.ec == std::errc{} &&
        parsed.ptr == value.data() + value.size() &&
        centimeters >= bfvr::settings::kMinimumHandPositionCentimeters &&
        centimeters <= bfvr::settings::kMaximumHandPositionCentimeters &&
        (centimeters - bfvr::settings::kMinimumHandPositionCentimeters) %
                bfvr::settings::kHandPositionStepCentimeters ==
            0;
}

bool IsAmbientOcclusionRadius(std::string_view value) noexcept
{
    return IsUnsignedInRangeStep(
        value,
        bfvr::settings::kMinimumAmbientOcclusionRadiusCentimeters,
        bfvr::settings::kMaximumAmbientOcclusionRadiusCentimeters,
        bfvr::settings::kAmbientOcclusionRadiusStepCentimeters);
}

bool IsFxaaSharpening(std::string_view value) noexcept
{
    return IsUnsignedInRangeStep(
        value,
        bfvr::settings::kMinimumFxaaSharpeningPercent,
        bfvr::settings::kMaximumFxaaSharpeningPercent,
        bfvr::settings::kFxaaSharpeningStepPercent);
}

bool IsCrosshairOpacity(std::string_view value) noexcept
{
    return IsUnsignedInRangeStep(
        value,
        bfvr::settings::kMinimumCrosshairOpacityPercent,
        bfvr::settings::kMaximumCrosshairOpacityPercent,
        bfvr::settings::kCrosshairOpacityStepPercent);
}

bool IsAmbientOcclusionStrength(std::string_view value) noexcept
{
    return IsUnsignedInRangeStep(
        value,
        bfvr::settings::kMinimumAmbientOcclusionStrengthPercent,
        bfvr::settings::kMaximumAmbientOcclusionStrengthPercent,
        bfvr::settings::kAmbientOcclusionStrengthStepPercent);
}

bool IsBloomThreshold(std::string_view value) noexcept
{
    return IsUnsignedInRangeStep(
        value,
        bfvr::settings::kMinimumBloomThresholdPercent,
        bfvr::settings::kMaximumBloomThresholdPercent,
        bfvr::settings::kBloomThresholdStepPercent);
}

bool IsBloomIntensity(std::string_view value) noexcept
{
    return IsUnsignedInRangeStep(
        value,
        bfvr::settings::kMinimumBloomIntensityPercent,
        bfvr::settings::kMaximumBloomIntensityPercent,
        bfvr::settings::kBloomIntensityStepPercent);
}

bool QueryFileSignature(
    const std::wstring& path,
    bool& exists,
    std::uint64_t& writeTime,
    std::uint64_t& fileSize) noexcept
{
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (GetFileAttributesExW(
            path.c_str(),
            GetFileExInfoStandard,
            &data) == FALSE)
    {
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
        {
            return false;
        }
        exists = false;
        writeTime = 0;
        fileSize = 0;
        return true;
    }
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        return false;
    }
    exists = true;
    writeTime =
        (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32U) |
        data.ftLastWriteTime.dwLowDateTime;
    fileSize = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32U) |
        data.nFileSizeLow;
    return true;
}

std::wstring ParentPath(const std::wstring& path)
{
    const std::size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos
        ? std::wstring{}
        : path.substr(0, separator);
}

std::wstring JoinPath(
    const std::wstring& directory,
    const wchar_t* child)
{
    if (directory.empty() || child == nullptr || child[0] == L'\0')
    {
        return {};
    }
    std::wstring result = directory;
    if (result.back() != L'\\' && result.back() != L'/')
    {
        result.push_back(L'\\');
    }
    result.append(child);
    return result;
}

bool ReadWholeFile(
    const std::wstring& path,
    std::string& bytes,
    DWORD& errorCode)
{
    bytes.clear();
    errorCode = ERROR_SUCCESS;
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        errorCode = GetLastError();
        return false;
    }
    LARGE_INTEGER size = {};
    bool success = GetFileSizeEx(file, &size) != FALSE &&
        size.QuadPart >= 0 &&
        static_cast<std::uint64_t>(size.QuadPart) <=
            kMaximumUserConfigBytes &&
        size.QuadPart <= std::numeric_limits<DWORD>::max();
    if (success)
    {
        bytes.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD bytesRead = 0;
        success = bytes.empty() ||
            (ReadFile(
                 file,
                 bytes.data(),
                 static_cast<DWORD>(bytes.size()),
                 &bytesRead,
                 nullptr) != FALSE &&
             bytesRead == bytes.size());
    }
    if (!success)
    {
        errorCode = GetLastError();
        if (errorCode == ERROR_SUCCESS)
        {
            errorCode = ERROR_INVALID_DATA;
        }
        bytes.clear();
    }
    CloseHandle(file);
    return success;
}

bool WriteWholeFile(HANDLE file, const std::string& bytes)
{
    std::size_t offset = 0;
    while (offset < bytes.size())
    {
        const DWORD chunk = static_cast<DWORD>((std::min)(
            bytes.size() - offset,
            static_cast<std::size_t>(
                std::numeric_limits<DWORD>::max())));
        DWORD written = 0;
        if (WriteFile(
                file,
                bytes.data() + offset,
                chunk,
                &written,
                nullptr) == FALSE ||
            written != chunk)
        {
            return false;
        }
        offset += written;
    }
    return true;
}
} // namespace

namespace bfvr::settings
{

UserSettingsSchema SeededUserSettingsSchema()
{
    return {
        {
            std::string(kPlayModeKey),
            "seated",
            {
                "Describes the player's current VR posture. seated captures the seated headset height as neutral; if selected while still standing, BFVR follows the ensuing sit-down and locks after the headset settles. standing continuously maps the OpenXR floor-to-head height onto Battlefield's existing 1.70-m infantry eye camera, so switching posture during a session does not stack two eye heights.",
                "Play Mode never rotates the character automatically. Infantry turning remains an explicit Snap or Smooth right-thumbstick action.",
                "Accepted values: seated or standing. This setting is applied only after VR Settings > Save. It does not add body trackers, change the BF1942 collision capsule, or change world scale."
            },
            IsPlayMode
        },
        {
            std::string(kArtificialTurnModeKey),
            "smooth",
            {
                "Controls infantry right-thumbstick turning. snap rotates by the configured angle once per stick deflection; smooth uses the configured Turn Speed.",
                "Accepted values: snap or smooth. The retired off value is read as snap for compatibility. This does not affect vehicles, aircraft, turrets, or mounted weapons and is applied only after VR Settings > Save."
            },
            IsArtificialTurnMode
        },
        {
            std::string(kSnapTurnAngleKey),
            std::to_string(kDefaultSnapTurnAngleDegrees),
            {
                "Sets the angle of one infantry snap-turn step. It is used only when artificial_turning is snap.",
                "Units are degrees. Accepted values: 15 through 90 in steps of 15. This setting is applied only after VR Settings > Save."
            },
            IsSnapTurnAngle
        },
        {
            std::string(kInfantryTurnSpeedKey),
            std::to_string(kDefaultInfantryTurnSpeedPercent),
            {
                "Infantry right-thumbstick smooth-turn speed as a percentage of BFVR's original tuned speed. This does not affect vehicles, aircraft, turrets, or mounted weapons.",
                "Accepted values: 50 through 300 in steps of 10. 100 is the original speed, 50 is half speed, and 300 is three times that speed."
            },
            IsInfantryTurnSpeed
        },
        {
            std::string(kMovementDirectionKey),
            "head",
            {
                "Chooses the forward basis for infantry left-thumbstick movement. character follows BF1942's character-facing basis; head follows the headset yaw; off_hand_controller follows the left controller's pointing yaw.",
                "Accepted values: character, head, or off_hand_controller. Head and controller pitch/roll are ignored. This setting is applied only after VR Settings > Save."
            },
            IsMovementDirection
        },
        {
            std::string(kVrHeightAdjustmentKey),
            std::to_string(kDefaultVrHeightAdjustmentCentimeters),
            {
                "Adds a manual vertical trim after Seated or Standing placement. It moves the infantry headset, both controllers, and weapon presentation together and is excluded from every vehicle or mounted-seat anchor.",
                "It does not resize the world or alter BF1942 collision, stance, or networked player position. Units are centimeters; accepted values are -30 through 30 in steps of 1. Positive values raise the infantry view. Applied only after VR Settings > Save."
            },
            IsVrHeightAdjustment
        },
        {
            std::string(kRightHandPositionXKey),
            std::to_string(kDefaultRightHandPositionXCentimeters),
            {
                "Moves the visible right hand and its attached held item sideways in the player's body-local frame. Positive X is right; negative X is left. It does not alter aim, firing, projectile direction, or the crosshair.",
                "Units are centimeters; accepted values are -20 through 20 in steps of 1. Applied live after VR Settings > Save without a restart."
            },
            IsHandPosition
        },
        {
            std::string(kRightHandPositionYKey),
            std::to_string(kDefaultRightHandPositionYCentimeters),
            {
                "Moves the visible right hand and its attached held item vertically in the player's body-local frame. Positive Y is up; negative Y is down. It does not alter aim, firing, projectile direction, or the crosshair.",
                "Units are centimeters; accepted values are -20 through 20 in steps of 1. Applied live after VR Settings > Save without a restart."
            },
            IsHandPosition
        },
        {
            std::string(kRightHandPositionZKey),
            std::to_string(kDefaultRightHandPositionZCentimeters),
            {
                "Moves the visible right hand and its attached held item forward or backward in the player's body-local frame. Positive Z is forward; negative Z is backward. It does not alter aim, firing, projectile direction, or the crosshair.",
                "Units are centimeters; accepted values are -20 through 20 in steps of 1. Applied live after VR Settings > Save without a restart."
            },
            IsHandPosition
        },
        {
            std::string(kLeftHandPositionXKey),
            std::to_string(kDefaultLeftHandPositionXCentimeters),
            {
                "Moves the visible free left hand sideways in the player's body-local frame. Positive X is right; negative X is left. A rifle or sidearm support pose keeps its authored hand-to-weapon relationship.",
                "Units are centimeters; accepted values are -20 through 20 in steps of 1. Applied live after VR Settings > Save without a restart; aim and firing are unchanged."
            },
            IsHandPosition
        },
        {
            std::string(kLeftHandPositionYKey),
            std::to_string(kDefaultLeftHandPositionYCentimeters),
            {
                "Moves the visible free left hand vertically in the player's body-local frame. Positive Y is up; negative Y is down. A rifle or sidearm support pose keeps its authored hand-to-weapon relationship.",
                "Units are centimeters; accepted values are -20 through 20 in steps of 1. Applied live after VR Settings > Save without a restart; aim and firing are unchanged."
            },
            IsHandPosition
        },
        {
            std::string(kLeftHandPositionZKey),
            std::to_string(kDefaultLeftHandPositionZCentimeters),
            {
                "Moves the visible free left hand forward or backward in the player's body-local frame. Positive Z is forward; negative Z is backward. A rifle or sidearm support pose keeps its authored hand-to-weapon relationship.",
                "Units are centimeters; accepted values are -20 through 20 in steps of 1. Applied live after VR Settings > Save without a restart; aim and firing are unchanged."
            },
            IsHandPosition
        },
        {
            std::string(kStandingEyeHeightKey),
            std::to_string(kDefaultStandingEyeHeightCentimeters),
            {
                "Records the latest Auto-Calibrate Standing measurement from the OpenXR STAGE floor to the headset. Standing placement itself remains tied to that live floor, so sitting, standing, or switching Play Mode cannot add the full measurement on top of Battlefield's camera.",
                "Units are centimeters; accepted values are 50 through 250. Seated mode ignores the floor measurement and captures the current posture as neutral. Auto-Calibrate stages the current value and Save applies the selected Play Mode."
            },
            IsStandingEyeHeight
        },
        {
            std::string(kComfortVignetteEnabledKey),
            "false",
            {
                "Enables a movement-only VR comfort vignette. BFVR filters translation of the local infantry or occupied vehicle control object into a stable moving/stopped state, then eases a soft black peripheral aperture in and out. Physical head movement, head look, artificial turning, turret aim, and vehicle rotation in place do not activate it.",
                "The effect is composited above the stereo world but below Ref2 HUD, scope, Quick Menu, VR Settings, and other overlays, so interface elements remain clear. Accepted values: true or false. Applied after VR Settings > Save without a restart."
            },
            IsBoolean
        },
        {
            std::string(kDeathCameraComfortEnabledKey),
            "true",
            {
                "Enables BFVR's death-camera comfort effect. A verified local-player alive-to-dead transition quickly closes a stronger muted dark-red peripheral aperture for the native death-camera flight, then eases it away without flattening, freezing, or head-locking the stereo world.",
                "This effect is independent of comfort_vignette_enabled and takes priority through the same single vignette compositor, so the two effects never stack. Accepted values: true or false. Applied after VR Settings > Save without a restart."
            },
            IsBoolean
        },
        {
            std::string(kShowArmsKey),
            "arms_and_hands",
            {
                "Controls BFVR's stereo replay of native first-person arms and hands. arms_and_hands shows the complete game-selected presentation; hands_only hides separate arm/combined meshes while retaining explicitly identified left/right hand meshes; no_hands_or_arms hides every classified first-person part.",
                "A mod which combines hands and arms into one mesh cannot be split: that combined or unrecognized mesh is hidden in hands_only and restored by arms_and_hands. Native animation, controller IK, hand placement, elbow placement, weapon transforms, and gameplay state continue in every mode. Applied after VR Settings > Save without a restart."
            },
            IsFirstPersonVisibility
        },
        {
            std::string(kInvertFlightPitchKey),
            "false",
            {
                "Inverts the aircraft pitch stick's vertical axis only. It does not change infantry turning, aircraft throttle, or turret elevation.",
                "Accepted values: true or false. false keeps stick-up as dive/nose-down; true makes stick-up climb/nose-up."
            },
            IsBoolean
        },
        {
            std::string(kAircraftPitchWithRollKey),
            "false",
            {
                "Chooses the aircraft pitch stick's horizontal action. false pairs pitch with yaw; true pairs pitch with roll.",
                "Accepted values: true or false. The separate swap_aircraft_sticks option chooses whether that pitch stick is the right or left controller. Applied after Controls > Save."
            },
            IsBoolean
        },
        {
            std::string(kSwapAircraftSticksKey),
            "false",
            {
                "Exchanges the complete left- and right-stick aircraft roles. false keeps throttle on the left stick and pitch on the right; true puts pitch on the left and throttle on the right.",
                "Accepted values: true or false. This works together with aircraft_pitch_with_roll, allowing pitch/yaw or pitch/roll on either physical stick. Applied after Controls > Save."
            },
            IsBoolean
        },
        {
            std::string(kInvertTurretPitchKey),
            "true",
            {
                "Inverts up/down aiming for surface vehicles, sea vehicles, turrets, and mounted weapons, including right-stick and right-grip motion aim.",
                "Accepted values: true or false. false keeps stick and controller movement aligned in their normal pitch direction; true reverses both."
            },
            IsBoolean
        },
        {
            std::string(kInvertTurretYawKey),
            "false",
            {
                "Inverts left/right aiming for surface vehicles, sea vehicles, turrets, and mounted weapons, including right-stick and right-grip motion aim.",
                "Accepted values: true or false. false keeps stick and controller movement aligned in their normal yaw direction; true reverses both."
            },
            IsBoolean
        },
        {
            std::string(kVehicleMotionAimSensitivityKey),
            std::to_string(kDefaultVehicleMotionAimSensitivityPercent),
            {
                "Sets how strongly right-controller movement aims tank cannons and other land/sea vehicle, turret, AA, and mounted weapons. It does not change right-stick sensitivity, infantry aim, aircraft controls, or BF1942's authored turret speed limits.",
                "Accepted values are 50 through 300 percent in steps of 10. 100 is the original motion-aim response and the default 200 requires half the original physical hand travel. Applied after Controls > Save without a restart."
            },
            IsVehicleMotionAimSensitivity
        },
        {
            std::string(kControllerHapticsEnabledKey),
            "true",
            {
                "Enables all BFVR controller vibration: a light impulse when the right-hand pointer enters a menu control, one firing impulse per accepted local weapon shot, and a death impulse on both hands.",
                "One-handed firing vibrates only the right controller; an acquired two-hand weapon-support grip vibrates both controllers. Accepted values: true or false. Applied after Controls > Save."
            },
            IsBoolean
        },
        {
            std::string(kKillSoundEnabledKey),
            "true",
            {
                "Plays the supplied BFVR kill sound when Battlefield's authoritative score handling confirms that the current local player killed another player. Shots, hit markers, deaths, suicides, and team-kill score variants do not trigger it.",
                "Kills confirmed within 300 ms count as one multi-kill sound. A later kill starts an independent voice without restarting a sound already playing. Accepted values: true or false. Applied after Graphics / Audio > Save without a restart."
            },
            IsBoolean
        },
        {
            std::string(kSniperScopeSmoothingEnabledKey),
            "true",
            {
                "Softens scoped micro-motion with a frame-time-aware bounded angular stabilizer. Filtering applies only while total stabilized-to-raw error is below 1.5 degrees; deliberate movement catches up to raw immediately at that boundary. The same result drives aim and scope presentation, while current weapon translation stays raw. Controls > Save applies the toggle live. After a scoped shot, the weapon's native zoom state decides whether the scope exits or remains active.",
                "Accepted values: true or false. Applied after Controls > Save without a restart."
            },
            IsBoolean
        },
        {
            std::string(kMenuPointerSmoothingEnabledKey),
            "true",
            {
                "Stabilizes the right-controller pointer in Battlefield's native menus, the Quick Menu, and the VR Settings dialog. A small deadzone suppresses hand tremor while frame-time-aware adaptive filtering keeps deliberate movement responsive.",
                "Accepted values: true or false. Applied after VR Settings > Save without a restart. Turning it off restores direct unfiltered controller-ray coordinates."
            },
            IsBoolean
        },
        {
            std::string(kOffHandGripStyleKey),
            "hold",
            {
                "Chooses how the left-hand grip controls weapon support. hold keeps the off hand attached only while the grip is held; toggle attaches on one grip press and releases on the next.",
                "Accepted values: hold or toggle. toggle is intended for controllers such as Vive Wands and Valve Index controllers where continuously holding grip may be uncomfortable or unreliable. Applied only after VR Settings > Save."
            },
            IsOffHandGripStyle
        },
        {
            std::string(kHandWeaponCrosshairKey),
            "hit_marker_only",
            {
                "Controls the stereo 3D HUD crosshair only for shooting hand weapons such as rifles and pistols. off hides both the aiming crosshair and its hit marker; on shows both; hit_marker_only hides the aiming crosshair but still shows confirmed-hit feedback.",
                "Accepted values: off, on, or hit_marker_only. Knife, throwable, and gadget items have their own setting. This does not change weapon aim, bullet direction, scopes, or BF1942 hit detection. Applied only after Controls > Save."
            },
            IsWorldCrosshairMode
        },
        {
            std::string(kMountedWeaponCrosshairKey),
            "on",
            {
                "Controls the stereo 3D HUD crosshair for vehicles, turrets, and mounted guns. off hides both the aiming crosshair and its hit marker; on shows both; hit_marker_only hides the aiming crosshair but still shows confirmed-hit feedback.",
                "Accepted values: off, on, or hit_marker_only. This does not change mounted aim, projectile direction, or BF1942 hit detection. Applied only after Controls > Save."
            },
            IsWorldCrosshairMode
        },
        {
            std::string(kPointerItemCrosshairKey),
            "on",
            {
                "Controls the stereo 3D HUD crosshair for BFVR's controller-pointer knife, throwable, and gadget item classification. off hides both the aiming crosshair and its hit marker; on shows both; hit_marker_only hides the aiming crosshair but retains confirmed-hit feedback.",
                "Accepted values: off, on, or hit_marker_only. The default on value preserves the existing BFVR behavior. This does not change aim, item use, projectile direction, or BF1942 hit detection. Applied after Controls > Save without a restart."
            },
            IsWorldCrosshairMode
        },
        {
            std::string(kCrosshairColorKey),
            "green",
            {
                "Selects the base tint shared by BFVR's stereo 3D aiming crosshair and 3D hit marker. The existing D3D8 crosshair renderer, endpoint, per-eye projection, angular size, and composition timing remain unchanged.",
                "Accepted values: white, green, blue, purple, red, pink, orange, or yellow. The legacy value magenta loads as pink. The default green preserves the current appearance. World post-processing and non-neutral Color settings may alter the final displayed tint. Applied after Controls > Save without a restart."
            },
            IsCrosshairColor
        },
        {
            std::string(kCrosshairOpacityKey),
            std::to_string(kDefaultCrosshairOpacityPercent),
            {
                "Controls the opacity shared by BFVR's stereo 3D aiming crosshair and confirmed-hit marker. It does not change their endpoint, per-eye projection, angular size, color, or hit detection.",
                "Accepted values: 5 through 100 percent in steps of 5. Zero is deliberately rejected so enabled crosshair feedback can never become fully invisible. Applied after Controls > Save without a restart."
            },
            IsCrosshairOpacity
        },
        {
            std::string(kFxaaEnabledKey),
            "true",
            {
                "Enables BFVR's world-only Fast Approximate Anti-Aliasing pass. It smooths jagged world edges without filtering the separate VR menu and HUD composition layers.",
                "Accepted values: true or false. This setting is applied only after VR Settings > Save and does not require a restart."
            },
            IsBoolean
        },
        {
            std::string(kFxaaSharpeningKey),
            std::to_string(kDefaultFxaaSharpeningPercent),
            {
                "Controls a lightweight contrast-adaptive sharpening step fused into BFVR's world-only FXAA shader. It restores detail softened by FXAA without filtering the separate VR menu and HUD layers.",
                "Accepted values: 0 through 100 percent in steps of 5. 0 disables sharpening while retaining FXAA; 30 is the seeded default. Applied after VR Settings > Save without requiring a restart."
            },
            IsFxaaSharpening
        },
        {
            std::string(kAmbientOcclusionEnabledKey),
            "true",
            {
                "Enables BFVR's world-only screen-space ambient occlusion, adding contact shading where nearby surfaces meet. It uses the stereo depth resources produced by the game process.",
                "Accepted values: true or false. Changing this setting requires restarting BFVR after Save because the depth resources are negotiated at startup."
            },
            IsBoolean
        },
        {
            std::string(kAmbientOcclusionRadiusKey),
            std::to_string(kDefaultAmbientOcclusionRadiusCentimeters),
            {
                "Sets the view-space radius around each visible surface point searched for ambient occlusion. Larger values spread contact shading farther but can make broad halos more noticeable.",
                "Units are centimeters. Accepted values: 10 through 150 in steps of 5. This setting is applied only after VR Settings > Save."
            },
            IsAmbientOcclusionRadius
        },
        {
            std::string(kAmbientOcclusionStrengthKey),
            std::to_string(kDefaultAmbientOcclusionStrengthPercent),
            {
                "Controls how strongly the computed ambient occlusion darkens the world image. 0 preserves the AO computation but applies no darkening; 100 applies the full tuned result.",
                "Accepted values: 0 through 100 percent in steps of 5. This setting is applied only after VR Settings > Save."
            },
            IsAmbientOcclusionStrength
        },
        {
            std::string(kWaterReflectionsEnabledKey),
            "true",
            {
                "Enables BFVR's water-only per-eye screen-space reflections, including finite scene geometry and the clear-depth skybox fallback. Only BF1942's exact depth-writing additive/specular WaterSurface pass can receive the effect.",
                "Accepted values: true or false. Changing this setting requires restarting BFVR after Save because the x86 producer negotiates the packed depth/water-mask resources and the x64 reflection shader at startup. Off-screen or occluded source detail remains unavailable."
            },
            IsBoolean
        },
        {
            std::string(kBloomEnabledKey),
            "true",
            {
                "Enables BFVR's world-only bloom pass, extracting bright scene areas, blurring them, and adding the resulting glow back to the two eye images. VR menus and HUD layers are excluded.",
                "Accepted values: true or false. Changing this setting requires restarting BFVR after Save because bloom shaders and render targets are created at startup."
            },
            IsBoolean
        },
        {
            std::string(kBloomThresholdKey),
            std::to_string(kDefaultBloomThresholdPercent),
            {
                "Sets the linear brightness threshold at which pixels begin contributing to bloom. Lower values allow more of the scene to glow; higher values restrict bloom to the brightest areas.",
                "Accepted values: 5 through 95 percent in steps of 5. 75 means a linear threshold of 0.75. This setting is applied only after VR Settings > Save."
            },
            IsBloomThreshold
        },
        {
            std::string(kBloomIntensityKey),
            std::to_string(kDefaultBloomIntensityPercent),
            {
                "Controls how strongly the blurred bloom image is added back to the world. 0 produces no visible glow while retaining the saved bloom enable choice; 100 is the maximum menu value.",
                "Accepted values: 0 through 100 percent in steps of 5. 45 means an intensity of 0.45. This setting is applied only after VR Settings > Save."
            },
            IsBloomIntensity
        },
        {
            std::string(kColorProfileKey),
            "original",
            {
                "Selects BFVR's world-only final color treatment. original is an identity profile, filmic applies a restrained toe and highlight shoulder, and vibrant applies a richer but bounded treatment. Ref2 HUD, native menus, scope UI, Quick Menu, and VR Settings remain outside this processing.",
                "Accepted values: original, filmic, or vibrant. The three manual Color adjustments are applied relative to the selected profile. Applied after Graphics > Save without a restart."
            },
            IsColorProfile
        },
        {
            std::string(kColorExposureKey),
            "0.0",
            {
                "Adjusts stereo-world exposure in linear light before the selected Color Profile. Zero is neutral and appears at the middle of the in-game slider.",
                "Units are EV. Accepted values: -1.0 through +1.0 in steps of 0.1. Ref2 HUD and separately composed interface layers are unchanged. Applied after Graphics > Save without a restart."
            },
            IsColorExposure
        },
        {
            std::string(kColorContrastKey),
            "0",
            {
                "Adjusts stereo-world contrast after the selected Color Profile. Zero is neutral; negative values reduce contrast and positive values increase it.",
                "Accepted values: -50 through +50 percent in steps of 1. Ref2 HUD and separately composed interface layers are unchanged. Applied after Graphics > Save without a restart."
            },
            IsColorContrast
        },
        {
            std::string(kColorSaturationKey),
            "0",
            {
                "Adjusts stereo-world saturation after the selected Color Profile. Zero is neutral, -100 is grayscale, and positive values increase saturation.",
                "Accepted values: -100 through +100 percent in steps of 1. BFVR's 3D crosshair is already part of the stereo-world targets and is adjusted with the scene; Ref2 HUD and separately composed interface layers are unchanged. Applied after Graphics > Save without a restart."
            },
            IsColorSaturation
        }
    };
}

UserSettingsValues DecodeUserSettings(const UserSettings& settings) noexcept
{
    UserSettingsValues result;
    const auto readEnumText = [&settings](
                                  std::string_view key,
                                  std::string_view fallback) {
        const auto found = settings.values.find(std::string(key));
        return found == settings.values.end()
            ? fallback
            : std::string_view(found->second);
    };
    result.playMode = readEnumText(kPlayModeKey, "seated") == "standing"
        ? PlayMode::Standing
        : PlayMode::Seated;
    const std::string_view turnMode = readEnumText(
        kArtificialTurnModeKey, "smooth");
    result.artificialTurnMode = turnMode == "smooth"
        ? ArtificialTurnMode::Smooth
        : ArtificialTurnMode::Snap;
    const std::string_view movementDirection = readEnumText(
        kMovementDirectionKey, "head");
    result.movementDirection = movementDirection == "head"
        ? MovementDirection::Head
        : movementDirection == "off_hand_controller"
        ? MovementDirection::OffHandController
        : MovementDirection::Character;
    const auto turnSpeed = settings.values.find(
        std::string(kInfantryTurnSpeedKey));
    if (turnSpeed != settings.values.end())
    {
        std::uint32_t parsedPercent = 0;
        const auto parsed = std::from_chars(
            turnSpeed->second.data(),
            turnSpeed->second.data() + turnSpeed->second.size(),
            parsedPercent);
        if (parsed.ec == std::errc{} &&
            parsed.ptr == turnSpeed->second.data() + turnSpeed->second.size() &&
            IsInfantryTurnSpeed(turnSpeed->second))
        {
            result.infantryTurnSpeedPercent = parsedPercent;
        }
    }
    const auto vehicleMotionAimSensitivity = settings.values.find(
        std::string(kVehicleMotionAimSensitivityKey));
    if (vehicleMotionAimSensitivity != settings.values.end())
    {
        std::uint32_t parsedPercent = 0;
        const auto parsed = std::from_chars(
            vehicleMotionAimSensitivity->second.data(),
            vehicleMotionAimSensitivity->second.data() +
                vehicleMotionAimSensitivity->second.size(),
            parsedPercent);
        if (parsed.ec == std::errc{} &&
            parsed.ptr == vehicleMotionAimSensitivity->second.data() +
                vehicleMotionAimSensitivity->second.size() &&
            IsVehicleMotionAimSensitivity(
                vehicleMotionAimSensitivity->second))
        {
            result.vehicleMotionAimSensitivityPercent = parsedPercent;
        }
    }
    const auto readBoolean = [&](std::string_view key, bool fallback) {
        const auto found = settings.values.find(std::string(key));
        return found == settings.values.end()
            ? fallback
            : found->second == "true";
    };
    result.invertFlightPitch = readBoolean(kInvertFlightPitchKey, false);
    result.aircraftPitchWithRoll = readBoolean(
        kAircraftPitchWithRollKey,
        false);
    result.swapAircraftSticks = readBoolean(
        kSwapAircraftSticksKey,
        false);
    result.invertTurretPitch = readBoolean(kInvertTurretPitchKey, true);
    result.invertTurretYaw = readBoolean(kInvertTurretYawKey, false);
    result.controllerHapticsEnabled = readBoolean(
        kControllerHapticsEnabledKey,
        true);
    result.killSoundEnabled = readBoolean(kKillSoundEnabledKey, true);
    result.sniperScopeSmoothingEnabled = readBoolean(
        kSniperScopeSmoothingEnabledKey,
        true);
    result.menuPointerSmoothingEnabled = readBoolean(
        kMenuPointerSmoothingEnabledKey,
        true);
    result.comfortVignetteEnabled = readBoolean(
        kComfortVignetteEnabledKey,
        false);
    result.deathCameraComfortEnabled = readBoolean(
        kDeathCameraComfortEnabledKey,
        true);
    const auto visibility = settings.values.find(std::string(kShowArmsKey));
    if (visibility == settings.values.end() ||
        visibility->second == "arms_and_hands" ||
        visibility->second == "true")
    {
        result.firstPersonVisibility = FirstPersonVisibility::ArmsAndHands;
    }
    else if (visibility->second == "hands_only")
    {
        result.firstPersonVisibility = FirstPersonVisibility::HandsOnly;
    }
    else
    {
        result.firstPersonVisibility = FirstPersonVisibility::NoHandsOrArms;
    }
    const auto readGripStyle = [&settings]() {
        const auto found = settings.values.find(
            std::string(kOffHandGripStyleKey));
        return found != settings.values.end() && found->second == "toggle"
            ? OffHandGripStyle::Toggle
            : OffHandGripStyle::Hold;
    };
    const auto readCrosshairMode = [&settings](
                                       std::string_view key,
                                       WorldCrosshairMode fallback) {
        const auto found = settings.values.find(std::string(key));
        if (found == settings.values.end())
        {
            return fallback;
        }
        if (found->second == "on")
        {
            return WorldCrosshairMode::On;
        }
        return found->second == "hit_marker_only"
            ? WorldCrosshairMode::HitMarkerOnly
            : WorldCrosshairMode::Off;
    };
    result.offHandGripStyle = readGripStyle();
    result.handWeaponCrosshair = readCrosshairMode(
        kHandWeaponCrosshairKey,
        WorldCrosshairMode::HitMarkerOnly);
    result.mountedWeaponCrosshair = readCrosshairMode(
        kMountedWeaponCrosshairKey,
        WorldCrosshairMode::On);
    result.pointerItemCrosshair = readCrosshairMode(
        kPointerItemCrosshairKey,
        WorldCrosshairMode::On);
    const std::string_view crosshairColor = readEnumText(
        kCrosshairColorKey,
        "green");
    result.crosshairColor = crosshairColor == "white"
        ? CrosshairColor::White
        : crosshairColor == "blue"
        ? CrosshairColor::Blue
        : crosshairColor == "purple"
        ? CrosshairColor::Purple
        : crosshairColor == "red"
        ? CrosshairColor::Red
        : crosshairColor == "pink" || crosshairColor == "magenta"
        ? CrosshairColor::Pink
        : crosshairColor == "orange"
        ? CrosshairColor::Orange
        : crosshairColor == "yellow"
        ? CrosshairColor::Yellow
        : CrosshairColor::Green;
    result.fxaaEnabled = readBoolean(kFxaaEnabledKey, true);
    result.ambientOcclusionEnabled = readBoolean(
        kAmbientOcclusionEnabledKey,
        true);
    result.waterReflectionsEnabled = readBoolean(
        kWaterReflectionsEnabledKey,
        true);
    result.bloomEnabled = readBoolean(kBloomEnabledKey, true);
    const auto readUnsigned = [&settings](
                                  std::string_view key,
                                  std::uint32_t fallback,
                                  UserSettingValidator validator) {
        const auto found = settings.values.find(std::string(key));
        if (found == settings.values.end() || validator == nullptr ||
            !validator(found->second))
        {
            return fallback;
        }
        std::uint32_t value = fallback;
        const auto parsed = std::from_chars(
            found->second.data(),
            found->second.data() + found->second.size(),
            value);
        return parsed.ec == std::errc{} ? value : fallback;
    };
    result.ambientOcclusionRadiusCentimeters = readUnsigned(
        kAmbientOcclusionRadiusKey,
        kDefaultAmbientOcclusionRadiusCentimeters,
        IsAmbientOcclusionRadius);
    result.fxaaSharpeningPercent = readUnsigned(
        kFxaaSharpeningKey,
        kDefaultFxaaSharpeningPercent,
        IsFxaaSharpening);
    result.crosshairOpacityPercent = readUnsigned(
        kCrosshairOpacityKey,
        kDefaultCrosshairOpacityPercent,
        IsCrosshairOpacity);
    result.ambientOcclusionStrengthPercent = readUnsigned(
        kAmbientOcclusionStrengthKey,
        kDefaultAmbientOcclusionStrengthPercent,
        IsAmbientOcclusionStrength);
    result.bloomThresholdPercent = readUnsigned(
        kBloomThresholdKey,
        kDefaultBloomThresholdPercent,
        IsBloomThreshold);
    result.bloomIntensityPercent = readUnsigned(
        kBloomIntensityKey,
        kDefaultBloomIntensityPercent,
        IsBloomIntensity);
    result.snapTurnAngleDegrees = readUnsigned(
        kSnapTurnAngleKey,
        kDefaultSnapTurnAngleDegrees,
        IsSnapTurnAngle);
    result.standingEyeHeightCentimeters = readUnsigned(
        kStandingEyeHeightKey,
        kDefaultStandingEyeHeightCentimeters,
        IsStandingEyeHeight);
    const std::string_view colorProfile = readEnumText(
        kColorProfileKey,
        "original");
    result.colorProfile = colorProfile == "filmic"
        ? ColorProfile::Filmic
        : colorProfile == "vibrant"
        ? ColorProfile::Vibrant
        : ColorProfile::Original;
    const auto readSigned = [&settings](
                                std::string_view key,
                                std::int32_t fallback,
                                UserSettingValidator validator) {
        const auto found = settings.values.find(std::string(key));
        if (found == settings.values.end() || validator == nullptr ||
            !validator(found->second))
        {
            return fallback;
        }
        std::int32_t value = fallback;
        const auto parsed = std::from_chars(
            found->second.data(),
            found->second.data() + found->second.size(),
            value);
        return parsed.ec == std::errc{} ? value : fallback;
    };
    const auto exposure = settings.values.find(std::string(kColorExposureKey));
    if (exposure != settings.values.end() &&
        IsColorExposure(exposure->second))
    {
        std::int32_t tenths = kDefaultColorExposureTenthsEv;
        if (ParseFixedTenths(exposure->second, tenths))
        {
            result.colorExposureTenthsEv = tenths;
        }
    }
    result.colorContrastPercent = readSigned(
        kColorContrastKey,
        kDefaultColorContrastPercent,
        IsColorContrast);
    result.colorSaturationPercent = readSigned(
        kColorSaturationKey,
        kDefaultColorSaturationPercent,
        IsColorSaturation);
    result.rightHandPositionXCentimeters = readSigned(
        kRightHandPositionXKey,
        kDefaultRightHandPositionXCentimeters,
        IsHandPosition);
    result.rightHandPositionYCentimeters = readSigned(
        kRightHandPositionYKey,
        kDefaultRightHandPositionYCentimeters,
        IsHandPosition);
    result.rightHandPositionZCentimeters = readSigned(
        kRightHandPositionZKey,
        kDefaultRightHandPositionZCentimeters,
        IsHandPosition);
    result.leftHandPositionXCentimeters = readSigned(
        kLeftHandPositionXKey,
        kDefaultLeftHandPositionXCentimeters,
        IsHandPosition);
    result.leftHandPositionYCentimeters = readSigned(
        kLeftHandPositionYKey,
        kDefaultLeftHandPositionYCentimeters,
        IsHandPosition);
    result.leftHandPositionZCentimeters = readSigned(
        kLeftHandPositionZKey,
        kDefaultLeftHandPositionZCentimeters,
        IsHandPosition);
    const auto height = settings.values.find(
        std::string(kVrHeightAdjustmentKey));
    if (height != settings.values.end() &&
        IsVrHeightAdjustment(height->second))
    {
        std::int32_t centimeters = kDefaultVrHeightAdjustmentCentimeters;
        const auto parsed = std::from_chars(
            height->second.data(),
            height->second.data() + height->second.size(),
            centimeters);
        if (parsed.ec == std::errc{})
        {
            result.vrHeightAdjustmentCentimeters = centimeters;
        }
    }
    return result;
}

void EncodeUserSettings(
    const UserSettingsValues& values,
    UserSettings& settings)
{
    settings.values[std::string(kPlayModeKey)] =
        values.playMode == PlayMode::Standing ? "standing" : "seated";
    const char* turnMode = values.artificialTurnMode == ArtificialTurnMode::Snap
        ? "snap"
        : "smooth";
    settings.values[std::string(kArtificialTurnModeKey)] = turnMode;
    const char* movementDirection = "character";
    if (values.movementDirection == MovementDirection::Head)
    {
        movementDirection = "head";
    }
    else if (values.movementDirection ==
             MovementDirection::OffHandController)
    {
        movementDirection = "off_hand_controller";
    }
    settings.values[std::string(kMovementDirectionKey)] = movementDirection;
    const std::uint32_t clampedTurnSpeed = std::clamp(
        values.infantryTurnSpeedPercent,
        kMinimumInfantryTurnSpeedPercent,
        kMaximumInfantryTurnSpeedPercent);
    const std::uint32_t snappedTurnSpeed =
        ((clampedTurnSpeed + kInfantryTurnSpeedStepPercent / 2U) /
         kInfantryTurnSpeedStepPercent) *
        kInfantryTurnSpeedStepPercent;
    settings.values[std::string(kInfantryTurnSpeedKey)] =
        std::to_string(snappedTurnSpeed);
    const std::uint32_t clampedVehicleMotionAimSensitivity = std::clamp(
        values.vehicleMotionAimSensitivityPercent,
        kMinimumVehicleMotionAimSensitivityPercent,
        kMaximumVehicleMotionAimSensitivityPercent);
    const std::uint32_t snappedVehicleMotionAimSensitivity =
        ((clampedVehicleMotionAimSensitivity +
          kVehicleMotionAimSensitivityStepPercent / 2U) /
         kVehicleMotionAimSensitivityStepPercent) *
        kVehicleMotionAimSensitivityStepPercent;
    settings.values[std::string(kVehicleMotionAimSensitivityKey)] =
        std::to_string(snappedVehicleMotionAimSensitivity);
    settings.values[std::string(kInvertFlightPitchKey)] =
        values.invertFlightPitch ? "true" : "false";
    settings.values[std::string(kAircraftPitchWithRollKey)] =
        values.aircraftPitchWithRoll ? "true" : "false";
    settings.values[std::string(kSwapAircraftSticksKey)] =
        values.swapAircraftSticks ? "true" : "false";
    settings.values[std::string(kInvertTurretPitchKey)] =
        values.invertTurretPitch ? "true" : "false";
    settings.values[std::string(kInvertTurretYawKey)] =
        values.invertTurretYaw ? "true" : "false";
    settings.values[std::string(kControllerHapticsEnabledKey)] =
        values.controllerHapticsEnabled ? "true" : "false";
    settings.values[std::string(kKillSoundEnabledKey)] =
        values.killSoundEnabled ? "true" : "false";
    settings.values[std::string(kSniperScopeSmoothingEnabledKey)] =
        values.sniperScopeSmoothingEnabled ? "true" : "false";
    settings.values[std::string(kMenuPointerSmoothingEnabledKey)] =
        values.menuPointerSmoothingEnabled ? "true" : "false";
    settings.values[std::string(kComfortVignetteEnabledKey)] =
        values.comfortVignetteEnabled ? "true" : "false";
    settings.values[std::string(kDeathCameraComfortEnabledKey)] =
        values.deathCameraComfortEnabled ? "true" : "false";
    const char* firstPersonVisibility = "arms_and_hands";
    if (values.firstPersonVisibility == FirstPersonVisibility::HandsOnly)
    {
        firstPersonVisibility = "hands_only";
    }
    else if (values.firstPersonVisibility ==
             FirstPersonVisibility::NoHandsOrArms)
    {
        firstPersonVisibility = "no_hands_or_arms";
    }
    settings.values[std::string(kShowArmsKey)] = firstPersonVisibility;
    settings.values[std::string(kOffHandGripStyleKey)] =
        values.offHandGripStyle == OffHandGripStyle::Toggle
        ? "toggle"
        : "hold";
    const auto encodeCrosshairMode = [](WorldCrosshairMode mode) {
        switch (mode)
        {
        case WorldCrosshairMode::Off: return "off";
        case WorldCrosshairMode::HitMarkerOnly: return "hit_marker_only";
        case WorldCrosshairMode::On:
        default: return "on";
        }
    };
    settings.values[std::string(kHandWeaponCrosshairKey)] =
        encodeCrosshairMode(values.handWeaponCrosshair);
    settings.values[std::string(kMountedWeaponCrosshairKey)] =
        encodeCrosshairMode(values.mountedWeaponCrosshair);
    settings.values[std::string(kPointerItemCrosshairKey)] =
        encodeCrosshairMode(values.pointerItemCrosshair);
    const auto encodeCrosshairColor = [](CrosshairColor color) {
        switch (color)
        {
        case CrosshairColor::White: return "white";
        case CrosshairColor::Blue: return "blue";
        case CrosshairColor::Purple: return "purple";
        case CrosshairColor::Red: return "red";
        case CrosshairColor::Pink: return "pink";
        case CrosshairColor::Orange: return "orange";
        case CrosshairColor::Yellow: return "yellow";
        case CrosshairColor::Green:
        default: return "green";
        }
    };
    settings.values[std::string(kCrosshairColorKey)] =
        encodeCrosshairColor(values.crosshairColor);
    settings.values[std::string(kFxaaEnabledKey)] =
        values.fxaaEnabled ? "true" : "false";
    settings.values[std::string(kAmbientOcclusionEnabledKey)] =
        values.ambientOcclusionEnabled ? "true" : "false";
    settings.values[std::string(kWaterReflectionsEnabledKey)] =
        values.waterReflectionsEnabled ? "true" : "false";
    settings.values[std::string(kBloomEnabledKey)] =
        values.bloomEnabled ? "true" : "false";
    const auto snap = [](std::uint32_t value,
                         std::uint32_t minimum,
                         std::uint32_t maximum,
                         std::uint32_t step) {
        const std::uint32_t clamped = std::clamp(value, minimum, maximum);
        return minimum +
            ((clamped - minimum + step / 2U) / step) * step;
    };
    settings.values[std::string(kSnapTurnAngleKey)] =
        std::to_string(snap(
            values.snapTurnAngleDegrees,
            kMinimumSnapTurnAngleDegrees,
            kMaximumSnapTurnAngleDegrees,
            kSnapTurnAngleStepDegrees));
    settings.values[std::string(kFxaaSharpeningKey)] =
        std::to_string(snap(
            values.fxaaSharpeningPercent,
            kMinimumFxaaSharpeningPercent,
            kMaximumFxaaSharpeningPercent,
            kFxaaSharpeningStepPercent));
    settings.values[std::string(kCrosshairOpacityKey)] =
        std::to_string(snap(
            values.crosshairOpacityPercent,
            kMinimumCrosshairOpacityPercent,
            kMaximumCrosshairOpacityPercent,
            kCrosshairOpacityStepPercent));
    settings.values[std::string(kVrHeightAdjustmentKey)] =
        std::to_string(std::clamp(
            values.vrHeightAdjustmentCentimeters,
            kMinimumVrHeightAdjustmentCentimeters,
            kMaximumVrHeightAdjustmentCentimeters));
    const auto encodeHandPosition = [&](std::string_view key,
                                        std::int32_t value) {
        settings.values[std::string(key)] = std::to_string(std::clamp(
            value,
            kMinimumHandPositionCentimeters,
            kMaximumHandPositionCentimeters));
    };
    encodeHandPosition(
        kRightHandPositionXKey, values.rightHandPositionXCentimeters);
    encodeHandPosition(
        kRightHandPositionYKey, values.rightHandPositionYCentimeters);
    encodeHandPosition(
        kRightHandPositionZKey, values.rightHandPositionZCentimeters);
    encodeHandPosition(
        kLeftHandPositionXKey, values.leftHandPositionXCentimeters);
    encodeHandPosition(
        kLeftHandPositionYKey, values.leftHandPositionYCentimeters);
    encodeHandPosition(
        kLeftHandPositionZKey, values.leftHandPositionZCentimeters);
    settings.values[std::string(kStandingEyeHeightKey)] =
        std::to_string(std::clamp(
            values.standingEyeHeightCentimeters,
            kMinimumStandingEyeHeightCentimeters,
            kMaximumStandingEyeHeightCentimeters));
    settings.values[std::string(kAmbientOcclusionRadiusKey)] =
        std::to_string(snap(
            values.ambientOcclusionRadiusCentimeters,
            kMinimumAmbientOcclusionRadiusCentimeters,
            kMaximumAmbientOcclusionRadiusCentimeters,
            kAmbientOcclusionRadiusStepCentimeters));
    settings.values[std::string(kAmbientOcclusionStrengthKey)] =
        std::to_string(snap(
            values.ambientOcclusionStrengthPercent,
            kMinimumAmbientOcclusionStrengthPercent,
            kMaximumAmbientOcclusionStrengthPercent,
            kAmbientOcclusionStrengthStepPercent));
    settings.values[std::string(kBloomThresholdKey)] =
        std::to_string(snap(
            values.bloomThresholdPercent,
            kMinimumBloomThresholdPercent,
            kMaximumBloomThresholdPercent,
            kBloomThresholdStepPercent));
    settings.values[std::string(kBloomIntensityKey)] =
        std::to_string(snap(
            values.bloomIntensityPercent,
            kMinimumBloomIntensityPercent,
            kMaximumBloomIntensityPercent,
            kBloomIntensityStepPercent));
    const auto encodeColorProfile = [](ColorProfile profile) {
        switch (profile)
        {
        case ColorProfile::Filmic: return "filmic";
        case ColorProfile::Vibrant: return "vibrant";
        case ColorProfile::Original:
        default: return "original";
        }
    };
    settings.values[std::string(kColorProfileKey)] =
        encodeColorProfile(values.colorProfile);
    settings.values[std::string(kColorExposureKey)] = EncodeFixedTenths(
        std::clamp(
            values.colorExposureTenthsEv,
            kMinimumColorExposureTenthsEv,
            kMaximumColorExposureTenthsEv));
    settings.values[std::string(kColorContrastKey)] = std::to_string(
        std::clamp(
            values.colorContrastPercent,
            kMinimumColorContrastPercent,
            kMaximumColorContrastPercent));
    settings.values[std::string(kColorSaturationKey)] = std::to_string(
        std::clamp(
            values.colorSaturationPercent,
            kMinimumColorSaturationPercent,
            kMaximumColorSaturationPercent));
}

bool UserSettingsRequireRestart(
    const UserSettingsValues& startup,
    const UserSettingsValues& saved) noexcept
{
    return saved.ambientOcclusionEnabled != startup.ambientOcclusionEnabled ||
        saved.bloomEnabled != startup.bloomEnabled ||
        saved.waterReflectionsEnabled != startup.waterReflectionsEnabled;
}

float ComputeManualHeightAdjustmentMeters(
    const UserSettingsValues& values) noexcept
{
    return static_cast<float>(std::clamp(
        values.vrHeightAdjustmentCentimeters,
        kMinimumVrHeightAdjustmentCentimeters,
        kMaximumVrHeightAdjustmentCentimeters)) / 100.0F;
}

bool UserSettingsStore::Initialize(
    const std::wstring& path,
    UserSettingsSchema schema)
{
    path_.clear();
    schema_.clear();
    ready_ = false;
    const std::wstring parent = ParentPath(path);
    if (path.empty() || parent.empty() || !IsDirectory(parent))
    {
        return false;
    }
    for (const UserSettingSeed& seed : schema)
    {
        if (!IsValidKey(seed.key) ||
            !IsValid(seed, seed.defaultValue) ||
            seed.documentation.size() < 2 ||
            std::any_of(
                seed.documentation.begin(),
                seed.documentation.end(),
                [](const std::string& line) {
                    return line.empty() ||
                        line.find_first_of("\r\n") != std::string::npos;
                }) ||
            std::any_of(
                schema_.begin(),
                schema_.end(),
                [&](const UserSettingSeed& existing) {
                    return existing.key == seed.key;
                }))
        {
            schema_.clear();
            return false;
        }
        schema_.push_back(seed);
    }
    path_ = path;
    ready_ = true;
    return true;
}

UserSettings UserSettingsStore::Defaults() const
{
    UserSettings result;
    for (const UserSettingSeed& seed : schema_)
    {
        result.values.emplace(seed.key, seed.defaultValue);
    }
    return result;
}

UserSettingsLoadResult UserSettingsStore::Load() const
{
    UserSettingsLoadResult result;
    result.settings = Defaults();
    if (!ready_)
    {
        result.status = UserSettingsLoadStatus::IoErrorUsedDefaults;
        return result;
    }
    std::string bytes;
    DWORD errorCode = ERROR_SUCCESS;
    if (!ReadWholeFile(path_, bytes, errorCode))
    {
        result.status = errorCode == ERROR_FILE_NOT_FOUND ||
                errorCode == ERROR_PATH_NOT_FOUND
            ? UserSettingsLoadStatus::MissingUsedDefaults
            : UserSettingsLoadStatus::IoErrorUsedDefaults;
        return result;
    }
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEFU &&
        static_cast<unsigned char>(bytes[1]) == 0xBBU &&
        static_cast<unsigned char>(bytes[2]) == 0xBFU)
    {
        bytes.erase(0, 3);
    }

    bool schemaSeen = false;
    std::vector<bool> settingSeen(schema_.size(), false);
    std::size_t offset = 0;
    while (offset <= bytes.size())
    {
        const std::size_t end = bytes.find('\n', offset);
        std::string_view line(
            bytes.data() + offset,
            (end == std::string::npos ? bytes.size() : end) - offset);
        line = Trim(line);
        if (!line.empty() && line.front() != '#')
        {
            const std::size_t equals = line.find('=');
            if (equals == std::string_view::npos)
            {
                result.settings = Defaults();
                result.status = UserSettingsLoadStatus::InvalidUsedDefaults;
                return result;
            }
            const std::string_view key = Trim(line.substr(0, equals));
            const std::string_view value = Trim(line.substr(equals + 1));
            if (key == "schema_version")
            {
                std::uint32_t version = 0;
                const auto parsed = std::from_chars(
                    value.data(),
                    value.data() + value.size(),
                    version);
                if (schemaSeen || parsed.ec != std::errc{} ||
                    parsed.ptr != value.data() + value.size() ||
                    version != kUserSettingsSchemaVersion)
                {
                    result.settings = Defaults();
                    result.status =
                        UserSettingsLoadStatus::InvalidUsedDefaults;
                    return result;
                }
                schemaSeen = true;
            }
            else if (const UserSettingSeed* seed = FindSeed(key))
            {
                if (!IsValid(*seed, value))
                {
                    result.settings = Defaults();
                    result.status =
                        UserSettingsLoadStatus::InvalidUsedDefaults;
                    return result;
                }
                result.settings.values[seed->key] = std::string(value);
                const std::size_t seedIndex = static_cast<std::size_t>(
                    seed - schema_.data());
                if (seedIndex < settingSeen.size())
                {
                    settingSeen[seedIndex] = true;
                }
            }
        }
        if (end == std::string::npos)
        {
            break;
        }
        offset = end + 1;
    }
    result.status = schemaSeen
        ? (std::all_of(settingSeen.begin(), settingSeen.end(), [](bool seen) {
               return seen;
           })
               ? UserSettingsLoadStatus::Loaded
               : UserSettingsLoadStatus::LoadedCompletedSeed)
        : UserSettingsLoadStatus::InvalidUsedDefaults;
    if (!schemaSeen)
    {
        result.settings = Defaults();
    }
    return result;
}

UserSettingsLoadResult UserSettingsStore::LoadOrCreateDefaults() const
{
    UserSettingsLoadResult result = Load();
    if (result.status == UserSettingsLoadStatus::MissingUsedDefaults)
    {
        result.status = Save(result.settings)
            ? UserSettingsLoadStatus::MissingCreatedDefaults
            : UserSettingsLoadStatus::IoErrorUsedDefaults;
    }
    else if (result.status == UserSettingsLoadStatus::LoadedCompletedSeed)
    {
        result.status = Save(result.settings)
            ? UserSettingsLoadStatus::LoadedCompletedSeed
            : UserSettingsLoadStatus::IoErrorUsedDefaults;
    }
    return result;
}

bool UserSettingsStore::Save(const UserSettings& settings) const
{
    if (!ready_)
    {
        return false;
    }
    std::string contents =
        "# BFVR user configuration\r\n"
        "# You may edit this file manually with BFVR stopped.\r\n"
        "# Every setting includes its behavior, accepted values, and seeded default.\r\n"
        "# Keep each setting in the form: setting_name = value\r\n"
        "# Restart BFVR after manual edits so both runtime processes reload it.\r\n"
        "# Invalid values make BFVR safely use seeded defaults without replacing this file.\r\n"
        "# After initial creation, this is rewritten only by VR Settings > Save.\r\n"
        "# Delete this file to restore seeded defaults on the next open.\r\n"
        "schema_version = " +
        std::to_string(kUserSettingsSchemaVersion) + "\r\n";
    for (const UserSettingSeed& seed : schema_)
    {
        const auto value = settings.values.find(seed.key);
        const std::string& selected = value == settings.values.end()
            ? seed.defaultValue
            : value->second;
        if (!IsValid(seed, selected))
        {
            return false;
        }
        contents += "\r\n";
        for (const std::string& line : seed.documentation)
        {
            contents += "# " + line + "\r\n";
        }
        contents += "# Seeded default: " + seed.defaultValue + "\r\n";
        contents += seed.key + " = " + selected + "\r\n";
    }
    contents +=
        "\r\n# Settings entries will appear above as their controls are implemented.\r\n";

    const std::wstring temporaryPath = path_ + L"." +
        std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE file = CreateFileW(
        temporaryPath.c_str(),
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
    const bool written = WriteWholeFile(file, contents) &&
        FlushFileBuffers(file) != FALSE;
    const bool closed = CloseHandle(file) != FALSE;
    if (!written || !closed ||
        MoveFileExW(
            temporaryPath.c_str(),
            path_.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE)
    {
        DeleteFileW(temporaryPath.c_str());
        return false;
    }
    return true;
}

bool UserSettingsStore::IsReady() const noexcept
{
    return ready_;
}

const std::wstring& UserSettingsStore::Path() const noexcept
{
    return path_;
}

const UserSettingSeed* UserSettingsStore::FindSeed(
    std::string_view key) const noexcept
{
    const auto found = std::find_if(
        schema_.begin(),
        schema_.end(),
        [&](const UserSettingSeed& seed) { return seed.key == key; });
    return found == schema_.end() ? nullptr : &*found;
}

bool UserSettingsStore::IsValid(
    const UserSettingSeed& seed,
    std::string_view value) const noexcept
{
    return !value.empty() && value.find_first_of("\r\n") ==
            std::string_view::npos &&
        (seed.validator == nullptr || seed.validator(value));
}

UserSettingsLoadStatus UserSettingsSession::Begin(
    const UserSettingsStore& store)
{
    store_ = &store;
    const UserSettingsLoadResult loaded = store.Load();
    working_ = loaded.settings;
    active_ = true;
    return loaded.status;
}

void UserSettingsSession::ResetToDefaults()
{
    if (active_ && store_ != nullptr)
    {
        working_ = store_->Defaults();
    }
}

bool UserSettingsSession::Save()
{
    return active_ && store_ != nullptr && store_->Save(working_);
}

void UserSettingsSession::Cancel() noexcept
{
    working_ = {};
    store_ = nullptr;
    active_ = false;
}

bool UserSettingsSession::IsActive() const noexcept
{
    return active_;
}

const UserSettings& UserSettingsSession::Working() const noexcept
{
    return working_;
}

UserSettings& UserSettingsSession::Working() noexcept
{
    return working_;
}

UserSettingsLoadStatus UserSettingsRuntime::Initialize(
    const wchar_t* payloadDirectory)
{
    ready_ = store_.Initialize(
        ResolveUserSettingsPath(payloadDirectory),
        SeededUserSettingsSchema());
    if (!ready_)
    {
        current_ = {};
        return UserSettingsLoadStatus::IoErrorUsedDefaults;
    }
    return Reload();
}

UserSettingsLoadStatus UserSettingsRuntime::Reload()
{
    if (!ready_)
    {
        current_ = {};
        return UserSettingsLoadStatus::IoErrorUsedDefaults;
    }
    const UserSettingsLoadResult loaded = store_.LoadOrCreateDefaults();
    current_ = loaded.settings;
    (void)QueryFileSignature(
        store_.Path(),
        observedFileExists_,
        observedWriteTime_,
        observedFileSize_);
    return loaded.status;
}

bool UserSettingsRuntime::ReloadIfChanged()
{
    if (!ready_)
    {
        return false;
    }
    bool exists = false;
    std::uint64_t writeTime = 0;
    std::uint64_t fileSize = 0;
    if (!QueryFileSignature(store_.Path(), exists, writeTime, fileSize) ||
        (exists == observedFileExists_ &&
         writeTime == observedWriteTime_ &&
         fileSize == observedFileSize_))
    {
        return false;
    }
    (void)Reload();
    return true;
}

bool UserSettingsRuntime::Commit(const UserSettings& settings)
{
    if (!ready_ || !store_.Save(settings))
    {
        return false;
    }
    current_ = settings;
    (void)QueryFileSignature(
        store_.Path(),
        observedFileExists_,
        observedWriteTime_,
        observedFileSize_);
    return true;
}

bool UserSettingsRuntime::IsReady() const noexcept
{
    return ready_;
}

const UserSettings& UserSettingsRuntime::Current() const noexcept
{
    return current_;
}

const UserSettingsStore& UserSettingsRuntime::Store() const noexcept
{
    return store_;
}

UserSettingsRuntime& ProcessUserSettingsRuntime()
{
    static UserSettingsRuntime runtime;
    return runtime;
}

std::wstring ResolveUserSettingsPath(const wchar_t* payloadDirectory)
{
    std::array<wchar_t, 32768> environmentPath = {};
    const DWORD environmentLength = GetEnvironmentVariableW(
        kUserConfigEnvironmentName,
        environmentPath.data(),
        static_cast<DWORD>(environmentPath.size()));
    if (environmentLength > 0 &&
        environmentLength < environmentPath.size())
    {
        return environmentPath.data();
    }

    std::array<wchar_t, 32768> currentDirectory = {};
    const DWORD currentLength = GetCurrentDirectoryW(
        static_cast<DWORD>(currentDirectory.size()),
        currentDirectory.data());
    if (currentLength > 0 && currentLength < currentDirectory.size())
    {
        const std::wstring bfvrDirectory = JoinPath(
            currentDirectory.data(),
            L"BFVR");
        if (IsDirectory(bfvrDirectory))
        {
            return JoinPath(bfvrDirectory, kUserConfigFileName);
        }
    }
    return payloadDirectory == nullptr
        ? std::wstring{}
        : JoinPath(payloadDirectory, kUserConfigFileName);
}

const wchar_t* UserSettingsLoadStatusName(
    UserSettingsLoadStatus status) noexcept
{
    switch (status)
    {
    case UserSettingsLoadStatus::Loaded: return L"saved user values";
    case UserSettingsLoadStatus::LoadedCompletedSeed:
        return L"saved user values plus newly seeded settings";
    case UserSettingsLoadStatus::MissingUsedDefaults:
        return L"seeded defaults (no save exists)";
    case UserSettingsLoadStatus::MissingCreatedDefaults:
        return L"seeded defaults (created a new UserConfig.txt)";
    case UserSettingsLoadStatus::InvalidUsedDefaults:
        return L"seeded defaults (save is invalid)";
    case UserSettingsLoadStatus::IoErrorUsedDefaults:
    default: return L"seeded defaults (save could not be read)";
    }
}

} // namespace bfvr::settings
