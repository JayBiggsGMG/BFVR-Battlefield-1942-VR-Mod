#include "settings/UserSettings.h"

#include <windows.h>

#include <array>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
using bfvr::settings::UserSettingSeed;
using bfvr::settings::UserSettingsLoadStatus;
using bfvr::settings::UserSettingsSchema;
using bfvr::settings::UserSettingsSession;
using bfvr::settings::UserSettingsStore;

bool IsComfortMode(std::string_view value) noexcept
{
    return value == "balanced" || value == "smooth";
}

bool IsTurnDegrees(std::string_view value) noexcept
{
    return value == "30" || value == "45";
}

UserSettingsSchema TestSchema()
{
    return {
        UserSettingSeed{
            "comfort_mode",
            "balanced",
            {
                "Controls the deterministic comfort preset used by this test.",
                "Accepted values: balanced or smooth. Restart BFVR after a manual edit."
            },
            IsComfortMode},
        UserSettingSeed{
            "turn_degrees",
            "30",
            {
                "Sets the turn angle in degrees for the deterministic test setting.",
                "Accepted values: 30 or 45. Larger values turn farther per input."
            },
            IsTurnDegrees}
    };
}

std::wstring MakeTestDirectory()
{
    std::array<wchar_t, MAX_PATH> temporaryRoot = {};
    if (GetTempPathW(
            static_cast<DWORD>(temporaryRoot.size()),
            temporaryRoot.data()) == 0)
    {
        return {};
    }
    std::wstring directory = temporaryRoot.data();
    directory += L"BFVRUserSettingsTests-";
    directory += std::to_wstring(GetCurrentProcessId());
    directory += L"-";
    directory += std::to_wstring(GetTickCount64());
    return CreateDirectoryW(directory.c_str(), nullptr) != FALSE
        ? directory
        : std::wstring{};
}

std::wstring ConfigPath(const std::wstring& directory)
{
    return directory + L"\\UserConfig.txt";
}

bool WriteText(const std::wstring& path, std::string_view text)
{
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
    const bool success = text.size() <= MAXDWORD &&
        WriteFile(
            file,
            text.data(),
            static_cast<DWORD>(text.size()),
            &written,
            nullptr) != FALSE &&
        written == text.size();
    CloseHandle(file);
    return success;
}

std::string ReadText(const std::wstring& path)
{
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
        return {};
    }
    LARGE_INTEGER size = {};
    std::string result;
    if (GetFileSizeEx(file, &size) != FALSE && size.QuadPart >= 0 &&
        size.QuadPart <= MAXDWORD)
    {
        result.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        if (!result.empty() &&
            (ReadFile(
                 file,
                 result.data(),
                 static_cast<DWORD>(result.size()),
                 &read,
                 nullptr) == FALSE ||
             read != result.size()))
        {
            result.clear();
        }
    }
    CloseHandle(file);
    return result;
}

void CleanUp(const std::wstring& directory)
{
    const std::wstring path = ConfigPath(directory);
    const std::wstring temporaryPath = path + L"." +
        std::to_wstring(GetCurrentProcessId()) + L".tmp";
    DeleteFileW(temporaryPath.c_str());
    DeleteFileW(path.c_str());
    RemoveDirectoryW(directory.c_str());
}

bool TestTransactionalContract(const std::wstring& directory)
{
    UserSettingsStore store;
    if (!store.Initialize(ConfigPath(directory), TestSchema()))
    {
        return false;
    }
    const auto created = store.LoadOrCreateDefaults();
    if (created.status != UserSettingsLoadStatus::MissingCreatedDefaults ||
        created.settings != store.Defaults())
    {
        return false;
    }
    const std::string seededFile = ReadText(store.Path());
    if (seededFile.find("Controls the deterministic comfort preset") ==
            std::string::npos ||
        seededFile.find("# Seeded default: balanced") == std::string::npos ||
        seededFile.find("comfort_mode = balanced") == std::string::npos)
    {
        return false;
    }

    auto customized = store.Defaults();
    customized.values["comfort_mode"] = "smooth";
    customized.values["turn_degrees"] = "45";
    if (!store.Save(customized) || store.Load().settings != customized)
    {
        return false;
    }

    UserSettingsSession session;
    if (session.Begin(store) != UserSettingsLoadStatus::Loaded ||
        session.Working() != customized)
    {
        return false;
    }
    session.ResetToDefaults();
    if (session.Working() != store.Defaults())
    {
        return false;
    }
    session.Cancel();
    session.Begin(store);
    if (session.Working() != customized)
    {
        return false;
    }

    session.ResetToDefaults();
    if (!session.Save())
    {
        return false;
    }
    session.Cancel();
    session.Begin(store);
    return session.Working() == store.Defaults();
}

bool TestInvalidFallbackDoesNotOverwrite(const std::wstring& directory)
{
    UserSettingsStore store;
    if (!store.Initialize(ConfigPath(directory), TestSchema()))
    {
        return false;
    }
    constexpr std::string_view invalid =
        "schema_version = 1\r\ncomfort_mode = impossible\r\n";
    if (!WriteText(store.Path(), invalid))
    {
        return false;
    }
    const auto loaded = store.LoadOrCreateDefaults();
    return loaded.status == UserSettingsLoadStatus::InvalidUsedDefaults &&
        loaded.settings == store.Defaults() && ReadText(store.Path()) == invalid;
}

bool TestProductionSeedAndTypedValues(const std::wstring& directory)
{
    UserSettingsStore store;
    if (!store.Initialize(
            ConfigPath(directory),
            bfvr::settings::SeededUserSettingsSchema()))
    {
        return false;
    }
    const auto defaults = store.Defaults();
    const auto decodedDefaults = bfvr::settings::DecodeUserSettings(defaults);
    if (defaults.values.size() != 14 ||
        decodedDefaults.infantryTurnSpeedPercent != 100 ||
        decodedDefaults.invertFlightPitch ||
        decodedDefaults.invertTurretPitch ||
        decodedDefaults.invertTurretYaw ||
        decodedDefaults.offHandGripStyle !=
            bfvr::settings::OffHandGripStyle::Hold ||
        decodedDefaults.handWeaponCrosshair !=
            bfvr::settings::WorldCrosshairMode::On ||
        decodedDefaults.mountedWeaponCrosshair !=
            bfvr::settings::WorldCrosshairMode::On ||
        !decodedDefaults.fxaaEnabled ||
        !decodedDefaults.ambientOcclusionEnabled ||
        decodedDefaults.ambientOcclusionRadiusCentimeters != 60 ||
        decodedDefaults.ambientOcclusionStrengthPercent != 100 ||
        !decodedDefaults.bloomEnabled ||
        decodedDefaults.bloomThresholdPercent != 75 ||
        decodedDefaults.bloomIntensityPercent != 25)
    {
        return false;
    }
    if (!WriteText(store.Path(), "schema_version = 1\r\n"))
    {
        return false;
    }
    const auto upgraded = store.LoadOrCreateDefaults();
    if (upgraded.status != UserSettingsLoadStatus::LoadedCompletedSeed ||
        upgraded.settings != defaults ||
        ReadText(store.Path()).find("infantry_turn_speed_percent = 100") ==
            std::string::npos)
    {
        return false;
    }
    auto changed = decodedDefaults;
    changed.infantryTurnSpeedPercent = 270;
    changed.invertFlightPitch = true;
    changed.invertTurretPitch = true;
    changed.invertTurretYaw = true;
    changed.offHandGripStyle = bfvr::settings::OffHandGripStyle::Toggle;
    changed.handWeaponCrosshair =
        bfvr::settings::WorldCrosshairMode::HitMarkerOnly;
    changed.mountedWeaponCrosshair =
        bfvr::settings::WorldCrosshairMode::Off;
    changed.fxaaEnabled = false;
    changed.ambientOcclusionEnabled = false;
    changed.ambientOcclusionRadiusCentimeters = 95;
    changed.ambientOcclusionStrengthPercent = 65;
    changed.bloomEnabled = false;
    changed.bloomThresholdPercent = 55;
    changed.bloomIntensityPercent = 70;
    auto encoded = defaults;
    bfvr::settings::EncodeUserSettings(changed, encoded);
    if (!store.Save(encoded) ||
        bfvr::settings::DecodeUserSettings(store.Load().settings) != changed)
    {
        return false;
    }
    const std::string contents = ReadText(store.Path());
    return contents.find("infantry_turn_speed_percent = 270") !=
            std::string::npos &&
        contents.find("does not affect vehicles, aircraft, turrets") !=
            std::string::npos &&
        contents.find("invert_flight_pitch = true") != std::string::npos &&
        contents.find("including right-stick and right-grip motion aim") !=
            std::string::npos &&
        contents.find("off_hand_grip_style = toggle") != std::string::npos &&
        contents.find("Gadget crosshairs always remain enabled") !=
            std::string::npos &&
        contents.find("hand_weapon_3d_crosshair = hit_marker_only") !=
            std::string::npos &&
        contents.find("mounted_weapon_3d_crosshair = off") !=
            std::string::npos &&
        contents.find("fxaa_enabled = false") != std::string::npos &&
        contents.find("ambient_occlusion_radius_centimeters = 95") !=
            std::string::npos &&
        contents.find("requires restarting BFVR after Save") !=
            std::string::npos &&
        contents.find("bloom_threshold_percent = 55") !=
            std::string::npos &&
        contents.find("bloom_intensity_percent = 70") !=
            std::string::npos;
}
} // namespace

int wmain()
{
    const std::wstring directory = MakeTestDirectory();
    if (directory.empty())
    {
        std::cerr << "Could not create the User Settings test directory.\n";
        return 1;
    }
    const bool transactional = TestTransactionalContract(directory);
    const bool invalidFallback = TestInvalidFallbackDoesNotOverwrite(directory);
    const bool productionSeed = TestProductionSeedAndTypedValues(directory);
    CleanUp(directory);
    if (!transactional || !invalidFallback || !productionSeed)
    {
        std::cerr << "BFVR User Settings persistence tests failed.\n";
        return 1;
    }
    std::cout << "BFVR User Settings persistence tests passed.\n";
    return 0;
}
